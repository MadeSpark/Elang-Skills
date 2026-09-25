/* ============================================================================
 * probe_cbdrop.c —— 复现「业务调用点不属于任何已加载模块 ⇒ 全部 wkeOn* 回调被静默丢弃」
 * ----------------------------------------------------------------------------
 * 背景（2026-09-25 定位）：
 *   mbkit_cb.c 的 entry_alive() 要用「业务身份」判断回调是否还活着：
 *     ① cbMod   = mod_of(bizCb)        —— 业务回调所在模块
 *     ② regMod  = mod_of(regRet)       —— 注册那一刻业务调用点所在模块
 *   而**手工映射**加载的插件 DLL 不在进程加载器链表里（EnumProcessModules 查不到），
 *   GetModuleHandleExA(FROM_ADDRESS) 对它一律返回 0 —— 于是 ①② 同时落空。
 *   旧实现在这种情形下**保守丢弃**（return 0）⇒
 *     每一个 wkeOn* 回调都不转发：
 *       · wkeOnPaintUpdated 被丢 ⇒ 窗口缩放后新暴露区域永不重绘 ⇒ 画面撕裂
 *       · wkeOnLoadUrlBegin 等被丢 ⇒ 所有事件「没有任何反应」，而且**不崩**
 *   判不出来 ≠ 已经失效。新实现改为「两路都判不出则放行，只做 VirtualQuery 兜底」。
 *
 * 本探针把「业务回调桩」和「业务调用桩」**都放进 VirtualAlloc 出来的 RWX 页**，
 * 精确等价于「业务模块被手工映射」这一情形（两个地址都不属于任何模块）：
 *
 *   调用桩（26 B）:  push param; push cb; push wv; call [wkeOnLoadUrlBegin]; ret
 *   回调桩（14 B）:  inc [hits]; xor eax,eax; ret
 *
 * 关键点：调用桩里的 `call eax` 压进去的返回地址**落在那段 RWX 页里**
 * ⇒ wkeOnLoadUrlBegin 尾调用 cb_adopt 后，__builtin_return_address(0) 正好取到它
 * ⇒ regMod = 0；而 bizCb 也在同一页 ⇒ cbMod = 0 ⇒ 必然走 ③ 分支。
 *
 * 同时跑一个**控制组**：回调函数直接住在 probe_cbdrop.exe（真模块）里，
 * 由 exe 直接调用 wkeOnLoadUrlBegin 注册 ⇒ cbMod != 0 ⇒ 走 ① 分支 ⇒ 两版都应通过。
 * 控制组通过、实验组只在旧版失败，才说明差异来自 ③ 分支而不是探针本身。
 *
 * 用法：probe_cbdrop.exe <MbKit完整路径> <node.dll完整路径>
 * 输出：纯 ASCII，最后一行是机器可读的判据行。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void  (__cdecl *fn_v0)(void);
typedef void* (__cdecl *fn_p0)(void);
typedef void  (__cdecl *fn_v2)(void*, const char*);
typedef void  (__cdecl *fn_v3)(void*, int, int);
typedef void  (__cdecl *fn_reg)(void*, void*, void*);
typedef int   (__cdecl *fn_i0)(void);
typedef int   (__cdecl *fn_i1)(const char*);

static fn_v0   p_wkeInitialize;
static fn_p0   p_wkeCreateWebView;
static fn_v2   p_wkeLoadURL;
static fn_v3   p_wkeResize;
static fn_reg  p_wkeOnLoadUrlBegin;

static int (__cdecl *p_MbKit_SuppressedCount)(void);
static int (__cdecl *p_MbKit_CbBlocked)(void);
static int (__cdecl *p_MbKit_CbHits)(void);
static int (__cdecl *p_MbKit_CbCount)(void);
static int (__cdecl *p_MbKit_Init)(const char*);

/* ---------------- 控制组：回调住在 exe（真模块）里 ---------------- */
static volatile LONG g_hitsCtrl = 0;
static volatile LONG g_hitsStub = 0;

