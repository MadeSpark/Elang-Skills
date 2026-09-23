/* ============================================================================
 * mb_legacy.c —— 「旧做法」对照实验 DLL（用于复现用户遇到的热重载失败）
 * ----------------------------------------------------------------------------
 * 完全复刻用户 miniblink.e 里的做法：
 *   ① .DLL命令 用**相对路径** "main\corn\node.dll"（这里等价于 LoadLibraryA
 *      相对路径，按当前工作目录解析）→ node.dll 生命周期不归业务 DLL 管；
 *   ② WKE初始化 有 `如果真 (wkeIsInitialize () ＝ 0)` 守卫；
 *   ③ paint 回调函数**定义在本 DLL 内**并注册进内核；
 *   ④ 从不调用 wkeFinalize；
 *   ⑤ `销毁` 里恢复子类化是**条件式**的 —— 窗口先没了就整段跳过。
 *
 * 目的**不是**让它工作，而是让宿主的对照模式能够复现「卸载后无法重来」。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define LEG_API __declspec(dllexport)

typedef void           (*p_wkeInitialize)(void);
typedef void           (*p_wkeFinalize)(void);
typedef BOOL           (*p_wkeIsInitialize)(void);
typedef void*          (*p_wkeCreateWebView)(void);
typedef void           (*p_wkeDestroyWebView)(void*);
typedef void           (*p_wkeSetHandle)(void*, HWND);
typedef void           (*p_wkeResize)(void*, int, int);
typedef BOOL           (*p_wkeIsLoadingCompleted)(void*);
typedef const wchar_t* (*p_wkeGetTitleW)(void*);
typedef void           (*p_wkeOnPaintUpdated)(void*, void*, void*);
typedef void           (*p_wkeOnLoadingFinish)(void*, void*, void*);
typedef void           (*p_wkeLoadHTMLW)(void*, const wchar_t*);

static HMODULE hNode = NULL;
static void*   wv    = NULL;
static HWND    gWnd  = NULL;
static int     paints = 0;
static int     loads  = 0;
static int     inits  = 0;
static char    nodePath[MAX_PATH];
static char    errbuf[512];

static p_wkeInitialize         f_Initialize;
static p_wkeFinalize           f_Finalize;
static p_wkeIsInitialize       f_IsInitialize;
static p_wkeCreateWebView      f_CreateWebView;
static p_wkeDestroyWebView     f_DestroyWebView;
static p_wkeSetHandle          f_SetHandle;
static p_wkeResize             f_Resize;
static p_wkeIsLoadingCompleted f_IsLoaded;
static p_wkeGetTitleW          f_GetTitleW;
static p_wkeOnPaintUpdated     f_OnPaintUpdated;
static p_wkeOnLoadingFinish    f_OnLoadingFinish;
static p_wkeLoadHTMLW          f_LoadHTMLW;

/* ★ 回调定义在「会被卸载的本 DLL 内」—— 这正是野指针的来源 */
static void __cdecl legacy_paint(void* webView, void* param, void* hdc,
                                 int x, int y, int cx, int cy)
{
    (void)webView; (void)param; (void)hdc; (void)x; (void)y; (void)cx; (void)cy;
    paints++;
}

/* ★★ 更致命的一处：复刻模块里的 `_子类化` —— 业务 DLL 自己的窗口过程。
 * 它被装进窗口链后，只要窗口还活着就会一直被调用；
 * 而用户的源码里恢复子类化是**条件式**的（IsWindow 为假就整段跳过），
 * 于是「先关窗口 / 直接卸载」时子类化残留，窗口过程指向已释放代码页。 */
static void __cdecl legacy_loadfin(void* webView, void* param, void* url,
                                   int result, void* reason)
{
    (void)webView; (void)param; (void)url; (void)result; (void)reason;
    loads++;                         /* 载入完成回调 —— 内核必定调用它 */
}

static WNDPROC g_oldProc     = NULL;
static BOOL    g_subclassed  = 0;

static LRESULT CALLBACK legacy_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (g_oldProc) return CallWindowProcA(g_oldProc, h, m, w, l);
    return DefWindowProcA(h, m, w, l);
}

static void subclass_window(HWND h)
{
    if (!h || g_subclassed) return;
    SetPropA(h, "wkeEThis", (HANDLE)(ULONG_PTR)0x1234);       /* 复刻 SetPropA */
    g_oldProc = (WNDPROC)SetWindowLongA(h, GWLP_WNDPROC,
                                        (LONG)(LONG_PTR)&legacy_wndproc);
    g_subclassed = TRUE;
}

static void unsubclass_window(HWND h)
{
    /* ★ 复刻条件式恢复：窗口已销毁就整段跳过 → 残留 */
    if (!g_subclassed) return;
    if (!h) return;
    if (!IsWindow(h)) return;
    SetWindowLongA(h, GWLP_WNDPROC, (LONG)(LONG_PTR)g_oldProc);
    RemovePropA(h, "wkeEThis");
    g_subclassed = FALSE;
}

