/* min_wv2c.c —— UI_E_WRONG_THREAD 对照矩阵
 * 用法: min_wv2c.exe <mode>
 *   0 = ctl 在 main 里建（基线）
 *   1 = ctl 在 env 的 Invoke 回调里建（官方样例写法）
 *   2 = 用 OleInitialize 而非 CoInitializeEx
 *   3 = 先 ShowWindow + 跑真消息循环再建 ctl
 *   4 = parent 用 HWND_MESSAGE
 *   5 = 换一个全新的 userDataFolder（排除 profile 锁）
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <tlhelp32.h>
#include <stdio.h>

typedef HRESULT (STDAPICALLTYPE *PFN4)(PCWSTR, PCWSTR, void*, void*);

static LONG g_ref = 1;
static const GUID GUID_ENVH =
    {0x4e8a3389,0xc9d8,0x4bd2,{0xb6,0xb5,0x12,0x4f,0xee,0x6c,0xc1,0x4d}};
static const GUID GUID_CTLH =
    {0x6c4819f3,0xc9b7,0x4260,{0x81,0x27,0xc9,0xf5,0xbd,0xe7,0xf6,0x8c}};
static const GUID GUID_IUnknown =
    {0x00000000,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static int g_mode = 0;
static HWND g_hwnd = NULL;
#define WM_CTLNOW (WM_APP + 1)

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
static HWND  g_parent = NULL;
static DWORD g_t0 = 0;

static BOOL CALLBACK enum_tl(HWND h, LPARAM lp)
{
    DWORD pid = 0, tid;
    tid = GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) {
        char cls[128] = {0}, tit[128] = {0};
        GetClassNameA(h, cls, sizeof cls);
        GetWindowTextA(h, tit, sizeof tit);
        printf("    hwnd=%p tid=%lu class=%s title=%s%s\n", (void*)h,
               (unsigned long)tid, cls, tit,
               tid == GetCurrentThreadId() ? "" : "   <== NOT our thread");
    }
    return TRUE;
}
static BOOL CALLBACK enum_child(HWND h, LPARAM lp)
{
    DWORD pid = 0, tid;
    tid = GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) {
        char cls[128] = {0};
        GetClassNameA(h, cls, sizeof cls);
        printf("    child=%p tid=%lu class=%s%s\n", (void*)h,
               (unsigned long)tid, cls,
               tid == GetCurrentThreadId() ? "" : "   <== NOT our thread");
    }
    return TRUE;
}
static void dump_windows(const char* tag)
{
    printf("  [win] %s (our tid=%lu)\n", tag, (unsigned long)GetCurrentThreadId());
    EnumWindows(enum_tl, 0);
}

static void find_module(void* p, const char* tag)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                           GetCurrentProcessId());
    MODULEENTRY32W me;
    if (snap == INVALID_HANDLE_VALUE) { printf("  [mod] %s: snapshot failed\n", tag); return; }
    me.dwSize = sizeof me;
    if (Module32FirstW(snap, &me)) {
        do {
            if ((BYTE*)p >= me.modBaseAddr && (BYTE*)p < me.modBaseAddr + me.modBaseSize) {
                printf("  [mod] %s %p lives in %ls  base=%p size=%lu\n", tag, p,
                       me.szExePath, (void*)me.modBaseAddr, (unsigned long)me.modBaseSize);
                CloseHandle(snap); return;
            }
        } while (Module32NextW(snap, &me));
    }
    printf("  [mod] %s %p: not found in this process\n", tag, p);
    CloseHandle(snap);
}

static void print_thread_start(DWORD tid);

typedef LONG (WINAPI *PFN_NtQIT)(HANDLE, ULONG, PVOID, ULONG, PULONG);

static void print_thread_start(DWORD tid)
{
    HANDLE th, snap;
    PVOID start = NULL;
    ULONG got = 0;
    PFN_NtQIT q;
    MODULEENTRY32W me;
    int found = 0;

    th = OpenThread(0x0040 | 0x0010, FALSE, tid);   /* QUERY_INFORMATION | LIMITED */
    q  = (PFN_NtQIT)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread");
    if (th && q) q(th, 9, &start, sizeof start, &got);   /* ThreadQuerySetWin32StartAddress */
    if (th) {
        typedef HRESULT (WINAPI *PFN_GTD)(HANDLE, PWSTR*);
        PFN_GTD gtd = (PFN_GTD)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
        if (gtd) {
            PWSTR desc = NULL;
            if (SUCCEEDED(gtd(th, &desc)) && desc) {
                printf("        name=\"%ls\"\n", desc);
                LocalFree(desc);
            } else printf("        name=(none)\n");
        }
        CloseHandle(th);
    }
    if (!start) { printf("        start=(unavailable)\n"); return; }

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                    GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        me.dwSize = sizeof me;
        if (Module32FirstW(snap, &me)) do {
            if ((BYTE*)start >= me.modBaseAddr &&
                (BYTE*)start <  me.modBaseAddr + me.modBaseSize) {
                char nm[260];
                WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, nm, sizeof nm, NULL, NULL);
                printf("        start=%p in %s +0x%lX\n", start, nm,
                       (unsigned long)((BYTE*)start - me.modBaseAddr));
                found = 1; break;
            }
        } while (Module32NextW(snap, &me));
        CloseHandle(snap);
    }
    if (!found) printf("        start=%p (not in any module)\n", start);
}

