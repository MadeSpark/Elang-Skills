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
#include "mbk_thread.h"

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
/* 常驻内核线程 K + 同步编组（mbk_thread.c）—— 跨线程/跨 DLL 会话的唯一通路 */
extern void         mbk_live_add(long d);
extern long         mbk_live_get(void);

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

/* ==========================================================================
 * 内核属主与调用模式（2026-09-25 重做 —— 「常驻内核线程 K + 编组」）
 *
 * 事实链（全部实测，物证见 re/reinit_poc/）：
 *   ① miniblink 内核线程亲和：wkeInitialize 在哪条线程执行，内核就归哪条；
 *      跨线程碰内核 = 挂死（只读 API 也挂）或 0xC0000005（V8 线程局部区空解引用）。
 *   ② 内核属主**不可迁移**：finalize 是空实现、FreeLibrary 会段错误、
 *      换独立拷贝的加载管线彻底停摆（A 实例 init 过 ⇒ 拷贝 90s loaded=0）。
 *   ③ 唯一通路：让 MbKit 自有的一条**常驻线程 K** 持有内核（wkeInitialize 只在
 *      K 上执行），其它线程的内核调用全部同步编组到 K（marshal_probe r1 实测
 *      跨线程 SetHandle/载入/渲染全部成立，且 wkeSetHandle 不做跨线程子类化）。
 *
 * g_ownerTid 三态：
 *   0            —— 内核尚未初始化；
 *   =K 的 tid    —— 「K 模式」：内核由 MbKit_Init 在 K 上初始化，所有内核调用
 *                   经 mbk_kernel_run 编组（跨线程 / 跨 DLL 会话都成立）；
 *   =业务线程 tid —— 「兼容模式」：业务侧绕过 MbKit 直接裸调了 wkeInitialize
 *                   （转发桩 passthrough + 记账，见 mbkit_fwd.c），内核已绑在
 *                   该线程上，无法迁移 —— 属主线程内的调用直通（与旧版逐位
 *                   一致），跨线程调用返回明确失败码而不是崩（硬约束 5）。
 * ========================================================================*/
static DWORD g_ownerTid = 0;      /* 内核属主线程（0=未初始化） */
static int   g_kMode    = 0;      /* 1=K 模式（编组）  0=兼容模式（属主直通） */
static volatile LONG g_debugEnabled = 0;

int __stdcall MbKit_DebugEnabled(void)
{
    return InterlockedCompareExchange((LONG*)&g_debugEnabled, 0, 0) ? 1 : 0;
}

extern void mbk_veh_debug_enable(void);

int __stdcall MbKit_SetDebug(int on)
{
    int enabled = on ? 1 : 0;
    InterlockedExchange((LONG*)&g_debugEnabled, enabled);
    if (enabled) mbk_veh_debug_enable();
    return enabled;
}


static void lock(void)   { if (G_lockReady) EnterCriticalSection(&G_lock); }
static void unlock(void) { if (G_lockReady) LeaveCriticalSection(&G_lock); }

/* ---- 临时诊断（2026-10-13 卡死定位）：把内核启动关键节点追加写到
 *      %TEMP%\mbkit-trace-<pid>.log。只写文件、不碰内核，安全。 ---- */
static void mbtrace(const char* fmt, ...)
{
    char     path[MAX_PATH];
    char     buf[1024];
    DWORD    pid = GetCurrentProcessId();
    DWORD    tid = GetCurrentThreadId();
    int      n;
    HANDLE   h;
    DWORD    w;
    char     tmp[MAX_PATH];
    va_list  ap;

    if (!MbKit_DebugEnabled()) return;
    if (GetTempPathA(sizeof(tmp) - 12, tmp) == 0) { tmp[0] = '.'; tmp[1] = 0; }
    _snprintf(path, sizeof(path) - 1, "%smbkit-trace-%lu.log", tmp, (unsigned long)pid);
    path[sizeof(path) - 1] = 0;

    n = _snprintf(buf, sizeof(buf) - 8, "[%lu.pid%lu.%lu] ",
                  (unsigned long)GetTickCount(), (unsigned long)pid, (unsigned long)tid);
    if (n < 0) n = 0;
    va_start(ap, fmt);
    _vsnprintf(buf + n, sizeof(buf) - n - 4, fmt, ap);
    va_end(ap);
    strcat(buf, "\r\n");

    h = CreateFileA(path, FILE_APPEND_DATA,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        SetFilePointer(h, 0, NULL, FILE_END);
        WriteFile(h, buf, (DWORD)strlen(buf), &w, NULL);
        CloseHandle(h);
    }
}

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

/* ==========================================================================
 * 5b. 内核属主管理 + 内核调用编组（2026-09-25 重做）
 * ========================================================================*/
/* 属主线程是否还活着（兼容模式判断用；K 永不退出，不走这里）。
 * 2026-10-01：当前没有调用点（兼容模式的属主判定已简化为「不是 K 就是兼容」），
 * 保留为诊断辅助，显式标注 unused 以免 -Wextra 报警。 */
static int __attribute__((unused)) owner_thread_alive(DWORD tid)
{
    HANDLE h;
    DWORD  code = 0;
    int    alive = 0;

    if (!tid) return 0;
    h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid);
    if (!h) return 0;                        /* 打不开 = 线程已不存在 */
    if (GetExitCodeThread(h, &code)) alive = (code == STILL_ACTIVE);
    CloseHandle(h);
    return alive;
}

/* 内核「只加载、先别 initialize」：解析路径 + LoadLibrary(node.dll) + 钉住 + 绑符号。
 * ★ 2026-10-01（v1.4）：**wkeInitialize 被移出这里**了，原因见 5c 节 ——
 *   内核主线程 = 调 wkeInitialize 的那个线程，它必须就是创建/持有宿主窗口、
 *   并且有消息循环的那个业务线程；一旦落在别的线程上，视图永远渲染不出来
 *   （A/B 实测：K 上 initialize → 载入永不完结、画面空白）。 */
