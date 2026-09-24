/* ============================================================================
 * mb_reinit.c —— 「窗口句柄绑定 + 二次初始化」最小复现台
 * ----------------------------------------------------------------------------
 * 用户报告（逐字）：
 *   「宿主 exe 载入使用 miniblink 的 DLL 模块 → 内核与**首个**窗口句柄绑死；
 *     销毁 + 重新载入 DLL 也救不回来；必须重启宿主进程。」
 *   「用线程启动窗口使用 mb 浏览框，然后关闭，再创建窗口使用 mb 浏览框
 *     → 程序直接卡死然后闪退。」
 *
 * 已有 host_reload.c 的 mbkit/fault 模式**每轮复用同一个 hwnd**，
 * 因此从未覆盖「窗口被销毁 → 新建窗口 → 重新绑定」这条维度。
 * 本文件专门补这条维度，并同时给出**裸 node.dll 对照组**，
 * 用来判定缺陷到底住在 node.dll 还是 MbKit。
 *
 * 模式：
 *   same         同线程：init → wv1@hwnd1 → 销毁 wv1 → DestroyWindow(hwnd1)
 *                      → 新建 hwnd2 → wv2@hwnd2（→ 载入）
 *   thread_end   跨线程且**初始化线程结束**：init + 第1个窗口全在 T1 做，
 *                      T1 退出（窗口已销毁）后，再到主线程建 wv2@hwnd2
 *   thread_alive 跨线程但**初始化线程存活**：同上，但 T1 建完 wv2 前不退出
 *   raw_same     裸 node.dll 对照组：同 same
 *   raw_thread_end 裸 node.dll 对照组：同 thread_end
 *
 * 用法：mb_reinit.exe <mode> [logfile]
 * 全部输出 ASCII（日志文件 UTF-8），每步都打点，便于定位「卡在哪一步」。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "mbkit_api.h"
#include "mbload.h"

/* ---------------- 日志 ---------------- */
static FILE* g_log = NULL;

static void LOG(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

/* ---------------- 消息泵（当前线程队列） ---------------- */
static int pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)(ms < 0 ? 0 : ms);
    MSG   m;
    int   n = 0;
    do {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
            n++;
        }
        Sleep(2);
    } while ((int)(GetTickCount() - end) < 0);
    return n;
}

/* ---------------- 离屏窗口 ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

static volatile LONG g_wndSeq = 0;

static HWND make_window(int dx, int dy)
{
    WNDCLASSA wc;
    HWND      h;
    LONG      k = InterlockedIncrement(&g_wndSeq);
    char      cls[64];
    _snprintf(cls, sizeof(cls) - 1, "MbReinitHostWnd_%ld", k);
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);
    /* 放到屏幕外；用 DWM 披斗篷避免闪屏 */
    h = CreateWindowExA(0, cls, "MbReinitHost", WS_POPUP,
                        6000 + dx, 6000 + dy, 800, 600, NULL, NULL, wc.hInstance, NULL);
    if (!h) return NULL;
    {
        HMODULE dwm = LoadLibraryA("dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *pDwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
            pDwmSetWindowAttribute f =
                (pDwmSetWindowAttribute)GetProcAddress(dwm, "DwmSetWindowAttribute");
            BOOL cloak = TRUE;
            if (f) f(h, 13 /*DWMWA_CLOAK*/, &cloak, sizeof(cloak));
        }
    }
    ShowWindow(h, SW_SHOWNOACTIVATE);
    pump(50);
    return h;
}

static wchar_t* make_html(int round, wchar_t* buf, int cch)
{
    _snwprintf(buf, cch - 1,
               L"<html><head><meta charset=\"utf-8\"><title>R-%d</title></head>"
               L"<body style=\"font:16px sans-serif\"><h1>r%d</h1></body></html>",
               round, round);
    buf[cch - 1] = 0;
    return buf;
}

static void w_to_a(const wchar_t* buf, char* out, size_t cch)
{
    if (!buf) { out[0] = 0; return; }
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, (int)cch, NULL, NULL);
}

/* ==========================================================================
 * 一、MbKit 路径
 * ========================================================================*/
typedef struct {
    MBKIT* K;
    HWND   hwnd;
    int    round;
    void*  view;
    char   title[256];
    int    loaded;
} ROUND;

static int round_attach(ROUND* r)
{
    r->hwnd = make_window(r->round * 20, r->round * 20);
    LOG("   [r%d] make_window  -> hwnd=0x%08X", r->round, (unsigned)(UINT_PTR)r->hwnd);
    if (!r->hwnd) { LOG("   [r%d] !! CreateWindow 失败", r->round); return -1; }

    LOG("   [r%d] MbKit_Create(hwnd)...", r->round);
    r->view = r->K->Create(r->hwnd, 0, 0, 800, 600, NULL, NULL);
    LOG("   [r%d] MbKit_Create  -> view=%p  (%s)", r->round, r->view,
        r->view ? "OK" : (r->K->LastError ? r->K->LastError() : "?"));
    if (!r->view) return -2;

    {
        wchar_t html[512];
        make_html(r->round, html, 512);
        r->K->LoadHtml(r->view, html);
    }
    {
        int t;
        r->loaded = -1;
        for (t = 0; t < 240; t++) {           /* 最长 6s */
            if (r->K->IsLoaded(r->view) == 1) { r->loaded = 1; break; }
            r->K->Pump(25);
        }
    }
    r->title[0] = 0;
    {
        wchar_t tb[256]; tb[0] = 0;
        r->K->GetTitle(r->view, tb, 256);
        w_to_a(tb, r->title, sizeof(r->title));
    }
    LOG("   [r%d] loaded=%d  title=\"%s\"", r->round, r->loaded, r->title);
    return 0;
}

static void round_teardown(ROUND* r)
{
    if (r->view) {
        LOG("   [r%d] MbKit_Destroy(view)...", r->round);
        r->K->Destroy(r->view);
        r->view = NULL;
        LOG("   [r%d] MbKit_Destroy 完成", r->round);
    }
    if (r->hwnd) {
        LOG("   [r%d] DestroyWindow(hwnd=0x%08X)...", r->round, (unsigned)(UINT_PTR)r->hwnd);
        DestroyWindow(r->hwnd);
        r->hwnd = NULL;
        pump(150);
        LOG("   [r%d] DestroyWindow 完成", r->round);
    }
}