static int __cdecl ctrl_cb(void* wv, void* param, const char* url, void* job)
{
    (void)wv; (void)param; (void)job; (void)url;
    InterlockedIncrement(&g_hitsCtrl);
    return 0;
}

/* ---------------- 消息泵：内核的载入任务挂在消息队列上 ---------------- */
static void pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG m;
    while ((int)(GetTickCount() - end) < 0) {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(2);
    }
}

/* ---------------- 在 RWX 页里搭出两个桩 ---------------- */
static unsigned char* build_stubs(void)
{
    unsigned char* p = (unsigned char*)VirtualAlloc(NULL, 4096,
                          MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char cb[14], caller[26];
    DWORD old;
    void* hitsAddr = (void*)&g_hitsStub;
    void* regAddr  = (void*)p_wkeOnLoadUrlBegin;
    int i = 0;

    if (!p) return NULL;

    /* 回调桩：inc [hitsAddr]; xor eax,eax; ret
     *   A1 <abs32>   mov eax,[abs]
     *   40           inc eax
     *   A3 <abs32>   mov [abs],eax
     *   31 C0        xor eax,eax
     *   C3           ret
     * 注意：用 eax 做临时寄存器是安全的 —— 本桩返回值只用 eax。 */
    cb[i++] = 0xA1; memcpy(cb + i, &hitsAddr, 4); i += 4;
    cb[i++] = 0x40;
    cb[i++] = 0xA3; memcpy(cb + i, &hitsAddr, 4); i += 4;
    cb[i++] = 0x31; cb[i++] = 0xC0;
    cb[i++] = 0xC3;

    /* 调用桩：void caller(void* wv, void* cb, void* param)
     * 入口 esp: [0]=ret, [4]=wv, [8]=cb, [12]=param
     *   8B 44 24 0C   mov eax,[esp+0Ch]
     *   50            push eax
     *   ... 重复三次取出 param / cb / wv（cdecl 自右向左压栈）
     *   B8 <abs32>    mov eax, imm32(wkeOnLoadUrlBegin)
     *   FF D0         call eax
     *   83 C4 0C      add esp,0Ch
     *   C3            ret                                                   */
    i = 0;
    for (int k = 0; k < 3; k++) {
        caller[i++] = 0x8B; caller[i++] = 0x44; caller[i++] = 0x24; caller[i++] = 0x0C;
        caller[i++] = 0x50;
    }
    caller[i++] = 0xB8; memcpy(caller + i, &regAddr, 4); i += 4;
    caller[i++] = 0xFF; caller[i++] = 0xD0;
    caller[i++] = 0x83; caller[i++] = 0xC4; caller[i++] = 0x0C;
    caller[i++] = 0xC3;

    memcpy(p, cb, sizeof(cb));
    memcpy(p + 0x40, caller, sizeof(caller));

    VirtualProtect(p, 4096, PAGE_EXECUTE_READWRITE, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 4096);
    printf("  RWX 页 = %p  回调桩 = %p  调用桩 = %p\n", (void*)p, (void*)p, (void*)(p + 0x40));
    return p;
}

static int main_unused_guard(void) { return 0; }

int main(int argc, char** argv)
{
    HMODULE h;
    void* view = NULL;
    unsigned char* page;
    void* cbAddr;
    void* callerAddr = NULL;
    int sup0, supCtrl, supStub;
    const char* kit = (argc > 1) ? argv[1] : "MbKit.dll";
    const char* node = (argc > 2) ? argv[2] : NULL;
    int i;

    SetErrorMode(SEM_NOGPFAULTERRORBOX);
    printf("================ probe_cbdrop ================\n");
    printf("MbKit : %s\n", kit);

    h = LoadLibraryA(kit);
    if (!h) { printf("!! LoadLibrary 失败 err=%lu\n", GetLastError()); return 2; }

    /* 只取两版 DLL 都有的导出，保证「旧 vs 新」可比 */
    #define G(v, n) do { *(FARPROC*)&v = GetProcAddress(h, n); \
                         if (!v) { printf("!! 缺导出 %s\n", n); return 2; } } while (0)
    G(p_MbKit_Init,             "MbKit_Init");
    G(p_MbKit_SuppressedCount,  "MbKit_SuppressedCount");
    G(p_wkeInitialize,          "wkeInitialize");
    G(p_wkeCreateWebView,       "wkeCreateWebView");
    G(p_wkeLoadURL,             "wkeLoadURL");
    G(p_wkeResize,              "wkeResize");
    G(p_wkeOnLoadUrlBegin,      "wkeOnLoadUrlBegin");
    #undef G
    *(FARPROC*)&p_MbKit_CbBlocked = GetProcAddress(h, "MbKit_CbBlocked");
    *(FARPROC*)&p_MbKit_CbHits    = GetProcAddress(h, "MbKit_CbHits");
    *(FARPROC*)&p_MbKit_CbCount   = GetProcAddress(h, "MbKit_CbCount");

    (void)main_unused_guard;
    (void)callerAddr;

    if (p_MbKit_Init(node) != 0) { printf("!! MbKit_Init 失败 err=%lu\n", GetLastError()); return 2; }
    printf("内核已就绪\n");

    p_wkeInitialize();
    view = p_wkeCreateWebView();
    if (!view) { printf("!! wkeCreateWebView 返回 0（内核未初始化？）\n"); return 2; }
    p_wkeResize(view, 800, 600);
    printf("view = %p\n\n", view);

    page = build_stubs();
    if (!page) { printf("!! VirtualAlloc 失败\n"); return 2; }
    cbAddr = (void*)page;

    /* ---- 控制组：回调 + 注册调用点**都在 exe 模块里**（走 ①/② 分支）---- */
    printf("[控制组] 回调与调用点都在 exe 模块内\n");
    sup0 = p_MbKit_SuppressedCount();
    p_wkeOnLoadUrlBegin(view, (void*)ctrl_cb, NULL);
    p_wkeLoadURL(view, "http://127.0.0.1:9/probe-ctrl");
    pump(1500);
    supCtrl = p_MbKit_SuppressedCount();
    printf("  回调命中 = %ld   新增被挡 = %d\n\n", g_hitsCtrl, supCtrl - sup0);

    /* ---- 实验组：回调 + 注册调用点都在 RWX 页（= 手工映射插件，走 ③ 分支）---- */
    printf("[实验组] 回调与调用点都在 VirtualAlloc 页内（等价于手工映射的插件 DLL）\n");
    sup0 = p_MbKit_SuppressedCount();
    {
        typedef void (__cdecl *fn_caller)(void*, void*, void*);
        fn_caller call = (fn_caller)(page + 0x40);
        for (i = 0; i < 3; i++) {
            call(view, cbAddr, NULL);          /* 注册 */
            p_wkeLoadURL(view, "http://127.0.0.1:9/probe-stub");
            pump(1200);
        }
    }
    supStub = p_MbKit_SuppressedCount();
    printf("  回调命中 = %ld   新增被挡 = %d\n", g_hitsStub, supStub - sup0);
    if (p_MbKit_CbCount)   printf("  CbCount=%d  CbHits=%d  CbBlocked=%d\n",
                                 p_MbKit_CbCount(), p_MbKit_CbHits ? p_MbKit_CbHits() : -1,
                                 p_MbKit_CbBlocked ? p_MbKit_CbBlocked() : -1);
    printf("\n");
    printf("RESULT ctrl_hits=%ld ctrl_blocked=%d stub_hits=%ld stub_blocked=%d => %s\n",
           g_hitsCtrl, supCtrl,
           g_hitsStub, supStub - sup0,
           (g_hitsStub > 0) ? "STUB-CB-FORWARDED" : "STUB-CB-DROPPED");
    return 0;
}