static int k_boot_load_only(void)
{
    mbtrace("k_boot_load_only: enter");
    if (!mbk_ensure()) { mbtrace("k_boot_load_only: mbk_ensure FAILED"); return 0; }   /* 转发层：解析路径 + 加载 node.dll + 填槽 */
    mbtrace("k_boot_load_only: mbk_ensure ok");
    {   /* 钉住内核：从此 node.dll 常驻进程，内核全局状态由本层独占管理 */
        HMODULE pin = NULL;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_PIN, "node.dll", &pin);
    }
    G.hNode = mbk_node();
    _snprintf(G.nodePath, sizeof(G.nodePath) - 1, "%s", MbKit_KernelPath());
    G.nodePath[sizeof(G.nodePath) - 1] = 0;
    mbtrace("k_boot_load_only: node=%p path=%s", (void*)G.hNode, G.nodePath);
    if (bind_symbols() != MBKIT_OK) { mbtrace("k_boot_load_only: bind_symbols FAILED"); return 0; }
    mbtrace("k_boot_load_only: bind_symbols ok");
    return 1;
}

/* 版本串（GetVersionString 必须在 initialize 之后调用，否则崩） */
static void set_version(void)
{
    const char* vs = G.GetVersionString ? G.GetVersionString() : NULL;
    _snprintf(G.version, sizeof(G.version) - 1,
              "MbKit/1.4.5 (owner-thread, DllMain-safe, HWND_MESSAGE-covered)  node.dll  wke%d  %s",
              G.GetVersion ? G.GetVersion() : 0, vs ? vs : "(?)");
    G.version[sizeof(G.version) - 1] = 0;
}

/* ★ 临时诊断（2026-10-11）：wkeInitialize 返回时刻的 GetTickCount()。
 *   回答「首次 load 时内核虽 can_kernel，但是否尚未 warm」。0 = 尚未初始化。 */
static volatile DWORD g_ownerInitTick = 0;

/* 【MBKIT_K=1】旧行为：内核落在常驻线程 K 上，wkeInitialize 在 K 上执行。 */
static void k_boot(void* unused)
{
    (void)unused;
    if (!k_boot_load_only()) return;
    if (!G.IsInitialize || !G.IsInitialize()) {   /* 幂等：已初始化就不再 initialize */
        G.Initialize();
        G.initCount++;
    }
    g_ownerInitTick = GetTickCount();             /* ★ 临时：K 模式初始化完成时刻 */
    set_version();
}

/* ==========================================================================
 * ★ v1.4 核心：把内核主线程钉在**当前调用线程**上
 *
 * 为什么必须这样（2026-10-01 实测，本机 2021 blink 内核 98096）：
 *   A) K 模式（wkeInitialize 在常驻线程 K 上执行、所有调用编组到 K）：
 *      `wkeLoadHTMLW` 之后 `IsLoadingCompleted == 0`、`wkeGetTitleW == NULL`、
 *      窗口客户区一片空白 —— 页面**从来就没加载**。
 *      （对照：`wkeWake` 这种离屏专用 API 在 K 模式下直接把进程打成 0xC0000005。）
 *   B) 同一个进程里裸用（LoadLibrary + wkeInitialize + wkeCreateWebView +
 *      wkeSetHandle + wkeLoadHTMLW，全在同一线程）：`succeeded=1`，
 *      屏幕截图能看到渲染内容 —— 一切正常。
 *   结论：miniblink 要求「内核主线程 == 窗口线程 == 有消息循环的线程」。
 *   宿主（框架/易语言插件）的调用恰好来自那个线程，所以把属主设成调用线程即可。
 *
 * 同步在这里做是**安全**的：死锁只发生在「同步等**别的**线程 LoadLibrary(node.dll)」
 * （旧版事故：调用方持着加载锁，K 拿不到锁），而本线程自己 LoadLibrary 是
 * 递归加载锁，不会死锁（而且真实宿主进程里 node.dll 往往已经被自己的 webview
 * 加载过，这里只是一次引用计数 +1，实测 0~1 ms）。
 * ========================================================================*/
static int owner_init_here(void)
{
    mbtrace("owner_init_here: enter ownerTid=%lu", (unsigned long)g_ownerTid);
    if (g_ownerTid) return (GetCurrentThreadId() == g_ownerTid) ? MBKIT_OK : MBKIT_E_STATE;

    mbtrace("owner_init_here: locking");
    lock();
    mbtrace("owner_init_here: lock acquired");
    if (!g_ownerTid) {
        if (!k_boot_load_only()) {
            unlock();
            mbtrace("owner_init_here: load_only failed -> E_NODLL");
            return MBKIT_E_NODLL;
        }
        mbtrace("owner_init_here: load_only ok, calling IsInitialize");
        /* 幂等：宿主自己已经裸调过 wkeInitialize 时（框架进程内很常见），
         * 内核主线程就是它，我们只记账、不再重复 initialize。 */
        if (!G.IsInitialize || !G.IsInitialize()) {
            mbtrace("owner_init_here: calling G.Initialize()");
            G.Initialize();
            G.initCount++;
            mbtrace("owner_init_here: G.Initialize() returned");
        }
        g_ownerTid = GetCurrentThreadId();
        g_kMode    = 0;                  /* 属主直通：调用线程上就地执行 */
        g_ownerInitTick = GetTickCount();/* ★ 临时：wkeInitialize 返回时刻 */
        mbtrace("owner_init_here: set_version()...");
        set_version();
        mbtrace("owner_init_here: DONE ownerTid=%lu", (unsigned long)g_ownerTid);
        OutputDebugStringA("[MbKit] 内核属主 = 当前调用线程（属主直通模式）\n");
    }
    unlock();
    return (GetCurrentThreadId() == g_ownerTid) ? MBKIT_OK : MBKIT_E_STATE;
}

/* （旧版的 init_kernel_on —— **同步**在调用线程上等内核启动 —— 已于 2026-10-01
 *  删除，由下方 boot_begin / MbKit_WaitKernel 的异步启动取代。原因见 5c 节注释：
 *  同步等 LoadLibrary(node.dll) 会在「DllMain 里调 miniblink」的场景下死锁，
 *  也会让宿主的 UI 线程在启动期卡住。） */

