/* ============================================================================
 * lead_probe2.c —— 判据实验：第二份 node.dll 拷贝的加载停滞是「慢」还是「死」？
 * ----------------------------------------------------------------------------
 * 背景（已实测）：
 *   - mb_reinit raw_two_instances：A(原版,主线程,init+round1+销毁) 之后，
 *     T2 里加载 gen2 拷贝 → isInit 独立、create 不崩（对比 poc_cross 的 exit 139），
 *     但 raw_round 的 6 秒窗口内 loaded=-1 title=""。
 *   - 本探针把等待拉长到 90 秒、每 5 秒打点，用于区分：
 *       慢启动   → 某个时刻 loaded=1，title 有值  ⇒ 「按代际换拷贝」修复路线成立
 *       真停滞   → 90 秒仍 loaded=-1             ⇒ 两内核实例存在资源冲突，此路不通
 * 附加相位 P2：若 P1 停滞，再验证「A 未初始化时拷贝是否秒载」（已有 path_prime
 *   证据，此处仅对照）。
 *
 * 用法：lead_probe2.exe [copyPath] [logfile]
 *   copyPath 默认 gen2/node.dll；工作目录必须是 src/mbkit。
 * 全 ASCII 输出；每步 fflush。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef void* (__cdecl *p_Init)(void);
typedef int   (__cdecl *p_IsInit)(void);
typedef void* (__cdecl *p_Create)(void);
typedef void  (__cdecl *p_Destroy)(void*);
typedef void  (__cdecl *p_SetHandle)(void*, HWND);
typedef void  (__cdecl *p_Resize)(void*, int, int);
typedef int   (__cdecl *p_IsLoaded)(void*);
typedef const wchar_t* (__cdecl *p_Title)(void*);
typedef void  (__cdecl *p_LoadHTML)(void*, const wchar_t*);

typedef struct {
    HMODULE h;
    p_Init Init; p_IsInit IsInit; p_Create Create; p_Destroy Destroy;
    p_SetHandle SetHandle; p_Resize Resize; p_IsLoaded IsLoaded;
    p_Title Title; p_LoadHTML LoadHTML;
} NODE;

static FILE* g_log = NULL;

static void LOG(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = 0;
    fputs(buf, stdout); fputc('\n', stdout); fflush(stdout);
    if (g_log) { fputs(buf, g_log); fputc('\n', g_log); fflush(g_log); }
}

static int pump_ms(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG m;
    int n = 0;
    do {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m); DispatchMessageA(&m); n++;
        }
        Sleep(2);
    } while ((int)(GetTickCount() - end) < 0);
    return n;
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{ return DefWindowProcA(h, m, w, l); }

static HWND make_window(int dx)
{
    char cls[64];
    WNDCLASSA wc;
    static int seq = 0;
    ZeroMemory(&wc, sizeof(wc));
    wsprintfA(cls, "leadp2_%d_%d", GetCurrentProcessId(), seq++);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = cls;
    RegisterClassA(&wc);
    return CreateWindowExA(0, cls, "leadp2", WS_OVERLAPPEDWINDOW,
                           dx, dx, 800, 600, NULL, NULL, wc.hInstance, NULL);
}

static int node_bind(NODE* n, const char* path)
{
    ZeroMemory(n, sizeof(*n));
    n->h = LoadLibraryA(path);
    LOG("   LoadLibrary(%s) = %p err=%lu", path, (void*)n->h,
        n->h ? 0 : GetLastError());
    if (!n->h) return 0;
    #define GB(f, s) n->f = (p_##f)GetProcAddress(n->h, s); \
        if (!n->f) { LOG("!! missing %s", s); return 0; }
    GB(Init, "wkeInitialize") GB(IsInit, "wkeIsInitialize")
    GB(Create, "wkeCreateWebView") GB(Destroy, "wkeDestroyWebView")
    GB(SetHandle, "wkeSetHandle") GB(Resize, "wkeResize")
    GB(IsLoaded, "wkeIsLoadingCompleted") GB(Title, "wkeGetTitleW")
    GB(LoadHTML, "wkeLoadHTMLW")
    #undef GB
    return 1;
}

/* 在当前线程：init(可选) + create + sethandle + load + 轮询 waitSec 秒，每 tickSec 秒打点 */
static int round_poll(NODE* n, int round, int do_init, int waitSec, int tickSec)
{
    void* wv;
    HWND h;
    wchar_t html[256];
    char title[128];
    int loaded = -1, t;
    if (do_init) {
        LOG("   [r%d] wkeInitialize()... isInit(before)=%d", round, n->IsInit());
        n->Init();
        LOG("   [r%d] isInit(after)=%d", round, n->IsInit());
    }
    LOG("   [r%d] wkeCreateWebView...", round);
    wv = n->Create();
    LOG("   [r%d] -> %p", round, wv);
    if (!wv) return -1;
    h = make_window(round * 40);
    n->SetHandle(wv, h);
    n->Resize(wv, 800, 600);
    wsprintfW(html, L"<html><head><title>R-%d</title></head><body>probe2 r%d</body></html>",
              round, round);
    n->LoadHTML(wv, html);
    for (t = 0; t < waitSec; t += tickSec) {
        pump_ms(tickSec * 1000);
        loaded = n->IsLoaded(wv);
        {
            const wchar_t* wt = n->Title(wv);
            int i = 0;
            if (wt) while (i < 120 && wt[i] && i < (int)(sizeof(title) - 1)) {
                title[i] = (char)wt[i]; i++;
            }
            title[i] = 0;
        }
        LOG("   [r%d] t=%3ds loaded=%d title=\"%s\"", round, t + tickSec, loaded, title);
        if (loaded == 1) break;
    }
    LOG("   [r%d] FINAL loaded=%d title=\"%s\"", round, loaded, title);
    return (loaded == 1) ? 0 : 1;
}

static DWORD WINAPI t2_entry(LPVOID pv)
{
    NODE b;
    const char* path = (const char*)pv;
    LOG("   [T2] bind copy: %s", path);
    if (!node_bind(&b, path)) return 2;
    round_poll(&b, 2, 1, 90, 5);   /* 拷贝实例：init + 最长 90 秒，每 5 秒打点 */
    LOG("   [T2] exit");
    return 0;
}

int main(int argc, char** argv)
{
    const char* copyPath = (argc > 1) ? argv[1] : "gen2/node.dll";
    NODE a;
    int rc1, rc2 = 99;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 2) g_log = fopen(argv[2], "wb");

    LOG("======================================================================");
    LOG(" lead_probe2 : is the 2nd-copy load stall SLOW or DEAD?");
    LOG(" copy = %s", copyPath);
    LOG("======================================================================");

    /* P1: A(原版) init + 完整一轮 + 销毁 → B(拷贝) 在 T2 init + 长等待 */
    if (!node_bind(&a, "..\\..\\main\\corn\\node.dll")) return 2;
    rc1 = round_poll(&a, 1, 1, 15, 5);          /* A 应秒过 */
    LOG("");
    LOG("-- P1A done rc=%d ; destroy A view & window --", rc1);
    pump_ms(300);

    {
        DWORD tid = 0;
        HANDLE th;
        LOG("-- starting T2 for copy --");
        /* 直接在本线程跑 T2 逻辑会失去对照意义，仍用真线程 */
        th = CreateThread(NULL, 0, t2_entry, (void*)copyPath, 0, &tid);
        LOG("   T2 tid=%lu", (unsigned long)tid);
        WaitForSingleObject(th, 120000);
        CloseHandle(th);
    }
    LOG("");
    LOG("== done ==");
    if (g_log) fclose(g_log);
    return 0;
}
