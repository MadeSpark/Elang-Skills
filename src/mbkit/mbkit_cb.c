/* ============================================================================
 * mbkit_cb.c —— 回调登记与「野回调」拦截层
 *
 * 【要解决什么】
 * 原模块把「自己模块内的子程序」（经 _stdcall_to_cdecl 生成的 thunk）直接
 * 注册给内核。业务 DLL 一旦 FreeLibrary，内核手里就攥着指向已释放代码页的
 * 指针，下一次事件触发即跳进空地 —— 实测宿主在「卸载后第一条窗口消息」
 * 就 SIGSEGV(139)。这就是「必须重启宿主 exe」的机制。
 *
 * 【怎么解决】
 * 内核永远只看到 MbKit 里的 wrapper（随 MbKit 常驻，永不失效）；
 * 业务的 thunk 只作为**数据**存在登记表里。wrapper 每次被调用都先校验
 * 「业务回调是否仍然有效」，无效就丢弃并计数。于是：
 *   - 正常路径：业务先 DetachAll 再卸载 —— 干净注销；
 *   - 异常路径：忘了解绑直接卸载 —— 也只会被静默挡下，不崩。
 *
 * 【存活判据为何是三路】（2026-09-25 修：加第 ③ 路，事故级）
 * 业务回调有两种形态，不能一刀切：
 *   ① 真实的模块内函数  -> 直接用 GetModuleHandleEx(FROM_ADDRESS, cb) 查该模块；
 *   ② VirtualAlloc 出来的 thunk（本模块的 _stdcall_to_cdecl 就是这种）
 *      -> 它**不属于任何模块**，永远"可读"，FROM_ADDRESS 查不到；
 *         真正会失效的是 thunk 内嵌的业务子程序地址，无法从外部直接取用，
 *         所以改用「注册那一刻调用点的返回地址」所属模块来代表业务身份。
 *   ③ **两条路都查不到时，绝不能再「保守丢弃」**。
 *      实测事故（2026-09-25）：宿主「小栗子虚拟机」是**手工映射**加载插件 DLL 的
 *      （插件 DLL 不在进程加载器链表里，`EnumProcessModules` 里查不到），
 *      于是 ② 的 regRet 也落在一段"不属于任何模块"的映像里 ⇒ 两条路全返回 0
 *      ⇒ 每一个回调都被静默丢掉 ⇒ **事件全哑、而且不崩**（
 *      `wkeOnPaintUpdated` 一起丢 ⇒ 窗口缩放后新暴露区域永不重绘 ⇒ 画面撕裂）。
 *      用户完全看不出是回调被挡了。
 *      ⇒ 判不出来 ≠ 已经失效。此时**放行**，另用「注册点那段内存是否还在」
 *        （VirtualQuery，见 region_ok）兜底；确实被释放了才丢弃。
 *      放行的代价只是失去自动保护（业务方仍可显式 DetachAll），
 *      而「保守丢弃」的代价是功能全废 —— 两者不对称。
 * ==========================================================================*/
#include <windows.h>
#include <string.h>

#include "gen_fwd.h"
#include "mbkit_api.h"
#include "mbk_marshal.h"

#define MBK_CB_MAX 256

typedef struct {
    int      used;
    int      idx;        /* 回调注册类 API 的 MBK_IDX_*，兼作 kind */
    void*    wv;         /* webView，用于按 view 注销 */
    void*    bizCb;      /* 业务给的回调（cdecl） */
    void*    bizParam;
    unsigned cbMod;      /* bizCb 所属模块；0 = thunk（非模块内存） */
    unsigned regRet;     /* 注册时调用点的返回地址 */
    unsigned regMod;     /* regRet 所属模块 —— thunk 形态下的业务身份 */
    int      hits;       /* 成功转发次数 */
    int      blocked;    /* 被判为野指针而挡下的次数 */
} MBCB;

static MBCB          g_cb[MBK_CB_MAX];
static CRITICAL_SECTION g_cbLock;
static LONG          g_cbLockState = 0;   /* 0=未建 1=建设中 2=就绪 */
static LONG          g_blkTotal = 0;
static LONG          g_hitTotal = 0;

static void cb_lock(void)
{
    LONG s = g_cbLockState;
    if (s != 2) {
        if (InterlockedCompareExchange(&g_cbLockState, 1, 0) == 0) {
            InitializeCriticalSection(&g_cbLock);
            InterlockedExchange(&g_cbLockState, 2);
        } else {
            while (InterlockedCompareExchange(&g_cbLockState, 0, 0) != 2) Sleep(0);
        }
    }
    EnterCriticalSection(&g_cbLock);
}
static void cb_unlock(void) { LeaveCriticalSection(&g_cbLock); }

/* 所属模块；0 表示不属于任何已加载模块 */
static unsigned mod_of(void* addr)
{
    HMODULE hm = NULL;
    if (!addr) return 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(UINT_PTR)addr, &hm) && hm)
        return (unsigned)(UINT_PTR)hm;
    return 0;
}