/* ==========================================================================
 * 5c. 内核启动 = **异步**（2026-10-01）
 *
 * 为什么必须异步：
 *   宿主（聊天机器人框架）在**自己的线程**里调插件的启用/初始化回调，插件在这个
 *   回调里同步调进 miniblink。而内核的启动要做 `LoadLibraryA(node.dll)`（33 MB、
 *   带一堆依赖）。如果这个 LoadLibrary 发生在调用线程上、或被调用线程同步等待：
 *     · 调用线程若正处在 **DllMain（加载锁）** 里（易语言 DLL 的 `_启动子程序`
 *       就是在 DllMain 里跑的）—— 加载锁被自己占着，任何线程再去 LoadLibrary
 *       都会卡住；K 卡住 → 调用线程同步等 K → **死锁 / 宿主卡死**。
 *     · 即使不在 DllMain，让宿主的 UI 线程在启动期阻塞几秒也表现为「框架未响应」。
 *   所以：`MbKit_EnsureKernelInit` 只**投递**启动任务就返回；真正要碰内核的调用
 *   走 `MbKit_WaitKernel`（带消息泵地等启动完成，见 mbk_wait_pump）。
 *
 * 状态机：0=未启动 1=启动中 2=完成 3=失败
 * ========================================================================*/
#define MBKIT_BOOT_WAIT_MS 20000

static volatile LONG g_bootState = 0;
static HANDLE        g_bootDone  = NULL;
static volatile LONG g_bootWaits = 0;
static volatile LONG g_bootWedge = 0;      /* 启动等待超时过（诊断用） */

static void k_boot_task(void* unused)
{
    k_boot(unused);
    if (G.hNode && G.Initialize) {
        g_ownerTid = mbk_kernel_tid();
        g_kMode    = 1;
        InterlockedExchange(&g_bootState, 2);
    } else {
        InterlockedExchange(&g_bootState, 3);
    }
    if (g_bootDone) SetEvent(g_bootDone);
}

/* 【MBKIT_K=1】是否走旧「常驻线程 K」模式（离屏/无窗口场景的逃生通道）。
 * 默认 0 = 属主直通（内核落在调用线程上，能渲染）。 */
static volatile LONG g_kModeWanted = -1;
int mbk_k_mode_wanted(void)
{
    LONG m = g_kModeWanted;
    if (m < 0) {
        char v[8] = {0};
        LONG want = 0;
        if (GetEnvironmentVariableA("MBKIT_K", v, sizeof(v)) && v[0] == '1') want = 1;
        InterlockedCompareExchange(&g_kModeWanted, want, -1);
        m = g_kModeWanted;
    }
    return m == 1;
}

/* 开始（或复用）一次内核启动。
 * ★ v1.4：默认路径是**同步在调用线程上**把内核建起来（见 owner_init_here 的注释），
 *   只有 MBKIT_K=1 才走旧的「投递到 K、异步起来」路径。 */
static int boot_begin(void)
{
    LONG st = g_bootState;
    mbtrace("boot_begin: st=%ld ownerTid=%lu kMode=%d", (long)st,
            (unsigned long)g_ownerTid, (int)mbk_k_mode_wanted());

    if (g_ownerTid) return MBKIT_OK;
    if (st == 2) return MBKIT_OK;
    if (st == 3) return MBKIT_E_NODLL;

    if (!mbk_k_mode_wanted()) {
        /* ★ 2026-10-02（v1.4.1）：这里**只登记、不干活**。
         *
         * 本函数是被「初始化类」导出（wkeInitialize / wkeInitializeEx /
         * wkeIsInitialize / MbKit_EnsureKernelInit）调用的，而这类调用完全可能发生在
         * **DllMain**（易语言 DLL 的 `_启动子程序` 就在这里跑）里 —— 此时加载锁由
         * 调用线程自己持着：只要在本线程上 `LoadLibraryA(node.dll)`（或 wkeInitialize
         * 内部拉起的线程去加载任何 DLL），加载器就会互等。实测（repro/loadprobe.exe
         * + dllmain_kit.dll）：v1.4 同步版在 DllMain 里 300 秒都没返回，正是用户最初
         * 报的「宿主自动载入 DLL → DLL 调 miniblink → 卡死」。
         *
         * 所以：这里把状态标成「已请求(1)」，真正的属主线程初始化推迟到第一个
         * **确实要碰内核**的调用（mbk_marshal_exec → MbKit_EnsureOwner）—— 那时调用方
         * 已经在正常的启用回调/窗口线程里，既安全又是正确的属主线程。 */
        if (st == 0) InterlockedCompareExchange(&g_bootState, 1, 0);
        return MBKIT_OK;
    }

    /* ---- 旧路径（MBKIT_K=1）：内核放到常驻线程 K 上，异步 ---- */
    lock();
    if (g_bootState == 0) {
        if (!g_bootDone) g_bootDone = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!mbk_kernel_start()) {
            g_bootState = 3;
            unlock();
            seterr("无法启动常驻内核线程 K");
            return MBKIT_E_GENERIC;
        }
        g_bootState = 1;
        /* 版本串先给「启动中」占位：k_boot 成功后会覆盖成真实版本串。
         * 这样「取版本文本」在启动期拿到的是说明性文字而不是空串。 */
        _snprintf(G.version, sizeof(G.version) - 1,
                  "MbKit/1.4 (MBKIT_K=1 async-boot) 内核启动中…");
        G.version[sizeof(G.version) - 1] = 0;
        if (!mbk_kernel_post(k_boot_task, NULL)) {
            g_bootState = 3;
            unlock();
            seterr("内核启动任务投递到线程 K 失败");
            return MBKIT_E_GENERIC;
        }
    }
    unlock();
    return MBKIT_OK;                 /* K 模式：不在这里等 LoadLibrary */
}

/* 【导出】确保内核「已开始」启动（异步/幂等，**绝不在本线程干重活**）。
 * ★ v1.4.1：语义 = 「登记启动请求」，不再同步 LoadLibrary node.dll —— 见 boot_begin
 *   注释（DllMain 里同步干重活会死锁）。真正建内核请用 MbKit_EnsureOwner。 */
int __stdcall MbKit_EnsureKernelInit(void)
{
    int rc = boot_begin();
    mbtrace("MbKit_EnsureKernelInit -> %d", rc);
    return rc;
}

/* 【导出】v1.4.1 新增：把内核**真正**建在「当前调用线程」上（属主直通）。
 * 只有确实要碰内核的导出（建视图/载入/取版本…）才会走到这里，调用方此时一定是
 * 正常的业务线程（启用回调 / 窗口线程），不会是 DllMain。MBKIT_K=1 时退化成等 K。 */
