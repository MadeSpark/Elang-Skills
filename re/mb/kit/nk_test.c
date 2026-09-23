/* ============================================================================
 * nk_test.c —— 端到端验证：用「易语言模块的调用方式」驱动改造后的 MbKit
 *
 * 覆盖四件事：
 *   1) 转发桩：业务按**名字**取 wke* 地址并按 cdecl 调用，能正常建窗/载入；
 *   2) 懒加载：业务第一次调 wkeInitialize 时才去 LoadLibrary(node.dll)；
 *   3) 回调拦截：业务注册的是 VirtualAlloc thunk，内核侧只该看到 MbKit 的 wrapper；
 *   4) 热重载：业务 DLL 卸载→重载多轮都正常；且「忘记 Detach」也不会崩。
 *
 * 触发事件的手法：页面里用 setInterval 不停改 document.title，
 * 这样卸载业务 DLL 之后内核仍会继续触发 title 回调 —— 野指针若不设防，
 * 这里必然踩中。
 * ==========================================================================*/
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
static FILE* g_log = NULL;

static void LOG(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    va_end(ap);
    printf("%s\n", buf);
    if (g_log) { fprintf(g_log, "%s\n", buf); fflush(g_log); }
}
static void PASS(const char* m) { LOG("   [PASS] %s", m); }
static void FAILX(const char* m) { LOG("   [FAIL] %s", m); g_fail++; }

/* ---- MbKit 接口（宿主侧只需要这几个） ---- */
typedef int  (__stdcall *fn_i0)(void);
typedef const char* (__stdcall *fn_s0)(void);
typedef void (__stdcall *fn_v0)(void);

typedef struct {
    HMODULE h;
    fn_v0  DetachAll;
    fn_i0  CbCount, CbHits, CbBlocked, CbLive, Suppressed;
    fn_s0  KernelPath;
} KIT;

static int kit_load(KIT* k)
{
    memset(k, 0, sizeof(*k));
    k->h = LoadLibraryA("MbKit.dll");
    if (!k->h) return 0;
    k->DetachAll  = (fn_v0)GetProcAddress(k->h, "MbKit_DetachAll");
    k->CbCount    = (fn_i0)GetProcAddress(k->h, "MbKit_CbCount");
    k->CbHits     = (fn_i0)GetProcAddress(k->h, "MbKit_CbHits");
    k->CbBlocked  = (fn_i0)GetProcAddress(k->h, "MbKit_CbBlocked");
    k->CbLive     = (fn_i0)GetProcAddress(k->h, "MbKit_CbLive");
    k->Suppressed = (fn_i0)GetProcAddress(k->h, "MbKit_SuppressedCount");
    k->KernelPath = (fn_s0)GetProcAddress(k->h, "MbKit_KernelPath");
    return k->DetachAll && k->CbBlocked;
}

/* ---- 业务 DLL 薄壳 ---- */
typedef int  (__stdcall *fn_attach)(void*, int, int, const wchar_t*);
typedef int  (__stdcall *fn_ip)(int);
typedef int  (__stdcall *fn_i0b)(void);
typedef unsigned (__stdcall *fn_u0)(void);
typedef const char* (__stdcall *fn_s0b)(void);

typedef struct {
    HMODULE   h;
    fn_attach Attach;
    fn_ip     Pump, Detach;
    fn_i0b    IsDone, TitleHits, TitleLen;
    fn_u0     ThunkAddr;
    fn_s0b    Err;
} SHIM;