/* 「地址仍属于同一个模块」—— 模块被卸载后反查会失败或换主，即判定失效 */
static int addr_same_module(void* addr, unsigned expect)
{
    HMODULE hm = NULL;
    if (!addr) return 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(UINT_PTR)addr, &hm) || !hm)
        return 0;
    return (unsigned)(UINT_PTR)hm == expect;
}

/* 一条地址所处内存区是否还在（可读）。
 * 用途：业务模块查不到时（手工映射加载的插件 DLL），这是唯一还看得见的信号 ——
 * 宿主一旦释放那段映像，State 会变成 MEM_FREE。 */
static int region_ok(void* addr)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!addr) return 0;
    if (VirtualQuery(addr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;                 /* 已被释放/未提交 */
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return 1;
}

static int entry_alive(MBCB* e)
{
    if (!e || !e->used) return 0;

    if (e->cbMod)                                     /* ① 模块内函数 */
        return addr_same_module(e->bizCb, e->cbMod);

    if (e->regMod)                                    /* ② thunk：看注册者 */
        return addr_same_module((void*)(UINT_PTR)e->regRet, e->regMod);

    /* ③ 两条路都判不出业务身份（业务 DLL 不在加载器链表里，如宿主手工映射加载）。
     *    —— 放行！判不出来 ≠ 已失效。只做「那段内存还在不在」的兜底，
     *    且 VirtualQuery 不便宜 ⇒ 每 128 次才真查一次。 */
    if (e->hits % 128) return 1;
    return region_ok((void*)(UINT_PTR)e->regRet);
}

static void note_blocked(MBCB* e)
{
    InterlockedIncrement(&g_blkTotal);
    if (e) InterlockedIncrement((LONG*)&e->blocked);
}

/* 按 (idx, wv) 找已有登记 */
static MBCB* cb_find(int idx, void* wv)
{
    int i;
    for (i = 0; i < MBK_CB_MAX; i++)
        if (g_cb[i].used && g_cb[i].idx == idx && g_cb[i].wv == wv)
            return &g_cb[i];
    return NULL;
}

/* 释放该登记（并不通知内核；由调用方决定是否注销） */
static void cb_release(MBCB* e)
{
    if (!e) return;
    e->used = 0; e->wv = NULL; e->bizCb = NULL; e->bizParam = NULL;
    e->cbMod = e->regMod = e->regRet = 0;
}

typedef int (__cdecl *mb_cb10)(void*, void*, void*, void*, void*, void*,
                               void*, void*, void*, void*);

/* 内核调用统一走导出边界编组（mbk_marshal_3）—— 内核永远在常驻线程 K 上，
 * 回调登记/注销若直调内核（旧版 MBK_REAL 直通），跨线程必挂死（实测）。 */
static int cb_real_reg(int idx, void* wv, void* cb, void* param)
{
    return (int)mbk_marshal_3(idx, (unsigned)wv, (unsigned)cb, (unsigned)param);
}

/* 统一登记：复用同 (idx, wv) 的槽位，避免表被反复注册撑爆 */
static int cb_adopt(int idx, void* wv, void* cb, void* param, void* wrap)
{
    MBCB* e;
    int   r;

    cb_lock();
    e = cb_find(idx, wv);
    if (!e) {
        int i;
        for (i = 0; i < MBK_CB_MAX; i++) { if (!g_cb[i].used) { e = &g_cb[i]; break; } }
        if (!e) {
            /* 表满：先回收「业务模块已卸载」的失效登记 —— 这让长期反复装卸
             * 也能自愈，而不需要业务方一定记得调用 DetachAll。 */
            for (i = 0; i < MBK_CB_MAX; i++) {
                if (g_cb[i].used && !entry_alive(&g_cb[i])) { e = &g_cb[i]; break; }
            }
        }
    }
    if (!e) {                                         /* 表满：退回「不代理」，
                                                       * 功能不受影响，只是失去保护 */
        cb_unlock();
        return cb_real_reg(idx, wv, cb, param);
    }

    e->used     = 1;
    e->idx      = idx;
    e->wv       = wv;
    e->bizCb    = cb;
    e->bizParam = param;
    e->cbMod    = mod_of(cb);
    /* 注册那一刻调用点的返回地址 —— 对 thunk 形态的回调，它是唯一的业务身份线索 */
    e->regRet   = (unsigned)(UINT_PTR)__builtin_return_address(0);
    e->regMod   = mod_of((void*)(UINT_PTR)e->regRet);
    cb_unlock();

    r = cb_real_reg(idx, wv, wrap, e);
    return r;
}

/* 传 0 回调 = 关闭该事件（模块里大量这么用），必须原样转发，不得代理 */
static int cb_forget(int idx, void* wv)
{
    MBCB* e;
    cb_lock();
    e = cb_find(idx, wv);
    cb_release(e);
    cb_unlock();
    return 0;
}

/* ---- 22 个回调注册类：wrapper + 同名导出（名字与内核一致）---- */
#define DEF_CB(fn)                                                            \
static int __cdecl mbwrap_##fn(void* wv, void* wp, void* a3, void* a4,        \
                               void* a5, void* a6, void* a7, void* a8,        \
                               void* a9, void* a10)                           \
{                                                                             \
    MBCB* e = (MBCB*)wp;                                                      \
    if (!entry_alive(e)) { note_blocked(e); return 0; }                       \
    InterlockedIncrement(&g_hitTotal);                                        \
    InterlockedIncrement((LONG*)&e->hits);                                    \
    return ((mb_cb10)e->bizCb)(wv, e->bizParam, a3, a4, a5, a6, a7, a8,       \
                               a9, a10);                                      \
}                                                                             \
int __cdecl fn(void* wv, void* cb, void* param)                               \
{                                                                             \
    mbk_ensure();                                                            \
    if (!cb) {                                                                \
        cb_forget(MBK_IDX_##fn, wv);                                          \
        return cb_real_reg(MBK_IDX_##fn, wv, NULL, NULL);                     \
    }                                                                         \
    return cb_adopt(MBK_IDX_##fn, wv, cb, param, (void*)&mbwrap_##fn);        \
}

DEF_CB(wkeOnAlertBox)
DEF_CB(wkeOnConfirmBox)
DEF_CB(wkeOnConsole)
DEF_CB(wkeOnCreateView)
DEF_CB(wkeOnDidCreateScriptContext)
DEF_CB(wkeOnDocumentReady)
DEF_CB(wkeOnDocumentReady2)
DEF_CB(wkeOnDownload)
DEF_CB(wkeOnLoadUrlBegin)
DEF_CB(wkeOnLoadUrlEnd)
DEF_CB(wkeOnLoadingFinish)
DEF_CB(wkeOnMouseOverUrlChanged)
DEF_CB(wkeOnNavigation)
DEF_CB(wkeOnPaintUpdated)
DEF_CB(wkeOnPromptBox)
DEF_CB(wkeOnTitleChanged)
DEF_CB(wkeOnURLChanged)
DEF_CB(wkeOnURLChanged2)
DEF_CB(wkeOnWillMediaLoad)
DEF_CB(wkeOnWillReleaseScriptContext)
DEF_CB(wkeOnWindowClosing)
DEF_CB(wkeOnWindowDestroy)

/* --------------------------------------------------------------------------
 * 解绑：把全部仍在活动的登记逐一注销（用真函数 + NULL 回调）
 * 必须在业务 DLL 卸载前调用；即使漏调，wrapper 的存活校验也会兜住。
 * ------------------------------------------------------------------------*/
void mb_cb_detach_all(void)
{
    struct { int idx; void* wv; } pend[MBK_CB_MAX];
    int i, n = 0;

    /* 先在锁内只做摘表，内核调用一律放到锁外 ——
     * 否则内核在注销过程中若回调进来（它可能触发别的登记），会与同一把锁互等。 */
    cb_lock();
    for (i = 0; i < MBK_CB_MAX; i++) {
        if (!g_cb[i].used) continue;
        pend[n].idx = g_cb[i].idx;
        pend[n].wv  = g_cb[i].wv;
        n++;
        cb_release(&g_cb[i]);
    }
    cb_unlock();

    for (i = 0; i < n; i++)
        cb_real_reg(pend[i].idx, pend[i].wv, NULL, NULL);
}

/* 只解绑某一个 view 上的全部代理回调 —— 多浏览器实例场景下比 DetachAll 精准 */
int __stdcall MbKit_DetachView(void* wv)
{
    struct { int idx; void* wv; } pend[MBK_CB_MAX];
    int i, n = 0;

    if (!wv) return 0;
    cb_lock();
    for (i = 0; i < MBK_CB_MAX; i++) {
        MBCB* e = &g_cb[i];
        if (!e->used || e->wv != wv) continue;
        pend[n].idx = e->idx;
        pend[n].wv  = e->wv;
        n++;
        cb_release(e);
    }
    cb_unlock();

    for (i = 0; i < n; i++)
        cb_real_reg(pend[i].idx, pend[i].wv, NULL, NULL);
    return n;
}

/* --------------------------------------------------------------------------
 * 诊断接口
 * ------------------------------------------------------------------------*/
int __stdcall MbKit_CbCount(void)    { int i, n = 0; cb_lock();
                                       for (i = 0; i < MBK_CB_MAX; i++) if (g_cb[i].used) n++;
                                       cb_unlock(); return n; }
int __stdcall MbKit_CbHits(void)     { int i, n = 0; cb_lock();
                                       for (i = 0; i < MBK_CB_MAX; i++) if (g_cb[i].used) n += g_cb[i].hits;
                                       cb_unlock(); return n; }
int __stdcall MbKit_CbBlocked(void)  { return (int)g_blkTotal; }
int __stdcall MbKit_CbLive(void)     { int i, n = 0; cb_lock();
                                       for (i = 0; i < MBK_CB_MAX; i++)
                                           if (g_cb[i].used && entry_alive(&g_cb[i])) n++;
                                       cb_unlock(); return n; }