int __stdcall MbKit_EnsureOwner(void)
{
    mbtrace("MbKit_EnsureOwner: enter ownerTid=%lu", (unsigned long)g_ownerTid);
    if (g_ownerTid) return MBKIT_OK;
    if (mbk_k_mode_wanted()) return MbKit_WaitKernel(0);
    {
        int rc = owner_init_here();      /* 本线程 LoadLibrary(node.dll) + wkeInitialize */
        InterlockedExchange(&g_bootState, (rc == MBKIT_OK) ? 2 : 3);
        mbtrace("MbKit_EnsureOwner -> %d (ownerTid=%lu)", rc, (unsigned long)g_ownerTid);
        return rc;
    }
}

/* 【导出】等内核启动完成。ms<=0 用默认（20s）。
 * ★ v1.4.1：属主直通模式下「等待」= 现在就在本线程把内核建起来（阻塞但正确，
 *   而且本线程从此就是内核属主）；K 模式仍是带泵等待。
 * ★ 提醒：不要在 DllMain（`_启动子程序`）里调它 —— 那等于在加载锁里干重活。 */
int __stdcall MbKit_WaitKernel(int ms)
{
    if (g_ownerTid) return MBKIT_OK;
    if (!mbk_k_mode_wanted()) return MbKit_EnsureOwner();

    if (g_bootState == 0) {
        int rc = boot_begin();
        if (rc != MBKIT_OK) return rc;
    }
    if (g_ownerTid || g_bootState == 2) return MBKIT_OK;
    if (g_bootState == 3) return MBKIT_E_NODLL;
    if (!g_bootDone) return MBKIT_E_GENERIC;
    if (ms <= 0) ms = MBKIT_BOOT_WAIT_MS;

    InterlockedIncrement(&g_bootWaits);
    if (!mbk_wait_pump(g_bootDone, (DWORD)ms)) {
        InterlockedExchange(&g_bootWedge, 1);
        seterr("等内核启动超时(%d ms)：node.dll 的 LoadLibrary 被挡住。"
               "最常见原因是在 DllMain（=易语言 DLL 的 _启动子程序）里调了 miniblink："
               "此时加载锁由自己持有，任何线程都加载不了 DLL。请把首次调用挪到"
               "「初始化/启用回调」或另开线程里。", ms);
        return MBKIT_E_NODLL;
    }
    return (g_bootState == 2) ? MBKIT_OK : MBKIT_E_NODLL;
}

/* 内核启动状态字：0=未启动 1=启动中 2=完成 3=失败（诊断用） */
int __stdcall MbKit_BootState(void) { return (int)g_bootState; }

/* 【内部，不导出】从 wkeInitialize 返回至今的毫秒数；未初始化返回 -1。
 * 供编组层诊断（mbkit_fwd.c 的导航后验）判断「内核 can_kernel 但尚未 warm」。 */
int mbk_owner_init_ms(void)
{
    DWORD t = g_ownerInitTick;
    if (!t) return -1;
    return (int)(GetTickCount() - t);
}

/* 诊断：等启动的次数 / 编组等待超时次数 / 等待期间抽掉的消息数 */
int __stdcall MbKit_BootWaits(void)    { return (int)g_bootWaits; }
int __stdcall MbKit_WaitTimeouts(void) { return mbk_run_timeouts(); }
int __stdcall MbKit_PumpCount(void)    { return mbk_pump_count(); }

/* 统一的内核任务投递：先「确保内核已启动」（带泵等待，见 MbKit_WaitKernel），
 * 再编组执行。K 上重入直接就地在 K 上跑（绝不能等自己）。
 * 内核起不来时**明确失败、不投递** —— 调用方拿到的返回值保持初始值（0/失败码），
 * 与「安全桩」语义一致，不会踩空指针。 */
static void krun(mbk_task_fn fn, void* arg)
{
    if (!mbk_on_kernel_thread() && MbKit_WaitKernel(0) != MBKIT_OK)
        return;
    mbk_kernel_run(fn, arg);
}


/* —— 供 mbkit_fwd.c（导出边界编组 / MbKit_Diag）使用的状态小口 ——
 * 注意：k_boot 在 K 上执行时 kernel_tid 已就绪；属主 tid 在 init 成功后才记账。 */
static int can_kernel(void);                 /* 定义在本节下方（见 5b 末尾） */
int   mbk_can_kernel(void)  { return can_kernel(); }
int   mbk_owner_mode(void)  { return g_kMode; }      /* 1=K 模式 0=兼容模式 */
DWORD mbk_owner_tid(void)   { return g_ownerTid; }

/* 当前线程此刻能否安全碰内核：
 *   未初始化            → 可（MbKit_Create 会先走 MbKit_Init）；
 *   属主直通（默认）    → 只有属主线程可（内核主线程 = 那个线程，跨线程硬闯会崩）；
 *   K 模式              → 可（mbk_kernel_run 会编组到 K）。 */
static int can_kernel(void)
{
    if (!g_ownerTid) return 1;
    if (g_kMode) return 1;
    return GetCurrentThreadId() == g_ownerTid;
}

/* --------------------------------------------------------------------------
 * 内核调用编组的统一载荷：一个 KOP 结构 + 一个执行器，覆盖全部「操作类」API
 * （导航 / JS / 尺寸 / 缩放 / 状态查询 / 取标题）。带 view 生命周期的
 * Create / Destroy / DetachAll 各自有专门任务（见下）。
 * ------------------------------------------------------------------------*/
enum {
    KOP_LOADURLW = 1, KOP_LOADHTMLW, KOP_RUNJSW, KOP_RESIZE, KOP_SETZOOM,
    KOP_SETHEADLESS, KOP_ISLOADED, KOP_GETTITLEW
};

typedef struct {
    int          op;
    void*        wv;
    const void*  p1;          /* 字符串参数（URL / HTML / JS） */
    long long    a, b;        /* 整型参数（宽高 / 开关） */
    float        f;           /* 浮点参数（缩放） */
    long long    ret;         /* 返回值 */
    wchar_t*     tbuf;        /* KOP_GETTITLEW 的出参缓冲 */
    int          cch;
} KOP;

