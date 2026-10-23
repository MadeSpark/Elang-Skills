/* ============================================================================
 * diag_wv.c —— edgewebview 内核冒烟探针（只读观测，不改任何生产代码）
 *
 * 走一遍真实用例：LoadLibrary(MbKit.dll) → 建离屏窗口 → MbKit_Create →
 * MbKit_LoadHtml → 泵消息轮询 IsLoaded → 打印 MbKit_WebViewDiag / MbKit_Diag。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef int          (__stdcall *p_i0)(void);
typedef int          (__stdcall *p_i1)(void*);
typedef void*        (__stdcall *p_create)(void*, int, int, int, int, void*, void*);
typedef int          (__stdcall *p_resize)(void*, int, int);
typedef int          (__stdcall *p_load)(void*, const wchar_t*);
typedef int          (__stdcall *p_pump)(int);
typedef const char*  (__stdcall *p_s0)(void);
typedef void*        (__stdcall *p_self)(void);

static HWND make_win(void)
{
    WNDCLASSW wc; HWND h;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance      = GetModuleHandleW(NULL);
    wc.hbrBackground  = (HBRUSH)(UINT_PTR)(COLOR_WINDOW + 1);
    wc.lpszClassName  = L"diag_wv_host";
    RegisterClassW(&wc);
    h = CreateWindowExW(0, L"diag_wv_host", L"diag", WS_OVERLAPPEDWINDOW,
                        0, 0, 800, 600, NULL, NULL, wc.hInstance, NULL);
    return h;
}

int main(void)
{
    HMODULE h;
    HWND    hwnd;
    void*   view;
    int     t, loaded = -1;
    p_i0    hasEnv, kmode, init;
    p_s0    wvdiag, diag, ver;
    p_create create;
    p_i1    isLoaded;
    p_resize resize;
    p_load  loadHtml;
    p_pump  pump;
    p_self  selfMod;

    printf("main tid      = %lu\n", (unsigned long)GetCurrentThreadId());
    h = LoadLibraryA("MbKit.dll");
    if (!h) { printf("!! LoadLibrary(MbKit.dll) failed err=%lu\n", GetLastError()); return 1; }

    hasEnv   = (p_i0)GetProcAddress(h, "MbKit_HasWebViewEnv");
    kmode    = (p_i0)GetProcAddress(h, "MbKit_KernelMode");
    wvdiag   = (p_s0)GetProcAddress(h, "MbKit_WebViewDiag");
    diag     = (p_s0)GetProcAddress(h, "MbKit_Diag");
    ver      = (p_s0)GetProcAddress(h, "MbKit_Version");
    init     = (p_i0)GetProcAddress(h, "MbKit_EnsureKernelInit");
    create   = (p_create)GetProcAddress(h, "MbKit_Create");
    isLoaded = (p_i1)GetProcAddress(h, "MbKit_IsLoaded");
    loadHtml = (p_load)GetProcAddress(h, "MbKit_LoadHtml");
    pump     = (p_pump)GetProcAddress(h, "MbKit_Pump");
    resize   = (p_resize)GetProcAddress(h, "MbKit_Resize");
    selfMod  = (p_self)GetProcAddress(h, "MbKit_SelfModule");

    printf("HasWebViewEnv = %s\n", hasEnv ? (hasEnv() ? "1" : "0") : "(missing)");
    printf("KernelMode    = %s\n", kmode ? (kmode() == 2 ? "2(edge)" :
                                            kmode() == 1 ? "1(node)" : "?") : "(missing)");
    printf("Version       = %s\n", ver ? ver() : "(missing)");
    printf("SelfModule    = 0x%08X\n", selfMod ? (unsigned)(UINT_PTR)selfMod() : 0);

    if (init) printf("EnsureKernelInit = %d\n", init());
    printf("---- after init ----\n");
    printf("KernelMode    = %s\n", kmode ? (kmode() == 2 ? "2(edge)" :
                                            kmode() == 1 ? "1(node)" : "?") : "(missing)");

    hwnd = make_win();
    if (!hwnd) { printf("!! CreateWindow failed\n"); return 1; }
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    if (!create) { printf("!! MbKit_Create missing\n"); return 1; }
    view = create(hwnd, 0, 0, 800, 600, NULL, NULL);
    printf("view = %p   hwnd = %p  hwnd tid = %lu\n", view, (void*)hwnd,
           (unsigned long)GetWindowThreadProcessId(hwnd, NULL));
    if (!view) {
        printf("!! MbKit_Create returned NULL\n");
        if (wvdiag) printf("WebViewDiag: %s\n", wvdiag());
        if (diag)   printf("Diag:\n%s\n", diag());
        return 1;
    }
    {   void (__stdcall *p_set)(void*, void*) =
            (void (__stdcall *)(void*, void*))GetProcAddress(h, "wkeSetHandle");
        if (p_set) { p_set(view, hwnd); printf("wkeSetHandle done\n"); } }
    if (resize)    printf("MbKit_Resize -> %d\n", resize(view, 800, 600));

    {   wchar_t url[2048];
        if (GetEnvironmentVariableW(L"DIAG_URL", url, 2048) > 0) {
            p_load p_url = (p_load)GetProcAddress(h, "MbKit_LoadUrl");
            printf("LoadUrl -> %d  (%ls)\n", p_url ? p_url(view, url) : -1, url);
        } else if (loadHtml) printf("LoadHtml -> %d\n",
            loadHtml(view, L"<html><head><title>diagWV</title></head><body>hello edge</body></html>"));
    }

    for (t = 0; t < 400; t++) {
        if (pump) pump(25); else Sleep(25);
        if (isLoaded && isLoaded(view) == 1) { loaded = 1; break; }
    }
    printf("loaded = %d  (polls=%d)\n", loaded, t);
    printf("KernelMode(final) = %s\n", kmode ? (kmode() == 2 ? "2(edge)" :
                                 kmode() == 1 ? "1(node)" : "?") : "(missing)");
    printf("Version(final)    = %s\n", ver ? ver() : "(missing)");
    printf("BootState         = %d\n", (int)(UINT_PTR)GetProcAddress(h, "MbKit_BootState") ?
           ((p_i0)GetProcAddress(h, "MbKit_BootState"))() : -1);

    if (wvdiag) printf("WebViewDiag: %s\n", wvdiag());
    if (diag)   printf("---- MbKit_Diag ----\n%s\n", diag());
    return loaded == 1 ? 0 : 2;
}
