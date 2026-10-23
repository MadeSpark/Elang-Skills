/* min_wv2b.c —— 干净复现：env + controller（对照 MbKit 的 UI_E_WRONG_THREAD） */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef HRESULT (STDAPICALLTYPE *PFN4)(PCWSTR, PCWSTR, void*, void*);
typedef HRESULT (STDMETHODCALLTYPE *PFN_INVOKE)(void*, HRESULT, void*);

static LONG g_ref = 1;
static const GUID GUID_ENVH =
    {0x4e8a3389,0xc9d8,0x4bd2,{0xb6,0xb5,0x12,0x4f,0xee,0x6c,0xc1,0x4d}};
static const GUID GUID_CTLH =
    {0x6c4819f3,0xc9b7,0x4260,{0x81,0x27,0xc9,0xf5,0xbd,0xe7,0xf6,0x8c}};
static const GUID GUID_IUnknown =
    {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static HRESULT CALLBACK h_qi(void* self, const GUID* riid, void** ppv)
{
    (void)self;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (riid && (!memcmp(riid, &GUID_ENVH, sizeof(GUID))
              || !memcmp(riid, &GUID_CTLH, sizeof(GUID))
              || !memcmp(riid, &GUID_IUnknown, sizeof(GUID)))) {
        *ppv = self; InterlockedIncrement(&g_ref); return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE h_add(void* s) { (void)s; return (ULONG)InterlockedIncrement(&g_ref); }
static ULONG STDMETHODCALLTYPE h_rel(void* s) { (void)s; return (ULONG)InterlockedDecrement(&g_ref); }

static volatile LONG g_envReady = 0, g_ctlReady = 0;
static void* g_env = NULL;
static void* g_ctl = NULL;

static HRESULT CALLBACK env_inv(void* s, HRESULT hr, void* env)
{
    (void)s;
    printf("  [env]  Invoke hr=0x%08lX env=%p tid=%lu\n",
           (unsigned long)hr, env, (unsigned long)GetCurrentThreadId());
    if (SUCCEEDED(hr)) { g_env = env; InterlockedIncrement(&g_ref); }
    InterlockedExchange(&g_envReady, 1);
    return S_OK;
}
static HRESULT CALLBACK ctl_inv(void* s, HRESULT hr, void* ctl)
{
    (void)s;
    printf("  [ctl]  Invoke hr=0x%08lX ctl=%p tid=%lu\n",
           (unsigned long)hr, ctl, (unsigned long)GetCurrentThreadId());
    if (SUCCEEDED(hr)) { g_ctl = ctl; InterlockedIncrement(&g_ref); }
    InterlockedExchange(&g_ctlReady, 1);
    return S_OK;
}

static void* vtbl_env[5];
static void* vtbl_ctl[4];
struct H { void** vtbl; };
static struct H hEnv, hCtl;

static HWND make_win(void)
{
    WNDCLASSW wc; HWND h;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance      = GetModuleHandleW(NULL);
    wc.hbrBackground  = (HBRUSH)(UINT_PTR)(COLOR_WINDOW + 1);
    wc.lpszClassName  = L"minwv2b_host";
    RegisterClassW(&wc);
    h = CreateWindowExW(0, L"minwv2b_host", L"minwv2b", WS_OVERLAPPEDWINDOW,
                        0, 0, 640, 480, NULL, NULL, wc.hInstance, NULL);
    return h;
}

int main(void)
{
    HMODULE l;
    PFN4 p4;
    HRESULT hr;
    wchar_t dir[MAX_PATH];
    HWND hwnd;
    int i;
    void** envvt;
    APTTYPE apt = APTTYPE_NA;
    APTTYPEQUALIFIER aptq = APTTYPEQUALIFIER_IMPLICIT_MTA;

    printf("main tid = %lu\n", (unsigned long)GetCurrentThreadId());
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("CoInitializeEx STA hr=0x%08lX\n", (unsigned long)hr);
    if (SUCCEEDED(CoGetApartmentType(&apt, &aptq))) {
        printf("apartment type=%d qualifier=%d   (0=STA 1=MTA 2=NA 3=MAINSTA)\n",
               (int)apt, (int)aptq);
    } else printf("CoGetApartmentType failed\n");

    hwnd = make_win();
    printf("hwnd=%p tid=%lu\n", (void*)hwnd, (unsigned long)GetWindowThreadProcessId(hwnd, NULL));

    l = LoadLibraryW(L"WebView2Loader.dll");
    if (!l) { printf("!! loader err=%lu\n", GetLastError()); return 1; }
    p4 = (PFN4)GetProcAddress(l, "CreateCoreWebView2EnvironmentWithOptions");
    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"wv2minb");
    CreateDirectoryW(dir, NULL);

    vtbl_env[0] = (void*)h_qi; vtbl_env[1] = (void*)h_add; vtbl_env[2] = (void*)h_rel;
    vtbl_env[3] = (void*)env_inv; /* CreateCoreWebView2Controller? no: 3=get_BrowserVersionString */
    vtbl_env[4] = (void*)env_inv;
    hEnv.vtbl = vtbl_env;
    hCtl.vtbl = vtbl_ctl;
    vtbl_ctl[0] = (void*)h_qi; vtbl_ctl[1] = (void*)h_add; vtbl_ctl[2] = (void*)h_rel;
    vtbl_ctl[3] = (void*)ctl_inv;

    printf("[A] create environment\n"); fflush(stdout);
    hr = p4(NULL, dir, NULL, &hEnv);
    printf("    sync hr=0x%08lX\n", (unsigned long)hr);
    for (i = 0; i < 400 && !g_envReady; i++) {
        MSG m; Sleep(10);
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
    }
    printf("    envReady=%ld env=%p\n", g_envReady, g_env);
    if (!g_env) { printf("!! no env\n"); return 1; }

    /* vtable[6] = CreateCoreWebView2Controller(HWND, handler) —— 用 func ptr 取 */
    envvt = (void**)*(void***)g_env;
    printf("[B] create controller   (vtable[3]=%p)\n", envvt[3]); fflush(stdout);
    {   HRESULT (STDMETHODCALLTYPE *pCtl)(void*, HWND, void*) =
            (HRESULT (STDMETHODCALLTYPE *)(void*, HWND, void*))envvt[3];
        hr = pCtl(g_env, hwnd, &hCtl);
    }
    printf("    sync hr=0x%08lX\n", (unsigned long)hr);
    for (i = 0; i < 400 && !g_ctlReady; i++) {
        MSG m; Sleep(10);
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
    }
    printf("    ctlReady=%ld ctl=%p\n", g_ctlReady, g_ctl);
    return g_ctl ? 0 : 2;
}
