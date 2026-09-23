/* ============================================================================
 * mbkit.c —— MbKit.dll：miniblink(node.dll) 常驻宿主层
 * ----------------------------------------------------------------------------
 * 解决的原始问题：
 *   宿主 exe 载入「使用 miniblink 的业务 DLL」→ 业务 DLL initialize 内核并与
 *   窗口句柄绑定 → 卸载 + 重新载入业务 DLL → **内核无法重新初始化**，必须重启
 *   宿主 exe。
 *
 * 实测结论（见 docs/诊断-miniblink热重载（2026-09-23）.md）：
 *   内核本身没问题（重复 init / 重复建 view / 卸载重载均实测通过）。
 *   真因是**进程级资源生命周期**：
 *     ① node.dll 因加载方式与引用计数而常驻进程 → 内核全局状态跨 DLL 重载存活；
 *     ② 业务 DLL 把 wke 回调函数指针（paint 等）注册进了内核，
 *        DLL 一卸载，内核手里就是**指向已释放代码页的野指针**，之后仍会调用；
 *     ③ 窗口上还可能残留指向业务 DLL 的 SetProp 属性 / 子类化；
 *     ④ `wkeIsInitialize()` 在 `wkeFinalize()` 之后仍返回 1（实测），
 *        所以「靠 isInitialize 判断能否重来」的守卫无效，内核带着旧状态跑。
 *
 * 本层的做法：
 *   ★ 钉住自己：GET_MODULE_HANDLE_EX_FLAG_PIN，业务 DLL 怎么卸载都影响不到它。
 *   ★ 唯一进入内核的回调（paint 等）都定义在**本模块内**，业务侧只提供
 *     「数据级」事件函数；本层在每次转发前做**模块存活校验**，
 *     业务模块已卸载就直接跳过 → 野指针既不会被调用，也不会崩。
 *   ★ node.dll 由本层按 `<宿主目录>\main\corn\node.dll` 加载并 PIN 住
 *     （**不改用户项目里的依赖路径约定**），因此内核状态始终由本层独占管理。
 *   ★ 热重载契约 = MbKit_DetachAll()（解绑回调 + 解绑窗口 + 销毁 view）→
 *     卸载业务 DLL → 载入新业务 DLL → 重新 Attach。内核保持已初始化，
 *     重载代价极低。
 *
 * 编译（32 位，与 e.exe 同架构）：
 *   gcc -m32 -O2 -shared -o MbKit.dll mbkit.c -Wl,--kill-at -static -s
 *   （必须先把 32 位 gcc 所在目录前置进 PATH，否则 cc1/as/ld 找不到，静默失败）
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "mbkit_api.h"

/* --------------------------------------------------------------------------
 * 由 mbkit_fwd.c / mbkit_cb.c 提供：
 *   mbk_ensure / mbk_node —— 内核 node.dll 的**唯一**持有入口（懒加载 + 单份）
 *   MbKit_KernelPath/FwdError —— 转发层的解析结果与错误
 *   mb_cb_detach_all —— 注销全部代理回调（热重载契约的第一步）
 * ------------------------------------------------------------------------*/
extern int          mbk_ensure(void);
extern HMODULE      mbk_node(void);
extern int          mbk_set_node_path(const char* p);
extern const char*  __stdcall MbKit_KernelPath(void);
extern const char*  __stdcall MbKit_FwdError(void);
extern void         mb_cb_detach_all(void);
extern int          __stdcall MbKit_CbBlocked(void);

/* ==========================================================================
 * 1. miniblink 导出（全部 __cdecl —— 与易语言 "@wkeXXX" 的 @ 前缀一致）
 *    签名按本机模块内 .DLL命令 声明核对，不凭记忆：
 *      wkeRunJSW   -> 长整数型（jsValue 64 位）
 *      wkeSetZoomFactor -> 小数型(float)
 *      wkeGetTitleW/wkeGetVersionString -> 返回指针
 * ========================================================================*/
