/* ============================================================================
 * mb_biz.c —— 业务薄层（会被反复卸载 / 重新加载的那一层）
 * ----------------------------------------------------------------------------
 * 它扮演的角色 = 易语言里「使用了 miniblink 的那部分代码」。
 * 它**不碰内核**，只通过 MbKit 的纯 C 接口做事：
 *
 *   Biz_Attach(hwnd, w, h, html)  →  MbKit_Init + MbKit_Create + MbKit_LoadHtml
 *   Biz_Detach()                  →  MbKit_DetachAll()   ← 热重载契约
 *
 * ★ 关键点：`biz_on_event` 这个事件函数**住在本 DLL 里**。
 *   本 DLL 被卸载后，该地址就是野指针 —— 但 MbKit 每次转发前都会做
 *   模块存活校验，因此**即使业务侧忘了 Detach 也不会崩**。
 *
 * 编译为 biz_v1.dll / biz_v2.dll（不同 image-base，模拟「更新后的 DLL
 * 被载入到不同地址」这一最坏情况）。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "mbkit_api.h"
#include "mbload.h"

#define BIZ_API __declspec(dllexport)

static MBKIT   K;
static int     g_loaded = 0;
static void*   g_view   = NULL;
static HWND    g_hwnd   = NULL;
static int     g_paints = 0;
static int     g_loads  = 0;        /* 载入完成回调被调用的次数（真正会触发的那条路） */
static int     g_attaches = 0;
static char    g_err[512];

/* ---- 业务侧事件函数：故意放在「会被卸载的模块」里 ---- */
static void __stdcall biz_on_event(void* user, int ev, void* p1, void* p2)
{
    (void)user; (void)p2;
    switch (ev) {
    case MB_EV_PAINT:
        g_paints++;                    /* 这里代表业务侧的渲染逻辑（实测基本不会触发） */
        (void)p1;
        break;
    case MB_EV_LOADED:
        g_loads++;                     /* 载入完成 —— 内核必定调用，卸载后就是野指针 */
        break;
    default:
        break;
    }
}

static void seterr(const char* m)
{
    _snprintf(g_err, sizeof(g_err) - 1, "%s", m);
    g_err[sizeof(g_err) - 1] = 0;
}

static int ensure_kit(void)
{
    if (g_loaded) return 1;
    if (!mbk_load(&K, "MbKit.dll")) {
        seterr("加载 MbKit.dll 失败（应在宿主 exe 同目录）");
        return 0;
    }
    if (K.Init(NULL) != MBKIT_OK) {
        seterr(K.LastError ? K.LastError() : "MbKit_Init 失败");
        return 0;
    }
    g_loaded = 1;
    return 1;
}

/* ==========================================================================
 * 导出
 * ========================================================================*/
BIZ_API int __stdcall Biz_Attach(void* hwnd, int w, int h, const wchar_t* html)
{
    g_err[0] = 0;
    if (!ensure_kit()) return -1;

    g_hwnd = (HWND)hwnd;
    g_attaches++;
    g_view = K.Create(hwnd, 0, 0, w > 0 ? w : 800, h > 0 ? h : 600,
                      (MbEventCb)&biz_on_event, NULL);
    if (!g_view) {
        seterr(K.LastError ? K.LastError() : "MbKit_Create 返回 NULL");
        return -2;
    }
    if (html) K.LoadHtml(g_view, html);
    return 0;
}

/* ★ 热重载契约 */
BIZ_API int __stdcall Biz_Detach(void)
{
    int n = 0;
    if (!g_loaded) return 0;
    n = K.DetachAll();                 /* 解绑回调 + 解绑窗口 + 销毁 view */
    g_view = NULL;
    return n;
}

BIZ_API int __stdcall Biz_IsLoaded(void)
{
    if (!g_view) return -1;
    return K.IsLoaded(g_view);
}

BIZ_API int __stdcall Biz_Title(void* buf, int cch)
{
    if (!g_view || !buf || cch <= 0) return -1;
    return K.GetTitle(g_view, (wchar_t*)buf, cch);
}

BIZ_API int __stdcall Biz_PaintCount(void)   { return g_paints; }
BIZ_API int __stdcall Biz_LoadCount(void)    { return g_loads; }
BIZ_API int __stdcall Biz_AttachCount(void)  { return g_attaches; }
/* 把 view 句柄暴露给宿主 —— 故障注入测试要在业务层卸载后继续用它触发内核回调 */
BIZ_API void* __stdcall Biz_View(void)       { return g_view; }
BIZ_API int __stdcall Biz_LiveCount(void)    { return g_loaded ? K.LiveCount() : -1; }
BIZ_API int __stdcall Biz_LoadHtmlSafe(const wchar_t* html)
{
    if (!g_view) return -1;
    return K.LoadHtml(g_view, html);
}
BIZ_API int __stdcall Biz_Zoom(float f)
{
    if (!g_view) return -1;
    return K.SetZoom(g_view, f);
}
BIZ_API int __stdcall Biz_ForcePaint(void)
{
    if (!g_view) return -1;
    return K.ForcePaint(g_view);
}
BIZ_API int __stdcall Biz_Pump(int ms)
{
    if (!g_loaded) return 0;
    return K.Pump(ms);
}

/* ---- 诊断：把常驻层的状态暴露给宿主 ---- */
BIZ_API unsigned __stdcall Biz_NodeModule(void)      { return g_loaded ? K.NodeModule() : 0; }
BIZ_API unsigned __stdcall Biz_KitModule(void)       { return g_loaded ? K.SelfModule() : 0; }
BIZ_API int      __stdcall Biz_KernelInitCount(void) { return g_loaded ? K.KernelInitCount() : -1; }
BIZ_API int      __stdcall Biz_KitPinned(void)       { return g_loaded ? K.Pinned() : -1; }
BIZ_API const char* __stdcall Biz_KitVersion(void)   { return g_loaded ? K.Version() : "(not loaded)"; }
BIZ_API const char* __stdcall Biz_KitNodePath(void)  { return g_loaded ? K.NodePath() : "(not loaded)"; }
BIZ_API const char* __stdcall Biz_Err(void)          { return g_err; }

/* 供宿主拿到「本模块内某个函数地址」，用于野指针检测自证 */
BIZ_API unsigned __stdcall Biz_FnAddr(void)          { return (unsigned)(UINT_PTR)&biz_on_event; }