/* 供跨线程模式使用的参数包 */
typedef struct {
    MBKIT*  K;
    ROUND   r;
    int     do_init;      /* 线程内先做 MbKit_Init */
    int     do_teardown;  /* 线程内做第 1 轮拆除 */
    HANDLE  gate;         /* 置位后线程才退出（保持「存活」） */
    HANDLE  ready;        /* attach+teardown 完成后置位 */
} TARG;

/* 线程体：可选 init → 建窗口 + attach → 可选拆除 → （gate 时）保持存活 */
static DWORD WINAPI mb_thread_body(LPVOID pv)
{
    TARG* p = (TARG*)pv;
    LOG("   (thread) 开始 round=%d do_init=%d do_teardown=%d",
        p->r.round, p->do_init, p->do_teardown);
    if (p->do_init) {
        if (p->K->Init(NULL) != MBKIT_OK)
            LOG("   (thread) !! MbKit_Init 失败：%s", p->K->LastError ? p->K->LastError() : "?");
        else
            LOG("   (thread) MbKit_Init OK initCount=%d", p->K->KernelInitCount());
    }
    if (round_attach(&p->r) != 0) LOG("   (thread) attach 失败");
    p->K->Pump(300);
    if (p->do_teardown) round_teardown(&p->r);
    if (p->ready) SetEvent(p->ready);
    if (p->gate) {
        LOG("   (thread) 保持存活，等主线程放行…");
        WaitForSingleObject(p->gate, 30000);
    }
    LOG("   (thread) 结束");
    return 0;
}

/*
 * 两轮总控：
 *   init_on_thread —— 是否把 MbKit_Init + 第 1 个窗口放到工作线程 T1
 *   t1_ends        —— T1 在第 1 轮拆除后是否退出
 *   r2_where       —— 第 2 轮在 主线程(0) / 新线程(1) 上做
 */
static int mbkit_two(int init_on_thread, int t1_ends, int r2_where)
{
    MBKIT H;
    ROUND r1, r2;
    HANDLE h1 = NULL, ready1 = NULL, gate1 = NULL;
    TARG   a1;
    DWORD  tid1 = 0;
    int    rc;

    LOG("");
    LOG("== MbKit path : init_on_thread=%d t1_ends=%d r2_where=%s ==",
        init_on_thread, t1_ends, r2_where ? "new-thread" : "main-thread");

    if (!mbk_load(&H, "MbKit.dll")) { LOG("!! 加载 MbKit.dll 失败"); return 1; }
    LOG("   %s", H.Version ? H.Version() : "?");

    ZeroMemory(&r1, sizeof(r1)); r1.K = &H; r1.round = 1;

    if (init_on_thread) {
        ZeroMemory(&a1, sizeof(a1)); a1.K = &H; a1.r = r1;
        a1.do_init = 1; a1.do_teardown = 1;
        ready1 = CreateEventA(NULL, TRUE, FALSE, NULL);
        a1.ready = ready1;
        if (!t1_ends) { gate1 = CreateEventA(NULL, TRUE, FALSE, NULL); a1.gate = gate1; }
        h1 = CreateThread(NULL, 0, mb_thread_body, &a1, 0, &tid1);
        LOG("   T1 created tid=%lu", (unsigned long)tid1);
        WaitForSingleObject(ready1, 30000);
        r1 = a1.r;
        LOG("   T1 第1轮完成 loaded=%d title=\"%s\"", r1.loaded, r1.title);
    } else {
        if (H.Init(NULL) != MBKIT_OK) {
            LOG("!! MbKit_Init 失败：%s", H.LastError ? H.LastError() : "?");
            return 1;
        }
        LOG("   MbKit_Init OK initCount=%d node=0x%08X", H.KernelInitCount(), H.NodeModule());
        rc = round_attach(&r1);
        if (rc != 0) { LOG("!! round1 attach 失败 rc=%d", rc); return 1; }
        round_teardown(&r1);
    }

    if (init_on_thread && t1_ends) {
        LOG("   T1 等待其自身退出…");
        WaitForSingleObject(h1, 30000);
        LOG("   T1 已退出（初始化线程消亡）");
    }
    if (h1) { CloseHandle(h1); h1 = NULL; }
    if (ready1) { CloseHandle(ready1); ready1 = NULL; }

    LOG("");
    LOG("   —— 现在建 round2（%s）——", r2_where ? "新线程" : "主线程");
    ZeroMemory(&r2, sizeof(r2)); r2.K = &H; r2.round = 2;
    if (r2_where) {
        TARG   a2;
        HANDLE th2, ready2;
        DWORD  tid2 = 0;
        ZeroMemory(&a2, sizeof(a2)); a2.K = &H; a2.r = r2;
        ready2 = CreateEventA(NULL, TRUE, FALSE, NULL);
        a2.ready = ready2;
        th2 = CreateThread(NULL, 0, mb_thread_body, &a2, 0, &tid2);
        LOG("   T2 created tid=%lu", (unsigned long)tid2);
        WaitForSingleObject(ready2, 30000);
        r2 = a2.r;
        CloseHandle(ready2);
        CloseHandle(th2);
    } else {
        rc = round_attach(&r2);
        if (rc != 0) LOG("!! round2 attach 失败 rc=%d", rc);
    }

    LOG("");
    if (r2.loaded == 1 && strstr(r2.title, "R-2"))
        LOG("   RESULT: PASS —— 第二个窗口能正常二次绑定并载入");
    else
        LOG("   RESULT: FAIL —— 第二个窗口二次绑定失败 (loaded=%d title=\"%s\")",
            r2.loaded, r2.title);

    if (gate1) { SetEvent(gate1); CloseHandle(gate1); }
    return (r2.loaded == 1) ? 0 : 1;
}

/* ==========================================================================
 * 二、裸 node.dll 对照组（完全不走 MbKit）
 * ========================================================================*/
typedef void           (*p_wkeInitialize)(void);
typedef void           (*p_wkeFinalize)(void);
typedef BOOL           (*p_wkeIsInitialize)(void);
typedef void*          (*p_wkeCreateWebView)(void);
typedef void           (*p_wkeDestroyWebView)(void*);
typedef void           (*p_wkeSetHandle)(void*, HWND);
typedef void           (*p_wkeResize)(void*, int, int);
typedef BOOL           (*p_wkeIsLoadingCompleted)(void*);
typedef const wchar_t* (*p_wkeGetTitleW)(void*);
typedef void           (*p_wkeLoadHTMLW)(void*, const wchar_t*);

