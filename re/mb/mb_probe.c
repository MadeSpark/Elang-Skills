/* mb_probe.c —— miniblink(node.dll) 热重载诊断探针
 * ---------------------------------------------------------------------------
 * 目的：钉死「卸载业务 DLL 后无法重新初始化 miniblink」到底坏在哪一步。
 *
 * 全部用 GetProcAddress 动态获取（不依赖任何头文件），调用约定 cdecl
 * （与易语言 .DLL命令 的 "@wkeXXX" @ 前缀一致）。
 *
 * 阶段：
 *   A  首次 initialize → 建 WebView → 载入 HTML → 判活
 *   B  销毁 WebView（不 finalize）→ 再建 WebView → 判活        ★核心疑问1
 *   C  finalize → 再 initialize → 再建 WebView → 判活          ★核心疑问2
 *   D  FreeLibrary 是否真的卸载了 node.dll（看模块基址/引用）
 *   E  重新 LoadLibrary → 完整重来一遍 → 判活                   ★用户场景
 *
 * 编译（32 位）：见同目录 build_probe.sh
 */
#include <windows.h>
#include <stdio.h>

typedef void*        (*P_wkeCreateWebView)(void);
typedef void         (*P_wkeDestroyWebView)(void*);
typedef void         (*P_wkeInitialize)(void);
typedef void         (*P_wkeFinalize)(void);
typedef BOOL         (*P_wkeIsInitialize)(void);
typedef void         (*P_wkeSetHeadlessEnabled)(void*, BOOL);
typedef void         (*P_wkeLoadHTMLW)(void*, const wchar_t*);
typedef BOOL         (*P_wkeIsLoadingCompleted)(void*);
typedef BOOL         (*P_wkeIsLoading)(void*);
typedef int          (*P_wkeGetVersion)(void);
typedef const char*  (*P_wkeGetVersionString)(void);

typedef struct {
    HMODULE h;
    P_wkeCreateWebView        Create;
    P_wkeDestroyWebView       Destroy;
    P_wkeInitialize           Init;
    P_wkeFinalize             Finalize;
    P_wkeIsInitialize         IsInit;
    P_wkeSetHeadlessEnabled   SetHeadless;
    P_wkeLoadHTMLW            LoadHTMLW;
    P_wkeIsLoadingCompleted   IsDone;
    P_wkeIsLoading            IsLoading;
    P_wkeGetVersion           GetVersion;
    P_wkeGetVersionString     GetVersionString;
} MB;

static int  g_pass = 0, g_fail = 0;
static void ok(const char *t)   { printf("  [OK]   %s\n", t); g_pass++; }
static void bad(const char *t)  { printf("  [FAIL] %s\n", t); g_fail++; }

static void pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG msg;
    while ((int)(GetTickCount() - end) < 0) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(5);
    }
}