static void k_exec(void* p)
{
    KOP* k = (KOP*)p;
    switch (k->op) {
    case KOP_LOADURLW:    G.LoadURLW(k->wv, (const wchar_t*)k->p1);  k->ret = MBKIT_OK;    break;
    case KOP_LOADHTMLW:   G.LoadHTMLW(k->wv, (const wchar_t*)k->p1); k->ret = MBKIT_OK;    break;
    case KOP_RUNJSW:      k->ret = G.RunJSW(k->wv, (const wchar_t*)k->p1);                  break;
    case KOP_RESIZE:      G.Resize(k->wv, (int)k->a, (int)k->b);     k->ret = MBKIT_OK;    break;
    case KOP_SETZOOM:     G.SetZoomFactor(k->wv, k->f);              k->ret = MBKIT_OK;    break;
    case KOP_SETHEADLESS: if (G.SetHeadlessEnabled)
                              G.SetHeadlessEnabled(k->wv, k->a ? TRUE : FALSE);
                          k->ret = G.SetHeadlessEnabled ? MBKIT_OK : MBKIT_E_GENERIC;        break;
    case KOP_ISLOADED:    k->ret = G.IsLoadingCompleted(k->wv) ? 1 : 0;                    break;
    case KOP_GETTITLEW: {
        const wchar_t* t = G.GetTitleW(k->wv);
        if (!t) { if (k->tbuf && k->cch > 0) k->tbuf[0] = 0; k->ret = 1; }
        else    { lstrcpynW(k->tbuf, t, k->cch); k->ret = 0; }
        break;
    }
    default: k->ret = MBKIT_E_GENERIC; break;
    }
}

/* 同步把一个 KOP 编组到属主线程执行，返回 k.ret。
 * 不可碰内核（兼容模式跨线程）时返回 fallback，绝不碰内核。 */
static long long mbk_kcall(int op, void* wv, const void* p1,
                           long long a, long long b, float f,
                           long long fallback)
{
    KOP k;
    if (!can_kernel()) return fallback;
    ZeroMemory(&k, sizeof(k));
    k.op = op; k.wv = wv; k.p1 = p1; k.a = a; k.b = b; k.f = f;
    krun(k_exec, &k);
    return k.ret;
}