typedef struct {
    HMODULE                 h;
    p_wkeInitialize         Initialize;
    p_wkeFinalize           Finalize;
    p_wkeIsInitialize       IsInitialize;
    p_wkeCreateWebView      CreateWebView;
    p_wkeDestroyWebView     DestroyWebView;
    p_wkeSetHandle          SetHandle;
    p_wkeResize             Resize;
    p_wkeIsLoadingCompleted IsLoaded;
    p_wkeGetTitleW          GetTitleW;
    p_wkeLoadHTMLW          LoadHTMLW;
} RAWNODE;

static int raw_load(RAWNODE* n)
{
    const char* cand[] = { "..\\..\\main\\corn\\node.dll", "main\\corn\\node.dll", "node.dll" };
    int i;
    ZeroMemory(n, sizeof(*n));
    for (i = 0; i < 3 && !n->h; i++) {
        n->h = LoadLibraryA(cand[i]);
        LOG("   raw_load cand[%d]=\"%s\" -> %p err=%lu", i, cand[i],
            n->h, n->h ? 0 : GetLastError());
    }
    if (!n->h) { LOG("!! 裸加载 node.dll 失败 err=%lu", GetLastError()); return 0; }
    LOG("   裸 node.dll = 0x%08X", (unsigned)(UINT_PTR)n->h);
    #define B(m, s) n->m = (__typeof__(n->m))GetProcAddress(n->h, s); \
                    if (!n->m) { LOG("!! 裸 node.dll 缺符号 %s", s); return 0; }
    B(Initialize,   "wkeInitialize")
    B(Finalize,     "wkeFinalize")
    B(IsInitialize, "wkeIsInitialize")
    B(CreateWebView,"wkeCreateWebView")
    B(DestroyWebView,"wkeDestroyWebView")
    B(SetHandle,    "wkeSetHandle")
    B(Resize,       "wkeResize")
    B(IsLoaded,     "wkeIsLoadingCompleted")
    B(GetTitleW,    "wkeGetTitleW")
    B(LoadHTMLW,    "wkeLoadHTMLW")
    #undef B
    return 1;
}

static int raw_round(RAWNODE* n, int round, HWND hwnd, void** outView, char* title, int* loaded)
{
    void* wv;
    LOG("   [raw r%d] wkeCreateWebView...", round);
    wv = n->CreateWebView();
    LOG("   [raw r%d] -> %p", round, wv);
    if (!wv) return -1;
    LOG("   [raw r%d] wkeSetHandle(%p,0x%08X)...", round, wv, (unsigned)(UINT_PTR)hwnd);
    n->SetHandle(wv, hwnd);
    LOG("   [raw r%d] wkeSetHandle 完成", round);
    n->Resize(wv, 800, 600);
    { wchar_t html[512]; make_html(round, html, 512); n->LoadHTMLW(wv, html); }
    {
        int t; *loaded = -1;
        for (t = 0; t < 240; t++) {
            pump(25);
            if (n->IsLoaded(wv) == 1) { *loaded = 1; break; }
        }
    }
    title[0] = 0;
    { const wchar_t* t = n->GetTitleW(wv); w_to_a(t, title, 256); }
    LOG("   [raw r%d] loaded=%d title=\"%s\"", round, *loaded, title);
    *outView = wv;
    return 0;
}

/* 线程体：raw 版 init + round1 */
typedef struct { RAWNODE* n; HWND hwnd1; void* v1; char t1[256]; int l1; int do_init; } RAWT;

static HWND raw_thread_hwnd1;

static DWORD WINAPI raw_thread_body(LPVOID pv)
{
    RAWT* p = (RAWT*)pv;
    LOG("   [raw T1] 开始 do_init=%d", p->do_init);
    if (p->do_init) {
        LOG("   [raw T1] wkeInitialize...");
        p->n->Initialize();
        LOG("   [raw T1] wkeIsInitialize()=%d", p->n->IsInitialize());
    }
    p->hwnd1 = make_window(0, 0);
    raw_thread_hwnd1 = p->hwnd1;
    LOG("   [raw T1] hwnd1=0x%08X", (unsigned)(UINT_PTR)p->hwnd1);
    raw_round(p->n, 1, p->hwnd1, &p->v1, p->t1, &p->l1);
    LOG("   [raw T1] wkeDestroyWebView(v1)...");
    p->n->DestroyWebView(p->v1);
    LOG("   [raw T1] DestroyWindow(hwnd1)...");
    DestroyWindow(p->hwnd1);
    pump(200);
    LOG("   [raw T1] 结束");
    return 0;
}

static int raw_two(int init_on_thread_and_end)
{
    RAWNODE n;
    HWND    h2 = NULL;
    void   *v2 = NULL;
    char    t2[256] = {0};
    int     l2 = -1;

    LOG("");
    LOG("== RAW node.dll control : %s ==",
        init_on_thread_and_end ? "init on dying thread" : "both rounds same thread");
    if (!raw_load(&n)) return 1;

    if (init_on_thread_and_end) {
        RAWT a;
        HANDLE th; DWORD tid = 0;
        ZeroMemory(&a, sizeof(a)); a.n = &n; a.do_init = 1;
        th = CreateThread(NULL, 0, raw_thread_body, &a, 0, &tid);
        LOG("   [raw] T1 created tid=%lu", (unsigned long)tid);
        WaitForSingleObject(th, 30000);
        CloseHandle(th);
        LOG("   [raw] T1 已退出，loaded1=%d title1=\"%s\"", a.l1, a.t1);
    } else {
        LOG("   [raw] wkeInitialize...");
        n.Initialize();
        LOG("   [raw] wkeIsInitialize()=%d", n.IsInitialize());
        { HWND h1 = make_window(0, 0); void* v1 = NULL; char t1[256]={0}; int l1=-1;
          raw_round(&n, 1, h1, &v1, t1, &l1);
          n.DestroyWebView(v1); DestroyWindow(h1); pump(200); }
    }

    LOG("");
    LOG("   [raw] —— 建 round2 ——");
    h2 = make_window(20, 20);
    LOG("   [raw] hwnd2=0x%08X", (unsigned)(UINT_PTR)h2);
    raw_round(&n, 2, h2, &v2, t2, &l2);

    LOG("");
    if (l2 == 1 && strstr(t2, "R-2"))
        LOG("   RESULT(raw): PASS —— 裸 node.dll 也能二次绑定");
    else
        LOG("   RESULT(raw): FAIL —— 裸 node.dll 二次绑定失败（缺陷在 node.dll 内核）");
    return (l2 == 1) ? 0 : 1;
}