static int shim_load(SHIM* s, const char* dll)
{
    memset(s, 0, sizeof(*s));
    s->h = LoadLibraryA(dll);
    if (!s->h) { FAILX("加载业务模块失败"); return 0; }
    s->Attach    = (fn_attach)GetProcAddress(s->h, "Nk_Attach");
    s->Pump      = (fn_ip)GetProcAddress(s->h, "Nk_Pump");
    s->Detach    = (fn_ip)GetProcAddress(s->h, "Nk_Detach");
    s->IsDone    = (fn_i0b)GetProcAddress(s->h, "Nk_IsDone");
    s->TitleHits = (fn_i0b)GetProcAddress(s->h, "Nk_TitleHits");
    s->TitleLen  = (fn_i0b)GetProcAddress(s->h, "Nk_TitleLen");
    s->ThunkAddr = (fn_u0)GetProcAddress(s->h, "Nk_ThunkAddr");
    s->Err       = (fn_s0b)GetProcAddress(s->h, "Nk_Err");
    return s->Attach && s->Pump;
}
static void shim_unload(SHIM* s) { if (s->h) { FreeLibrary(s->h); s->h = NULL; } }

static const wchar_t* HTML =
    L"<html><head><meta charset=\"utf-8\"><title>BOOT</title>"
    L"<script>var n=0;setInterval(function(){document.title='T'+(++n);},80);</script>"
    L"</head><body><h1>MbKit 端到端验证</h1></body></html>";

static int wait_done(SHIM* s, int ms)
{
    int waited = 0;
    while (waited < ms) {
        if (s->IsDone && s->IsDone() == 1) return 1;
        s->Pump(50);
        waited += 50;
    }
    return s->IsDone && s->IsDone() == 1;
}