typedef void           (*p_wkeInitialize)(void);
typedef void           (*p_wkeFinalize)(void);
typedef BOOL           (*p_wkeIsInitialize)(void);
typedef void*          (*p_wkeCreateWebView)(void);
typedef void           (*p_wkeDestroyWebView)(void*);
typedef void           (*p_wkeSetHandle)(void*, HWND);
typedef void           (*p_wkeSetHandleOffset)(void*, int, int);
typedef void           (*p_wkeResize)(void*, int, int);
typedef void           (*p_wkeSetZoomFactor)(void*, float);
typedef BOOL           (*p_wkeIsLoadingCompleted)(void*);
typedef const wchar_t* (*p_wkeGetTitleW)(void*);
typedef int            (*p_wkeGetVersion)(void);
typedef const char*    (*p_wkeGetVersionString)(void);
typedef void           (*p_wkeSetHeadlessEnabled)(void*, BOOL);
typedef void           (*p_wkeSetFocus)(void*);

/* 回调注册：wkeOnPaintUpdated(view, callback, param)
 * 回调签名 7 参数：(view, param, hdc, x, y, cx, cy)，__cdecl */
typedef void           (*p_wkeOnPaintUpdated)(void*, void*, void*);
typedef void (__cdecl *mb_paint_forward)(void*, void*, void*, int, int, int, int);

/* ★ 这才是**真正会触发**的回调：页面载入结束时内核必定回调它。
 * 用户的模块若把它的实现放在业务 DLL 里，卸载后就等着内核来调野指针。
 * 签名 5 参数：(view, param, url, result(int), failedReason) */
typedef void           (*p_wkeOnLoadingFinish)(void*, void*, void*);
typedef void (__cdecl *mb_load_forward)(void*, void*, void*, int, void*);

typedef void           (*p_wkeLoadURLW)(void*, const wchar_t*);
typedef void           (*p_wkeLoadHTMLW)(void*, const wchar_t*);
typedef long long      (*p_wkeRunJSW)(void*, const wchar_t*);

/* ==========================================================================
 * 2. 状态
 * ========================================================================*/
#define MB_MAX_VIEW 64

typedef struct {
    void*     wv;                 /* wkeWebView          */
    HWND      hwnd;
    MbEventCb cb;                 /* 业务事件函数（可能位于会卸载的模块） */
    void*     user;
    HMODULE   hOwner;             /* 注册时 cb 所属模块句柄 */
    char      ownerPath[MAX_PATH];/* 该模块当时的全路径 */
    int       used;
} MBVIEW;

typedef struct {
    HMODULE hSelf;
    HMODULE hNode;
    int     ready;
    int     pinned;
    int     initCount;
    int     suppressed;           /* 被存活校验挡下的野回调次数 */
    char    err[512];
    char    nodePath[MAX_PATH];
    char    version[192];

    p_wkeInitialize          Initialize;
    p_wkeFinalize            Finalize;
    p_wkeIsInitialize        IsInitialize;
    p_wkeCreateWebView       CreateWebView;
    p_wkeDestroyWebView      DestroyWebView;
    p_wkeSetHandle           SetHandle;
    p_wkeSetHandleOffset     SetHandleOffset;   /* 可选 */
    p_wkeResize              Resize;
    p_wkeSetZoomFactor       SetZoomFactor;
    p_wkeIsLoadingCompleted  IsLoadingCompleted;
    p_wkeGetTitleW           GetTitleW;
    p_wkeGetVersion          GetVersion;
    p_wkeGetVersionString    GetVersionString;
    p_wkeSetHeadlessEnabled  SetHeadlessEnabled;/* 可选 */
    p_wkeSetFocus            SetFocus;          /* 可选 */
    p_wkeOnPaintUpdated      OnPaintUpdated;
    p_wkeOnLoadingFinish     OnLoadingFinish;
    p_wkeLoadURLW            LoadURLW;
    p_wkeLoadHTMLW           LoadHTMLW;
    p_wkeRunJSW              RunJSW;

    MBVIEW views[MB_MAX_VIEW];
} MBG;

static MBG              G;
static CRITICAL_SECTION G_lock;
static int              G_lockReady = 0;