#define MAXS 600
static char  s_mods[MAXS][260]; static int s_nmod = 0;
static DWORD s_tids[512];        static int s_ntid = 0;

static void snap_diff(const char* tag)
{
    HANDLE snap;
    MODULEENTRY32W me;
    THREADENTRY32 te;
    int i;
    char nm[260];
    printf("  [diff] %s:\n", tag);
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
                                    GetCurrentProcessId());
    if (snap != INVALID_HANDLE_VALUE) {
        me.dwSize = sizeof me;
        if (Module32FirstW(snap, &me)) do {
            WideCharToMultiByte(CP_ACP, 0, me.szModule, -1, nm, sizeof nm, NULL, NULL);
            for (i = 0; i < s_nmod; i++) if (!strcmp(nm, s_mods[i])) break;
            if (i == s_nmod && s_nmod < MAXS) {
                strncpy(s_mods[s_nmod++], nm, 259);
                printf("      +module %s  base=%p\n", nm, (void*)me.modBaseAddr);
            }
        } while (Module32NextW(snap, &me));
        CloseHandle(snap);
    }
    snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        te.dwSize = sizeof te;
        if (Thread32First(snap, &te)) do {
            if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
            for (i = 0; i < s_ntid; i++) if (s_tids[i] == te.th32ThreadID) break;
            if (i == s_ntid && s_ntid < 512) {
                s_tids[s_ntid++] = te.th32ThreadID;
                printf("      +tid %lu%s\n", (unsigned long)te.th32ThreadID,
                       te.th32ThreadID == GetCurrentThreadId() ? " (self)" : "");
                if (te.th32ThreadID != GetCurrentThreadId()) print_thread_start(te.th32ThreadID);
            }
        } while (Thread32Next(snap, &te));
        CloseHandle(snap);
    }
}

static void* vtbl_env[4];
static void* vtbl_ctl[4];
struct H { void** vtbl; };
static struct H hEnv, hCtl;

static void dump_err(const char* tag)
{
    IErrorInfo* ei = NULL;
    if (SUCCEEDED(GetErrorInfo(0, &ei)) && ei) {
        BSTR b = NULL;
        if (SUCCEEDED(ei->lpVtbl->GetDescription(ei, &b)) && b) {
            printf("    %s IErrorInfo: %ls\n", tag, b);
            SysFreeString(b);
        } else printf("    %s IErrorInfo: (no description)\n", tag);
        ei->lpVtbl->Release(ei);
    } else printf("    %s IErrorInfo: (none)\n", tag);
}

static HRESULT create_controller(void* env, HWND parent)
{
    void** vt = (void**)*(void***)env;
    HRESULT (STDMETHODCALLTYPE *pCtl)(void*, HWND, void*) =
        (HRESULT (STDMETHODCALLTYPE *)(void*, HWND, void*))vt[3];
    HRESULT hr;
    printf("    create controller parent=%p tid=%lu\n",
           (void*)parent, (unsigned long)GetCurrentThreadId()); fflush(stdout);
    hr = pCtl(env, parent, &hCtl);
    printf("    sync hr=0x%08lX%s\n", (unsigned long)hr,
           hr == (HRESULT)0x802A000C ? "  << UI_E_WRONG_THREAD" : "");
    if (FAILED(hr)) dump_err("ctl");
    return hr;
}