int main(int argc, char** argv)
{
    int      rounds = (argc > 1) ? atoi(argv[1]) : 3;
    const char* logPath = (argc > 2) ? argv[2] : "nk.log";
    KIT      K;
    HWND     hwnd;
    int      i, blocked0;

    g_log = fopen(logPath, "wb");
    SetConsoleOutputCP(65001);

    LOG("========== MbKit 转发+拦截 端到端验证 ==========");

    if (!kit_load(&K)) { LOG("!! 加载 MbKit.dll 失败"); return 2; }
    LOG("内核路径 : %s", K.KernelPath ? K.KernelPath() : "(?)");

    /* 一个普通的宿主窗口（离屏即可 —— 内核自己负责渲染） */
    hwnd = CreateWindowExA(0, "STATIC", "nk", WS_POPUP, 0, 0, 800, 600, NULL, NULL, NULL, NULL);
    if (!hwnd) hwnd = (HWND)0x00010010;   /* 极端环境下也不致中断 */

    /* ---------- 第 0 步：先答「业务按名字调用 wke* 是否真的通」 ---------- */
    LOG("");
    LOG("-- step 1: 业务模块经转发桩建窗 + 载入（首调触发内核懒加载） --");
    {
        SHIM s;
        if (!shim_load(&s, "nk_v1.dll")) return 2;
        if (s.Attach((void*)hwnd, 800, 600, HTML) != 0) {
            FAILX("Attach 失败");
            LOG("   业务报告的原因: %s", s.Err ? s.Err() : "(空)");
        } else {
            PASS("Attach 成功（转发桩 + 懒加载工作）");
        }
        if (wait_done(&s, 8000)) PASS("页面载入完成");
        else                     FAILX("页面未在 8s 内载入完成");

        /* 页面里的 setInterval 会持续改标题 —— 证明拦截层把回调正常转发了 */
        s.Pump(600);
        {
            int hits = s.TitleHits ? s.TitleHits() : -1;
            LOG("   title 回调命中 = %d   live 登记 = %d", hits, K.CbLive ? K.CbLive() : -1);
            if (hits > 0) PASS("回调经 MbKit wrapper 转发成功（拦截层未误杀）");
            else          FAILX("回调一次都没到业务侧 —— 存活判据可能误判了 thunk");
        }
        if (s.Detach) s.Detach(0);
        shim_unload(&s);
    }

    /* ---------- 热重载多轮 ---------- */
    LOG("");
    LOG("-- step 2: 热重载 %d 轮（v1/v2 交替，image-base 不同） --", rounds);
    for (i = 0; i < rounds; i++) {
        SHIM s;
        const char* dll = (i % 2 == 0) ? "nk_v2.dll" : "nk_v1.dll";

        K.DetachAll();                       /* 热重载契约：卸载前先解绑 */
        if (!shim_load(&s, dll)) return 2;

        if (s.Attach((void*)hwnd, 800, 600, HTML) != 0) { FAILX("Attach 失败"); shim_unload(&s); continue; }
        if (!wait_done(&s, 8000)) { FAILX("载入超时"); shim_unload(&s); continue; }
        s.Pump(400);
        LOG("   轮 %d [%s] 载入完成, title 命中=%d, 内核路径=%s",
            i + 1, dll, s.TitleHits ? s.TitleHits() : -1,
            K.KernelPath ? K.KernelPath() : "?");
        if (s.TitleHits && s.TitleHits() > 0) PASS("本轮回调可用");
        else                                  FAILX("本轮回调未到达");

        if (s.Detach) s.Detach(1);
        shim_unload(&s);
        K.DetachAll();
        Sleep(120);
    }

    /* ---------- 故障注入：忘记 Detach 直接卸载，然后继续让内核触发事件 ---------- */
    LOG("");
    LOG("-- step 3: 故障注入 —— 业务「忘记解绑」就卸载，之后仍持续触发回调 --");
    blocked0 = K.CbBlocked ? K.CbBlocked() : -1;
    {
        SHIM s;
        if (!shim_load(&s, "nk_v1.dll")) return 2;
        if (s.Attach((void*)hwnd, 800, 600, HTML) != 0) { FAILX("Attach 失败"); shim_unload(&s); return g_fail; }
        if (!wait_done(&s, 8000)) FAILX("注入轮载入超时");
        s.Pump(400);
        LOG("   卸载前：回调命中=%d  挡下=%d", s.TitleHits ? s.TitleHits() : -1,
            K.CbBlocked ? K.CbBlocked() : -1);

        /* ★ 故意不调 K.DetachAll()，也不销毁 view —— 直接卸模块 */
        LOG("   FreeLibrary(业务模块)，**不 DetachAll**");
        shim_unload(&s);

        /* 继续泵消息：页面里的 setInterval 会不停改标题，内核必然回调已失效的 thunk */
        { MSG m; DWORD end = GetTickCount() + 2500;
          while ((int)(GetTickCount() - end) < 0) {
              while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
              Sleep(20);
          } }
        PASS("卸载后持续触发 2.5s：进程存活，未踩空指针");

        {
            int blocked1 = K.CbBlocked ? K.CbBlocked() : -1;
            LOG("   卸载后：挡下 = %d（增量 %d）", blocked1, blocked1 - blocked0);
            if (blocked1 > blocked0) PASS("野回调被实际挡下 —— 保护生效，且这是可数的证据");
            else                     LOG("   (本轮内核未再触发该 view 的回调，未观测到挡下)");
        }
    }

    /* ---------- 收尾：用新业务模块继续工作，证明内核未被污染 ---------- */
    LOG("");
    LOG("-- step 4: 清残留后换新模块继续工作（证明内核未被上一轮污染） --");
    K.DetachAll();
    {
        SHIM s;
        if (!shim_load(&s, "nk_v2.dll")) return 2;
        if (s.Attach((void*)hwnd, 800, 600, HTML) != 0) FAILX("Attach 失败（内核可能已被污染）");
        else if (wait_done(&s, 8000)) {
            s.Pump(400);
            LOG("   载入完成，title 命中=%d", s.TitleHits ? s.TitleHits() : -1);
            PASS("新一轮照常工作");
        } else FAILX("新一轮载入失败");
        if (s.Detach) s.Detach(1);
        shim_unload(&s);
        K.DetachAll();
    }

    LOG("");
    LOG("汇总：登记中的回调=%d  累计命中=%d  挡下=%d  抑制计数=%d",
        K.CbCount ? K.CbCount() : -1, K.CbHits ? K.CbHits() : -1,
        K.CbBlocked ? K.CbBlocked() : -1, K.Suppressed ? K.Suppressed() : -1);
    LOG("SUMMARY: %s  (fail=%d)", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);

    if (g_log) fclose(g_log);
    return g_fail;
}
