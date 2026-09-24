/* ============================================================================
 * marshal_probe.c —— 「持久内核线程 + 调用编组」可行性探针
 * ----------------------------------------------------------------------------
 * 背景（团队方向修订，2026-09-25）：
 *   「按代际换独立拷贝」已被否定 —— A 实例一旦 initialize 过，独立拷贝的
 *   加载管线彻底停摆（create 不崩但 loaded 永远 0，90s 仍为 0，见
 *   lead_probe2_out.txt）。
 *
 * 新方向：内核的主线程身份由 MbKit 自有常驻线程 K 持有，业务线程只泵消息。
 * 本探针在动手改 mbkit.c 之前回答两个未知数：
 *   a. 跨线程 SetHandle：view 在 K 上 create，HWND 在业务线程 T 上 ——
 *      在 K 上调 wkeSetHandle(view, hwnd_T) 是否工作（含内核是否对 HWND 做了
 *      跨线程子类化 —— 用 GWLP_WNDPROC 前后对比判定）。
 *   b. 绘制路径：内核在 K 上渲染后往 T 的 HWND 画是否有效（PrintWindow 取色）。
 *
 * ⚠️ 探针自身踩到的坑（重要发现）：从业务线程**直调**任何内核 API（哪怕只读的
 *    wkeIsLoadingCompleted）都可能**挂死**（不是崩）—— 与 raw_cross_ops 的挂死
 *    一致。所以本探针把「轮询/取标题」全部放在 K 上执行，主线程只负责泵自己的
 *    消息队列；并把每个变体拆成**独立进程模式**，挂死也只挂死那一个模式。
 *
 * 模式（每次运行只跑一个）：
 *   r1  all_on_K      : init/create/sethandle/resize/loadhtml + 轮询全在 K
 *   r2  load_on_T     : create/sethandle/resize 在 K，LoadHTMLW 主线程直调
 *   r3  sethandle_on_T: create/resize/loadhtml 在 K，SetHandle 主线程直调
 *   r4  ops_on_T      : 沿用存活 view，主线程直调 resize/gettitle/isloaded/
 *                       firemouse/loadhtml —— 判定直通安全面（预期可能挂死）
 *
 * 用法：marshal_probe.exe <mode>
 *       日志路径用环境变量 MBPROBE_LOG（argv 一律不当日志路径 —— 教训见
 *       mb_reinit.c 顶部注释）。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 日志 ---------------- */
static FILE* g_log = NULL;
static void LOG(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
    va_end(ap);
}

/* ---------------- 消息泵（当前线程） ---------------- */
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

/* ---------------- 离屏窗口（DWM 披斗篷） ---------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}
static HWND make_window(int dx, int dy)
{
    WNDCLASSA wc;
    HWND      h;
    static int seq = 0;
    char      cls[64];
    _snprintf(cls, sizeof(cls) - 1, "MarshalProbeWnd_%d", ++seq);
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);
    h = CreateWindowExA(0, cls, "MarshalProbe", WS_POPUP,
                        6000 + dx, 6000 + dy, 800, 600, NULL, NULL, wc.hInstance, NULL);
    if (!h) return NULL;
    {
        HMODULE dwm = LoadLibraryA("dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *pD)(HWND, DWORD, LPCVOID, DWORD);
            pD f = (pD)GetProcAddress(dwm, "DwmSetWindowAttribute");
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
               L"<body style=\"font:28px sans-serif;background:#e8e8ff\">"
               L"<h1>ROUND %d marshal probe</h1></body></html>", round, round);
    buf[cch - 1] = 0;
    return buf;
}

/* ---------------- 取色：PrintWindow + 量化去重 ---------------- */
static int count_colors(HWND h)
{
    typedef BOOL (WINAPI *pPW)(HWND, HDC, UINT);
    static pPW  pw = NULL;
    static int  inited = 0;
    RECT   rc;
    int    w, hh, i, uniq = 0;
    HDC    wdc, mdc;
    HBITMAP bm;
    HGDIOBJ old;
    DWORD* px;
    static unsigned char seen[32768];          /* 3-bit/通道 量化去重 */

    if (!inited) {
        HMODULE u = GetModuleHandleA("user32.dll");
        if (u) pw = (pPW)GetProcAddress(u, "PrintWindow");
        inited = 1;
    }
    if (!GetWindowRect(h, &rc)) return -1;
    w = rc.right - rc.left; hh = rc.bottom - rc.top;
    if (w <= 0 || hh <= 0 || !pw) return -1;

    wdc = GetDC(h);
    mdc = CreateCompatibleDC(wdc);
    bm  = CreateCompatibleBitmap(wdc, w, hh);
    old = SelectObject(mdc, bm);
    memset(seen, 0, sizeof(seen));
    if (!pw(h, mdc, 2 /*PW_RENDERFULLCONTENT*/)) uniq = -1;
    else {
        px = (DWORD*)malloc((size_t)w * hh * 4);
        if (px) {
            BITMAPINFO bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.bmiHeader.biSize     = sizeof(BITMAPINFOHEADER);
            bi.bmiHeader.biWidth    = w;
            bi.bmiHeader.biHeight   = -hh;
            bi.bmiHeader.biPlanes   = 1;
            bi.bmiHeader.biBitCount = 32;
            bi.bmiHeader.biCompression = BI_RGB;
            if (GetDIBits(mdc, bm, 0, hh, px, &bi, DIB_RGB_COLORS)) {
                for (i = 0; i < w * hh; i++) {
                    DWORD c = px[i] & 0x00F8F8F8;
                    int   k = (int)(((c >> 3) & 0x1F) << 10)
                            | (int)(((c >> 11) & 0x1F) << 5)
                            | (int)(((c >> 19) & 0x1F));
                    if (!seen[k]) { seen[k] = 1; uniq++; }
                }
            }
            free(px);
        }
    }
    SelectObject(mdc, old);
    DeleteObject(bm);
    DeleteDC(mdc);
    ReleaseDC(h, wdc);
    return uniq;
}