/* ==========================================================================
 * 附加决定性探针：
 *   t1_two          init + 两轮（含拆除）**全在同一个非主线程 T1** 上
 *                   → 若 PASS，则「窗口销毁/重建」本身无害，纯粹是线程亲和
 *   main_cross_only 主线程 init，**完全不做第 1 轮**，直接在新线程上建第 1 个
 *                   webview → 隔离「跨线程 create」这一个变量
 * ========================================================================*/

/* 线程体：init → r1 attach → teardown → r2 attach，全在本线程 */
typedef struct { MBKIT* K; ROUND r1; ROUND r2; } TARG2;

static DWORD WINAPI mb_t1_two_body(LPVOID pv)
{
    TARG2* p = (TARG2*)pv;
    LOG("   (T1) 开始：init + r1 + teardown + r2 全在本线程");
    if (p->K->Init(NULL) != MBKIT_OK)
        LOG("   (T1) !! MbKit_Init 失败");
    else
        LOG("   (T1) MbKit_Init OK initCount=%d", p->K->KernelInitCount());
    if (round_attach(&p->r1) == 0) round_teardown(&p->r1);
    p->K->Pump(200);
    LOG("   (T1) —— 建 r2（同一线程）——");
    round_attach(&p->r2);
    p->K->Pump(300);
    LOG("   (T1) 结束");
    return 0;
}

static int mbkit_t1_two(void)
{
    MBKIT H;
    TARG2 a;
    HANDLE th; DWORD tid = 0;
    LOG("");
    LOG("== MbKit path : t1_two (init + r1 + r2 all on one non-main thread) ==");
    if (!mbk_load(&H, "MbKit.dll")) { LOG("!! 加载 MbKit.dll 失败"); return 1; }
    ZeroMemory(&a, sizeof(a)); a.K = &H;
    a.r1.K = &H; a.r1.round = 1;
    a.r2.K = &H; a.r2.round = 2;
    th = CreateThread(NULL, 0, mb_t1_two_body, &a, 0, &tid);
    LOG("   T1 created tid=%lu", (unsigned long)tid);
    WaitForSingleObject(th, 40000);
    CloseHandle(th);
    LOG("");
    if (a.r2.loaded == 1 && strstr(a.r2.title, "R-2"))
        LOG("   RESULT: PASS —— 同一线程上二次绑定成立（纯线程亲和）");
    else
        LOG("   RESULT: FAIL —— 同线程二次绑定也失败 (loaded=%d title=\"%s\")",
            a.r2.loaded, a.r2.title);
    return (a.r2.loaded == 1) ? 0 : 1;
}

/* 线程体：只做一次 attach（供 main_cross_only） */
static DWORD WINAPI mb_r2_only_body(LPVOID pv)
{
    ROUND* r = (ROUND*)pv;
    LOG("   (T2) 开始：直接建第 1 个 webview");
    round_attach(r);
    r->K->Pump(300);
    LOG("   (T2) 结束");
    return 0;
}

static int mbkit_main_cross_only(void)
{
    MBKIT H;
    ROUND r;
    HANDLE th; DWORD tid = 0;
    LOG("");
    LOG("== MbKit path : main_cross_only (init on main, create on NEW thread, no round1) ==");
    if (!mbk_load(&H, "MbKit.dll")) { LOG("!! 加载 MbKit.dll 失败"); return 1; }
    if (H.Init(NULL) != MBKIT_OK) { LOG("!! MbKit_Init 失败"); return 1; }
    LOG("   MbKit_Init OK initCount=%d", H.KernelInitCount());
    ZeroMemory(&r, sizeof(r)); r.K = &H; r.round = 2;
    th = CreateThread(NULL, 0, mb_r2_only_body, &r, 0, &tid);
    LOG("   T2 created tid=%lu", (unsigned long)tid);
    WaitForSingleObject(th, 30000);
    CloseHandle(th);
    LOG("");
    if (r.loaded == 1 && strstr(r.title, "R-2"))
        LOG("   RESULT: PASS —— 跨线程建首个 webview 也成立");
    else
        LOG("   RESULT: FAIL —— 跨线程建首个 webview 崩溃/失败 (loaded=%d)", r.loaded);
    return (r.loaded == 1) ? 0 : 1;
}

/* ==========================================================================
 * 探针：能否把内核「重新挂靠」到新线程（finalize + initialize）
 * ========================================================================*/
typedef struct { RAWNODE* n; int do_finalize; int do_init; HWND hwnd; void* v; char t[256]; int l; }
        REHOME;

static DWORD WINAPI rehome_body(LPVOID pv)
{
    REHOME* p = (REHOME*)pv;
    if (p->do_init) {
        LOG("   [T2] wkeIsInitialize()=%d（新线程视角）", p->n->IsInitialize());
        LOG("   [T2] wkeInitialize() 强制调用...");
        p->n->Initialize();
        LOG("   [T2] wkeIsInitialize()=%d", p->n->IsInitialize());
    }
    p->hwnd = make_window(30, 30);
    raw_round(p->n, 2, p->hwnd, &p->v, p->t, &p->l);
    pump(200);
    return 0;
}