static void lock(void)   { if (G_lockReady) EnterCriticalSection(&G_lock); }
static void unlock(void) { if (G_lockReady) LeaveCriticalSection(&G_lock); }

static void seterr(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(G.err, sizeof(G.err) - 1, fmt, ap);
    va_end(ap);
    G.err[sizeof(G.err) - 1] = 0;
}

/* ==========================================================================
 * 3. 模块存活校验 —— 防野指针的核心
 *
 * 业务 DLL 卸载后，它注册进内核的事件函数地址就落在一片「不再属于任何已加载
 * 模块」的内存上。三重校验：
 *   ① 该地址是否仍属于某个已加载模块？（GetModuleHandleEx FROM_ADDRESS）
 *   ② 是否与注册时的模块句柄一致？（防止基址被别的模块占用）
 *   ③ 该模块的全路径是否与注册时一致？（防止同基址换了个模块）
 * 任一不成立 → 视为野指针，直接跳过调用。
 * ========================================================================*/
static int alive_cb(unsigned int cbAddr, unsigned int hOwnerExpected)
{
    HMODULE hm = NULL;

    if (!cbAddr) return 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(UINT_PTR)cbAddr, &hm) || !hm)
        return 0;                                   /* ① 不属于任何已加载模块 */
    if (hOwnerExpected && (unsigned int)(UINT_PTR)hm != hOwnerExpected)
        return 0;                                   /* ② 句柄变了 */
    return 1;
}

static int view_cb_alive(const MBVIEW* v)
{
    HMODULE hm = NULL;
    char    p[MAX_PATH];

    if (!v->cb) return 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(const void*)v->cb, &hm) || !hm)
        return 0;                                   /* ① */
    if (v->hOwner && hm != v->hOwner) return 0;     /* ② */
    if (!GetModuleFileNameA(hm, p, MAX_PATH)) return 0;
    if (v->ownerPath[0] && lstrcmpiA(p, v->ownerPath) != 0) return 0;  /* ③ */
    return 1;
}

/* ==========================================================================
 * 4. 事件转发（本模块内的实现 —— 内核手里永远只握着 MbKit 的代码）
 * ========================================================================*/
static void __cdecl on_paint(void* wv, void* param, void* hdc,
                             int x, int y, int cx, int cy)
{
    MBVIEW*     v = (MBVIEW*)param;
    MbPaintInfo pi;
    MbEventCb   cb;
    void*       user;

    (void)wv;
    if (!v) return;
    lock();
    if (!v->used) { unlock(); return; }
    if (!view_cb_alive(v)) {
        if (v->cb) G.suppressed++;      /* ★ 计数：确实挡下了一个野回调 */
        unlock();
        return;
    }
    cb = v->cb; user = v->user;
    unlock();

    pi.x = x; pi.y = y; pi.cx = cx; pi.cy = cy;
    pi.hdc = (unsigned int)(UINT_PTR)hdc;
    cb(user, MB_EV_PAINT, &pi, 0);
}

static void set_paint_cb(MBVIEW* v)
{
    if (!G.OnPaintUpdated) return;
    G.OnPaintUpdated(v->wv, (void*)(mb_paint_forward)&on_paint, v);
}

/* ★ 真正会触发的回调：载入完成。每次 wkeLoadHtml/LoadUrl 结束内核必调它。
 * 业务侧若把它注册在自己模块里然后卸载 —— 内核下次载入就调野指针。
 * 本层的做法：让内核**只**认识 MbKit 的这个函数，再由本层带存活校验转发。 */
static void __cdecl on_load_finish(void* wv, void* param, void* url,
                                   int result, void* reason)
{
    MBVIEW*   v = (MBVIEW*)param;
    MbEventCb cb;
    void*     user;

    (void)wv; (void)url; (void)reason;
    if (!v) return;
    lock();
    if (!v->used) { unlock(); return; }
    if (!view_cb_alive(v)) {
        if (v->cb) G.suppressed++;      /* ★ 计数：确实挡下了一个野回调 */
        unlock();
        return;
    }
    cb = v->cb; user = v->user;
    unlock();

    cb(user, MB_EV_LOADED, (void*)(UINT_PTR)result, NULL);
}