static int load_mb(MB *m, const char *path)
{
    ZeroMemory(m, sizeof(*m));
    m->h = LoadLibraryA(path);
    if (!m->h) { printf("  !! LoadLibrary 失败 (%lu): %s\n", GetLastError(), path); return 0; }
    #define GET(member, sym) m->member = (P_##sym)(void*)GetProcAddress(m->h, #sym); \
        if (!m->member) { printf("  !! 缺符号 %s\n", #sym); return 0; }
    GET(Create,          wkeCreateWebView)
    GET(Destroy,         wkeDestroyWebView)
    GET(Init,            wkeInitialize)
    GET(Finalize,        wkeFinalize)
    GET(IsInit,          wkeIsInitialize)
    GET(SetHeadless,     wkeSetHeadlessEnabled)
    GET(LoadHTMLW,       wkeLoadHTMLW)
    GET(IsDone,          wkeIsLoadingCompleted)
    GET(IsLoading,       wkeIsLoading)
    GET(GetVersion,      wkeGetVersion)
    GET(GetVersionString,wkeGetVersionString)
    #undef GET
    return 1;
}

/* 建一个 headless WebView 并载入 HTML，返回是否成功「载入完成」 */
static int try_view(MB *m, const char *tag)
{
    void *v = m->Create();
    if (!v) { printf("  %-22s wkeCreateWebView -> NULL\n", tag); return 0; }
    m->SetHeadless(v, TRUE);
    m->LoadHTMLW(v, L"<html><head><title>MBPROBE</title></head><body><h1>hi</h1></body></html>");
    int done = 0;
    for (int i = 0; i < 400; i++) {
        pump(10);
        if (m->IsDone(v)) { done = 1; break; }
    }
    printf("  %-22s view=%p  loadingCompleted=%d\n", tag, v, done);
    m->Destroy(v);
    return done;
}

int main(int argc, char **argv)
{
    const char *dll = (argc > 1) ? argv[1]
                   : "D:\\DevelopmentFolder\\GS框架开发区\\main\\corn\\node.dll";
    MB m;

    setvbuf(stdout, NULL, _IONBF, 0);   /* 崩溃时不丢输出 */
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("=========== miniblink 热重载诊断 ===========\n");
    printf("node.dll = %s\n\n", dll);
    SetConsoleOutputCP(65001);

    /* ---------- 第一次加载 ---------- */
    printf("[1] 首次 LoadLibrary\n");
    if (!load_mb(&m, dll)) return 1;
    HMODULE h1 = m.h;
    printf("  module base = %p\n", (void*)h1);
    printf("  wkeGetVersion       = %d\n", m.GetVersion());
    /* 注意：wkeGetVersionString 依赖 initialize（内核全局对象），init 前调会崩，故移到后面 */

    printf("\n[2] 阶段 A：首次 initialize + 建 WebView\n");
    m.Init();
    printf("  wkeIsInitialize -> %d\n", m.IsInit() ? 1 : 0);
    { const char *vs = m.GetVersionString(); printf("  wkeGetVersionString = %s\n", vs ? vs : "(null)"); }
    (try_view(&m, "A-1st view") ? (void)ok("阶段A 首次 WebView 可用")
                                : (void)bad("阶段A 首次 WebView 不可用"));

    printf("\n[3] 阶段 B：销毁 WebView 后【不 finalize】再建\n");
    (try_view(&m, "B-2nd view") ? (void)ok("阶段B 未 finalize 也能再建 (内核可复用)")
                                : (void)bad("阶段B 未 finalize 就建不出可用 WebView ★"));

    printf("\n[4] 阶段 C：finalize -> initialize -> 再建 WebView\n");
    m.Finalize();
    printf("  finalize 后 wkeIsInitialize -> %d\n", m.IsInit() ? 1 : 0);
    m.Init();
    printf("  再 initialize 后 wkeIsInitialize -> %d\n", m.IsInit() ? 1 : 0);
    (try_view(&m, "C-3rd view") ? (void)ok("阶段C finalize+init 后可重建")
                                : (void)bad("阶段C finalize+init 后仍不可用 ★"));

    printf("\n[5] 阶段 D：FreeLibrary 能否真正卸载\n");
    /* 再 finalize 一次，尽量干净 */
    m.Finalize();
    BOOL freed = FreeLibrary(m.h);
    HMODULE again = GetModuleHandleA("node.dll");
    printf("  FreeLibrary=%d  GetModuleHandle(node.dll)=%p\n", freed, (void*)again);
    if (again == NULL) ok("node.dll 已从本进程卸载");
    else               bad("node.dll 仍在进程内 —— 引用计数未归零（有别的持有者）");

    printf("\n[6] 阶段 E：重新 LoadLibrary + 完整重来（用户场景）\n");
    if (!load_mb(&m, dll)) return 1;
    printf("  新 module base = %p  (与首次%s)\n", (void*)m.h, (m.h == h1) ? "相同=其实没卸载过" : "不同=真的重载了");
    m.Init();
    (try_view(&m, "E-4th view") ? (void)ok("重载后可用 —— 热重载可行")
                                : (void)bad("重载后不可用 —— 复现你的问题 ★★"));

    printf("\n=========== 汇总: %d 通过 / %d 失败 ===========\n", g_pass, g_fail);
    printf("\n（手动结束：本进程已 init 且未 finalize 时可能残留线程，10 秒后自动退出）\n");
    Sleep(3000);
    return 0;
}
