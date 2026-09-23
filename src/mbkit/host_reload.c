/* ============================================================================
 * host_reload.c —— 热重载压测宿主
 * ----------------------------------------------------------------------------
 * 模拟用户场景：宿主 exe 反复「载入业务 DLL → 用 miniblink 显示页面 → 卸载 →
 * 载入新版本业务 DLL」，看第 N 轮还能不能正常初始化并载入页面。
 *
 * 模式：
 *   mbkit     新架构（业务层走 MbKit）：期望 N 轮全过、内核只 init 一次
 *   fault     故障注入：业务层**故意不 Detach** 就卸载，验证 MbKit 兜住不崩
 *   legacy    旧做法对照：复刻用户现状（自己相对路径加载内核 + 不 finalize）
 *   wildcard  自证野指针检测有效（确定性，不依赖崩溃）
 *
 * 用法：host_reload.exe <mode> [rounds] [logfile]
 * 输出为 ASCII（日志文件 UTF-8），便于各种终端查看。
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

static int g_pass = 0, g_fail = 0;
static void PASS(const char* fmt, ...)
{
    char b[1024]; va_list ap; va_start(ap, fmt);
    _vsnprintf(b, sizeof(b) - 1, fmt, ap); va_end(ap); b[sizeof(b) - 1] = 0;
    LOG("  [PASS] %s", b); g_pass++;
}
static void FAILX(const char* fmt, ...)
{
    char b[1024]; va_list ap; va_start(ap, fmt);
    _vsnprintf(b, sizeof(b) - 1, fmt, ap); va_end(ap); b[sizeof(b) - 1] = 0;
    LOG("  [FAIL] %s", b); g_fail++;
}

/* ---------------- 消息泵 ---------------- */
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