static void set_load_cb(MBVIEW* v)
{
    if (!G.OnLoadingFinish) return;
    G.OnLoadingFinish(v->wv, (void*)(mb_load_forward)&on_load_finish, v);
}

static void clear_view_cbs(void* wv)
{
    if (!wv) return;
    if (G.OnPaintUpdated)   G.OnPaintUpdated(wv, NULL, NULL);
    if (G.OnLoadingFinish)  G.OnLoadingFinish(wv, NULL, NULL);
}

/* ==========================================================================
 * 5. 内核路径解析 —— 不改用户项目「统一放 <proj>\main\corn\」的约定
 * ========================================================================*/
static int file_exists(const char* p)
{
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static int try_combo(const char* baseDir, char* out)
{
    char buf[MAX_PATH];
    if (!baseDir || !*baseDir) return 0;
    /* 项目统一依赖目录（相对工程根） */
    _snprintf(buf, sizeof(buf) - 1, "%s\\main\\corn\\node.dll", baseDir);
    if (file_exists(buf)) { lstrcpynA(out, buf, MAX_PATH); return 1; }
    /* 同目录裸放 */
    _snprintf(buf, sizeof(buf) - 1, "%s\\node.dll", baseDir);
    if (file_exists(buf)) { lstrcpynA(out, buf, MAX_PATH); return 1; }
    return 0;
}

static int up_and_try(const char* startDir, int maxUp, char* out)
{
    char cur[MAX_PATH];
    int  i;
    if (!startDir || !*startDir) return 0;
    lstrcpynA(cur, startDir, MAX_PATH);
    for (i = 0; i <= maxUp; i++) {
        char* slash;
        if (try_combo(cur, out)) return 1;
        slash = strrchr(cur, '\\');
        if (!slash) break;
        *slash = 0;
        if (!cur[0] || (strlen(cur) == 2 && cur[1] == ':')) { /* 到盘根 */ }
    }
    return 0;
}

static void dir_of(const char* full, char* outDir)
{
    const char* slash;
    lstrcpynA(outDir, full, MAX_PATH);
    slash = strrchr(outDir, '\\');
    if (slash) *(char*)slash = 0; else outDir[0] = 0;
}

__attribute__((unused))
static int resolve_node_path(const char* explicitPath, char* out)
{
    char exePath[MAX_PATH], selfPath[MAX_PATH], cwd[MAX_PATH], dir[MAX_PATH];
    const char* env = getenv("MBKIT_NODE_DLL");

    if (explicitPath && *explicitPath) { lstrcpynA(out, explicitPath, MAX_PATH); return 1; }
    if (env && *env)                   { lstrcpynA(out, env, MAX_PATH);        return 1; }

    /* ② 当前工作目录 —— 与易语言现有 `.DLL命令 "main\corn\node.dll"` 行为一致 */
    if (GetCurrentDirectoryA(MAX_PATH, cwd) && try_combo(cwd, out)) return 1;

    /* ③ 宿主 exe 目录 + 上溯 4 级 */
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        dir_of(exePath, dir);
        if (up_and_try(dir, 4, out)) return 1;
    }
    /* ④ MbKit.dll 自身所在目录 + 上溯 2 级 */
    if (G.hSelf && GetModuleFileNameA(G.hSelf, selfPath, MAX_PATH)) {
        dir_of(selfPath, dir);
        if (up_and_try(dir, 2, out)) return 1;
    }
    /* ⑤ 裸名，交给系统搜索序 */
    lstrcpynA(out, "node.dll", MAX_PATH);
    return 1;
}

/* ==========================================================================
 * 6. 初始化
 * ========================================================================*/
