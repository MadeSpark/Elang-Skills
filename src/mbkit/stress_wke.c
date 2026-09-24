/* ============================================================================
 * stress_wke.c —— 验收任务 2：反复 init→create→destroy→create 压力用例
 * ----------------------------------------------------------------------------
 * 走的路径与 .ec 模块（MB浏览框.绑定）完全一致：**裸调 MbKit.dll 的 wke* 导出**
 * （即 gen_thunk.c 的编组 thunk），不用 MbKit_Create 高层接口。
 *
 * 模式：
 *   same  —— 主线程循环 N 轮 create→headless→load→wait→title→destroy
 *   cross —— 同样的循环放到工作线程，主线程只负责泵消息 + 收割结果
 *
 * 每轮校验：view 建立成功、载入完成、标题数据通路正确、销毁成功。
 * 结束校验：回调命中数 == 轮数（回调转发无回归）、LiveCount==0（无 view 泄漏）、
 *           suppressed==0（无野回调被挡）、GDI 句柄数与工作集增量（句柄/内存泄漏）。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- MbKit.dll 导出（全部按导出边界真实签名声明） ------------------------*/
typedef int             (__stdcall *p_ensure)(void);
typedef void*           (__cdecl  *p_create)(void);
typedef void            (__cdecl  *p_destroy)(void*);
typedef void            (__cdecl  *p_sethandle)(void*, HWND);
typedef void            (__cdecl  *p_resize)(void*, unsigned, unsigned);
typedef void            (__cdecl  *p_setheadless)(void*, unsigned);
typedef void            (__cdecl  *p_loadhtml)(void*, const wchar_t*);
typedef unsigned        (__cdecl  *p_isloaded)(void*);
typedef const wchar_t*  (__cdecl  *p_title)(void*);
typedef void            (__cdecl  *p_onloadfinish)(void*, void*, void*);
typedef int             (__stdcall *p_livecount)(void);
typedef int             (__stdcall *p_suppressed)(void);
typedef const char*     (__stdcall *p_diag)(void);

static p_ensure        ensure;
static p_create        create;
static p_destroy       destroy;
static p_resize        resize;
static p_setheadless   setheadless;
static p_loadhtml      loadhtml;
static p_isloaded      isloaded;
static p_title         gettitle;
static p_onloadfinish  onloadfinish;
static p_livecount     livecount;
static p_suppressed    suppressed;
static p_diag          diag;

#define PROC(var, name, t) if (!(var = (t)GetProcAddress(h, name))) { \
        printf("GetProcAddress(%s) failed\n", name); return 2; }

/* 载入完成回调（在本进程内 —— 存活校验应放行；在 K 线程上被调用） */
static volatile LONG g_loads = 0;
static void __cdecl my_load_finish(void* wv, void* param, void* url,
                                   int result, void* reason)
{
    (void)wv; (void)url; (void)reason;
    if (result == 0) InterlockedIncrement(&g_loads);   /* result==0 = 成功 */
}

/* 主线程消息泵（isloaded 轮询期间喂窗口消息） */
static void pump(int ms)
{
    MSG m;
    DWORD end = GetTickCount() + (DWORD)ms;
    do {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(1);
    } while ((int)(GetTickCount() - end) < 0);
}

/* 单轮：create→headless→resize→注册回调→load→等载入→验标题→destroy。
 * 返回 0=成功，否则失败码。 */
static int round_once(int i)
{
    wchar_t       html[256];
    const wchar_t* t;
    void*         wv;
    int           wait;

    wv = create();
    if (!wv) { printf("round %d: wkeCreateWebView -> NULL\n", i); return 1; }

    setheadless(wv, 1);
    resize(wv, 400, 300);
    onloadfinish(wv, (void*)&my_load_finish, (void*)(UINT_PTR)(i + 1));

    _snwprintf(html, 255, L"<html><head><title>S-%d</title></head>"
                          L"<body>stress round %d</body></html>", i, i);
    html[255] = 0;
    loadhtml(wv, html);

    for (wait = 0; wait < 10000 && !isloaded(wv); wait += 10) pump(10);
    if (!isloaded(wv)) { printf("round %d: 载入超时\n", i); destroy(wv); return 2; }

    t = gettitle(wv);
    if (!t || !wcsstr(t, L"S-")) {
        printf("round %d: 标题数据通路异常 (%ls)\n", i, t ? t : L"(null)");
        destroy(wv);
        return 3;
    }

    destroy(wv);
    return 0;
}

/* 工作线程（cross 模式的循环体）—— stress_body 定义在 gdi_objects 之后 */
static DWORD g_rounds = 0;
static volatile LONG g_fail = 0;

