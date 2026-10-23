#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static long long (__cdecl *arg)(unsigned, unsigned);
static unsigned (__cdecl *count_args)(unsigned);
static unsigned (__cdecl *to_string)(unsigned, long long);
static long long (__cdecl *string_value)(unsigned, unsigned);
static long long (__cdecl *run_js)(unsigned, unsigned);
static unsigned calls, ready_calls, failures;

static long long __cdecl notify(unsigned es, unsigned data)
{
    if (data != (unsigned)(UINT_PTR)notify || count_args(es) != 3) ++failures;
    const wchar_t* a = (const wchar_t*)(UINT_PTR)to_string(es, arg(es, 0));
    const wchar_t* b = (const wchar_t*)(UINT_PTR)to_string(es, arg(es, 1));
    const wchar_t* c = (const wchar_t*)(UINT_PTR)to_string(es, arg(es, 2));
    if (wcscmp(a, L"list") || wcscmp(b, L"refresh") || wcscmp(c, L"\x4e2d\x6587\xd83d\xde00")) ++failures;
    ++calls;
    return string_value(es, (unsigned)(UINT_PTR)L"OK|\x4e2d\x6587\xd83d\xde00");
}

static void __cdecl ready(unsigned view, unsigned param, unsigned frame)
{
    (void)param; (void)frame;
    ++ready_calls;
    run_js(view, (unsigned)(UINT_PTR)L"window.nativeReady='ready';");
}

static void pump(unsigned ms)
{
    DWORD start = GetTickCount();
    do {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(5);
    } while (GetTickCount() - start < ms);
}

int main(void)
{
    HMODULE dll = LoadLibraryW(L"MbKit.dll");
    if (!dll) return 10;
#define LOAD(var, name) (*(FARPROC*)&(var) = GetProcAddress(dll, name))
    int (__stdcall *init)(void);
    unsigned (__cdecl *create)(void);
    void (__cdecl *set_handle)(unsigned, unsigned);
    void (__cdecl *load_html)(unsigned, unsigned);
    void (__cdecl *bind_fn)(unsigned, unsigned, unsigned, unsigned);
    void (__cdecl *on_ready)(unsigned, unsigned, unsigned);
    void (__cdecl *destroy)(unsigned);
    LOAD(init, "MbKit_EnsureKernelInit"); LOAD(create, "wkeCreateWebView");
    LOAD(set_handle, "wkeSetHandle"); LOAD(load_html, "wkeLoadHTMLW");
    LOAD(bind_fn, "wkeJsBindFunction"); LOAD(on_ready, "wkeOnDocumentReady2");
    LOAD(destroy, "wkeDestroyWebView"); LOAD(arg, "jsArg"); LOAD(count_args, "jsArgCount");
    LOAD(to_string, "jsToStringW"); LOAD(string_value, "jsStringW"); LOAD(run_js, "wkeRunJSW");
    if (init() < 0) return 11;
    HWND window = CreateWindowExW(0, L"STATIC", L"JS bridge test", WS_OVERLAPPEDWINDOW,
                                  -3000, 0, 640, 480, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        unsigned view = create();
        set_handle(view, (unsigned)(UINT_PTR)window);
        bind_fn((unsigned)(UINT_PTR)"e_notify", (unsigned)(UINT_PTR)notify, (unsigned)(UINT_PTR)notify, 3);
        on_ready(view, (unsigned)(UINT_PTR)ready, 0);
        load_html(view, (unsigned)(UINT_PTR)L"<html><body><script>try { window.answer=e_notify('list','refresh','\x4e2d\x6587\xd83d\xde00'); } catch(e) { window.answer='ERROR:'+e.message; }</script></body></html>");
        /* Controller creation and navigation are asynchronous; slow machines
           must reach readiness rather than racing a fixed 1.5-second sleep. */
        DWORD deadline = GetTickCount() + 15000;
        while ((calls < cycle + 1 || ready_calls < cycle + 1) &&
               (LONG)(GetTickCount() - deadline) < 0) pump(25);
        pump(50);
        long long result = run_js(view, (unsigned)(UINT_PTR)L"String(window.answer)+'/'+String(window.nativeReady)");
        const wchar_t* text = (const wchar_t*)(UINT_PTR)to_string(0, result);
        int ok = !wcscmp(text, L"OK|\x4e2d\x6587\xd83d\xde00/ready");
        printf("cycle=%u roundtrip=%s calls=%u ready=%u errors=%u\n", cycle + 1,
               ok ? "PASS" : "FAIL", calls, ready_calls, failures);
        if (!ok) { char buf[1024]; WideCharToMultiByte(CP_UTF8, 0, text, -1, buf, sizeof(buf), NULL, NULL); puts(buf); ++failures; }
        destroy(view);
        pump(100);
    }
    DestroyWindow(window);
    return failures || calls != 3 || ready_calls != 3 ? 1 : 0;
}