#define BIND_REQ(member, sym)                                                \
    do {                                                                     \
        G.member = (__typeof__(G.member))GetProcAddress(G.hNode, #sym);      \
        if (!G.member) { seterr("node.dll 缺少导出 %s", #sym);               \
                         return MBKIT_E_SYMBOL; }                            \
    } while (0)

#define BIND_OPT(member, sym)                                                \
    do { G.member = (__typeof__(G.member))GetProcAddress(G.hNode, #sym); } while (0)

static int bind_symbols(void)
{
    BIND_REQ(Initialize,         wkeInitialize);
    BIND_REQ(Finalize,           wkeFinalize);
    BIND_REQ(IsInitialize,       wkeIsInitialize);
    BIND_REQ(CreateWebView,      wkeCreateWebView);
    BIND_REQ(DestroyWebView,     wkeDestroyWebView);
    BIND_REQ(SetHandle,          wkeSetHandle);
    BIND_REQ(Resize,             wkeResize);
    BIND_REQ(SetZoomFactor,      wkeSetZoomFactor);
    BIND_REQ(IsLoadingCompleted, wkeIsLoadingCompleted);
    BIND_REQ(GetTitleW,          wkeGetTitleW);
    BIND_REQ(GetVersion,         wkeGetVersion);
    BIND_REQ(GetVersionString,   wkeGetVersionString);
    BIND_REQ(OnPaintUpdated,     wkeOnPaintUpdated);
    BIND_REQ(OnLoadingFinish,    wkeOnLoadingFinish);
    BIND_REQ(LoadURLW,           wkeLoadURLW);
    BIND_REQ(LoadHTMLW,          wkeLoadHTMLW);
    BIND_REQ(RunJSW,             wkeRunJSW);

    BIND_OPT(SetHandleOffset,    wkeSetHandleOffset);
    BIND_OPT(SetHeadlessEnabled, wkeSetHeadlessEnabled);
    BIND_OPT(SetFocus,           wkeSetFocus);
    return MBKIT_OK;
}
#undef BIND_REQ
#undef BIND_OPT

int __stdcall MbKit_Init(const char* nodeDllPathOrNull)
{
    HMODULE pin = NULL;
    int rc;

    /* 2026-09-24：显式传入的路径现在**真的生效**了（此前被 (void) 忽略）。
     * 语义与易语言的 `WKE设置node路径` 完全一致：目录 或 node.dll 完整文件名；
     * 空/NULL = 用默认链（宿主程序根目录优先）。
     * 注意：内核一旦加载就拒绝改路径（进程内只允许一份 node.dll）。 */
    if (nodeDllPathOrNull)
        mbk_set_node_path(nodeDllPathOrNull);

    lock();
    if (G.ready) { unlock(); return MBKIT_OK; }
    unlock();

    /* ★ 内核在进程内**只能被加载一次**。
     *   本层若自行 LoadLibrary，就可能出现「两份 node.dll、两套内核状态」——
     *   症状是莫名奇妙的"初始化过了却不动"，且 wkeIsInitialize 还会骗人。
     *   所以统一走转发层的单一持有者。 */
    if (!mbk_ensure()) {
        seterr("内核加载失败：%s", MbKit_FwdError());
        return MBKIT_E_NODLL;
    }
    G.hNode = mbk_node();
    _snprintf(G.nodePath, sizeof(G.nodePath) - 1, "%s", MbKit_KernelPath());
    G.nodePath[sizeof(G.nodePath) - 1] = 0;

    /* 钉住内核：从此 node.dll 常驻进程，内核全局状态由本层独占管理 */
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, "node.dll", &pin);

    rc = bind_symbols();
    if (rc != MBKIT_OK) return rc;

    /* 真正的内核初始化：本层是整个进程里**唯一**会调 initialize 的地方 */
    G.Initialize();
    G.initCount++;

    {   /* 版本串（注意：GetVersionString 必须在 initialize 之后调用，否则崩） */
        const char* vs = G.GetVersionString ? G.GetVersionString() : NULL;
        _snprintf(G.version, sizeof(G.version) - 1,
                  "MbKit/1.0  node.dll  wke%d  %s",
                  G.GetVersion ? G.GetVersion() : 0, vs ? vs : "(?)");
        G.version[sizeof(G.version) - 1] = 0;
    }

    lock();
    G.ready = 1;
    unlock();
    return MBKIT_OK;
}

/* ==========================================================================
 * 7. view 管理
 * ========================================================================*/
static MBVIEW* as_view(void* p)
{
    MBVIEW* v = (MBVIEW*)p;
    if (!v) return NULL;
    if (v < &G.views[0] || v >= &G.views[MB_MAX_VIEW]) return NULL;
    if (!v->used) return NULL;
    return v;
}

static MBVIEW* alloc_view(void)
{
    int i;
    for (i = 0; i < MB_MAX_VIEW; i++)
        if (!G.views[i].used) { ZeroMemory(&G.views[i], sizeof(MBVIEW)); return &G.views[i]; }
    return NULL;
}

static void remember_owner(MBVIEW* v)
{
    HMODULE hm = NULL;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(const void*)v->cb, &hm) && hm) {
        v->hOwner = hm;
        if (!GetModuleFileNameA(hm, v->ownerPath, MAX_PATH)) v->ownerPath[0] = 0;
    }
}

void* __stdcall MbKit_Create(void* hwnd, int x, int y, int w, int h,
                             MbEventCb cb, void* user)
{
    MBVIEW* v;
    void*   wv;

    if (!G.ready) {
        int rc = MbKit_Init(NULL);
        if (rc != MBKIT_OK) return NULL;
    }
    if (w <= 0) w = 400;
    if (h <= 0) h = 300;

    wv = G.CreateWebView();
    if (!wv) { seterr("wkeCreateWebView 返回 NULL"); return NULL; }

    lock();
    v = alloc_view();
    if (!v) { unlock(); G.DestroyWebView(wv); seterr("view 槽位已满"); return NULL; }
    v->wv    = wv;
    v->hwnd  = (HWND)hwnd;
    v->cb    = cb;
    v->user  = user;
    v->hOwner = NULL;
    v->ownerPath[0] = 0;
    if (cb) remember_owner(v);
    v->used  = 1;
    unlock();

    if (hwnd) {
        G.SetHandle(wv, (HWND)hwnd);
        if (G.SetHandleOffset && (x || y)) G.SetHandleOffset(wv, x, y);
    } else if (G.SetHeadlessEnabled) {
        G.SetHeadlessEnabled(wv, TRUE);
    }
    G.Resize(wv, w, h);
    set_paint_cb(v);
    set_load_cb(v);
    return v;
}

int __stdcall MbKit_Destroy(void* view)
{
    MBVIEW* v;
    void*   wv = NULL;

    lock();
    v = as_view(view);
    if (!v) { unlock(); return MBKIT_E_PARAM; }
    wv = v->wv;
    v->cb = NULL; v->hOwner = NULL; v->user = NULL;
    v->used = 0;
    v->wv = NULL;
    unlock();

    if (wv) {
        clear_view_cbs(wv);
        G.SetHandle(wv, NULL);
        G.DestroyWebView(wv);
    }
    return MBKIT_OK;
}

int __stdcall MbKit_DetachAll(void)
{
    void* killed[MB_MAX_VIEW];
    int   n = 0, i;

    /* ★ 先注销「回调登记层」里全部仍活动的代理回调。
     * 这是热重载契约的第一步：业务 DLL 在卸载前调用本函数，
     * 内核手里就不再持有任何指向业务模块的指针。 */
    mb_cb_detach_all();

    /* 再在锁内摘掉全部登记 —— 业务 DLL 此刻起已不可能再被回调进来 */
    lock();
    for (i = 0; i < MB_MAX_VIEW; i++) {
        MBVIEW* v = &G.views[i];
        if (!v->used) continue;
        v->cb = NULL;
        v->hOwner = NULL;
        v->user = NULL;
        if (v->wv) { killed[n++] = v->wv; v->wv = NULL; }
        v->used = 0;
    }
    unlock();

    /* 锁外做内核调用，避免与内核内部线程互等 */
    for (i = 0; i < n; i++) {
        if (!killed[i]) continue;
        clear_view_cbs(killed[i]);
        G.SetHandle(killed[i], NULL);
        G.DestroyWebView(killed[i]);
    }
    return n;
}

int __stdcall MbKit_LiveCount(void)
{
    int i, n = 0;
    lock();
    for (i = 0; i < MB_MAX_VIEW; i++) if (G.views[i].used) n++;
    unlock();
    return n;
}

/* ==========================================================================
 * 8. 导航与操作
 * ========================================================================*/
int __stdcall MbKit_LoadUrl(void* view, const wchar_t* url)
{
    MBVIEW* v = as_view(view);
    if (!v || !url) return MBKIT_E_PARAM;
    G.LoadURLW(v->wv, url);
    return MBKIT_OK;
}

int __stdcall MbKit_LoadHtml(void* view, const wchar_t* html)
{
    MBVIEW* v = as_view(view);
    if (!v || !html) return MBKIT_E_PARAM;
    G.LoadHTMLW(v->wv, html);
    return MBKIT_OK;
}

long long __stdcall MbKit_RunJs(void* view, const wchar_t* js)
{
    MBVIEW* v = as_view(view);
    if (!v || !js) return 0;
    return G.RunJSW(v->wv, js);
}

int __stdcall MbKit_Resize(void* view, int w, int h)
{
    MBVIEW* v = as_view(view);
    if (!v) return MBKIT_E_PARAM;
    G.Resize(v->wv, w, h);
    return MBKIT_OK;
}

int __stdcall MbKit_SetZoom(void* view, float factor)
{
    MBVIEW* v = as_view(view);
    if (!v) return MBKIT_E_PARAM;
    G.SetZoomFactor(v->wv, factor);
    return MBKIT_OK;
}

int __stdcall MbKit_SetHeadless(void* view, int on)
{
    MBVIEW* v = as_view(view);
    if (!v) return MBKIT_E_PARAM;
    if (!G.SetHeadlessEnabled) return MBKIT_E_GENERIC;
    G.SetHeadlessEnabled(v->wv, on ? TRUE : FALSE);
    return MBKIT_OK;
}

int __stdcall MbKit_IsLoaded(void* view)
{
    MBVIEW* v = as_view(view);
    if (!v) return -1;
    return G.IsLoadingCompleted(v->wv) ? 1 : 0;
}

int __stdcall MbKit_GetTitle(void* view, wchar_t* buf, int cch)
{
    MBVIEW* v = as_view(view);
    const wchar_t* t;
    if (!v || !buf || cch <= 0) return MBKIT_E_PARAM;
    t = G.GetTitleW(v->wv);
    if (!t) { buf[0] = 0; return 1; }
    lstrcpynW(buf, t, cch);
    return 0;
}

int __stdcall MbKit_ForcePaint(void* view)
{
    MBVIEW* v = as_view(view);
    if (!v || !v->hwnd) return MBKIT_E_PARAM;
    InvalidateRect(v->hwnd, NULL, TRUE);
    UpdateWindow(v->hwnd);
    return MBKIT_OK;
}

int __stdcall MbKit_Pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)(ms < 0 ? 0 : ms);
    MSG   msg;
    int   n = 0;
    do {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            n++;
        }
        Sleep(2);
    } while ((int)(GetTickCount() - end) < 0);
    return n;
}