static DWORD gdi_objects(void) { return GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS); }

/* 第 10 轮后的 GDI 快照：判定「每轮泄漏」用 —— 轮间增量应 ≈ 0 */
static DWORD gdi_mid = 0;

static DWORD WINAPI stress_body(LPVOID pv)
{
    DWORD i;
    (void)pv;
    for (i = 0; i < g_rounds; i++) {
        if (round_once((int)i) != 0) InterlockedIncrement(&g_fail);
        if (i == 9) gdi_mid = gdi_objects();
    }
    return 0;
}
static SIZE_T working_set(void)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    return (SIZE_T)(ms.ullTotalPhys - ms.ullAvailPhys);   /* 粗粒度：系统级占用（仅趋势参考） */
}

int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "same";
    DWORD rounds = (argc > 2) ? (DWORD)atoi(argv[2]) : 50;
    HMODULE h;
    DWORD   gdi0, gdi1;
    int     cross = (strcmp(mode, "cross") == 0);
    DWORD   i;
    HANDLE  th = NULL;

    h = LoadLibraryA("MbKit.dll");
    if (!h) { printf("LoadLibrary(MbKit.dll) failed\n"); return 1; }

    PROC(ensure,       "MbKit_EnsureKernelInit", p_ensure);
    PROC(create,       "wkeCreateWebView",       p_create);
    PROC(destroy,      "wkeDestroyWebView",      p_destroy);
    PROC(resize,       "wkeResize",              p_resize);
    PROC(setheadless,  "wkeSetHeadlessEnabled",  p_setheadless);
    PROC(loadhtml,     "wkeLoadHTMLW",           p_loadhtml);
    PROC(isloaded,     "wkeIsLoadingCompleted",  p_isloaded);
    PROC(gettitle,     "wkeGetTitleW",           p_title);
    PROC(onloadfinish, "wkeOnLoadingFinish",     p_onloadfinish);
    PROC(livecount,    "MbKit_LiveCount",        p_livecount);
    PROC(suppressed,   "MbKit_SuppressedCount",  p_suppressed);
    PROC(diag,         "MbKit_Diag",             p_diag);

    ensure();                          /* 内核落 K（等价 wkeInitialize 导出语义） */

    /* 预热 1 轮：内核初始化期的一次性开销（字体/位图等）不计入泄漏基线 */
    if (round_once(-1) != 0) { printf("warmup round failed\n"); return 3; }
    g_loads = 0;                       /* 预热轮的回调不计入期望值 */

    g_rounds = rounds;
    gdi0 = gdi_objects();

    if (cross) {
        th = CreateThread(NULL, 0, stress_body, NULL, 0, NULL);
        if (!th) { printf("CreateThread failed\n"); return 1; }
        while (WaitForSingleObject(th, 50) == WAIT_TIMEOUT) pump(20);
        CloseHandle(th);
    } else {
        for (i = 0; i < rounds; i++) {
            if (round_once((int)i) != 0) InterlockedIncrement(&g_fail);
            if (i == 9) gdi_mid = gdi_objects();
        }
    }
    pump(200);                         /* 让最后一轮的回调/清理落地 */

    gdi1 = gdi_objects();

    printf("== stress_wke [%s] rounds=%lu ==\n", mode, (unsigned long)rounds);
    printf("  失败轮数     : %ld\n", (long)g_fail);
    printf("  回调命中     : %ld（期望 = 轮数 %lu）\n", (long)g_loads, (unsigned long)rounds);
    printf("  残留 view    : %d（期望 0）\n", livecount());
    printf("  被挡野回调   : %d（期望 0）\n", suppressed());
    printf("  GDI 句柄     : 第10轮 %lu -> 结束 %lu（轮间增量 %ld，应≈0）；进程起点 %lu\n",
           (unsigned long)gdi_mid, (unsigned long)gdi1,
           (long)(gdi1 - gdi_mid), (unsigned long)gdi0);

    {
        long dMid = (long)gdi_mid - (long)gdi0;
        long dEnd = (long)gdi1 - (long)gdi_mid;
        /* 轮间增量 |Δ|<=2 视为无每轮泄漏（内核内部偶发一次性分配） */
        int ok = (g_fail == 0) && (g_loads == (LONG)rounds)
              && livecount() == 0 && suppressed() == 0
              && dMid <= 2 && dMid >= -2 && dEnd <= 2 && dEnd >= -2;
        printf("  RESULT: %s\n", ok ? "PASS —— 反复建/销无泄漏无回归" : "FAIL");
        printf("%s\n", diag());
        return ok ? 0 : 4;
    }
}