static int bind(void)
{
    #define B(m, n) f_##m = (__typeof__(f_##m))GetProcAddress(hNode, n); \
                    if (!f_##m) { _snprintf(errbuf, 500, "缺符号 %s", n); return 0; }
    B(Initialize,         "wkeInitialize")
    B(Finalize,           "wkeFinalize")
    B(IsInitialize,       "wkeIsInitialize")
    B(CreateWebView,      "wkeCreateWebView")
    B(DestroyWebView,     "wkeDestroyWebView")
    B(SetHandle,          "wkeSetHandle")
    B(Resize,             "wkeResize")
    B(IsLoaded,           "wkeIsLoadingCompleted")
    B(GetTitleW,          "wkeGetTitleW")
    B(OnPaintUpdated,     "wkeOnPaintUpdated")
    B(OnLoadingFinish,    "wkeOnLoadingFinish")
    B(LoadHTMLW,          "wkeLoadHTMLW")
    #undef B
    return 1;
}

/* 相对路径加载 —— 与易语言 `.DLL命令 "main\corn\node.dll"` 等价 */
static int load_node(void)
{
    if (hNode) return 1;
    lstrcpynA(nodePath, "main\\corn\\node.dll", MAX_PATH);
    hNode = LoadLibraryA(nodePath);
    if (!hNode) {
        /* 工作目录不对时，再试宿主 exe 目录及其上溯 —— 仅为让对照实验可跑 */
        char exe[MAX_PATH], cur[MAX_PATH];
        int i;
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        lstrcpynA(cur, exe, MAX_PATH);
        for (i = 0; i < 5; i++) {
            char* s = strrchr(cur, '\\');
            char  t[MAX_PATH];
            if (!s) break;
            *s = 0;
            _snprintf(t, MAX_PATH - 1, "%s\\main\\corn\\node.dll", cur);
            hNode = LoadLibraryA(t);
            if (hNode) { lstrcpynA(nodePath, t, MAX_PATH); break; }
        }
    }
    if (!hNode) { _snprintf(errbuf, 500, "加载 node.dll 失败(%lu)", GetLastError()); return 0; }
    return bind();
}

LEG_API int __stdcall Legacy_Attach(void* hwnd, int w, int h, const wchar_t* html)
{
    if (!load_node()) return -1;

    /* ③ 复刻「如果真 (wkeIsInitialize () ＝ 0)」守卫 */
    if (!f_IsInitialize()) {
        f_Initialize();
        inits++;
    }

    gWnd = (HWND)hwnd;
    wv = f_CreateWebView();
    if (!wv) { _snprintf(errbuf, 500, "wkeCreateWebView 返回 NULL"); return -2; }
    if (hwnd) f_SetHandle(wv, (HWND)hwnd);
    f_Resize(wv, w > 0 ? w : 800, h > 0 ? h : 600);
    /* ④ 注册「本 DLL 内」的 paint 回调 + **必定触发**的载入完成回调 */
    f_OnPaintUpdated(wv, (void*)(UINT_PTR)&legacy_paint, (void*)(UINT_PTR)hwnd);
    if (f_OnLoadingFinish)
        f_OnLoadingFinish(wv, (void*)(UINT_PTR)&legacy_loadfin, (void*)(UINT_PTR)hwnd);
    /* ⑤ 复刻 _子类化：把本 DLL 的窗口过程装进窗口链 */
    subclass_window((HWND)hwnd);
    if (html) f_LoadHTMLW(wv, html);
    return 0;
}

/* clean = 0 ：复刻「不调销毁就卸载」（先关窗口 / 忘了解绑 / 直接卸载都是这一类） */
LEG_API int __stdcall Legacy_Detach(int clean)
{
    if (!clean) {
        /* ★ 故意什么都不做：内核、被绑定的窗口、窗口过程链里
         *   全都留着指向本 DLL 的回调/代码 —— 卸载后就是野指针 */
        wv = NULL;
        return 0;
    }
    if (wv) {
        f_OnPaintUpdated(wv, NULL, NULL);
        if (f_OnLoadingFinish) f_OnLoadingFinish(wv, NULL, NULL);
        f_SetHandle(wv, NULL);
        f_DestroyWebView(wv);
    }
    unsubclass_window(gWnd);
    wv = NULL;
    return 1;
}

/* 供对照：报告子类化是否残留 */
LEG_API int __stdcall Legacy_Subclassed(void) { return g_subclassed ? 1 : 0; }
LEG_API void* __stdcall Legacy_View(void)     { return wv; }

/* 复刻易语言 WKE卸载：调了 finalize，但 isInitialize 仍为 1（实测） */
LEG_API int __stdcall Legacy_Finalize(void)
{
    if (!hNode || !f_Finalize) return -1;
    f_Finalize();
    return f_IsInitialize() ? 1 : 0;   /* 返回 isInitialize 的实际值，供对照记录 */
}

LEG_API int __stdcall Legacy_IsLoaded(void)
{
    if (!wv || !f_IsLoaded) return -1;
    return f_IsLoaded(wv) ? 1 : 0;
}

LEG_API int __stdcall Legacy_Title(void* buf, int cch)
{
    const wchar_t* t;
    if (!wv || !f_GetTitleW || !buf || cch <= 0) return -1;
    t = f_GetTitleW(wv);
    if (!t) { ((wchar_t*)buf)[0] = 0; return 1; }
    lstrcpynW((wchar_t*)buf, t, cch);
    return 0;
}

LEG_API int __stdcall Legacy_PaintCount(void)  { return paints; }
LEG_API int __stdcall Legacy_LoadCount(void)   { return loads; }
LEG_API int __stdcall Legacy_InitCount(void)   { return inits; }
LEG_API unsigned __stdcall Legacy_NodeModule(void) { return (unsigned)(UINT_PTR)hNode; }
LEG_API unsigned __stdcall Legacy_FnAddr(void) { return (unsigned)(UINT_PTR)&legacy_paint; }
LEG_API const char* __stdcall Legacy_NodePath(void) { return nodePath; }
LEG_API const char* __stdcall Legacy_Err(void) { return errbuf; }
LEG_API int __stdcall Legacy_UnloadNode(void)
{
    if (hNode && FreeLibrary(hNode)) { hNode = NULL; return 1; }
    return 0;
}