/* --------------------------------------------------------------------------
 * ANSI(GBK) 变体 —— 易语言的「文本型」就是 ANSI 字节流，用这组接口最省事，
 * 不必像 wkeXXXW 那样手工拼 UTF-16 字节集。
 * ------------------------------------------------------------------------*/
static const wchar_t* ansi2wide(const char* s, wchar_t* buf, int cch)
{
    if (!s || !buf || cch <= 0) return NULL;
    if (!MultiByteToWideChar(CP_ACP, 0, s, -1, buf, cch)) buf[0] = 0;
    buf[cch - 1] = 0;
    return buf;
}

int __stdcall MbKit_LoadUrlA(void* view, const char* url)
{
    MBVIEW* v = as_view(view);
    wchar_t w[8192];
    if (!v || !url) return MBKIT_E_PARAM;
    if (!ansi2wide(url, w, 8192)) return MBKIT_E_PARAM;
    G.LoadURLW(v->wv, w);
    return MBKIT_OK;
}

int __stdcall MbKit_LoadHtmlA(void* view, const char* html)
{
    MBVIEW* v = as_view(view);
    int     need;
    wchar_t stackbuf[4096];
    wchar_t* w = stackbuf;
    if (!v || !html) return MBKIT_E_PARAM;
    need = MultiByteToWideChar(CP_ACP, 0, html, -1, NULL, 0);
    if (need > 4096) {
        static wchar_t* heap = NULL;                 /* 大页面兜底（单线程使用） */
        heap = (wchar_t*)realloc(heap, (size_t)need * sizeof(wchar_t));
        if (!heap) return MBKIT_E_GENERIC;
        w = heap;
    }
    MultiByteToWideChar(CP_ACP, 0, html, -1, w, need > 4096 ? need : 4096);
    G.LoadHTMLW(v->wv, w);
    return MBKIT_OK;
}