static int raw_rehome(void)
{
    RAWNODE n;
    REHOME  p;
    HANDLE  th; DWORD tid = 0;
    HWND    h1; void* v1 = NULL; char t1[256] = {0}; int l1 = -1;

    LOG("");
    LOG("== RAW control : re-home kernel to new thread (finalize on main + init on T2) ==");
    if (!raw_load(&n)) return 1;

    LOG("   [main] wkeInitialize() ...");
    n.Initialize();
    LOG("   [main] isInit=%d", n.IsInitialize());
    /* 复刻第 1 轮：建窗口 + 建 view + 拆除 */
    h1 = make_window(0, 0);
    raw_round(&n, 1, h1, &v1, t1, &l1);
    LOG("   [main] wkeDestroyWebView + DestroyWindow ...");
    n.DestroyWebView(v1);
    DestroyWindow(h1);
    pump(200);

    LOG("   [main] wkeFinalize() ...");
    n.Finalize();
    LOG("   [main] finalize 后 isInit=%d（已实测：仍返回 1）", n.IsInitialize());

    ZeroMemory(&p, sizeof(p)); p.n = &n; p.do_init = 1;
    th = CreateThread(NULL, 0, rehome_body, &p, 0, &tid);
    LOG("   [main] T2 created tid=%lu", (unsigned long)tid);
    WaitForSingleObject(th, 30000);
    CloseHandle(th);

    LOG("");
    if (p.l == 1 && strstr(p.t, "R-2"))
        LOG("   RESULT(rehome): PASS —— finalize + 新线程 initialize 可以重新挂靠内核");
    else
        LOG("   RESULT(rehome): FAIL —— 重新挂靠无效 (loaded=%d)", p.l);
    return (p.l == 1) ? 0 : 1;
}

/* ==========================================================================
 * 探针：在新线程上「再调一次 initialize」是否能补齐该线程的 V8 TLS？
 *   raw_double_init : main Initialize → T2 再 Initialize（不清 finalize）→ 建 view
 * ========================================================================*/
typedef struct { RAWNODE* n; int n_init; HWND hwnd; void* v; char t[256]; int l; } DBL;

static DWORD WINAPI dbl_body(LPVOID pv)
{
    DBL* p = (DBL*)pv;
    int k;
    LOG("   [T2] 进入，n_init=%d", p->n_init);
    for (k = 0; k < p->n_init; k++) {
        LOG("   [T2] 第%d次 wkeInitialize()... (isInit=%d)", k + 1, p->n->IsInitialize());
        p->n->Initialize();
    }
    p->hwnd = make_window(40, 40);
    raw_round(p->n, 2, p->hwnd, &p->v, p->t, &p->l);
    pump(200);
    return 0;
}

static int raw_double_init(int n_init_on_t2)
{
    RAWNODE n;
    DBL     p;
    HANDLE  th; DWORD tid = 0;
    HWND    h1; void* v1 = NULL; char t1[256] = {0}; int l1 = -1;

    LOG("");
    LOG("== RAW control : main init, then T2 calls wkeInitialize x%d before create ==",
        n_init_on_t2);
    if (!raw_load(&n)) return 1;

    LOG("   [main] wkeInitialize() ...");
    n.Initialize();
    LOG("   [main] isInit=%d", n.IsInitialize());
    h1 = make_window(0, 0);
    raw_round(&n, 1, h1, &v1, t1, &l1);
    LOG("   [main] 拆除 round1 ...");
    n.DestroyWebView(v1);
    DestroyWindow(h1);
    pump(200);

    ZeroMemory(&p, sizeof(p)); p.n = &n; p.n_init = n_init_on_t2;
    th = CreateThread(NULL, 0, dbl_body, &p, 0, &tid);
    LOG("   [main] T2 created tid=%lu", (unsigned long)tid);
    WaitForSingleObject(th, 30000);
    CloseHandle(th);

    LOG("");
    if (p.l == 1 && strstr(p.t, "R-2"))
        LOG("   RESULT(double_init x%d): PASS", n_init_on_t2);
    else
        LOG("   RESULT(double_init x%d): FAIL (loaded=%d)", n_init_on_t2, p.l);
    return (p.l == 1) ? 0 : 1;
}

/* ==========================================================================
 * 探针：为「新线程」加载一份**独立的 node.dll 拷贝**（独立内核实例）
 * ——— 检验「按代际重建内核」这一修复思路是否成立。
 * ========================================================================*/
static int raw_load_path(RAWNODE* n, const char* path)
{
    ZeroMemory(n, sizeof(*n));
    n->h = LoadLibraryA(path);
    if (!n->h) { LOG("!! LoadLibrary(%s) 失败 err=%lu", path, GetLastError()); return 0; }
    LOG("   LoadLibrary(%s) = 0x%08X", path, (unsigned)(UINT_PTR)n->h);
    #define B(m, s) n->m = (__typeof__(n->m))GetProcAddress(n->h, s); \
                    if (!n->m) { LOG("!! 缺符号 %s", s); return 0; }
    B(Initialize,   "wkeInitialize")
    B(Finalize,     "wkeFinalize")
    B(IsInitialize, "wkeIsInitialize")
    B(CreateWebView,"wkeCreateWebView")
    B(DestroyWebView,"wkeDestroyWebView")
    B(SetHandle,    "wkeSetHandle")
    B(Resize,       "wkeResize")
    B(IsLoaded,     "wkeIsLoadingCompleted")
    B(GetTitleW,    "wkeGetTitleW")
    B(LoadHTMLW,    "wkeLoadHTMLW")
    #undef B
    return 1;
}

typedef struct { RAWNODE* n; const char* path; HWND hwnd; void* v; char t[256]; int l; }
        GEN2;

static DWORD WINAPI gen2_body(LPVOID pv)
{
    GEN2* p = (GEN2*)pv;
    LOG("   [T2] 加载独立拷贝：%s", p->path);
    if (!raw_load_path(p->n, p->path)) return 0;
    LOG("   [T2] 新实例 wkeIsInitialize()=%d（应为 0：独立实例）", p->n->IsInitialize());
    LOG("   [T2] 新实例 wkeInitialize()...");
    p->n->Initialize();
    LOG("   [T2] 新实例 isInit=%d", p->n->IsInitialize());
    p->hwnd = make_window(60, 60);
    raw_round(p->n, 3, p->hwnd, &p->v, p->t, &p->l);
    pump(200);
    return 0;
}

static const char* g_pathA = "..\\..\\main\\corn\\node.dll";
static const char* g_pathB = "gen2\\node.dll";