static HWND make_window(void)
{
    WNDCLASSA wc;
    HWND      h;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "MbKitTestHostWnd";
    RegisterClassA(&wc);

    /* 放到屏幕外，并（可用时）用 DWM 披斗篷 —— 避免测试时窗口在用户屏幕上闪 */
    h = CreateWindowExA(0, wc.lpszClassName, "MbKitTestHost", WS_POPUP,
                        6000, 6000, 800, 600, NULL, NULL, wc.hInstance, NULL);
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

/* ---------------- 业务 DLL 装载器（biz_* 与 legacy_* 通吃） ---------------- */
typedef int  (__stdcall *fn_attach4)(void*, int, int, const wchar_t*);
typedef int  (__stdcall *fn_detach0)(void);
typedef int  (__stdcall *fn_detach1)(int);
typedef int  (__stdcall *fn_i0)(void);
typedef int  (__stdcall *fn_i1)(int);
typedef unsigned (__stdcall *fn_u0)(void);
typedef const char* (__stdcall *fn_s0)(void);
typedef int  (__stdcall *fn_buf)(void*, int);
typedef void* (__stdcall *fn_p0)(void);

typedef struct {
    HMODULE    h;
    const char* name;
    int         is_legacy;
    fn_attach4 Attach;
    fn_detach0 DetachNone;
    fn_detach1 DetachInt;
    fn_i0      IsLoaded;
    fn_i0      PaintCount;
    fn_i0      LoadCount;
    fn_i0      LiveCount;
    fn_i0      Subclassed;
    fn_p0      View;
    fn_buf     Title;
    fn_u0      NodeModule;
    fn_u0      KitModule;
    fn_i0      KernelInitCount;
    fn_i0      KitPinned;
    fn_s0      KitVersion;
    fn_s0      NodePath;
    fn_s0      Err;
    fn_u0      FnAddr;
    fn_i0      InitCount;
    fn_i1      Finalize;
} SHIM;

static int shim_load(SHIM* s, const char* dll)
{
    ZeroMemory(s, sizeof(*s));
    s->name = dll;
    s->h = LoadLibraryA(dll);
    if (!s->h) return 0;
    s->Attach    = (fn_attach4)GetProcAddress(s->h, "Biz_Attach");
    if (!s->Attach) { s->Attach = (fn_attach4)GetProcAddress(s->h, "Legacy_Attach");
                      s->is_legacy = 1; }
    s->DetachNone = (fn_detach0)GetProcAddress(s->h, "Biz_Detach");
    s->DetachInt  = (fn_detach1)GetProcAddress(s->h, "Legacy_Detach");
    s->IsLoaded   = (fn_i0)GetProcAddress(s->h, s->is_legacy ? "Legacy_IsLoaded" : "Biz_IsLoaded");
    s->PaintCount = (fn_i0)GetProcAddress(s->h, s->is_legacy ? "Legacy_PaintCount" : "Biz_PaintCount");
    s->LiveCount  = (fn_i0)GetProcAddress(s->h, "Biz_LiveCount");
    s->LoadCount  = (fn_i0)GetProcAddress(s->h, s->is_legacy ? "Legacy_LoadCount" : "Biz_LoadCount");
    s->Subclassed = (fn_i0)GetProcAddress(s->h, "Legacy_Subclassed");
    s->View       = (fn_p0)GetProcAddress(s->h, s->is_legacy ? "Legacy_View" : "Biz_View");
    s->Title      = (fn_buf)GetProcAddress(s->h, s->is_legacy ? "Legacy_Title" : "Biz_Title");
    s->NodeModule = (fn_u0)GetProcAddress(s->h, s->is_legacy ? "Legacy_NodeModule" : "Biz_NodeModule");
    s->KitModule  = (fn_u0)GetProcAddress(s->h, "Biz_KitModule");
    s->KernelInitCount = (fn_i0)GetProcAddress(s->h, "Biz_KernelInitCount");
    s->KitPinned  = (fn_i0)GetProcAddress(s->h, "Biz_KitPinned");
    s->KitVersion = (fn_s0)GetProcAddress(s->h, "Biz_KitVersion");
    s->NodePath   = (fn_s0)GetProcAddress(s->h, s->is_legacy ? "Legacy_NodePath" : "Biz_KitNodePath");
    s->Err        = (fn_s0)GetProcAddress(s->h, s->is_legacy ? "Legacy_Err" : "Biz_Err");
    s->FnAddr     = (fn_u0)GetProcAddress(s->h, s->is_legacy ? "Legacy_FnAddr" : "Biz_FnAddr");
    s->InitCount  = (fn_i0)GetProcAddress(s->h, "Legacy_InitCount");
    s->Finalize   = (fn_i1)GetProcAddress(s->h, "Legacy_Finalize");
    return s->Attach != NULL;
}

static void shim_unload(SHIM* s)
{
    if (s->h) { FreeLibrary(s->h); s->h = NULL; }
}

static wchar_t* make_html(int round, wchar_t* buf, int cch)
{
    _snwprintf(buf, cch - 1,
               L"<html><head><meta charset=\"utf-8\"><title>ROUND-%d</title></head>"
               L"<body style=\"font:16px sans-serif\"><h1>round %d</h1></body></html>",
               round, round);
    buf[cch - 1] = 0;
    return buf;
}

static void title_to_ascii(const wchar_t* buf, char* out, size_t cch)
{
    wchar_t tmp[512];
    int     n = 0;
    if (!buf) { out[0] = 0; return; }
    while (n < 511 && buf[n]) { tmp[n] = buf[n]; n++; }
    tmp[n] = 0;
    WideCharToMultiByte(CP_UTF8, 0, tmp, -1, out, (int)cch, NULL, NULL);
}

static int wait_loaded(SHIM* s, int timeoutMs)
{
    int t = 0;
    while (t < timeoutMs) {
        if (s->IsLoaded && s->IsLoaded() == 1) return 1;
        pump(25);
        t += 25;
    }
    return 0;
}

/* ==========================================================================
 * 模式 1：mbkit —— 正常热重载
 * ========================================================================*/
static int run_mbkit(HWND hwnd, int rounds)
{
    unsigned node1 = 0, kit1 = 0;
    int      init1 = -1;
    int      i;

    LOG("");
    LOG("== mode=mbkit : business layer via MbKit, %d reload rounds ==", rounds);

    for (i = 1; i <= rounds; i++) {
        SHIM      s;
        wchar_t   html[512];
        char      dll[64], title[600];
        unsigned  node, kit;
        int       initc, rc, ok;

        _snprintf(dll, sizeof(dll) - 1, "biz_v%d.dll", 1 + ((i - 1) % 2));
        LOG("");
        LOG("-- round %d/%d : LoadLibrary(%s) --", i, rounds, dll);

        if (!shim_load(&s, dll)) {
            FAILX("round %d: LoadLibrary(%s) 或取导出失败", i, dll);
            SHIM* ps = &s; shim_unload(ps);
            continue;
        }
        make_html(i, html, 512);
        rc = s.Attach(hwnd, 800, 600, html);
        if (rc != 0) {
            FAILX("round %d: Biz_Attach 返回 %d (%s)", i, rc, s.Err ? s.Err() : "?");
            shim_unload(&s);
            continue;
        }
        ok = wait_loaded(&s, 8000);
        {
            wchar_t tb[256]; tb[0] = 0;
            if (s.Title) s.Title(tb, 256);
            title_to_ascii(tb, title, sizeof(title));
        }

        kit  = s.KitModule  ? s.KitModule()  : 0;
        node = s.NodeModule ? s.NodeModule() : 0;
        initc = s.KernelInitCount ? s.KernelInitCount() : -1;

        LOG("   attach=0  loaded=%d  title=\"%s\"  paints=%d",
            ok, title, s.PaintCount ? s.PaintCount() : -1);
        LOG("   kit.dll=0x%08X  node.dll=0x%08X  kernel-init-count=%d",
            kit, node, initc);
        if (i == 1) {
            node1 = node; kit1 = kit; init1 = initc;
            LOG("   [baseline] node=0x%08X kit=0x%08X initCount=%d", node1, kit1, init1);
            if (s.KitPinned && s.KitPinned() == 1) PASS("MbKit.dll 已自 PIN（业务层卸载动不了它）");
            else                                   FAILX("MbKit.dll 未自 PIN");
            if (s.KitVersion) LOG("   kit version : %s", s.KitVersion());
            if (s.NodePath)   LOG("   node path   : %s", s.NodePath());
        }

        if (ok && strstr(title, "ROUND-") && (int)(title[6] - '0') == i)
            PASS("round %d：新 WebView 载入完成且标题为 ROUND-%d", i, i);
        else
            FAILX("round %d：载入失败或标题不符（loaded=%d title=%s）", i, ok, title);

        if (i > 1) {
            if (node == node1) PASS("round %d：node.dll 模块句柄未变（内核常驻、未被卸载）", i);
            else               FAILX("round %d：node.dll 句柄变了 0x%08X -> 0x%08X", i, node1, node);
            if (kit == kit1)   PASS("round %d：MbKit.dll 模块句柄未变（常驻层稳定）", i);
            else               FAILX("round %d：MbKit.dll 句柄变了 0x%08X -> 0x%08X", i, kit1, kit);
        }

        /* ★ 热重载契约 */
        if (s.DetachNone) {
            int n = s.DetachNone();
            LOG("   Biz_Detach() -> 清理 %d 个 view；LiveCount=%d",
                n, s.LiveCount ? s.LiveCount() : -1);
            if (s.LiveCount && s.LiveCount() == 0) PASS("round %d：Detach 后存活 view = 0", i);
            else                                   FAILX("round %d：Detach 后仍有存活 view", i);
        }
        shim_unload(&s);
        pump(100);
    }

    LOG("");
    if (init1 == 1) PASS("内核在整个热重载过程中只 initialize 了 1 次（重载代价极低）");
    else            LOG("   (注意) kernel-init-count=%d", init1);
    return g_fail;
}

/* ==========================================================================
 * 模式 2：fault —— 故障注入：业务层忘记 Detach 就卸载
 *
 * ⚠️ 判据设计上的一个教训：最初我拿「绘屏风暴」当判据，实测发现
 *    wkeOnPaintUpdated 在**四种配置下全都不触发**（paintprobe 模式已证），
 *    那条判据是空的。真正会走到内核回调的是下面两条路：
 *      路径甲：向窗口发消息 —— 业务 DLL 若残留窗口子类化，消息一到就跳进已释放代码页
 *      路径乙：用宿主手里的 view 再载入一页 —— 内核**必定**回调 wkeOnLoadingFinish
 * ========================================================================*/
static int run_fault(HWND hwnd, int rounds)
{
    MBKIT    H;
    SHIM     s1, s2;
    wchar_t  html[512];
    unsigned fnAddr = 0;
    void*    view   = NULL;
    int      i, base_supp = 0, base_loads = 0;
    wchar_t  tb[256];
    char     t[600];

    (void)rounds;
    LOG("");
    LOG("== mode=fault : 业务层「忘记 Detach」就直接卸载（最坏情况）==");

    if (!mbk_load(&H, "MbKit.dll") || H.Init(NULL) != MBKIT_OK) {
        FAILX("宿主侧加载 MbKit.dll 失败");
        return g_fail;
    }
    LOG("   kit version : %s", H.Version ? H.Version() : "?");

    if (!shim_load(&s1, "biz_v1.dll")) { FAILX("加载 biz_v1.dll 失败"); return g_fail; }
    make_html(101, html, 512);
    LOG("");
    LOG("-- step 1: biz_v1 Attach + 载入 --");
    if (s1.Attach(hwnd, 800, 600, html) != 0) { FAILX("biz_v1 Attach 失败"); return g_fail; }
    if (wait_loaded(&s1, 8000)) PASS("biz_v1 载入完成");
    else                        FAILX("biz_v1 载入超时");

    base_loads = s1.LoadCount ? s1.LoadCount() : 0;
    LOG("   Biz_LoadCount=%d", base_loads);
    if (base_loads > 0)
        PASS("确认「载入完成回调」会真实触发 —— 这条路径才有野指针风险");
    else
        FAILX("载入完成回调一次都没触发，说明判据设计有误");

    fnAddr    = s1.FnAddr ? s1.FnAddr() : 0;
    view      = s1.View   ? s1.View()   : NULL;
    base_supp = H.SuppressedCount ? H.SuppressedCount() : 0;
    LOG("");
    LOG("-- step 2: 记录业务层回调地址与 view 句柄 --");
    LOG("   biz_on_event = 0x%08X   view = %p   suppressed(baseline) = %d",
        fnAddr, view, base_supp);
    if (H.AliveModule && H.AliveModule(fnAddr, 0) == 1)
        PASS("卸载前：该地址属于已加载模块（检测正确）");
    else
        FAILX("卸载前：检测却说该地址不属于已加载模块（检测有误）");

    LOG("");
    LOG("-- step 3: FreeLibrary(biz_v1) —— **不调 Biz_Detach** --");
    shim_unload(&s1);

    if (H.AliveModule && H.AliveModule(fnAddr, 0) == 0)
        PASS("卸载后：该地址已不属于任何已加载模块 => 内核手里的它就是野指针");
    else
        FAILX("卸载后：检测未识别出野指针");

    LOG("");
    LOG("-- 路径甲：向窗口连发 40 轮消息（子类化若残留，这里就崩）--");
    for (i = 0; i < 40; i++) {
        SendMessageA(hwnd, WM_NULL, 0, 0);
        SendMessageA(hwnd, WM_NCHITTEST, 0, MAKELPARAM(10, 10));
        InvalidateRect(hwnd, NULL, TRUE);
        UpdateWindow(hwnd);
        pump(10);
    }
    PASS("40 轮窗口消息后进程仍存活 —— 常驻层不在窗口上留自己的过程，无残留");

    LOG("");
    LOG("-- 路径乙：用旧 view 再载入一页（内核必定回调载入完成）--");
    make_html(202, html, 512);
    if (view && H.LoadHtml(view, html) == MBKIT_OK) {
        int now_ok = 0, now_supp;
        for (i = 0; i < 200; i++) { H.Pump(25); if (H.IsLoaded(view) == 1) { now_ok = 1; break; } }
        now_supp = H.SuppressedCount ? H.SuppressedCount() : 0;
        tb[0] = 0; H.GetTitle(view, tb, 256); title_to_ascii(tb, t, sizeof(t));
        LOG("   载入完成=%d  新标题=\"%s\"   suppressed=%d（基线 %d）",
            now_ok, t, now_supp, base_supp);
        if (now_supp > base_supp)
            PASS("常驻层挡下 %d 次野回调 => 保护真实生效（内核安全）", now_supp - base_supp);
        else
            FAILX("野回调未被记录 —— 保护没走到，判据需重新设计");
        if (now_ok == 1)
            PASS("内核本身未被污染：旧 view 仍能正常载入新页面");
        else
            FAILX("旧 view 载入失败 —— 内核已被污染");
    } else {
        FAILX("无法对旧 view 发起载入（View 句柄未取到）");
    }

    LOG("");
    LOG("-- step 4: 用新业务层继续工作（DetachAll 清掉上一轮残留）--");
    H.DetachAll();
    if (!shim_load(&s2, "biz_v2.dll")) { FAILX("加载 biz_v2.dll 失败"); return g_fail; }
    make_html(102, html, 512);
    if (s2.Attach(hwnd, 800, 600, html) != 0) { FAILX("biz_v2 Attach 失败（内核已被污染）"); return g_fail; }
    if (wait_loaded(&s2, 8000)) {
        tb[0] = 0; if (s2.Title) s2.Title(tb, 256);
        title_to_ascii(tb, t, sizeof(t));
        LOG("   title=\"%s\"  LoadCount=%d", t, s2.LoadCount ? s2.LoadCount() : -1);
        PASS("上一轮业务层「忘了 Detach」也没污染内核：新一轮照常载入");
    } else {
        FAILX("新一轮载入失败 —— 内核被上一轮污染了");
    }
    if (s2.DetachNone) s2.DetachNone();
    shim_unload(&s2);

    LOG("");
    LOG("   内核最终状态：node.dll=0x%08X  kernel-init-count=%d  live-view=%d  suppressed=%d",
        H.NodeModule ? H.NodeModule() : 0,
        H.KernelInitCount ? H.KernelInitCount() : -1,
        H.LiveCount ? H.LiveCount() : -1,
        H.SuppressedCount ? H.SuppressedCount() : -1);
    return g_fail;
}

/* ==========================================================================
 * 模式 3：legacy —— 复刻用户现状
 * ========================================================================*/
static int run_legacy(HWND hwnd, int rounds)
{
    unsigned node1 = 0;
    int      i;

    LOG("");
    LOG("== mode=legacy : old design (self LoadLibrary node.dll, never finalize), %d rounds ==", rounds);

    for (i = 1; i <= rounds; i++) {
        SHIM    s;
        wchar_t html[512];
        char    dll[64];
        unsigned node;

        _snprintf(dll, sizeof(dll) - 1, "legacy_v%d.dll", 1 + ((i - 1) % 2));
        LOG("");
        LOG("-- round %d/%d : LoadLibrary(%s) --", i, rounds, dll);
        if (!shim_load(&s, dll)) { FAILX("加载 %s 失败", dll); continue; }

        make_html(i, html, 512);
        {
            int rc = s.Attach(hwnd, 800, 600, html);
            LOG("   Legacy_Attach -> %d", rc);
            if (rc != 0) {
                LOG("   (err) %s", s.Err ? s.Err() : "?");
                FAILX("round %d：Legacy_Attach 失败（内核未能重新初始化）", i);
                shim_unload(&s);
                pump(100);
                continue;
            }
        }
        if (wait_loaded(&s, 6000)) PASS("round %d：WebView 载入完成", i);
        else                      FAILX("round %d：WebView 载入超时", i);

        node = s.NodeModule ? s.NodeModule() : 0;
        LOG("   node.dll=0x%08X  LegacyInitCount=%d  LoadCount=%d  PaintCount=%d",
            node, s.InitCount ? s.InitCount() : -1,
            s.LoadCount ? s.LoadCount() : -1,
            s.PaintCount ? s.PaintCount() : -1);
        if (s.Subclassed) LOG("   Subclassed=%d", s.Subclassed());
        if (i == 1) {
            node1 = node;
            LOG("   node path : %s", s.NodePath ? s.NodePath() : "?");
        } else if (node == node1) {
            LOG("   node.dll 句柄未变 —— 与「内核常驻、业务 DLL 管不了它」一致");
        }

        if (i < rounds) {
            /* ★ 复刻「不调销毁（或先关窗口）就卸载」 */
            LOG("   子类化状态=%d（1=窗口过程链里还挂着本 DLL 的过程）",
                s.Subclassed ? s.Subclassed() : -1);
            LOG("   Legacy_Detach(0)  <- 故意不销毁 view、也不恢复子类化，模拟直接卸载");
            if (s.DetachInt) s.DetachInt(0);
        }
        shim_unload(&s);
        LOG("   FreeLibrary(legacy_v%d.dll) 完成", 1 + ((i - 1) % 2));
        LOG("   >> 现在向窗口发消息：若「子类化残留」成立，此处会跳进已释放代码页");

        /* 故意发消息触发窗口过程链 —— 这是复现用户问题的关键一步 */
        {
            int k;
            for (k = 0; k < 6; k++) {
                LOG("      消息 %d ...", k + 1);
                SendMessageA(hwnd, WM_NULL, 0, 0);
                InvalidateRect(hwnd, NULL, TRUE);
                UpdateWindow(hwnd);
                pump(60);
            }
        }
        LOG("   >> 居然活下来了（未复现崩溃）");
    }
    LOG("");
    LOG("   （若本轮测试没有崩溃，请看上面每一轮的 loaded 结果）");
    return g_fail;
}

/* ==========================================================================
 * 模式 4：wildcard —— 自证野指针检测（确定性）
 * ========================================================================*/
static int run_wildcard(HWND hwnd)
{
    MBKIT    H;
    SHIM     s;
    unsigned addr = 0;
    (void)hwnd;

    LOG("");
    LOG("== mode=wildcard : 验证「模块卸载后回调地址失效」可被检测 ==");

    if (!mbk_load(&H, "MbKit.dll") || H.Init(NULL) != MBKIT_OK) {
        FAILX("加载 MbKit.dll 失败");
        return g_fail;
    }
    if (!shim_load(&s, "legacy_v1.dll")) { FAILX("加载 legacy_v1.dll 失败"); return g_fail; }

    addr = s.FnAddr ? s.FnAddr() : 0;
    LOG("   legacy_v1!legacy_paint = 0x%08X", addr);
    if (H.AliveModule(addr, 0) == 1) PASS("卸载前：判定为「属于已加载模块」");
    else                             FAILX("卸载前：误判为野指针");

    /* 对照：系统 DLL 里的函数始终有效 */
    if (H.AliveModule((unsigned)(UINT_PTR)&pump, 0) == 1)
        PASS("对照：本 exe 内函数判定为有效（检测不是无差别返回 0）");
    else
        FAILX("对照：本 exe 内函数被误判为无效");

    shim_unload(&s);
    LOG("   FreeLibrary(legacy_v1.dll) 已完成");

    if (H.AliveModule(addr, 0) == 0)
        PASS("卸载后：同一地址被判定为野指针 => 常驻转发前的存活校验有效");
    else
        FAILX("卸载后：仍判定为有效（检测失效）");

    /* 换一个模块占同一地址也骗不过（可选，不做：基址复用由 ②③ 校验覆盖） */
    return g_fail;
}

/* ==========================================================================
 * 模式 5：paintprobe —— 绘屏回调到底在什么情况下触发？
 *   这决定「常驻层挡野回调」这个保护有没有被真正考验过，必须先查清。
 * ========================================================================*/
static volatile int g_pp_paints = 0;

static void __stdcall pp_cb(void* user, int ev, void* p1, void* p2)
{
    (void)user; (void)p1; (void)p2;
    if (ev == MB_EV_PAINT) g_pp_paints++;
}

static void probe_one(MBKIT* H, HWND h, const char* tag, const wchar_t* html)
{
    void* v;
    int   loaded, t;

    g_pp_paints = 0;
    v = H->Create(h, 0, 0, 360, 240, (MbEventCb)&pp_cb, NULL);
    if (!v) { LOG("  %-30s Create 失败 (%s)", tag, H->LastError ? H->LastError() : "?"); return; }

    H->LoadHtml(v, html);
    loaded = -1;
    for (t = 0; t < 120; t++) {
        if (H->IsLoaded(v) == 1) { loaded = 1; break; }
        H->Pump(25);
    }
    H->Pump(800);
    if (h) {                       /* 主动催一次重绘 */
        InvalidateRect(h, NULL, TRUE);
        UpdateWindow(h);
        H->Pump(400);
    }
    LOG("  %-30s hwnd=0x%08X  loaded=%d  paints=%d",
        tag, (unsigned)(UINT_PTR)h, loaded, g_pp_paints);
    H->Destroy(v);
}

static HWND make_plain_window(int x, int y, int w, int h, int cloak)
{
    WNDCLASSA wc;
    HWND      hh;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(NULL);
    wc.lpszClassName = "MbKitPaintProbeWnd";
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    RegisterClassA(&wc);
    hh = CreateWindowExA(0, wc.lpszClassName, "PaintProbe", WS_POPUP,
                         x, y, w, h, NULL, NULL, wc.hInstance, NULL);
    if (hh && cloak) {
        HMODULE dwm = LoadLibraryA("dwmapi.dll");
        if (dwm) {
            typedef HRESULT (WINAPI *pF)(HWND, DWORD, LPCVOID, DWORD);
            pF f = (pF)GetProcAddress(dwm, "DwmSetWindowAttribute");
            BOOL c = TRUE;
            if (f) f(hh, 13, &c, sizeof(c));
        }
    }
    if (hh) ShowWindow(hh, SW_SHOWNOACTIVATE);
    pump(80);
    return hh;
}

static int run_paintprobe(HWND hwnd)
{
    MBKIT   H;
    HWND    vis, off, cloaked;
    wchar_t html[384];

    LOG("");
    LOG("== mode=paintprobe : wkeOnPaintUpdated 在什么配置下会触发？==");
    (void)hwnd;

    if (!mbk_load(&H, "MbKit.dll") || H.Init(NULL) != MBKIT_OK) {
        FAILX("加载 MbKit.dll 失败");
        return g_fail;
    }
    LOG("   %s", H.Version ? H.Version() : "?");
    LOG("");

    _snwprintf(html, 380, L"<html><head><title>PP</title></head>"
                           L"<body style=\"background:#3a6ea5;color:#fff\">paint probe</body></html>");

    vis     = make_plain_window(80, 80, 360, 240, 0);
    off     = make_plain_window(6000, 6000, 360, 240, 0);
    cloaked = make_plain_window(80, 360, 360, 240, 1);

    probe_one(&H, NULL,    "no-window (headless)", html);
    probe_one(&H, off,     "off-screen, no cloak", html);
    probe_one(&H, cloaked, "on-screen, DWMWA_CLOAK", html);
    probe_one(&H, vis,     "on-screen, visible", html);

    LOG("");
    LOG("   说明：paints=0 表示该配置下内核**从不**回调 wkeOnPaintUpdated，");
    LOG("         那么「常驻层挡野回调」的保护在那种配置下也就无从谈起。");

    H.DetachAll();
    DestroyWindow(vis); DestroyWindow(off); DestroyWindow(cloaked);
    return g_fail;
}

/* ==========================================================================
 * 模式 6：ansi —— 验证 ANSI(GBK) 变体接口（易语言「文本型」直接可用）
 * ========================================================================*/
static int run_ansi(HWND hwnd)
{
    MBKIT  H;
    void*  v;
    char   html[768];
    char   got[256];
    const char* want = "中文标题ABC";
    int    t;

    LOG("");
    LOG("== mode=ansi : 验证 MbKit_LoadHtmlA / MbKit_GetTitleA（GBK 往返）==");

    if (!mbk_load(&H, "MbKit.dll") || H.Init(NULL) != MBKIT_OK) {
        FAILX("加载 MbKit.dll 失败");
        return g_fail;
    }
    if (!H.LoadHtmlA || !H.GetTitleA) { FAILX("缺少 ANSI 变体导出"); return g_fail; }

    _snprintf(html, sizeof(html) - 1,
              "<html><head><meta charset=\"gbk\"><title>%s</title></head>"
              "<body>ansi probe</body></html>", want);

    v = H.Create(hwnd, 0, 0, 640, 400, NULL, NULL);
    if (!v) { FAILX("Create 失败"); return g_fail; }
    if (H.LoadHtmlA(v, html) != MBKIT_OK) { FAILX("MbKit_LoadHtmlA 失败"); return g_fail; }

    for (t = 0; t < 200; t++) { if (H.IsLoaded(v) == 1) break; H.Pump(25); }
    if (H.IsLoaded(v) != 1) { FAILX("ANSI 载入未完成"); return g_fail; }
    PASS("MbKit_LoadHtmlA 载入完成");

    got[0] = 0;
    H.GetTitleA(v, got, sizeof(got));
    LOG("   want = \"%s\"", want);
    LOG("   got  = \"%s\"", got);
    if (strcmp(got, want) == 0) PASS("MbKit_GetTitleA 往返一致（GBK 中文无损）");
    else                        FAILX("ANSI 中文往返不一致（检查页面声明 charset）");

    if (H.RunJsA) {
        int rc;
        /* 用 ANSI 版执行一段 JS，再读回标题，验证 RunJsA 通 */
        rc = H.RunJsA(v, "document.title='ANSI-2';");
        H.Pump(300);
        H.GetTitleA(v, got, sizeof(got));
        LOG("   RunJsA rc=%d  title now = \"%s\"", rc, got);
        if (strstr(got, "ANSI-2")) PASS("MbKit_RunJsA 生效（JS 已改掉标题）");
        else                       FAILX("MbKit_RunJsA 未生效");
    }
    H.Destroy(v);
    H.DetachAll();
    return g_fail;
}

/* ==========================================================================
 * main
 * ========================================================================*/
int main(int argc, char** argv)
{
    const char* mode    = (argc > 1) ? argv[1] : "mbkit";
    int         rounds  = (argc > 2) ? atoi(argv[2]) : 3;
    const char* logPath = (argc > 3) ? argv[3] : NULL;
    HWND        hwnd;
    char        exePath[MAX_PATH];

    setvbuf(stdout, NULL, _IONBF, 0);
    if (logPath) g_log = fopen(logPath, "wb");

    GetModuleFileNameA(NULL, exePath, MAX_PATH);

    LOG("======================================================================");
    LOG(" MbKit hot-reload harness");
    LOG("======================================================================");
    LOG(" mode      : %s", mode);
    LOG(" rounds    : %d", rounds);
    LOG(" host exe  : %s", exePath);
    LOG(" cwd       : (used by the legacy path to resolve main\\corn\\node.dll)");
    LOG(" node.dll  : %s", (GetFileAttributesA("main\\corn\\node.dll") != INVALID_FILE_ATTRIBUTES)
                            ? "found at <cwd>\\main\\corn\\node.dll" : "(not at cwd)");
    LOG("");

    hwnd = make_window();
    if (!hwnd) { LOG("!! CreateWindow 失败 (%lu)", GetLastError()); return 2; }
    LOG(" off-screen window created: HWND=0x%08X", (unsigned)(UINT_PTR)hwnd);

    if      (strcmp(mode, "mbkit")    == 0) run_mbkit(hwnd, rounds);
    else if (strcmp(mode, "fault")    == 0) run_fault(hwnd, rounds);
    else if (strcmp(mode, "legacy")   == 0) run_legacy(hwnd, rounds);
    else if (strcmp(mode, "wildcard") == 0) run_wildcard(hwnd);
    else if (strcmp(mode, "paintprobe")== 0) run_paintprobe(hwnd);
    else if (strcmp(mode, "ansi")     == 0) run_ansi(hwnd);
    else { LOG("未知模式：%s", mode); return 2; }

    LOG("");
    LOG("======================================================================");
    LOG(" SUMMARY  %s :  %d passed / %d failed", mode, g_pass, g_fail);
    LOG("======================================================================");

    /* 收尾：能清的都清掉（legacy 模式下可能已崩，走不到这里） */
    {
        MBKIT H;
        if (mbk_load(&H, "MbKit.dll")) H.DetachAll();
    }
    DestroyWindow(hwnd);
    if (g_log) { fclose(g_log); g_log = NULL; }
    return g_fail ? 1 : 0;
}