/* ---------------- 内核绑定 ---------------- */
typedef void           (*p_wkeInitialize)(void);
typedef BOOL           (*p_wkeIsInitialize)(void);
typedef void*          (*p_wkeCreateWebView)(void);
typedef void           (*p_wkeDestroyWebView)(void*);
typedef void           (*p_wkeSetHandle)(void*, HWND);
typedef void           (*p_wkeResize)(void*, int, int);
typedef BOOL           (*p_wkeIsLoadingCompleted)(void*);
typedef const wchar_t* (*p_wkeGetTitleW)(void*);
typedef void           (*p_wkeLoadHTMLW)(void*, const wchar_t*);
typedef void           (*p_wkeOnPaintUpdated)(void*, void*, void*);
typedef void           (*p_wkeFireMouseEvent)(void*, unsigned, int, int, unsigned);
typedef const char*    (*p_wkeGetVersionString)(void);

typedef struct {
    HMODULE                 h;
    p_wkeInitialize         Initialize;
    p_wkeIsInitialize       IsInitialize;
    p_wkeCreateWebView      CreateWebView;
    p_wkeDestroyWebView     DestroyWebView;
    p_wkeSetHandle          SetHandle;
    p_wkeResize             Resize;
    p_wkeIsLoadingCompleted IsLoaded;
    p_wkeGetTitleW          GetTitleW;
    p_wkeLoadHTMLW          LoadHTMLW;
    p_wkeOnPaintUpdated     OnPaintUpdated;
    p_wkeFireMouseEvent     FireMouse;
    p_wkeGetVersionString   GetVersionString;
} RAWNODE;

static int raw_load(RAWNODE* n)
{
    const char* cand[] = { "..\\..\\..\\main\\corn\\node.dll",
                           "..\\..\\main\\corn\\node.dll",
                           "main\\corn\\node.dll", "node.dll" };
    int i;
    ZeroMemory(n, sizeof(*n));
    for (i = 0; i < 4 && !n->h; i++) {
        n->h = LoadLibraryA(cand[i]);
        LOG("   raw_load cand[%d]=\"%s\" -> %p err=%lu", i, cand[i],
            n->h, n->h ? 0 : GetLastError());
    }
    if (!n->h) { LOG("!! 裸加载 node.dll 失败 err=%lu", GetLastError()); return 0; }
    LOG("   裸 node.dll = 0x%08X", (unsigned)(UINT_PTR)n->h);
    #define B(m, s) n->m = (__typeof__(n->m))GetProcAddress(n->h, s); \
                    if (!n->m) { LOG("!! 缺符号 %s", s); return 0; }
    B(Initialize,   "wkeInitialize")
    B(IsInitialize, "wkeIsInitialize")
    B(CreateWebView,"wkeCreateWebView")
    B(DestroyWebView,"wkeDestroyWebView")
    B(SetHandle,    "wkeSetHandle")
    B(Resize,       "wkeResize")
    B(IsLoaded,     "wkeIsLoadingCompleted")
    B(GetTitleW,    "wkeGetTitleW")
    B(LoadHTMLW,    "wkeLoadHTMLW")
    B(OnPaintUpdated,"wkeOnPaintUpdated")
    B(GetVersionString,"wkeGetVersionString")
    #undef B
    n->FireMouse = (p_wkeFireMouseEvent)GetProcAddress(n->h, "wkeFireMouseEvent");
    return 1;
}