static int raw_two_instances(void)
{
    RAWNODE a, b;
    GEN2    g;
    HANDLE  th; DWORD tid = 0;
    HWND    h1; void* v1 = NULL; char t1[256] = {0}; int l1 = -1;
    const char* p2 = g_pathB;

    LOG("");
    LOG("== RAW control : independent node.dll instances (A=%s B=%s) ==", g_pathA, p2);
    {
        /* 主实例：用显式路径 A */
        ZeroMemory(&a, sizeof(a));
        a.h = LoadLibraryA(g_pathA);
        if (!a.h) { LOG("!! A 加载失败 err=%lu", GetLastError()); return 1; }
        LOG("   A = 0x%08X (%s)", (unsigned)(UINT_PTR)a.h, g_pathA);
        #define BA(m, s) a.m = (__typeof__(a.m))GetProcAddress(a.h, s); \
                         if (!a.m) { LOG("!! A 缺 %s", s); return 1; }
        BA(Initialize,"wkeInitialize") BA(Finalize,"wkeFinalize") BA(IsInitialize,"wkeIsInitialize")
        BA(CreateWebView,"wkeCreateWebView") BA(DestroyWebView,"wkeDestroyWebView")
        BA(SetHandle,"wkeSetHandle") BA(Resize,"wkeResize")
        BA(IsLoaded,"wkeIsLoadingCompleted") BA(GetTitleW,"wkeGetTitleW") BA(LoadHTMLW,"wkeLoadHTMLW")
        #undef BA
    }
    LOG("   [main] 实例A wkeInitialize()...");
    a.Initialize();
    LOG("   [main] 实例A isInit=%d", a.IsInitialize());
    h1 = make_window(0, 0);
    raw_round(&a, 1, h1, &v1, t1, &l1);
    LOG("   [main] 拆除 round1 ...");
    a.DestroyWebView(v1);
    DestroyWindow(h1);
    pump(200);

    ZeroMemory(&g, sizeof(g)); g.n = &b; g.path = p2;
    th = CreateThread(NULL, 0, gen2_body, &g, 0, &tid);
    LOG("   [main] T2 created tid=%lu", (unsigned long)tid);
    WaitForSingleObject(th, 40000);
    CloseHandle(th);

    LOG("");
    LOG("   A(主)isInit=%d   B(T2)isInit=%d", a.IsInitialize(), b.h ? b.IsInitialize() : -1);
    if (g.l == 1 && strstr(g.t, "R-3"))
        LOG("   RESULT(two_instances): PASS —— 独立拷贝 = 独立内核，可在新线程工作");
    else
        LOG("   RESULT(two_instances): FAIL (loaded=%d title=\"%s\")", g.l, g.t);
    return (g.l == 1) ? 0 : 1;
}

/* 探针 A：只用一份拷贝实例（单实例单线程）—— 检验拷贝本身能不能工作 */
static int raw_path_only(const char* path, int prime)
{
    RAWNODE n;
    HWND    h; void* v = NULL; char t[256] = {0}; int l = -1;
    LOG("");
    LOG("== RAW : single instance loaded from %s (prime=%d) ==", path, prime);
    if (prime) {
        /* 复刻 raw_load 的第一步（一个注定失败的 LoadLibrary），看是否影响后续加载 */
        HMODULE z = LoadLibraryA("..\\..\\main\\corn\\node.dll");
        LOG("   prime: LoadLibrary(..\\..\\main\\corn\\node.dll) -> %p err=%lu",
            z, z ? 0 : GetLastError());
    }
    if (!raw_load_path(&n, path)) return 1;
    LOG("   isInit(before)=%d", n.IsInitialize());
    n.Initialize();
    LOG("   isInit(after)=%d", n.IsInitialize());
    h = make_window(0, 0);
    raw_round(&n, 1, h, &v, t, &l);
    LOG("");
    LOG("   RESULT(path_only): loaded=%d title=\"%s\"", l, t);
    return (l == 1) ? 0 : 1;
}

/* 探针 B：view 在主线程建好；从**另一个线程**调用各类只读/输入类 kernel API，
 * 逐项判定哪些跨线程安全、哪些崩。 */
typedef int  (__cdecl *p_firemouse)(void*, unsigned, int, int, unsigned);
typedef long long (__cdecl *p_runjsw2)(void*, const wchar_t*);

typedef struct {
    void* wv; p_firemouse fire; p_runjsw2 runjs; RAWNODE* n;
} CROSSOPS;

static DWORD WINAPI cross_ops_body(LPVOID pv)
{
    CROSSOPS* p = (CROSSOPS*)pv;
    LOG("   [T2] (1) wkeFireMouseEvent(view, LBUTTONDOWN, 10,10,0)...");
    if (p->fire) p->fire(p->wv, 0x0201 /*WM_LBUTTONDOWN*/, 10, 10, 0);
    LOG("   [T2] (1) OK");
    LOG("   [T2] (2) wkeResize(view, 800,600)...");
    p->n->Resize(p->wv, 800, 600);
    LOG("   [T2] (2) OK");
    LOG("   [T2] (3) wkeGetTitleW(view)...");
    { const wchar_t* s = p->n->GetTitleW(p->wv); LOG("   [T2] (3) OK title=%ls", s ? s : L"(null)"); }
    LOG("   [T2] (4) wkeIsLoadingCompleted(view)...");
    LOG("   [T2] (4) = %d", p->n->IsLoaded(p->wv));
    LOG("   [T2] (5) wkeRunJSW(view, \"1+1\")...");
    if (p->runjs) p->runjs(p->wv, L"1+1");
    LOG("   [T2] (5) OK");
    LOG("   [T2] 全部跨线程调用完成，均未崩溃");
    return 0;
}

static int raw_cross_ops(void)
{
    RAWNODE n;
    HWND    h; void* v = NULL; char t[256] = {0}; int l = -1;
    CROSSOPS c; HANDLE th; DWORD tid = 0;
    LOG("");
    LOG("== RAW : which kernel ops are safe cross-thread? ==");
    if (!raw_load(&n)) return 1;
    n.Initialize();
    h = make_window(0, 0);
    raw_round(&n, 1, h, &v, t, &l);
    LOG("   [main] 主线程建好 view=%p loaded=%d", v, l);

    ZeroMemory(&c, sizeof(c));
    c.wv = v; c.n = &n;
    c.fire  = (p_firemouse)GetProcAddress(n.h, "wkeFireMouseEvent");
    c.runjs = (p_runjsw2)GetProcAddress(n.h, "wkeRunJSW");
    th = CreateThread(NULL, 0, cross_ops_body, &c, 0, &tid);
    LOG("   [main] T2 created tid=%lu（主线程保持消息泵）", (unsigned long)tid);
    {
        DWORD end = GetTickCount() + 20000;
        while (WaitForSingleObject(th, 20) == WAIT_TIMEOUT && (int)(GetTickCount() - end) < 0)
            pump(20);
    }
    CloseHandle(th);
    LOG("");
    LOG("   RESULT(cross_ops): 若此处无崩溃，则输入类调用跨线程安全");
    return 0;
}

