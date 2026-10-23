/* min_wv2.c —— 最小 WebView2 环境创建复现（隔离 MbKit 集成代码） */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

typedef struct H H;
typedef HRESULT (STDAPICALLTYPE *PFN4)(
    PCWSTR browserFolder, PCWSTR userDataFolder,
    void* options, void* handler);
typedef HRESULT (STDAPICALLTYPE *PFN1)(void* handler);
typedef HRESULT (STDMETHODCALLTYPE *PFN_QI)(void*, const GUID*, void**);
typedef ULONG    (STDMETHODCALLTYPE *PFN_REL)(void*);
typedef HRESULT  (STDMETHODCALLTYPE *PFN_INVOKE)(void*, HRESULT, void*);

/* 手写 4 接口 COM handler：QI/AddRef/Release/Invoke */
static LONG g_ref = 1;
static HRESULT CALLBACK h_qi(void* self, const GUID* riid, void** ppv)
{
    static const GUID g1 = {0x4e8a3389,0xc9d8,0x4bd2,{0xb6,0xb5,0x12,0x4f,0xee,0x6c,0xc1,0x4d}};
    static const GUID gI = {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
    (void)self;
    if (!ppv) return E_POINTER;
    *ppv = NULL;
    if (riid && !memcmp(riid, &g1, sizeof(GUID))) { *ppv = self; InterlockedIncrement(&g_ref); return S_OK; }
    if (riid && !memcmp(riid, &gI, sizeof(GUID))) { *ppv = self; InterlockedIncrement(&g_ref); return S_OK; }
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE h_add(void* s) { (void)s; return (ULONG)InterlockedIncrement(&g_ref); }
static ULONG STDMETHODCALLTYPE h_rel(void* s) { (void)s; return (ULONG)InterlockedDecrement(&g_ref); }
static HRESULT CALLBACK h_inv(void* s, HRESULT hr, void* env)
{
    (void)s; (void)env;
    printf("  Invoke hr=0x%08lX  env=%p\n", (unsigned long)hr, env);
    if (GetConsoleWindow()) { fflush(stdout); }
    return S_OK;
}
struct H { void** vtbl; };
static void* vtbl[4] = { (void*)h_qi, (void*)h_add, (void*)h_rel, (void*)h_inv };

int main(void)
{
    HMODULE l;
    PFN4 p4;
    PFN1 p1;
    HRESULT hr;
    wchar_t dir[MAX_PATH];
    struct H h; h.vtbl = vtbl;

    l = LoadLibraryW(L"WebView2Loader.dll");
    if (!l) { printf("!! LoadLibrary(WebView2Loader.dll) err=%lu\n", GetLastError()); return 1; }

    p4 = (PFN4)GetProcAddress(l, "CreateCoreWebView2EnvironmentWithOptions");
    p1 = (PFN1)GetProcAddress(l, "CreateCoreWebView2Environment");
    printf("p4=%p  p1=%p\n", (void*)p4, (void*)p1);

    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"wv2min");
    CreateDirectoryW(dir, NULL);
    printf("userDataFolder = %ls\n", dir);

    printf("[0] CoInitializeEx(STA) ... ");
    fflush(stdout);
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("hr=0x%08lX\n", (unsigned long)hr);

    printf("[A] CreateCoreWebView2EnvironmentWithOptions(NULL, dir, NULL, h)\n");
    fflush(stdout);
    hr = p4 ? p4(NULL, dir, NULL, &h) : E_FAIL;
    printf("    hr=0x%08lX\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        int i;
        for (i = 0; i < 300; i++) { Sleep(50); }
    }

    printf("[B] CreateCoreWebView2Environment(h)\n");
    fflush(stdout);
    hr = p1 ? p1(&h) : E_FAIL;
    printf("    hr=0x%08lX\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) { int i; for (i = 0; i < 300; i++) Sleep(50); }
    return 0;
}