/* ---------------- 常驻内核线程 K + 任务编组 ---------------- */
#define WM_APP_TASK (WM_APP + 0x4D42)

typedef void (*task_fn)(void* arg);
typedef struct { task_fn fn; void* arg; HANDLE done; } TASK;

static volatile DWORD g_kernelTid = 0;

static DWORD WINAPI kernel_thread(LPVOID pv)
{
    HANDLE ready = (HANDLE)pv;
    MSG   m;
    g_kernelTid = GetCurrentThreadId();
    PeekMessageA(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);   /* 强制建队列 */
    SetEvent(ready);
    LOG("   [K] 内核线程就绪 tid=%lu", (unsigned long)g_kernelTid);
    for (;;) {
        int r = (int)GetMessageA(&m, NULL, 0, 0);
        if (r <= 0) break;
        if (m.message == WM_APP_TASK && m.lParam) {
            TASK* t = (TASK*)m.lParam;
            t->fn(t->arg);
            if (t->done) SetEvent(t->done);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    LOG("   [K] 内核线程退出");
    return 0;
}

static void run_task(task_fn fn, void* arg)
{
    TASK t;
    t.fn = fn; t.arg = arg;
    t.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!PostThreadMessageA(g_kernelTid, WM_APP_TASK, 0, (LPARAM)&t))
        LOG("!! PostThreadMessage 失败 err=%lu", GetLastError());
    else if (WaitForSingleObject(t.done, 60000) == WAIT_TIMEOUT)
        LOG("!! 任务超时（60s）");
    CloseHandle(t.done);
}

/* ---------------- 上下文与任务 ---------------- */
typedef struct {
    RAWNODE      n;
    HWND         hwnd;        /* 业务线程（主）创建的 HWND */
    WNDPROC      origProc;    /* 供子类化对比 */
    void*        wv;
    int          round;
    char         title[256];
    int          loaded;
    volatile LONG paintHits;
} CTX;

static void __cdecl paint_cb(void* wv, void* param, void* hdc,
                             int x, int y, int cx, int cy)
{
    CTX* c = (CTX*)param;
    (void)wv; (void)hdc; (void)x; (void)y; (void)cx; (void)cy;
    if (c) InterlockedIncrement(&c->paintHits);
}

static void t_init(void* p)
{
    CTX* c = (CTX*)p;
    LOG("   [K] wkeInitialize ...");
    c->n.Initialize();
    LOG("   [K] isInit=%d  ver=\"%s\"", c->n.IsInitialize(),
        c->n.GetVersionString ? c->n.GetVersionString() : "(?)");
}
static void t_create(void* p)
{
    CTX* c = (CTX*)p;
    c->wv = c->n.CreateWebView();
    LOG("   [K] wkeCreateWebView -> %p", c->wv);
    if (c->wv && c->n.OnPaintUpdated)
        c->n.OnPaintUpdated(c->wv, (void*)&paint_cb, c);
}
static void t_sethandle(void* p)
{
    CTX* c = (CTX*)p;
    WNDPROC now;
    if (!c->wv) { LOG("   [K] sethandle 跳过（无 view）"); return; }
    now = (WNDPROC)GetWindowLongPtrA(c->hwnd, GWLP_WNDPROC);
    LOG("   [K] sethandle 前 WndProc=0x%08X（业务原生=0x%08X）",
        (unsigned)(UINT_PTR)now, (unsigned)(UINT_PTR)c->origProc);
    c->n.SetHandle(c->wv, c->hwnd);
    now = (WNDPROC)GetWindowLongPtrA(c->hwnd, GWLP_WNDPROC);
    LOG("   [K] wkeSetHandle(view, 0x%08X) 完成；sethandle 后 WndProc=0x%08X"
        "（%s）", (unsigned)(UINT_PTR)c->hwnd, (unsigned)(UINT_PTR)now,
        (now == c->origProc) ? "未被子类化" : "★已被子类化");
}
static void t_resize(void* p)
{
    CTX* c = (CTX*)p;
    if (c->wv) c->n.Resize(c->wv, 800, 600);
    LOG("   [K] wkeResize(800,600) 完成");
}
static void t_loadhtml(void* p)
{
    CTX* c = (CTX*)p;
    wchar_t html[512];
    if (!c->wv) { LOG("   [K] loadhtml 跳过（无 view）"); return; }
    make_html(c->round, html, 512);
    c->n.LoadHTMLW(c->wv, html);
    LOG("   [K] wkeLoadHTMLW(R-%d) 完成", c->round);
}
static void t_destroy(void* p)
{
    CTX* c = (CTX*)p;
    if (c->wv) { c->n.DestroyWebView(c->wv); c->wv = NULL; }
    LOG("   [K] wkeDestroyWebView 完成");
}

static void w2a(const wchar_t* s, char* out, size_t cch)
{
    if (!s) { out[0] = 0; return; }
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, (int)cch, NULL, NULL);
    out[cch - 1] = 0;
}