/* ==========================================================================
 * 探针：能否 FreeLibrary(node.dll) 彻底卸载，再在新线程加载新拷贝（=「换内核代际」）
 * ========================================================================*/
typedef struct { const char* path; RAWNODE n; HWND hwnd; void* v; char t[256]; int l; } RELOADT2;

static DWORD WINAPI reload_t2(LPVOID pv)
{
    RELOADT2* p = (RELOADT2*)pv;
    LOG("   [T2] 加载新代际：%s", p->path);
    if (!raw_load_path(&p->n, p->path)) return 0;
    LOG("   [T2] 新代际 isInit=%d", p->n.IsInitialize());
    if (!p->n.IsInitialize()) p->n.Initialize();
    LOG("   [T2] 新代际 isInit(after)=%d", p->n.IsInitialize());
    p->hwnd = make_window(80, 80);
    raw_round(&p->n, 4, p->hwnd, &p->v, p->t, &p->l);
    pump(300);
    return 0;
}

static int raw_reload(void)
{
    RAWNODE a; HWND h1; void* v1 = NULL; char t1[256] = {0}; int l1 = -1;
    RELOADT2 g; HANDLE th; DWORD tid = 0; BOOL freed; HMODULE still = NULL;

    LOG("");
    LOG("== RAW control : FreeLibrary(node.dll) then load a fresh copy on a new thread ==");
    if (!raw_load(&a)) return 1;
    a.Initialize();
    h1 = make_window(0, 0);
    raw_round(&a, 1, h1, &v1, t1, &l1);
    a.DestroyWebView(v1);
    DestroyWindow(h1);
    pump(200);

    SetLastError(0);
    freed = FreeLibrary(a.h);
    LOG("   [main] FreeLibrary(node.dll) -> %d  err=%lu", freed, GetLastError());
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                     | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)(UINT_PTR)a.CreateWebView, &still);
    LOG("   [main] 旧代际 CreateWebView 仍映射? hmod=0x%08X（0=已卸载）",
        (unsigned)(UINT_PTR)still);

    ZeroMemory(&g, sizeof(g)); g.path = g_pathB;
    th = CreateThread(NULL, 0, reload_t2, &g, 0, &tid);
    LOG("   [main] T2 created tid=%lu（新代际路径=%s）", (unsigned long)tid, g_pathB);
    WaitForSingleObject(th, 40000);
    CloseHandle(th);
    LOG("");
    if (g.l == 1 && strstr(g.t, "R-4"))
        LOG("   RESULT(reload): PASS —— 卸载旧内核 + 新线程加载新拷贝 = 可二次工作");
    else
        LOG("   RESULT(reload): FAIL (loaded=%d title=\"%s\")", g.l, g.t);
    return (g.l == 1) ? 0 : 1;
}

/* ==========================================================================
 * 验收 A：host_reload_sim —— 模拟「宿主重载 biz DLL」（2026-09-25 用户拍板场景）
 *   T1: LoadLibrary(biz_v1.dll) → Biz_Attach(hwnd1) → 等载入 → Biz_Detach
 *       → FreeLibrary(biz_v1.dll)
 *   T2（新线程）: LoadLibrary(biz_v2.dll) → Biz_Attach(hwnd2) → 等载入 → 取标题
 *   判据 = 第 2 轮 loaded=1 且标题非空，进程不崩。
 *   biz DLL 只走 MbKit_* API；MbKit 自钉 ⇒ biz FreeLibrary 后必须存活。
 * ========================================================================*/
typedef int  (__stdcall *pBizAttach)(void*, int, int, const wchar_t*);
typedef int  (__stdcall *pBizDetach)(void);
typedef int  (__stdcall *pBizIsLoaded)(void);
typedef int  (__stdcall *pBizTitle)(void*, int);
typedef int  (__stdcall *pBizLoadCount)(void);

typedef struct {
    HMODULE        h;
    pBizAttach     Attach;
    pBizDetach     Detach;
    pBizIsLoaded   IsLoaded;
    pBizTitle      Title;
    pBizLoadCount  Loads;
    char           path[MAX_PATH];
} BIZDLL;

static int biz_load(BIZDLL* b, const char* dll)
{
    char full[MAX_PATH];
    ZeroMemory(b, sizeof(*b));
    if (!GetFullPathNameA(dll, MAX_PATH, full, NULL)) lstrcpynA(full, dll, MAX_PATH);
    b->h = LoadLibraryA(full);
    lstrcpynA(b->path, full, MAX_PATH);
    if (!b->h) { LOG("!! LoadLibrary(%s) 失败 err=%lu", full, GetLastError()); return 0; }
    LOG("   biz = %s @ 0x%08X", full, (unsigned)(UINT_PTR)b->h);
    #define BG(f, s) b->f = (__typeof__(b->f))GetProcAddress(b->h, s); \
        if (!b->f) { LOG("!! biz 缺导出 %s", s); return 0; }
    BG(Attach,   "Biz_Attach")
    BG(Detach,   "Biz_Detach")
    BG(IsLoaded, "Biz_IsLoaded")
    BG(Title,    "Biz_Title")
    BG(Loads,    "Biz_LoadCount")
    #undef BG
    return 1;
}

typedef struct { int round; const char* dll; int loaded; char title[256]; char err[256]; } SIMARG;