static HRESULT CALLBACK env_inv(void* s, HRESULT hr, void* env)
{
    (void)s;
    printf("  [env]  Invoke hr=0x%08lX env=%p tid=%lu at +%lu ms\n",
           (unsigned long)hr, env, (unsigned long)GetCurrentThreadId(),
           (unsigned long)(GetTickCount() - g_t0));
    if (SUCCEEDED(hr)) { g_env = env; InterlockedIncrement(&g_ref); }
    InterlockedExchange(&g_envReady, 1);
    if ((g_mode == 1 || g_mode == 8) && g_env) create_controller(g_env, g_parent);
    if (g_mode == 10 && g_env) {
        printf("  [env] posting WM_CTLNOW to host hwnd=%p\n", (void*)g_hwnd);
        PostMessageW(g_hwnd, WM_CTLNOW, 0, 0);
    }
    if (g_mode == 9 && g_env) create_controller(g_env, HWND_MESSAGE);
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

static LRESULT CALLBACK host_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CTLNOW) {
        printf("  [msg] WM_CTLNOW on tid=%lu (post-p4)\n",
               (unsigned long)GetCurrentThreadId());
        create_controller(g_env, g_parent);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HWND make_win(void)
{
    WNDCLASSW wc; HWND h;
    ZeroMemory(&wc, sizeof wc);
    wc.lpfnWndProc   = host_wndproc;
    wc.hInstance      = GetModuleHandleW(NULL);
    wc.hbrBackground  = (HBRUSH)(UINT_PTR)(COLOR_WINDOW + 1);
    wc.lpszClassName  = L"minwv2c_host";
    RegisterClassW(&wc);
    h = CreateWindowExW(0, L"minwv2c_host", L"minwv2c", WS_OVERLAPPEDWINDOW,
                        0, 0, 640, 480, NULL, NULL, wc.hInstance, NULL);
    return h;
}

static void pump(int ms)
{
    DWORD t0 = GetTickCount();
    while ((int)(GetTickCount() - t0) < ms) {
        MSG m;
        while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
        Sleep(5);
    }
}

int main(int argc, char** argv)
{
    HMODULE l;
    PFN4 p4;
    HRESULT hr;
    wchar_t dir[MAX_PATH];
    HWND hwnd;
    APTTYPE apt = APTTYPE_NA;
    APTTYPEQUALIFIER aptq = APTTYPEQUALIFIER_IMPLICIT_MTA;

    g_mode = argc > 1 ? atoi(argv[1]) : 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== mode %d ===\n", g_mode);
    printf("main tid = %lu\n", (unsigned long)GetCurrentThreadId());

    {   char nv[8] = {0};
        int wantDpi = 1;
        if (GetEnvironmentVariableA("MINWV_DPI", nv, sizeof nv) > 0 && nv[0] == '0') wantDpi = 0;
        if (wantDpi) {
            BOOL ok = FALSE;
            HMODULE u32 = GetModuleHandleW(L"user32.dll");
            typedef BOOL (WINAPI *PFN_DPICtx)(HANDLE);
            if (u32) {
                PFN_DPICtx f = (PFN_DPICtx)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
                if (f) ok = f((HANDLE)(LONG_PTR)-4);   /* PER_MONITOR_AWARE_V2 */
            }
            if (!ok) ok = SetProcessDPIAware();
            printf("dpi awareness set = %d\n", ok);
        } else printf("dpi awareness SKIPPED (MINWV_DPI=0)\n");
    }

    if (g_mode == 2) {
        hr = OleInitialize(NULL);
        printf("OleInitialize hr=0x%08lX\n", (unsigned long)hr);
    } else {
        hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        printf("CoInitializeEx STA hr=0x%08lX\n", (unsigned long)hr);
    }
    if (SUCCEEDED(CoGetApartmentType(&apt, &aptq)))
        printf("apartment type=%d qualifier=%d\n", (int)apt, (int)aptq);

    hwnd = make_win();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    printf("hwnd=%p tid=%lu\n", (void*)hwnd, (unsigned long)GetWindowThreadProcessId(hwnd, NULL));

    g_parent = (g_mode == 4) ? HWND_MESSAGE : hwnd;
    g_hwnd   = hwnd;

    l = LoadLibraryW(L"WebView2Loader.dll");
    if (!l) { printf("!! loader err=%lu\n", GetLastError()); return 1; }
    p4 = (PFN4)GetProcAddress(l, "CreateCoreWebView2EnvironmentWithOptions");
    GetTempPathW(MAX_PATH, dir);
    if (g_mode == 8) {                          /* 全新 profile，逼出「异步回调」 */
        wchar_t tmp[64];
        wsprintfW(tmp, L"wv2minc8_%lu", (unsigned long)GetTickCount());
        wcscat(dir, tmp);
    } else if (g_mode == 11) {                  /* 长路径（非 8.3 短名）profile */
        wchar_t base[MAX_PATH], tmp[64];
        GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
        wsprintfW(tmp, L"\\wv2minc11_%lu", (unsigned long)GetTickCount());
        wcscat(base, tmp);
        wcscpy(dir, base);
    } else if (g_mode == 5) wcscat(dir, L"wv2minc5");
    else                   wcscat(dir, L"wv2minc");
    {   wchar_t envp[MAX_PATH];
        if (GetEnvironmentVariableW(L"MINWV2_PROFILE", envp, MAX_PATH) > 0) wcscpy(dir, envp);
    }
    CreateDirectoryW(dir, NULL);
    printf("    userDataFolder=%ls\n", dir);

    vtbl_env[0] = (void*)h_qi; vtbl_env[1] = (void*)h_add; vtbl_env[2] = (void*)h_rel;
    vtbl_env[3] = (void*)env_inv;   /* IUnknown(0..2) + Invoke(3) */
    vtbl_ctl[0] = (void*)h_qi; vtbl_ctl[1] = (void*)h_add; vtbl_ctl[2] = (void*)h_rel;
    vtbl_ctl[3] = (void*)ctl_inv;
    hEnv.vtbl = vtbl_env; hCtl.vtbl = vtbl_ctl;

    g_t0 = GetTickCount();
    printf("[A] create environment\n"); fflush(stdout);
    snap_diff("baseline before env");
    hr = p4(NULL, dir, NULL, &hEnv);
    printf("    sync hr=0x%08lX  (p4 took %lu ms)\n", (unsigned long)hr, (unsigned long)(GetTickCount()-g_t0));
    if (FAILED(hr)) dump_err("env");
    if (g_mode == 6) {
        int k; for (k = 0; k < 600 && !g_envReady; k++) pump(10);   /* 一就绪立刻建 */
    } else {
        pump(4000);
    }
    printf("    envReady=%ld env=%p\n", g_envReady, g_env);
    snap_diff("after env creation");
    if (!g_env) { printf("!! no env\n"); return 1; }
    find_module(g_env, "env object");
    if (g_mode == 7) {
        dump_windows("after env");
        EnumThreadWindows(GetCurrentThreadId(), enum_child, 0);
        printf("  [ctx] CoGetObjectContext: ");
        {   IUnknown* ctx = NULL;
            static const GUID IID_CTXCB =
                {0x000001DA,0x0000,0x0000,{0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
            if (SUCCEEDED(CoGetObjectContext(&IID_CTXCB, (void**)&ctx)) && ctx) {
                printf("IContextCallback=%p\n", (void*)ctx);
                ctx->lpVtbl->Release(ctx);
            } else printf("(none / error 0x%08lX)\n", (unsigned long)GetLastError());
        }
        printf("  [B] create controller now (outside callback)\n");
        create_controller(g_env, g_parent);
        pump(8000);
        printf("    ctlReady=%ld ctl=%p\n", g_ctlReady, g_ctl);
        dump_windows("after ctl attempt");
        printf("=> mode %d %s\n", g_mode, g_ctl ? "OK" : "FAIL");
        return g_ctl ? 0 : 2;
    }

    if (g_mode == 3) {
        printf("[A2] run real message loop 1500ms\n"); fflush(stdout);
        {   DWORD t0 = GetTickCount(); MSG m;
            while ((int)(GetTickCount() - t0) < 1500) {
                if (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
                else Sleep(10);
            } }
    }

    if (g_mode != 1 && g_mode != 8 && g_mode != 9 && g_mode != 10) {
        printf("[B] create controller\n"); fflush(stdout);
        create_controller(g_env, g_parent);
    }
    if (g_mode == 1 || g_mode == 8 || g_mode == 9 || g_mode == 10) {
        printf("[B] controller requested from inside env callback / posted msg; waiting...\n");
        {   int k; for (k = 0; k < 300 && !g_ctl; k++) pump(100); }
    } else {
        pump(8000);
    }
    printf("    ctlReady=%ld ctl=%p\n", g_ctlReady, g_ctl);
    snap_diff("after controller attempt");
    if (!g_ctl && g_mode != 1 && g_mode != 8 && g_mode != 9 && g_mode != 10) {
        printf("[C] retry with HWND_MESSAGE\n"); fflush(stdout);
        create_controller(g_env, HWND_MESSAGE);
        pump(8000);
        printf("    ctlReady=%ld ctl=%p\n", g_ctlReady, g_ctl);
    }
    printf("=> mode %d %s\n", g_mode, g_ctl ? "OK" : "FAIL");
    return g_ctl ? 0 : 2;
}