int __stdcall MbKit_Init(const char* nodeDllPathOrNull)
{
    /* 2026-09-24：显式传入的路径现在**真的生效**了（此前被 (void) 忽略）。
     * 语义与易语言的 `WKE设置node路径` 完全一致：目录 或 node.dll 完整文件名；
     * 空/NULL = 用默认链（宿主程序根目录优先）。
     * 注意：内核一旦加载就拒绝改路径（进程内只允许一份 node.dll）。 */
    if (nodeDllPathOrNull)
        mbk_set_node_path(nodeDllPathOrNull);

    lock();
    if (G.ready) {
        int live = 0, i;
        for (i = 0; i < MB_MAX_VIEW; i++) if (G.views[i].used) live++;
        unlock();

        /* 会话语义（硬约束 4，重载场景）：biz DLL 换代时重新 Init 进来，
         * 上个实例可能没来得及销毁 view 就被 FreeLibrary 了 —— 残留 view
         * 全部清理，保证新实例拿到干净状态。内核与线程 K 原样复用。 */
        if (live > 0) MbKit_DetachAll();
        return MBKIT_OK;
    }
    unlock();

    /* ★ 内核从第一天起就落在 MbKit 自有的常驻线程 K 上（业务线程生死无关）。
     *   例外：业务侧已裸调 wkeInitialize（转发桩已把属主记账成调用线程，
     *   兼容模式）—— 此时内核绑在该业务线程上无法迁移，保持不动作旧版语义。 */
    {
        int rc = MbKit_EnsureKernelInit();
        if (rc != MBKIT_OK) return rc;
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

/* Create 的内核侧任务：在属主线程上执行（K 模式=编组到 K）。
 * 建 view + 绑窗口/无窗口 + 尺寸 + 注册两层回调，一次编组全部做完。 */
typedef struct { MBVIEW* v; HWND hwnd; int x, y, w, h; int ok; } CARG;

static void k_create(void* p)
{
    CARG* a = (CARG*)p;
    void* wv = G.CreateWebView();

    if (!wv) return;
    a->v->wv = wv;
    if (a->hwnd) {
        G.SetHandle(wv, a->hwnd);
        if (G.SetHandleOffset && (a->x || a->y)) G.SetHandleOffset(wv, a->x, a->y);
    } else if (G.SetHeadlessEnabled) {
        G.SetHeadlessEnabled(wv, TRUE);
    }
    G.Resize(wv, a->w, a->h);
    set_paint_cb(a->v);
    set_load_cb(a->v);
    a->ok = 1;
}

void* __stdcall MbKit_Create(void* hwnd, int x, int y, int w, int h,
                             MbEventCb cb, void* user)
{
    MBVIEW* v;
    CARG    a;

    if (!G.ready) {
        int rc = MbKit_Init(NULL);
        if (rc != MBKIT_OK) return NULL;
    }
    if (w <= 0) w = 400;
    if (h <= 0) h = 300;

    lock();
    v = alloc_view();
    if (!v) { unlock(); seterr("view 槽位已满"); return NULL; }
    v->hwnd  = (HWND)hwnd;
    v->cb    = cb;
    v->user  = user;
    v->hOwner = NULL;
    v->ownerPath[0] = 0;
    if (cb) remember_owner(v);
    v->used  = 1;
    v->wv    = NULL;
    unlock();

    ZeroMemory(&a, sizeof(a));
    a.v = v; a.hwnd = (HWND)hwnd; a.x = x; a.y = y; a.w = w; a.h = h;

    if (!can_kernel()) {
        /* 兼容模式 + 跨线程：内核绑在别的（业务）线程上无法迁移。
         * 明确失败，绝不跨线程硬闯 —— 返回 NULL 而不是 0xC0000005。 */
        lock(); v->used = 0; unlock();
        seterr("内核属于线程 %lu（兼容模式，非编组），当前线程 %lu 无法建 view",
               (unsigned long)g_ownerTid, (unsigned long)GetCurrentThreadId());
        return NULL;
    }
    krun(k_create, &a);
    if (!a.ok) {
        lock(); v->used = 0; unlock();
        seterr("wkeCreateWebView 返回 NULL");
        return NULL;
    }
    mbk_live_add(+1);                    /* 与转发路径统一口径的活跃 view 计数 */
    return v;
}

/* Destroy 的内核侧任务（在属主线程上执行 —— 跨线程直调会挂死，实测） */
static void k_destroy_one(void* p)
{
    void* wv = *(void**)p;
    clear_view_cbs(wv);
    G.SetHandle(wv, NULL);
    G.DestroyWebView(wv);
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
        /* ★ 必须经 mbk_kernel_run 编组到属主线程（K 模式下就是 K）——
         *   跨线程直调 wkeDestroyWebView 会挂死（fix_same/fix_cross 实测）。 */
        if (can_kernel()) krun(k_destroy_one, &wv);
        /* 不可碰内核时（兼容模式跨线程）：只放弃记账 —— 内核对象随属主会话
         * 结束由 MbKit_DetachAll 兜底回收，绝不跨线程硬闯。 */
        mbk_live_add(-1);
    }
    return MBKIT_OK;
}

/* 2026-10-19 修复（memreg 100 轮只回收 36 个）：k_destroy_batch 里的
 *   G.SetHandle / G.DestroyWebView 是**导出桩**（edgewebview 下 g_hNode=自身），
 *   桩会重进编组层 → mbk_defer_destroy() 把刚弹出队列的 view 又登记回去，
 *   待回收队列永远排不空；MB_DQ_CAP=64 灌满后靠「队列满」才落回同步销毁，
 *   于是每次运行固定有 64 个 WV 永不 delete（有界泄漏 + g_liveViews 僵尸项）。
 *   批量销毁期间置位本标志，桩重入的销毁直接穿透到真实现。 */
static volatile LONG g_dqb_busy = 0;

/* DetachAll 的内核侧批量销毁任务（在属主线程上执行） */
static void k_destroy_batch(void* p)
{
    void** killed = (void**)p;
    int    i;
    LONG   prev = InterlockedExchange(&g_dqb_busy, 1);
    for (i = 0; i < MB_MAX_VIEW && killed[i]; i++) {
        clear_view_cbs(killed[i]);
        G.SetHandle(killed[i], NULL);
        G.DestroyWebView(killed[i]);
    }
    InterlockedExchange(&g_dqb_busy, prev);
}

/* ==========================================================================
 * 7b. view「延迟回收」—— 把内核销毁动作移出插件卸载窗口（2026-10-10，任务5）
 *
 * 现象：插件卸载窗口里同步 wkeDestroyWebView → Chromium 拆除其工作线程 → 线程
 *   退出要抢加载器锁；此刻加载器正 LdrUnloadDll、插件又发起 LoadLibraryA ⇒ 三方
 *   交叉死锁，宿主卡死。
 * 做法：把 wkeDestroyWebView 改为「延迟回收」—— 登记到本队列，延迟（默认 400ms，
 *   可用环境变量 MBKIT_DESTROY_DELAY_MS 覆盖）后由 MbKit 回收；期间同一 view 若又
 *   被绑定/使用（下一次内核调用以该 view 为首参）则取消回收。DetachAll 的
 *   「回调解绑 + 记账摘除」仍同步完成，只有内核侧 wkeDestroyWebView 被推迟。
 *
 * ★ 线程亲和适配（关键）：miniblink 要求内核只在属主线程上碰。属主直通模式下，
 *   回收**只能由属主线程**执行 —— 于是由属主线程在下一次编组入口（到点后）回收
 *   （mbkit_fwd.c 在 can_kernel 的编组入口调 mbk_flush_deferred_destroy）；专用
 *   回收线程在 K 模式下驱动回收（mbk_kernel_run 会编组到 K）。二者共用同一队列
 *   与到时判定。
 * ========================================================================*/
#define MB_DQ_CAP         64
#define MB_DQ_DEFAULT_MS  400

static void*  g_dq_wv[MB_DQ_CAP];          /* 待回收的 wkeWebView（raw 指针） */
static DWORD  g_dq_at[MB_DQ_CAP];          /* 登记时刻（GetTickCount） */
static int    g_dq_n = 0;                  /* 队列长度（G_lock 保护） */
static volatile LONG g_dq_count = 0;       /* == g_dq_n 的无锁快照（入口 O(1) 快检用） */
static HANDLE g_dq_thread = NULL;          /* 专用回收线程（懒启动；K 模式驱动回收） */
static volatile LONG g_dq_delay = -1;      /* 延迟毫秒（-1=未解析） */
static volatile LONG g_dq_deferred  = 0;   /* 诊断：累计登记数 */
static volatile LONG g_dq_cancelled = 0;   /* 诊断：复用取消数 */
static volatile LONG g_dq_reclaimed = 0;   /* 诊断：实际回收批次数 */
static volatile LONG g_dq_flushing  = 0;   /* 回收进行中（防重入：销毁触发回调再入） */

int mbk_flush_deferred_destroy(void);      /* 前向声明（dq_worker 用到） */

static DWORD dq_delay_ms(void)
{
    LONG d = g_dq_delay;
    if (d < 0) {
        char  v[16] = {0};
        DWORD ms = MB_DQ_DEFAULT_MS;
        if (GetEnvironmentVariableA("MBKIT_DESTROY_DELAY_MS", v, sizeof(v)) && v[0]) {
            long x = atol(v);
            if (x < 0)     x = 0;
            if (x > 60000) x = 60000;
            ms = (DWORD)x;
        }
        InterlockedCompareExchange(&g_dq_delay, (LONG)ms, -1);
        d = g_dq_delay;
    }
    return (DWORD)d;
}

/* 专用回收线程：K 模式下驱动回收（mbk_kernel_run 会把销毁编组到线程 K）。
 * 属主直通模式下本线程不是内核属主，跨线程碰内核会挂 —— 这里不碰，交给属主线程
 * 在下一次编组入口回收（见 mbk_flush_deferred_destroy 的调用点）。 */
static DWORD WINAPI dq_worker(LPVOID pv)
{
    (void)pv;
    for (;;) {
        Sleep(50);
        if (mbk_owner_mode() && mbk_owner_tid() != 0)
            mbk_flush_deferred_destroy();
    }
    return 0;
}

/* 登记一个 view 到延迟回收队列。返回 1=已登记（调用方不要同步销毁）；0=队列满
 * 或回收线程起不来（调用方落回同步销毁，保持旧行为）。 */
int mbk_defer_destroy(void* wv)
{
    int i;
    if (!wv) return 0;
    /* 批量销毁窗口内的销毁请求（经导出桩重入）不再延迟，否则会自我重登记，
     * 队列永远排不空 —— 见 k_destroy_batch 上方说明。 */
    if (InterlockedCompareExchange(&g_dqb_busy, 0, 0) != 0) return 0;
    lock();
    for (i = 0; i < g_dq_n; i++) {
        if (g_dq_wv[i] == wv) { g_dq_at[i] = GetTickCount(); unlock(); return 1; }  /* 已在队：刷新计时 */
    }
    if (g_dq_n >= MB_DQ_CAP) { unlock(); return 0; }
    if (!g_dq_thread) {
        DWORD tid = 0;
        g_dq_thread = CreateThread(NULL, 0, dq_worker, NULL, 0, &tid);
        if (!g_dq_thread) { unlock(); return 0; }   /* 起不来：调用方同步销毁 */
    }
    g_dq_wv[g_dq_n] = wv;
    g_dq_at[g_dq_n] = GetTickCount();
    g_dq_n++;
    g_dq_count = g_dq_n;
    InterlockedIncrement(&g_dq_deferred);
    unlock();
    return 1;
}

/* 复用取消：同一 view 又被绑定/使用 → 撤下待回收登记。返回 1=取消成功。 */
int mbk_cancel_destroy(void* wv)
{
    int i;
    if (!wv) return 0;
    lock();
    for (i = 0; i < g_dq_n; i++) {
        if (g_dq_wv[i] == wv) {
            g_dq_n--;
            g_dq_wv[i] = g_dq_wv[g_dq_n];
            g_dq_at[i] = g_dq_at[g_dq_n];
            g_dq_count = g_dq_n;
            InterlockedIncrement(&g_dq_cancelled);
            unlock();
            return 1;
        }
    }
    unlock();
    return 0;
}

/* 是否存在待回收项（供转发层入口做 O(1) 无锁快检）。 */
int mbk_destroy_pending(void) { return (int)g_dq_count; }

/* 诊断：view 延迟回收队列计数（供 MbKit_Diag 读取）。 */
void mbk_destroy_stats(long* deferred, long* cancelled, long* reclaimed, int* qlen)
{
    if (deferred)  *deferred  = (long)g_dq_deferred;
    if (cancelled) *cancelled = (long)g_dq_cancelled;
    if (reclaimed) *reclaimed = (long)g_dq_reclaimed;
    if (qlen)      *qlen      = (int)g_dq_count;
}

/* 回收已到期的项。**必须由属主线程调用**（属主直通）——转发层在「确认当前线程
 * 可碰内核（can_kernel）」的编组入口调用；K 模式下回收线程也可调用（krun 会编组到
 * K）。内部仍以 mbk_can_kernel() 兜底：不可碰内核则原样返回、绝不碰内核。 */
int mbk_flush_deferred_destroy(void)
{
    void* due[MB_MAX_VIEW];
    int   nd = 0, i;
    DWORD now, delay;

    if (!mbk_can_kernel()) return 0;
    /* 防重入：销毁过程若触发回调、回调又经编组入口进来，挡掉嵌套回收（剩余项留给下一轮） */
    if (InterlockedCompareExchange(&g_dq_flushing, 1, 0) != 0) return 0;

    delay = dq_delay_ms();
    now   = GetTickCount();

    lock();
    for (i = 0; i < g_dq_n; ) {
        if ((int)(now - g_dq_at[i]) >= (int)delay) {
            if (nd < MB_MAX_VIEW) due[nd++] = g_dq_wv[i];
            g_dq_n--;
            g_dq_wv[i] = g_dq_wv[g_dq_n];
            g_dq_at[i] = g_dq_at[g_dq_n];
        } else {
            i++;
        }
    }
    g_dq_count = g_dq_n;
    unlock();

    if (nd > 0) {
        while (nd < MB_MAX_VIEW) due[nd++] = NULL;   /* NULL 结尾，喂给批量销毁 */
        krun(k_destroy_batch, due);                  /* 属主线程上：清回调 + SetHandle(NULL) + DestroyWebView */
        InterlockedIncrement(&g_dq_reclaimed);
    }
    InterlockedExchange(&g_dq_flushing, 0);
    return (nd > 0) ? 1 : 0;
}

/* DetachAll 的真正实现。defer_destroy=1：内核侧 wkeDestroyWebView 走延迟回收
 * （移出插件卸载窗口）；=0：同步批量销毁（仅用于进程退出，那时不能再拉起线程）。
 * 「回调解绑 + 记账摘除」无论哪种模式都**同步完成** —— 热重载契约不弱化。 */
static int detach_all_impl(int defer_destroy)
{
    void* killed[MB_MAX_VIEW];
    int   total = 0, i;

    /* ★ 第一步（同步）：注销回调登记层里全部仍活动的代理回调。
     * 业务 DLL 在卸载前调用本函数，内核手里就不再持有指向业务模块的指针。 */
    mb_cb_detach_all();

    /* 第二步（同步）：锁内摘掉全部登记 —— 业务 DLL 此刻起已不可能再被回调进来 */
    lock();
    for (i = 0; i < MB_MAX_VIEW; i++) {
        MBVIEW* v = &G.views[i];
        if (!v->used) continue;
        v->cb = NULL;
        v->hOwner = NULL;
        v->user = NULL;
        if (v->wv) { killed[total++] = v->wv; v->wv = NULL; }
        v->used = 0;
    }
    unlock();

    /* 第三步：内核侧销毁 */
    if (total > 0 && can_kernel()) {
        if (defer_destroy) {
            void* sync[MB_MAX_VIEW];
            int   ns = 0, j, t;
            for (j = 0; j < total; j++)
                if (!mbk_defer_destroy(killed[j])) sync[ns++] = killed[j];
            if (ns > 0) {                          /* 队列满/起不来：剩下的同步销毁，不漏 */
                for (t = ns; t < MB_MAX_VIEW; t++) sync[t] = NULL;
                krun(k_destroy_batch, sync);
            }
        } else {
            int t;
            for (t = total; t < MB_MAX_VIEW; t++) killed[t] = NULL;   /* NULL 结尾（修正原依赖未初始化栈） */
            krun(k_destroy_batch, killed);
        }
        mbk_live_add(-(long)total);
    }
    /* 不可碰内核时（兼容模式跨线程）：只放弃记账，内核对象由属主会话兜底 */
    return total;
}

int __stdcall MbKit_DetachAll(void)
{
    /* 默认：内核侧 wkeDestroyWebView 延迟回收，移出插件卸载/加载窗口 */
    return detach_all_impl(1);
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
    return (int)mbk_kcall(KOP_LOADURLW, v->wv, url, 0, 0, 0, MBKIT_E_STATE);
}

int __stdcall MbKit_LoadHtml(void* view, const wchar_t* html)
{
    MBVIEW* v = as_view(view);
    if (!v || !html) return MBKIT_E_PARAM;
    return (int)mbk_kcall(KOP_LOADHTMLW, v->wv, html, 0, 0, 0, MBKIT_E_STATE);
}

long long __stdcall MbKit_RunJs(void* view, const wchar_t* js)
{
    MBVIEW* v = as_view(view);
    if (!v || !js) return 0;
    return mbk_kcall(KOP_RUNJSW, v->wv, js, 0, 0, 0, 0);
}

int __stdcall MbKit_Resize(void* view, int w, int h)
{
    MBVIEW* v = as_view(view);
    if (!v) return MBKIT_E_PARAM;
    return (int)mbk_kcall(KOP_RESIZE, v->wv, NULL, w, h, 0, MBKIT_E_STATE);
}

int __stdcall MbKit_SetZoom(void* view, float factor)
{
    MBVIEW* v = as_view(view);
    if (!v) return MBKIT_E_PARAM;
    return (int)mbk_kcall(KOP_SETZOOM, v->wv, NULL, 0, 0, factor, MBKIT_E_STATE);
}

int __stdcall MbKit_SetHeadless(void* view, int on)
{
    MBVIEW* v = as_view(view);
    if (!v) return MBKIT_E_PARAM;
    if (!G.SetHeadlessEnabled) return MBKIT_E_GENERIC;
    return (int)mbk_kcall(KOP_SETHEADLESS, v->wv, NULL, on, 0, 0, MBKIT_E_STATE);
}

int __stdcall MbKit_IsLoaded(void* view)
{
    MBVIEW* v = as_view(view);
    if (!v) return -1;
    return (int)mbk_kcall(KOP_ISLOADED, v->wv, NULL, 0, 0, 0, -1);
}

int __stdcall MbKit_GetTitle(void* view, wchar_t* buf, int cch)
{
    MBVIEW* v = as_view(view);
    KOP k;
    if (!v || !buf || cch <= 0) return MBKIT_E_PARAM;
    if (!can_kernel()) return MBKIT_E_STATE;   /* 兼容模式跨线程：明确失败 */
    ZeroMemory(&k, sizeof(k));
    k.op = KOP_GETTITLEW; k.wv = v->wv; k.tbuf = buf; k.cch = cch;
    krun(k_exec, &k);
    return (int)k.ret;
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
    return (int)mbk_kcall(KOP_LOADURLW, v->wv, w, 0, 0, 0, MBKIT_E_STATE);
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
    return (int)mbk_kcall(KOP_LOADHTMLW, v->wv, w, 0, 0, 0, MBKIT_E_STATE);
}

int __stdcall MbKit_RunJsA(void* view, const char* js)
{
    MBVIEW* v = as_view(view);
    wchar_t w[8192];
    if (!v || !js) return MBKIT_E_PARAM;
    if (!ansi2wide(js, w, 8192)) return MBKIT_E_PARAM;
    return (int)mbk_kcall(KOP_RUNJSW, v->wv, w, 0, 0, 0, 0);
}

int __stdcall MbKit_GetTitleA(void* view, char* buf, int cch)
{
    MBVIEW* v = as_view(view);
    wchar_t wbuf[1024];
    if (!v || !buf || cch <= 0) return MBKIT_E_PARAM;
    {
        KOP k;
        int rc;
        if (!can_kernel()) return MBKIT_E_STATE;   /* 兼容模式跨线程：明确失败 */
        ZeroMemory(&k, sizeof(k));
        k.op = KOP_GETTITLEW; k.wv = v->wv; k.tbuf = wbuf; k.cch = 1024;
        krun(k_exec, &k);
        rc = (int)k.ret;
        if (rc != 0) { buf[0] = 0; return rc; }
    }
    if (!WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, cch, NULL, NULL)) buf[0] = 0;
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

        /* ★ 2026-10-13 诊断：自动装崩溃探针（只装 VEH，不碰消息钩子），
         *   用于抓「宿主在插件初始化期崩/卡」的现场。 */
        { extern void mbk_veh_auto(void); mbk_veh_auto(); }
        mbtrace("DllMain: PROCESS_ATTACH done pid=%lu", (unsigned long)GetCurrentProcessId());

        /* ★ 把自己钉在进程里：业务 DLL 任意卸载/重载都动不了本层。
         *   ⚠️ 手工映射边界（小栗子 PE 加载器）：MbKit 不在加载器链表时
         *   GetModuleHandleExA(PIN) 会失败 —— 退化路径 = 用自身路径主动
         *   LoadLibrary 一次拉高引用计数（同样达成「biz FreeLibrary 不卸载我」）。
         *   G.pinned：1=PIN 成功  2=引用计数兜底  0=两种都失败（Diag 可见） */
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                             | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               (LPCSTR)hInst, &self)) {
            G.pinned = 1;
            G.hSelf = self;
        } else {
            char selfPath[MAX_PATH];
            if (GetModuleFileNameA(hInst, selfPath, MAX_PATH) &&
                LoadLibraryA(selfPath)) {
                G.pinned = 2;
                G.hSelf = hInst;
                OutputDebugStringA("[MbKit] 自 PIN 失败（疑似手工映射），已用引用计数兜底\n");
            }
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (!reserved) {
            /* 进程正常退出：尽量干净地收尾（内核此时反正也要销毁）。
             * ★ 这里必须**关掉消息泵**：DllMain(DETACH) 里再派发消息是危险的
             *   （窗口/模块正在被拆掉）。 */
            mbk_pump_enable(0);
            /* ★ 任务5：进程退出路径必须**同步**销毁（reserved!=0 时不能再拉起/等待回收线程） */
            if (G.ready && G.DestroyWebView) detach_all_impl(0);
            G_lockReady = 0;
            DeleteCriticalSection(&G_lock);
        }
    }
    return TRUE;
}