static DWORD WINAPI sim_thread_body(LPVOID pv)
{
    SIMARG* p = (SIMARG*)pv;
    BIZDLL  b;
    HWND    hwnd;
    wchar_t html[512];
    wchar_t tb[256];
    int     t, rc;

    LOG("   [T%d tid=%lu] 载入 biz DLL: %s", p->round,
        (unsigned long)GetCurrentThreadId(), p->dll);
    if (!biz_load(&b, p->dll)) { lstrcpyA(p->err, "biz load fail"); return 1; }

    hwnd = make_window(p->round * 20, p->round * 20);
    LOG("   [T%d] hwnd=0x%08X", p->round, (unsigned)(UINT_PTR)hwnd);
    if (!hwnd) { lstrcpyA(p->err, "CreateWindow fail"); return 1; }

    make_html(p->round, html, 512);
    rc = b.Attach(hwnd, 800, 600, html);
    LOG("   [T%d] Biz_Attach -> %d", p->round, rc);
    if (rc != 0) { lstrcpyA(p->err, "attach fail"); return 1; }

    p->loaded = -1;
    for (t = 0; t < 240; t++) {                 /* 最长 6s */
        pump(25);
        if (b.IsLoaded() == 1) { p->loaded = 1; break; }
    }
    tb[0] = 0;
    b.Title(tb, 256);
    w_to_a(tb, p->title, sizeof(p->title));
    LOG("   [T%d] loaded=%d title=\"%s\" loads=%d",
        p->round, p->loaded, p->title, b.Loads());

    if (p->round == 1) {
        LOG("   [T%d] Biz_Detach -> %d", p->round, b.Detach());
        DestroyWindow(hwnd);
        pump(150);
        LOG("   [T%d] FreeLibrary(biz DLL)", p->round);
        FreeLibrary(b.h);
        pump(100);
        LOG("   [T%d] biz DLL 已卸载（MbKit 应自钉存活）", p->round);
    } else {
        b.Detach();
        DestroyWindow(hwnd);
        pump(150);
    }
    return 0;
}

static int sim_reload(void)
{
    SIMARG a1, a2;
    HANDLE th;

    LOG("");
    LOG("== host_reload_sim : T1 biz_v1 attach→detach→FreeLibrary；T2 biz_v2 attach ==");

    ZeroMemory(&a1, sizeof(a1)); a1.round = 1; a1.dll = "biz_v1.dll";
    th = CreateThread(NULL, 0, sim_thread_body, &a1, 0, NULL);
    WaitForSingleObject(th, 90000);
    CloseHandle(th);
    LOG("   第1轮完成 loaded=%d title=\"%s\"", a1.loaded, a1.title);

    LOG("");
    LOG("   —— 重新载入新 biz DLL（第 2 轮，新线程）——");
    ZeroMemory(&a2, sizeof(a2)); a2.round = 2; a2.dll = "biz_v2.dll";
    th = CreateThread(NULL, 0, sim_thread_body, &a2, 0, NULL);
    WaitForSingleObject(th, 90000);
    CloseHandle(th);

    LOG("");
    if (a2.loaded == 1 && a2.title[0])
        LOG("   RESULT(sim): PASS —— 重载 biz DLL 后再次初始化成功（未重启宿主）");
    else
        LOG("   RESULT(sim): FAIL (loaded=%d title=\"%s\" err=%s)",
            a2.loaded, a2.title, a2.err);
    return (a2.loaded == 1) ? 0 : 1;
}

/* ==========================================================================
 * main
 * ========================================================================*/
int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "same";
    /* ⚠️ 日志路径**只能**来自环境变量：早期版本把 argv[2] 当日志路径 fopen(,"wb")，
     * 结果 `mb_reinit.exe raw_reload <node.dll 路径>` 会把 node.dll 截断成日志
     * ——这是一次真实的血泪教训（见 re/ 记录）。 */
    const char* logPath = getenv("MBREINIT_LOG");
    if (argc > 2) g_pathA = argv[2];
    if (argc > 3) g_pathB = argv[3];

    setvbuf(stdout, NULL, _IONBF, 0);
    if (logPath) g_log = fopen(logPath, "wb");

    LOG("======================================================================");
    LOG(" mb_reinit —— window-handle rebind + reinit minimal repro");
    LOG("======================================================================");
    LOG(" mode : %s", mode);
    {
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        LOG(" exe  : %s", exe);
    }

    if      (strcmp(mode, "same")          == 0) mbkit_two(0, 0, 0);
    else if (strcmp(mode, "thread_end")    == 0) mbkit_two(1, 1, 0);
    else if (strcmp(mode, "thread_alive")  == 0) mbkit_two(1, 0, 0);
    else if (strcmp(mode, "cross")         == 0) mbkit_two(0, 0, 1);
    else if (strcmp(mode, "t1_two")        == 0) mbkit_t1_two();
    else if (strcmp(mode, "main_cross_only")== 0) mbkit_main_cross_only();
    else if (strcmp(mode, "raw_same")       == 0) raw_two(0);
    else if (strcmp(mode, "raw_thread_end") == 0) raw_two(1);
    else if (strcmp(mode, "raw_rehome")     == 0) raw_rehome();
    else if (strcmp(mode, "raw_double_init")== 0) raw_double_init(1);
    else if (strcmp(mode, "raw_double_init2")== 0) raw_double_init(2);
    else if (strcmp(mode, "raw_two_instances")== 0) raw_two_instances();
    else if (strcmp(mode, "raw_path_only")    == 0) raw_path_only(g_pathA, 0);
    else if (strcmp(mode, "raw_path_prime")   == 0) raw_path_only(g_pathA, 1);
    else if (strcmp(mode, "raw_cross_ops")    == 0) raw_cross_ops();
    else if (strcmp(mode, "raw_probe_load")   == 0) {
        char cwd[MAX_PATH]; HMODULE h1, h2, h3;
        GetCurrentDirectoryA(MAX_PATH, cwd);
        LOG("   cwd = %s", cwd);
        SetLastError(0); h1 = LoadLibraryA(g_pathA);
        LOG("   LoadLibrary(\"%s\") -> %p err=%lu", g_pathA, h1, h1 ? 0 : GetLastError());
        SetLastError(0); h2 = LoadLibraryA(g_pathA);
        LOG("   again -> %p err=%lu", h2, h2 ? 0 : GetLastError());
        SetLastError(0); h3 = LoadLibraryExA(g_pathA, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        LOG("   Ex(ALTERED) -> %p err=%lu", h3, h3 ? 0 : GetLastError());
    }
    else if (strcmp(mode, "raw_reload")       == 0) raw_reload();
    else if (strcmp(mode, "host_reload_sim")  == 0) sim_reload();
    else { LOG("未知模式 %s", mode); }

    LOG("");
    LOG("== done ==");
    if (g_log) { fclose(g_log); g_log = NULL; }
    return 0;
}