int __stdcall MbKit_RunJsA(void* view, const char* js)
{
    MBVIEW* v = as_view(view);
    wchar_t w[8192];
    if (!v || !js) return MBKIT_E_PARAM;
    if (!ansi2wide(js, w, 8192)) return MBKIT_E_PARAM;
    return (int)G.RunJSW(v->wv, w);
}

int __stdcall MbKit_GetTitleA(void* view, char* buf, int cch)
{
    MBVIEW* v = as_view(view);
    const wchar_t* t;
    if (!v || !buf || cch <= 0) return MBKIT_E_PARAM;
    t = G.GetTitleW(v->wv);
    if (!t) { buf[0] = 0; return 1; }
    if (!WideCharToMultiByte(CP_ACP, 0, t, -1, buf, cch, NULL, NULL)) buf[0] = 0;
    buf[cch - 1] = 0;
    return 0;
}

/* ==========================================================================
 * 9. 只读访问器 / 诊断
 * ========================================================================*/
const char* __stdcall MbKit_NodePath(void)  { return G.nodePath; }
const char* __stdcall MbKit_Version(void)   { return G.version; }
const char* __stdcall MbKit_LastError(void) { return G.err; }
int __stdcall MbKit_IsReady(void)           { return G.ready; }
unsigned int __stdcall MbKit_NodeModule(void)  { return (unsigned int)(UINT_PTR)G.hNode; }
unsigned int __stdcall MbKit_SelfModule(void)  { return (unsigned int)(UINT_PTR)G.hSelf; }
int __stdcall MbKit_KernelInitCount(void)   { return G.initCount; }
int __stdcall MbKit_Pinned(void)            { return G.pinned; }
int __stdcall MbKit_SuppressedCount(void)   { return G.suppressed + MbKit_CbBlocked(); }
int __stdcall MbKit_AliveModule(unsigned int cbAddr, unsigned int hOwner)
{
    return alive_cb(cbAddr, hOwner);
}

/* ==========================================================================
 * 10. DllMain —— 自 PIN（此处在加载锁内，只做最轻的事，绝不 LoadLibrary）
 * ========================================================================*/
BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        HMODULE self = NULL;
        DisableThreadLibraryCalls(hInst);
        ZeroMemory(&G, sizeof(G));
        G.hSelf = hInst;
        InitializeCriticalSection(&G_lock);
        G_lockReady = 1;

        /* ★ 把自己钉在进程里：业务 DLL 任意卸载/重载都动不了本层 */
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                             | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               (LPCSTR)hInst, &self)) {
            G.pinned = 1;
            G.hSelf = self;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (!reserved) {
            /* 进程正常退出：尽量干净地收尾（内核此时反正也要销毁） */
            if (G.ready && G.DestroyWebView) MbKit_DetachAll();
            G_lockReady = 0;
            DeleteCriticalSection(&G_lock);
        }
    }
    return TRUE;
}