/* ---------------- 监视（跑在 K 上；主线程并发泵自己的队列） ---------------- */
typedef struct { CTX* c; int secs; const char* tag; volatile LONG finished; } MON;

static void t_monitor(void* p)
{
    MON*  m = (MON*)p;
    CTX*  c = m->c;
    DWORD end = GetTickCount() + (DWORD)(m->secs * 1000);
    int   last = -9999;
    MSG   mm;

    PeekMessageA(&mm, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    LOG("   -- monitor(%s) %ds（轮询在 K 上，主线程并发泵）--", m->tag, m->secs);
    while ((int)(GetTickCount() - end) < 0) {
        while (PeekMessageA(&mm, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&mm);
            DispatchMessageA(&mm);
        }
        Sleep(50);
        {
            int now = (m->secs * 1000 - (int)(end - GetTickCount())) / 1000;
            if (now / 5 != last / 5) {
                last = now;
                { const wchar_t* t = c->wv ? c->n.GetTitleW(c->wv) : NULL;
                  w2a(t, c->title, sizeof(c->title)); }
                c->loaded = c->wv ? (c->n.IsLoaded(c->wv) ? 1 : 0) : -1;
                /* ⚠️ 绝不在 K 上调 count_colors：PrintWindow 会向主线程的窗口
                 * 发跨线程 SendMessage，而主线程此刻正阻塞在 run_task 的
                 * WaitForSingleObject 里不泵消息 → 死锁（已实测踩坑）。
                 * 颜色统计改由主线程在 monitor() 返回后做。 */
                LOG("   [%s] t=%2ds loaded=%d title=\"%s\" paints=%ld",
                    m->tag, now, c->loaded, c->title, (long)c->paintHits);
            }
        }
    }
    { const wchar_t* t = c->wv ? c->n.GetTitleW(c->wv) : NULL;
      w2a(t, c->title, sizeof(c->title)); }
    c->loaded = c->wv ? (c->n.IsLoaded(c->wv) ? 1 : 0) : -1;
    LOG("   [%s] FINAL loaded=%d title=\"%s\" paints=%ld",
        m->tag, c->loaded, c->title, (long)c->paintHits);
    InterlockedExchange(&m->finished, 1);
}

static void monitor(CTX* c, int secs, const char* tag)
{
    MON m;
    DWORD end;
    m.c = c; m.secs = secs; m.tag = tag;
    InterlockedExchange(&m.finished, 0);
    run_task(t_monitor, &m);
    /* 主线程并发泵：让窗口消息流动；颜色统计必须在主线程做（跨线程 GDI 安全） */
    end = GetTickCount() + 2000;
    while ((int)(GetTickCount() - end) < 0) pump(30);
    LOG("   [%s] colors=%d（主线程 PrintWindow 统计）",
        tag, count_colors(c->hwnd));
    if (!m.finished) LOG("!! monitor 超时未结束");
}

/* ---------------- 各变体 ---------------- */
/* r0 对照组：全部在主线程（与 r1 完全同构，仅线程不同）——
 * 用来标定「能正常渲染时 PrintWindow 应取到多少色」，从而判定 colors=1
 * 究竟是「绘制失败」还是「披斗篷窗口取不到内容」的取色假象。 */
static void v_control(CTX* c)
{
    DWORD end;
    int   last = -9999;
    LOG("");
    LOG("== r0  control : init/create/sethandle/resize/loadhtml 全在主线程 ==");
    c->round = 0;
    t_init(c);
    t_create(c);
    if (!c->wv) { LOG("   RESULT(r0): FAIL —— create 失败"); return; }
    t_sethandle(c);
    t_resize(c);
    t_loadhtml(c);
    end = GetTickCount() + 16000;
    while ((int)(GetTickCount() - end) < 0) {
        int now;
        pump(50);
        now = (16000 - (int)(end - GetTickCount())) / 1000;
        if (now / 5 != last / 5) {
            last = now;
            { const wchar_t* t = c->wv ? c->n.GetTitleW(c->wv) : NULL;
              w2a(t, c->title, sizeof(c->title)); }
            c->loaded = c->wv ? (c->n.IsLoaded(c->wv) ? 1 : 0) : -1;
            LOG("   [r0] t=%2ds loaded=%d title=\"%s\" paints=%ld",
                now, c->loaded, c->title, (long)c->paintHits);
        }
    }
    LOG("   [r0] colors=%d（主线程 PrintWindow 统计）", count_colors(c->hwnd));
    LOG("   RESULT(r0 control): %s",
        (c->loaded == 1 && strstr(c->title, "R-0")) ? "PASS（基线）" : "FAIL");
}

static void v_all_on_K(CTX* c)
{
    LOG("");
    LOG("== r1  all_on_K : init/create/sethandle/resize/loadhtml/轮询 全在 K ==");
    c->round = 1;
    run_task(t_init,      c);
    run_task(t_create,    c);
    if (!c->wv) { LOG("   RESULT(r1): FAIL —— create 失败"); return; }
    run_task(t_sethandle, c);
    run_task(t_resize,    c);
    run_task(t_loadhtml,  c);
    monitor(c, 16, "r1");
    LOG("   RESULT(r1 all_on_K): %s",
        (c->loaded == 1 && strstr(c->title, "R-1"))
            ? "PASS —— 跨线程 SetHandle 可行，页面载入正常" : "FAIL");
}

static void v_load_on_T(CTX* c)
{
    LOG("");
    LOG("== r2  load_on_T : create/sethandle/resize 在 K，LoadHTMLW 主线程直调 ==");
    c->round = 2;
    run_task(t_destroy,   c);
    run_task(t_create,    c);
    if (!c->wv) { LOG("   RESULT(r2): FAIL —— create 失败"); return; }
    run_task(t_sethandle, c);
    run_task(t_resize,    c);
    {   /* ★ 直通：在业务线程（主）上直接调内核 API */
        wchar_t html[512];
        make_html(c->round, html, 512);
        LOG("   [T] 直调 wkeLoadHTMLW(R-2) ...");
        c->n.LoadHTMLW(c->wv, html);
        LOG("   [T] 直调返回");
    }
    monitor(c, 16, "r2");
    LOG("   RESULT(r2 load_on_T): %s",
        (c->loaded == 1 && strstr(c->title, "R-2")) ? "PASS —— LoadHTMLW 可直通" : "FAIL");
}

static void v_sethandle_on_T(CTX* c)
{
    LOG("");
    LOG("== r3  sethandle_on_T : create/resize/loadhtml 在 K，SetHandle 主线程直调 ==");
    c->round = 3;
    run_task(t_destroy,   c);
    run_task(t_create,    c);
    if (!c->wv) { LOG("   RESULT(r3): FAIL —— create 失败"); return; }
    {   /* ★ 直通：SetHandle 在 HWND 所属线程（主）执行 */
        LOG("   [T] 直调 wkeSetHandle(view, 0x%08X) ...", (unsigned)(UINT_PTR)c->hwnd);
        c->n.SetHandle(c->wv, c->hwnd);
        LOG("   [T] 直调返回");
        c->n.Resize(c->wv, 800, 600);
    }
    run_task(t_loadhtml,  c);
    monitor(c, 16, "r3");
    LOG("   RESULT(r3 sethandle_on_T): %s",
        (c->loaded == 1 && strstr(c->title, "R-3")) ? "PASS —— SetHandle 可直通" : "FAIL");
}

static void v_ops_on_T(CTX* c)
{
    LOG("");
    LOG("== r4  ops_on_T : 主线程直调各类内核 API（预期可能挂死/崩溃）==");
    LOG("   [T] (1) wkeResize ...");
    c->n.Resize(c->wv, 800, 600);
    LOG("   [T] (1) OK");
    LOG("   [T] (2) wkeGetTitleW ...");
    { const wchar_t* t = c->n.GetTitleW(c->wv); char a[256]; w2a(t, a, sizeof(a));
      LOG("   [T] (2) OK title=\"%s\"", a); }
    LOG("   [T] (3) wkeIsLoadingCompleted ...");
    LOG("   [T] (3) OK = %d", c->n.IsLoaded(c->wv));
    if (c->n.FireMouse) {
        LOG("   [T] (4) wkeFireMouseEvent(LBUTTONDOWN 10,10) ...");
        c->n.FireMouse(c->wv, 0x0201, 10, 10, 0);
        LOG("   [T] (4) OK");
        pump(300);
    }
    LOG("   [T] (5) wkeLoadHTMLW(R-4 直通) ...");
    { wchar_t html[512]; c->round = 4; make_html(c->round, html, 512);
      c->n.LoadHTMLW(c->wv, html); }
    LOG("   [T] (5) 直调返回");
    monitor(c, 16, "r4");
    LOG("   RESULT(r4 ops_on_T): %s",
        (c->loaded == 1 && strstr(c->title, "R-4"))
            ? "PASS —— 全部可直通（只 create/destroy 需编组）" : "FAIL/部分");
}

/* ---------------- main ---------------- */
int main(int argc, char** argv)
{
    CTX     c;
    HANDLE  readyK;
    const char* mode = (argc > 1) ? argv[1] : "r1";

    {
        const char* lp = getenv("MBPROBE_LOG");
        if (lp) g_log = fopen(lp, "wb");
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    LOG("======================================================================");
    LOG(" marshal_probe —— persistent kernel thread + marshaling feasibility");
    LOG(" mode : %s", mode);
    LOG("======================================================================");

    ZeroMemory(&c, sizeof(c));
    if (!raw_load(&c.n)) return 1;

    readyK = CreateEventA(NULL, TRUE, FALSE, NULL);
    { HANDLE thK = CreateThread(NULL, 0, kernel_thread, readyK, 0, NULL);
      (void)thK; }
    WaitForSingleObject(readyK, 10000);
    CloseHandle(readyK);

    c.hwnd = make_window(0, 0);
    c.origProc = c.hwnd ? (WNDPROC)GetWindowLongPtrA(c.hwnd, GWLP_WNDPROC) : NULL;
    LOG("   业务线程（主）创建 hwnd=0x%08X tid=%lu 原生WndProc=0x%08X",
        (unsigned)(UINT_PTR)c.hwnd, (unsigned long)GetCurrentThreadId(),
        (unsigned)(UINT_PTR)c.origProc);
    if (!c.hwnd) { LOG("!! CreateWindow 失败"); return 1; }

    if      (strcmp(mode, "r0") == 0) v_control(&c);
    else if (strcmp(mode, "r1") == 0) v_all_on_K(&c);
    else if (strcmp(mode, "r2") == 0) { run_task(t_init, &c); v_load_on_T(&c); }
    else if (strcmp(mode, "r3") == 0) { run_task(t_init, &c); v_sethandle_on_T(&c); }
    else if (strcmp(mode, "r4") == 0) {
        run_task(t_init, &c); c.round = 4;
        run_task(t_create, &c); run_task(t_sethandle, &c);
        run_task(t_resize, &c); run_task(t_loadhtml, &c);
        pump(500);
        v_ops_on_T(&c);
    }
    else LOG("未知模式 %s", mode);

    run_task(t_destroy, &c);
    DestroyWindow(c.hwnd);
    pump(150);

    LOG("");
    LOG("== done ==");
    if (g_log) { fclose(g_log); g_log = NULL; }
    return 0;
}
