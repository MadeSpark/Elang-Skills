/* ============================================================================
 * nk_biz.c —— 「改造后的易语言模块」在 C 侧的等价物（验证用）
 *
 * 它刻意模仿易语言编译产物的两个关键行为，否则测不出真问题：
 *   ① .DLL命令 一律**按名字**从库里取地址（易语言是运行时 GetProcAddress，
 *      不是静态导入表），且 wke* 系列按 cdecl 调用（对应声明里的 @ 前缀）。
 *   ② 回调不是直接把自己的函数指针给内核，而是先经 **_stdcall_to_cdecl**
 *      生成一段 VirtualAlloc 出来的 thunk —— 这段内存**不属于任何模块**，
 *      所以「模块存活校验」不能用它来判断，这是个必须踩到的坑。
 *
 * 于是本文件同时验证：转发桩、懒加载、回调拦截、thunk 存活判据。
 * ==========================================================================*/
#include <windows.h>
#include <stdio.h>
#include <string.h>

#define NK_API __declspec(dllexport)

/* ---- 从 MbKit.dll 取到的 wke 函数（全部 cdecl） ---- */
typedef void  (__cdecl *P_v0)(void);
typedef void* (__cdecl *P_p0)(void);
typedef void  (__cdecl *P_vpp)(void*, void*);
typedef void  (__cdecl *P_vpii)(void*, int, int);
typedef void  (__cdecl *P_vppw)(void*, const wchar_t*);
typedef int   (__cdecl *P_bp)(void*);
typedef const wchar_t* (__cdecl *P_wp)(void*);
typedef int   (__cdecl *P_p_int)(void*);
typedef int   (__cdecl *P_on3)(void*, void*, void*);   /* wkeOnTitleChanged(view, cb, param) */

static P_v0   A_init;
static P_p0   A_create;
static P_vpp  A_setHandle;
static P_vpii A_resize;
static P_vppw A_loadHtml;
static P_bp   A_isDone;
static P_bp   A_isFailed;
static P_wp   A_getTitle;
typedef void (*P_destroy)(void*);
static P_destroy A_destroy;
static P_on3   A_onTitle;

/* ---- 本"业务模块"内部状态 ---- */
static void*   g_view   = NULL;
static HWND    g_hwnd   = NULL;
static void*   g_thunk  = NULL;      /* 注册给内核的 cdecl thunk（VirtualAlloc） */
static wchar_t g_title[512];
static int     g_titleGot = 0;
static char    g_err[512];

/* --------------------------------------------------------------------------
 * 复刻易语言模块的 _stdcall_to_cdecl：
 *   push ebp; mov ebp,esp
 *   mov  eax, <子程序地址>
 *   push dword [ebp+disp]  × N     （反序，使参数顺序与 cdecl 一致）
 *   call eax
 *   pop ebp
 *   ret                            （cdecl：由调用方清栈）
 * 生成的这段代码既不保留也不释放，与模块行为一致。
 * ------------------------------------------------------------------------*/
static void* make_thunk(void* fn, int nargs)
{
    unsigned char* p = (unsigned char*)VirtualAlloc(
        NULL, 12 + 6 * nargs, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int o = 0, j;
    if (!p) return NULL;

    p[o++] = 0x55; p[o++] = 0x8B; p[o++] = 0xEC;            /* push ebp; mov ebp,esp */
    p[o++] = 0xB8; *(unsigned*)(p + o) = (unsigned)(UINT_PTR)fn; o += 4;  /* mov eax, fn */
    for (j = 0; j < nargs; j++) {
        int disp = 4 * (nargs - j) + 4;                      /* 最高地址的参数先压 */
        p[o++] = 0xFF; p[o++] = 0xB5; *(int*)(p + o) = disp; o += 4;
    }
    p[o++] = 0xFF; p[o++] = 0xD0;                            /* call eax */
    p[o++] = 0x5D;                                           /* pop ebp */
    p[o++] = 0xC3;                                           /* ret */
    FlushInstructionCache(GetCurrentProcess(), p, o);
    return p;
}

/* ---- 业务回调：刻意定义在"会被卸载的模块"里 ---- */
static void __stdcall on_title(void* wv, void* param, void* titleStr)
{
    (void)wv; (void)param;
    g_titleGot++;
    if (titleStr) {
        /* wkeString -> (wkeGetStringW 未取到时就退化为记录非空) */
        (void)titleStr;
        g_title[0] = 0;
    }
}

static int bind_api(void)
{
    HMODULE h = LoadLibraryA("MbKit.dll");
    void*   f;
    if (!h) { _snprintf(g_err, sizeof(g_err), "加载 MbKit.dll 失败"); return 0; }

#define GET(m, n) f = (void*)GetProcAddress(h, n); \
                  if (!f) { _snprintf(g_err, sizeof(g_err), "缺导出 %s", n); return 0; } \
                  m = (void*)f
    GET(A_init,     "wkeInitialize");
    GET(A_create,   "wkeCreateWebView");
    GET(A_setHandle,"wkeSetHandle");
    GET(A_resize,   "wkeResize");
    GET(A_loadHtml, "wkeLoadHTMLW");
    GET(A_isDone,   "wkeIsLoadingCompleted");
    GET(A_isFailed, "wkeIsLoadingFailed");
    GET(A_getTitle, "wkeGetTitleW");
#undef GET
    {
        typedef void (*P_d)(void*);
        P_d d = (P_d)GetProcAddress(h, "wkeDestroyWebView");
        A_destroy = d;
    }
    A_onTitle = (P_on3)GetProcAddress(h, "wkeOnTitleChanged");
    return 1;
}

NK_API int __stdcall Nk_Attach(void* hwnd, int w, int h, const wchar_t* html)
{
    if (!A_init && !bind_api()) return -1;

    A_init();                                    /* 经转发桩 -> 内核（首调触发懒加载） */
    g_hwnd = (HWND)hwnd;
    g_view = A_create();
    if (!g_view) return -2;
    A_setHandle(g_view, hwnd);
    A_resize(g_view, w, h);

    /* ★ 关键：注册的是 VirtualAlloc 出来的 thunk，不是本模块内的函数地址 */
    if (A_onTitle && !g_thunk) {
        g_thunk = make_thunk((void*)&on_title, 3);
        if (g_thunk) A_onTitle(g_view, g_thunk, (void*)0x1234);
    }
    if (html) A_loadHtml(g_view, html);
    return 0;
}

NK_API int __stdcall Nk_Pump(int ms)
{
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG   m;
    do {
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageA(&m); }
        Sleep(2);
    } while ((int)(GetTickCount() - end) < 0);
    return 0;
}

NK_API int __stdcall Nk_IsDone(void)   { return (A_isDone && g_view) ? A_isDone(g_view) : -1; }
NK_API int __stdcall Nk_TitleHits(void){ return g_titleGot; }
NK_API int __stdcall Nk_TitleLen(void)
{
    const wchar_t* t;
    if (!A_getTitle || !g_view) return -1;
    t = A_getTitle(g_view);
    if (!t) return -2;
    return (int)wcslen(t);
}
NK_API unsigned __stdcall Nk_ThunkAddr(void) { return (unsigned)(UINT_PTR)g_thunk; }
NK_API const char* __stdcall Nk_Err(void)    { return g_err; }

/* clean=1 走正常契约（先由宿主调 MbKit_DetachAll）；clean=0 模拟"忘了解绑" */
NK_API int __stdcall Nk_Detach(int clean)
{
    if (g_view) { A_destroy(g_view); g_view = NULL; }   /* 注意：销毁 view 不解除 thunk 登记 */
    if (clean) { g_thunk = NULL; g_titleGot = 0; }
    return 0;
}
