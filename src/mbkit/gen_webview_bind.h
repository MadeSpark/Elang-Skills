/* 本文件由 gen_webview.py 自动生成 —— 请勿手改。
 * 数据来源：易语言模块「miniblink」的 .DLL命令 声明表（e2t 产物，与 gen_fwd.py 同源）。
 * 重新生成：python gen_webview.py <清单.json>
 */
#ifndef MBK_GEN_WEBVIEW_BIND_H
#define MBK_GEN_WEBVIEW_BIND_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
/* ---- 实现原型（定义在 mbk_webview.cpp，必须与本签名逐字一致）---- */
extern unsigned mbkwv_wkeIsInitialize(void);
extern void mbkwv_wkeFinalize(void);
extern void mbkwv_wkeUpdate(void);
extern unsigned mbkwv_wkeGetVersion(void);
extern unsigned mbkwv_wkeGetVersionString(void);
extern unsigned mbkwv_wkeCreateWebView(void);
extern void mbkwv_wkeDestroyWebView(unsigned a0);
extern void mbkwv_wkeSetHandle(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetHandleOffset(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_wkeGetHostHWND(unsigned a0);
extern unsigned mbkwv_wkeGetWindowHandle(unsigned a0);
extern void mbkwv_wkeResize(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_wkeGetWidth(unsigned a0);
extern unsigned mbkwv_wkeGetHeight(unsigned a0);
extern unsigned mbkwv_wkeGetContentWidth(unsigned a0);
extern unsigned mbkwv_wkeGetContentHeight(unsigned a0);
extern void mbkwv_wkeRepaintIfNeeded(unsigned a0);
extern unsigned mbkwv_wkeGetViewDC(unsigned a0);
extern void mbkwv_wkeSetZoomFactor(unsigned a0, float a1);
extern float mbkwv_wkeGetZoomFactor(unsigned a0);
extern void mbkwv_wkeSetFocus(unsigned a0);
extern void mbkwv_wkeKillFocus(unsigned a0);
extern void mbkwv_wkeSetTransparent(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetEditable(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetName(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeGetName(unsigned a0);
extern unsigned mbkwv_wkeIsAwake(unsigned a0);
extern void mbkwv_wkeSleep(unsigned a0);
extern void mbkwv_wkeWake(unsigned a0);
extern void mbkwv_wkeSetHeadlessEnabled(unsigned a0, unsigned a1);
extern void mbkwv_wkeLayoutIfNeeded(unsigned a0);
extern void mbkwv_wkePaint(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkePaint2(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5, unsigned a6, unsigned a7, unsigned a8, unsigned a9, unsigned a10);
extern void mbkwv_wkeAddDirtyArea(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4);
extern unsigned mbkwv_wkeIsDirty(unsigned a0);
extern void mbkwv_wkeSetDirty(unsigned a0, unsigned a1);
extern void mbkwv_wkeMoveToCenter(unsigned a0);
extern void mbkwv_wkeMoveWindow(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4);
extern void mbkwv_wkeResizeWindow(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeShowWindow(unsigned a0, unsigned a1);
extern void mbkwv_wkeEnableWindow(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetWindowTitleW(unsigned a0, unsigned a1);
extern void mbkwv_wkeLoadW(unsigned a0, unsigned a1);
extern void mbkwv_wkeLoadURL(unsigned a0, unsigned a1);
extern void mbkwv_wkeLoadURLW(unsigned a0, unsigned a1);
extern void mbkwv_wkeLoadHTML(unsigned a0, unsigned a1);
extern void mbkwv_wkeLoadHTMLW(unsigned a0, unsigned a1);
extern void mbkwv_wkeLoadHtmlWithBaseUrl(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeLoadFileW(unsigned a0, unsigned a1);
extern void mbkwv_wkeStopLoading(unsigned a0);
extern void mbkwv_wkeReload(unsigned a0);
extern unsigned mbkwv_wkeGetURL(unsigned a0);
extern unsigned mbkwv_wkeGetTitle(unsigned a0);
extern unsigned mbkwv_wkeGetTitleW(unsigned a0);
extern unsigned mbkwv_wkeIsLoading(unsigned a0);
extern unsigned mbkwv_wkeIsLoadingCompleted(unsigned a0);
extern unsigned mbkwv_wkeIsLoadingFailed(unsigned a0);
extern unsigned mbkwv_wkeIsLoadingSucceeded(unsigned a0);
extern unsigned mbkwv_wkeIsDocumentReady(unsigned a0);
extern unsigned mbkwv_wkeCanGoBack(unsigned a0);
extern unsigned mbkwv_wkeGoBack(unsigned a0);
extern unsigned mbkwv_wkeCanGoForward(unsigned a0);
extern unsigned mbkwv_wkeGoForward(unsigned a0);
extern long long mbkwv_wkeRunJS(unsigned a0, unsigned a1);
extern long long mbkwv_wkeRunJSW(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeGetSource(unsigned a0);
extern unsigned mbkwv_wkeCreateStringW(unsigned a0, unsigned a1);
extern void mbkwv_wkeDeleteString(unsigned a0);
extern unsigned mbkwv_wkeGetStringW(unsigned a0);
extern unsigned mbkwv_wkeGetString(unsigned a0);
extern void mbkwv_wkeSetStringW(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeSetUserKeyValue(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_wkeGetUserKeyValue(unsigned a0, unsigned a1);
extern void mbkwv_wkeOnAlertBox(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnConfirmBox(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnConsole(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnCreateView(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDidCreateScriptContext(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDocumentReady(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDocumentReady2(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDownload(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnLoadUrlBegin(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnLoadUrlEnd(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnLoadingFinish(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDownloadFinish(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDownloadProgress(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnDownloadStart(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeSetDownloadDir(unsigned a0, unsigned a1);
extern void mbkwv_wkeOnMouseOverUrlChanged(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnNavigation(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnPaintUpdated(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnPromptBox(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnTitleChanged(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnURLChanged(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnURLChanged2(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnWillMediaLoad(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnWillReleaseScriptContext(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnWindowClosing(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeOnWindowDestroy(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeNetOnResponse(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeSetUserAgent(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetUserAgentW(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeGetUserAgent(unsigned a0);
extern void mbkwv_wkeSetDebugConfig(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeSetCookieJarPath(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetCookieJarFullPath(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetLocalStorageFullPath(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetProxy(unsigned a0);
extern void mbkwv_wkeSetViewProxy(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetCspCheckEnable(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetViewNetInterface(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetNavigationToNewWindowEnable(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetMouseEnabled(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetTouchEnabled(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetDragEnable(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetDragDropEnable(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetMemoryCacheEnable(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetNpapiPluginsEnabled(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetResourceGc(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetDeviceParameter(unsigned a0, unsigned a1, unsigned a2, unsigned a3, float a4);
extern void mbkwv_wkeAddPluginDirectory(unsigned a0, unsigned a1);
extern void mbkwv_wkeSetMediaVolume(unsigned a0, float a1);
extern float mbkwv_wkeGetMediaVolume(unsigned a0);
extern void mbkwv_wkeSetFileSystem(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4);
extern void mbkwv_wkeGC(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeFireKeyDownEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3);
extern unsigned mbkwv_wkeFireKeyUpEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3);
extern unsigned mbkwv_wkeFireKeyPressEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3);
extern unsigned mbkwv_wkeFireMouseEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4);
extern unsigned mbkwv_wkeFireContextMenuEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3);
extern unsigned mbkwv_wkeFireMouseWheelEvent(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4);
extern unsigned mbkwv_wkeFireWindowsMessage(unsigned a0, unsigned a1, unsigned a2, unsigned a3, unsigned a4, unsigned a5);
extern void mbkwv_wkeGetCaretRect(unsigned a0, unsigned a1);
extern void mbkwv_wkeEditorSelectAll(unsigned a0);
extern void mbkwv_wkeEditorUnSelect(unsigned a0);
extern void mbkwv_wkeEditorCopy(unsigned a0);
extern void mbkwv_wkeEditorCut(unsigned a0);
extern void mbkwv_wkeEditorPaste(unsigned a0);
extern void mbkwv_wkeEditorDelete(unsigned a0);
extern void mbkwv_wkeEditorUndo(unsigned a0);
extern void mbkwv_wkeEditorRedo(unsigned a0);
extern unsigned mbkwv_wkeGlobalExec(unsigned a0);
extern unsigned mbkwv_wkeGetGlobalExecByFrame(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeWebFrameGetMainFrame(unsigned a0);
extern unsigned mbkwv_wkeIsMainFrame(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeIsWebRemoteFrame(unsigned a0, unsigned a1);
extern unsigned mbkwv_wkeGetFrameUrl(unsigned a0, unsigned a1);
extern void mbkwv_wkeInsertCSSByFrame(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_wkeGetCursorInfoType(unsigned a0);
extern unsigned mbkwv_wkeUtilSerializeToMHTML(unsigned a0);
extern unsigned mbkwv_wkeIsProcessingUserGesture(unsigned a0);
extern void mbkwv_wkePerformCookieCommand(unsigned a0, unsigned a1);
extern void mbkwv_wkeVisitAllCookie(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeShowDevtools(unsigned a0, unsigned a1, unsigned a2, unsigned a3);
extern unsigned mbkwv_wkeGetWebView(unsigned a0);
extern unsigned mbkwv_wkeGetWebViewForCurrentContext(void);
extern void mbkwv_wkeWebFrameGetMainWorldScriptContext(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_wkeGetCookie(unsigned a0);
extern unsigned mbkwv_wkeGetCookieW(unsigned a0);
extern void mbkwv_wkeSetCookie(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_wkeIsCookieEnabled(unsigned a0);
extern void mbkwv_wkeSetCookieEnabled(unsigned a0, unsigned a1);
extern long long mbkwv_jsUndefined(void);
extern long long mbkwv_jsNull(void);
extern long long mbkwv_jsTrue(void);
extern long long mbkwv_jsFalse(void);
extern long long mbkwv_jsInt(unsigned a0);
extern long long mbkwv_jsStringW(unsigned a0, unsigned a1);
extern unsigned mbkwv_jsToStringW(unsigned a0, long long a1);
extern unsigned mbkwv_jsToTempStringW(unsigned a0, long long a1);
extern long long mbkwv_jsEvalW(unsigned a0, unsigned a1);
extern long long mbkwv_jsEvalExW(unsigned a0, unsigned a1, unsigned a2);
extern unsigned mbkwv_jsIsString(long long a0);
extern unsigned mbkwv_jsIsUndefined(long long a0);
extern void mbkwv_jsBindFunction(unsigned a0, unsigned a1, unsigned a2);
extern void mbkwv_wkeJsBindFunction(unsigned a0, unsigned a1, unsigned a2, unsigned a3);
extern long long mbkwv_jsArg(unsigned a0, unsigned a1);
extern unsigned mbkwv_jsArgCount(unsigned a0);
extern unsigned mbkwv_jsArgType(unsigned a0, unsigned a1);
extern unsigned mbkwv_jsTypeOf(long long a0);
extern unsigned mbkwv_jsToInt(unsigned a0, long long a1);
extern unsigned mbkwv_jsToBoolean(unsigned a0, long long a1);
extern double mbkwv_jsToDouble(unsigned a0, long long a1);
extern float mbkwv_jsToFloat(unsigned a0, long long a1);
extern long long mbkwv_wkeRunJsByFrame(unsigned a0, unsigned a1, unsigned a2, unsigned a3);

#ifdef __cplusplus
}
#endif

/* ---- ② 名字 -> 实现指针 ----
 * 不在名单里的返回 0：调用方保持槽位不动，由 mbkit_fwd.c 的安全桩兜底。 */
static void* mbkwv_lookup(const char* nm)
{
    static const struct { const char* n; void* f; } k_tab[] = {
        { "wkeIsInitialize", (void*)(size_t)&mbkwv_wkeIsInitialize },
        { "wkeFinalize", (void*)(size_t)&mbkwv_wkeFinalize },
        { "wkeUpdate", (void*)(size_t)&mbkwv_wkeUpdate },
        { "wkeGetVersion", (void*)(size_t)&mbkwv_wkeGetVersion },
        { "wkeGetVersionString", (void*)(size_t)&mbkwv_wkeGetVersionString },
        { "wkeCreateWebView", (void*)(size_t)&mbkwv_wkeCreateWebView },
        { "wkeDestroyWebView", (void*)(size_t)&mbkwv_wkeDestroyWebView },
        { "wkeSetHandle", (void*)(size_t)&mbkwv_wkeSetHandle },
        { "wkeSetHandleOffset", (void*)(size_t)&mbkwv_wkeSetHandleOffset },
        { "wkeGetHostHWND", (void*)(size_t)&mbkwv_wkeGetHostHWND },
        { "wkeGetWindowHandle", (void*)(size_t)&mbkwv_wkeGetWindowHandle },
        { "wkeResize", (void*)(size_t)&mbkwv_wkeResize },
        { "wkeGetWidth", (void*)(size_t)&mbkwv_wkeGetWidth },
        { "wkeGetHeight", (void*)(size_t)&mbkwv_wkeGetHeight },
        { "wkeGetContentWidth", (void*)(size_t)&mbkwv_wkeGetContentWidth },
        { "wkeGetContentHeight", (void*)(size_t)&mbkwv_wkeGetContentHeight },
        { "wkeRepaintIfNeeded", (void*)(size_t)&mbkwv_wkeRepaintIfNeeded },
        { "wkeGetViewDC", (void*)(size_t)&mbkwv_wkeGetViewDC },
        { "wkeSetZoomFactor", (void*)(size_t)&mbkwv_wkeSetZoomFactor },
        { "wkeGetZoomFactor", (void*)(size_t)&mbkwv_wkeGetZoomFactor },
        { "wkeSetFocus", (void*)(size_t)&mbkwv_wkeSetFocus },
        { "wkeKillFocus", (void*)(size_t)&mbkwv_wkeKillFocus },
        { "wkeSetTransparent", (void*)(size_t)&mbkwv_wkeSetTransparent },
        { "wkeSetEditable", (void*)(size_t)&mbkwv_wkeSetEditable },
        { "wkeSetName", (void*)(size_t)&mbkwv_wkeSetName },
        { "wkeGetName", (void*)(size_t)&mbkwv_wkeGetName },
        { "wkeIsAwake", (void*)(size_t)&mbkwv_wkeIsAwake },
        { "wkeSleep", (void*)(size_t)&mbkwv_wkeSleep },
        { "wkeWake", (void*)(size_t)&mbkwv_wkeWake },
        { "wkeSetHeadlessEnabled", (void*)(size_t)&mbkwv_wkeSetHeadlessEnabled },
        { "wkeLayoutIfNeeded", (void*)(size_t)&mbkwv_wkeLayoutIfNeeded },
        { "wkePaint", (void*)(size_t)&mbkwv_wkePaint },
        { "wkePaint2", (void*)(size_t)&mbkwv_wkePaint2 },
        { "wkeAddDirtyArea", (void*)(size_t)&mbkwv_wkeAddDirtyArea },
        { "wkeIsDirty", (void*)(size_t)&mbkwv_wkeIsDirty },
        { "wkeSetDirty", (void*)(size_t)&mbkwv_wkeSetDirty },
        { "wkeMoveToCenter", (void*)(size_t)&mbkwv_wkeMoveToCenter },
        { "wkeMoveWindow", (void*)(size_t)&mbkwv_wkeMoveWindow },
        { "wkeResizeWindow", (void*)(size_t)&mbkwv_wkeResizeWindow },
        { "wkeShowWindow", (void*)(size_t)&mbkwv_wkeShowWindow },
        { "wkeEnableWindow", (void*)(size_t)&mbkwv_wkeEnableWindow },
        { "wkeSetWindowTitleW", (void*)(size_t)&mbkwv_wkeSetWindowTitleW },
        { "wkeLoadW", (void*)(size_t)&mbkwv_wkeLoadW },
        { "wkeLoadURL", (void*)(size_t)&mbkwv_wkeLoadURL },
        { "wkeLoadURLW", (void*)(size_t)&mbkwv_wkeLoadURLW },
        { "wkeLoadHTML", (void*)(size_t)&mbkwv_wkeLoadHTML },
        { "wkeLoadHTMLW", (void*)(size_t)&mbkwv_wkeLoadHTMLW },
        { "wkeLoadHtmlWithBaseUrl", (void*)(size_t)&mbkwv_wkeLoadHtmlWithBaseUrl },
        { "wkeLoadFileW", (void*)(size_t)&mbkwv_wkeLoadFileW },
        { "wkeStopLoading", (void*)(size_t)&mbkwv_wkeStopLoading },
        { "wkeReload", (void*)(size_t)&mbkwv_wkeReload },
        { "wkeGetURL", (void*)(size_t)&mbkwv_wkeGetURL },
        { "wkeGetTitle", (void*)(size_t)&mbkwv_wkeGetTitle },
        { "wkeGetTitleW", (void*)(size_t)&mbkwv_wkeGetTitleW },
        { "wkeIsLoading", (void*)(size_t)&mbkwv_wkeIsLoading },
        { "wkeIsLoadingCompleted", (void*)(size_t)&mbkwv_wkeIsLoadingCompleted },
        { "wkeIsLoadingFailed", (void*)(size_t)&mbkwv_wkeIsLoadingFailed },
        { "wkeIsLoadingSucceeded", (void*)(size_t)&mbkwv_wkeIsLoadingSucceeded },
        { "wkeIsDocumentReady", (void*)(size_t)&mbkwv_wkeIsDocumentReady },
        { "wkeCanGoBack", (void*)(size_t)&mbkwv_wkeCanGoBack },
        { "wkeGoBack", (void*)(size_t)&mbkwv_wkeGoBack },
        { "wkeCanGoForward", (void*)(size_t)&mbkwv_wkeCanGoForward },
        { "wkeGoForward", (void*)(size_t)&mbkwv_wkeGoForward },
        { "wkeRunJS", (void*)(size_t)&mbkwv_wkeRunJS },
        { "wkeRunJSW", (void*)(size_t)&mbkwv_wkeRunJSW },
        { "wkeGetSource", (void*)(size_t)&mbkwv_wkeGetSource },
        { "wkeCreateStringW", (void*)(size_t)&mbkwv_wkeCreateStringW },
        { "wkeDeleteString", (void*)(size_t)&mbkwv_wkeDeleteString },
        { "wkeGetStringW", (void*)(size_t)&mbkwv_wkeGetStringW },
        { "wkeGetString", (void*)(size_t)&mbkwv_wkeGetString },
        { "wkeSetStringW", (void*)(size_t)&mbkwv_wkeSetStringW },
        { "wkeSetUserKeyValue", (void*)(size_t)&mbkwv_wkeSetUserKeyValue },
        { "wkeGetUserKeyValue", (void*)(size_t)&mbkwv_wkeGetUserKeyValue },
        { "wkeOnAlertBox", (void*)(size_t)&mbkwv_wkeOnAlertBox },
        { "wkeOnConfirmBox", (void*)(size_t)&mbkwv_wkeOnConfirmBox },
        { "wkeOnConsole", (void*)(size_t)&mbkwv_wkeOnConsole },
        { "wkeOnCreateView", (void*)(size_t)&mbkwv_wkeOnCreateView },
        { "wkeOnDidCreateScriptContext", (void*)(size_t)&mbkwv_wkeOnDidCreateScriptContext },
        { "wkeOnDocumentReady", (void*)(size_t)&mbkwv_wkeOnDocumentReady },
        { "wkeOnDocumentReady2", (void*)(size_t)&mbkwv_wkeOnDocumentReady2 },
        { "wkeOnDownload", (void*)(size_t)&mbkwv_wkeOnDownload },
        { "wkeOnLoadUrlBegin", (void*)(size_t)&mbkwv_wkeOnLoadUrlBegin },
        { "wkeOnLoadUrlEnd", (void*)(size_t)&mbkwv_wkeOnLoadUrlEnd },
        { "wkeOnLoadingFinish", (void*)(size_t)&mbkwv_wkeOnLoadingFinish },
        { "wkeOnDownloadFinish", (void*)(size_t)&mbkwv_wkeOnDownloadFinish },
        { "wkeOnDownloadProgress", (void*)(size_t)&mbkwv_wkeOnDownloadProgress },
        { "wkeOnDownloadStart", (void*)(size_t)&mbkwv_wkeOnDownloadStart },
        { "wkeSetDownloadDir", (void*)(size_t)&mbkwv_wkeSetDownloadDir },
        { "wkeOnMouseOverUrlChanged", (void*)(size_t)&mbkwv_wkeOnMouseOverUrlChanged },
        { "wkeOnNavigation", (void*)(size_t)&mbkwv_wkeOnNavigation },
        { "wkeOnPaintUpdated", (void*)(size_t)&mbkwv_wkeOnPaintUpdated },
        { "wkeOnPromptBox", (void*)(size_t)&mbkwv_wkeOnPromptBox },
        { "wkeOnTitleChanged", (void*)(size_t)&mbkwv_wkeOnTitleChanged },
        { "wkeOnURLChanged", (void*)(size_t)&mbkwv_wkeOnURLChanged },
        { "wkeOnURLChanged2", (void*)(size_t)&mbkwv_wkeOnURLChanged2 },
        { "wkeOnWillMediaLoad", (void*)(size_t)&mbkwv_wkeOnWillMediaLoad },
        { "wkeOnWillReleaseScriptContext", (void*)(size_t)&mbkwv_wkeOnWillReleaseScriptContext },
        { "wkeOnWindowClosing", (void*)(size_t)&mbkwv_wkeOnWindowClosing },
        { "wkeOnWindowDestroy", (void*)(size_t)&mbkwv_wkeOnWindowDestroy },
        { "wkeNetOnResponse", (void*)(size_t)&mbkwv_wkeNetOnResponse },
        { "wkeSetUserAgent", (void*)(size_t)&mbkwv_wkeSetUserAgent },
        { "wkeSetUserAgentW", (void*)(size_t)&mbkwv_wkeSetUserAgentW },
        { "wkeGetUserAgent", (void*)(size_t)&mbkwv_wkeGetUserAgent },
        { "wkeSetDebugConfig", (void*)(size_t)&mbkwv_wkeSetDebugConfig },
        { "wkeSetCookieJarPath", (void*)(size_t)&mbkwv_wkeSetCookieJarPath },
        { "wkeSetCookieJarFullPath", (void*)(size_t)&mbkwv_wkeSetCookieJarFullPath },
        { "wkeSetLocalStorageFullPath", (void*)(size_t)&mbkwv_wkeSetLocalStorageFullPath },
        { "wkeSetProxy", (void*)(size_t)&mbkwv_wkeSetProxy },
        { "wkeSetViewProxy", (void*)(size_t)&mbkwv_wkeSetViewProxy },
        { "wkeSetCspCheckEnable", (void*)(size_t)&mbkwv_wkeSetCspCheckEnable },
        { "wkeSetViewNetInterface", (void*)(size_t)&mbkwv_wkeSetViewNetInterface },
        { "wkeSetNavigationToNewWindowEnable", (void*)(size_t)&mbkwv_wkeSetNavigationToNewWindowEnable },
        { "wkeSetMouseEnabled", (void*)(size_t)&mbkwv_wkeSetMouseEnabled },
        { "wkeSetTouchEnabled", (void*)(size_t)&mbkwv_wkeSetTouchEnabled },
        { "wkeSetDragEnable", (void*)(size_t)&mbkwv_wkeSetDragEnable },
        { "wkeSetDragDropEnable", (void*)(size_t)&mbkwv_wkeSetDragDropEnable },
        { "wkeSetMemoryCacheEnable", (void*)(size_t)&mbkwv_wkeSetMemoryCacheEnable },
        { "wkeSetNpapiPluginsEnabled", (void*)(size_t)&mbkwv_wkeSetNpapiPluginsEnabled },
        { "wkeSetResourceGc", (void*)(size_t)&mbkwv_wkeSetResourceGc },
        { "wkeSetDeviceParameter", (void*)(size_t)&mbkwv_wkeSetDeviceParameter },
        { "wkeAddPluginDirectory", (void*)(size_t)&mbkwv_wkeAddPluginDirectory },
        { "wkeSetMediaVolume", (void*)(size_t)&mbkwv_wkeSetMediaVolume },
        { "wkeGetMediaVolume", (void*)(size_t)&mbkwv_wkeGetMediaVolume },
        { "wkeSetFileSystem", (void*)(size_t)&mbkwv_wkeSetFileSystem },
        { "wkeGC", (void*)(size_t)&mbkwv_wkeGC },
        { "wkeFireKeyDownEvent", (void*)(size_t)&mbkwv_wkeFireKeyDownEvent },
        { "wkeFireKeyUpEvent", (void*)(size_t)&mbkwv_wkeFireKeyUpEvent },
        { "wkeFireKeyPressEvent", (void*)(size_t)&mbkwv_wkeFireKeyPressEvent },
        { "wkeFireMouseEvent", (void*)(size_t)&mbkwv_wkeFireMouseEvent },
        { "wkeFireContextMenuEvent", (void*)(size_t)&mbkwv_wkeFireContextMenuEvent },
        { "wkeFireMouseWheelEvent", (void*)(size_t)&mbkwv_wkeFireMouseWheelEvent },
        { "wkeFireWindowsMessage", (void*)(size_t)&mbkwv_wkeFireWindowsMessage },
        { "wkeGetCaretRect", (void*)(size_t)&mbkwv_wkeGetCaretRect },
        { "wkeEditorSelectAll", (void*)(size_t)&mbkwv_wkeEditorSelectAll },
        { "wkeEditorUnSelect", (void*)(size_t)&mbkwv_wkeEditorUnSelect },
        { "wkeEditorCopy", (void*)(size_t)&mbkwv_wkeEditorCopy },
        { "wkeEditorCut", (void*)(size_t)&mbkwv_wkeEditorCut },
        { "wkeEditorPaste", (void*)(size_t)&mbkwv_wkeEditorPaste },
        { "wkeEditorDelete", (void*)(size_t)&mbkwv_wkeEditorDelete },
        { "wkeEditorUndo", (void*)(size_t)&mbkwv_wkeEditorUndo },
        { "wkeEditorRedo", (void*)(size_t)&mbkwv_wkeEditorRedo },
        { "wkeGlobalExec", (void*)(size_t)&mbkwv_wkeGlobalExec },
        { "wkeGetGlobalExecByFrame", (void*)(size_t)&mbkwv_wkeGetGlobalExecByFrame },
        { "wkeWebFrameGetMainFrame", (void*)(size_t)&mbkwv_wkeWebFrameGetMainFrame },
        { "wkeIsMainFrame", (void*)(size_t)&mbkwv_wkeIsMainFrame },
        { "wkeIsWebRemoteFrame", (void*)(size_t)&mbkwv_wkeIsWebRemoteFrame },
        { "wkeGetFrameUrl", (void*)(size_t)&mbkwv_wkeGetFrameUrl },
        { "wkeInsertCSSByFrame", (void*)(size_t)&mbkwv_wkeInsertCSSByFrame },
        { "wkeGetCursorInfoType", (void*)(size_t)&mbkwv_wkeGetCursorInfoType },
        { "wkeUtilSerializeToMHTML", (void*)(size_t)&mbkwv_wkeUtilSerializeToMHTML },
        { "wkeIsProcessingUserGesture", (void*)(size_t)&mbkwv_wkeIsProcessingUserGesture },
        { "wkePerformCookieCommand", (void*)(size_t)&mbkwv_wkePerformCookieCommand },
        { "wkeVisitAllCookie", (void*)(size_t)&mbkwv_wkeVisitAllCookie },
        { "wkeShowDevtools", (void*)(size_t)&mbkwv_wkeShowDevtools },
        { "wkeGetWebView", (void*)(size_t)&mbkwv_wkeGetWebView },
        { "wkeGetWebViewForCurrentContext", (void*)(size_t)&mbkwv_wkeGetWebViewForCurrentContext },
        { "wkeWebFrameGetMainWorldScriptContext", (void*)(size_t)&mbkwv_wkeWebFrameGetMainWorldScriptContext },
        { "wkeGetCookie", (void*)(size_t)&mbkwv_wkeGetCookie },
        { "wkeGetCookieW", (void*)(size_t)&mbkwv_wkeGetCookieW },
        { "wkeSetCookie", (void*)(size_t)&mbkwv_wkeSetCookie },
        { "wkeIsCookieEnabled", (void*)(size_t)&mbkwv_wkeIsCookieEnabled },
        { "wkeSetCookieEnabled", (void*)(size_t)&mbkwv_wkeSetCookieEnabled },
        { "jsUndefined", (void*)(size_t)&mbkwv_jsUndefined },
        { "jsNull", (void*)(size_t)&mbkwv_jsNull },
        { "jsTrue", (void*)(size_t)&mbkwv_jsTrue },
        { "jsFalse", (void*)(size_t)&mbkwv_jsFalse },
        { "jsInt", (void*)(size_t)&mbkwv_jsInt },
        { "jsStringW", (void*)(size_t)&mbkwv_jsStringW },
        { "jsToStringW", (void*)(size_t)&mbkwv_jsToStringW },
        { "jsToTempStringW", (void*)(size_t)&mbkwv_jsToTempStringW },
        { "jsEvalW", (void*)(size_t)&mbkwv_jsEvalW },
        { "jsEvalExW", (void*)(size_t)&mbkwv_jsEvalExW },
        { "jsIsString", (void*)(size_t)&mbkwv_jsIsString },
        { "jsIsUndefined", (void*)(size_t)&mbkwv_jsIsUndefined },
        { "jsBindFunction", (void*)(size_t)&mbkwv_jsBindFunction },
        { "wkeJsBindFunction", (void*)(size_t)&mbkwv_wkeJsBindFunction },
        { "jsArg", (void*)(size_t)&mbkwv_jsArg },
        { "jsArgCount", (void*)(size_t)&mbkwv_jsArgCount },
        { "jsArgType", (void*)(size_t)&mbkwv_jsArgType },
        { "jsTypeOf", (void*)(size_t)&mbkwv_jsTypeOf },
        { "jsToInt", (void*)(size_t)&mbkwv_jsToInt },
        { "jsToBoolean", (void*)(size_t)&mbkwv_jsToBoolean },
        { "jsToDouble", (void*)(size_t)&mbkwv_jsToDouble },
        { "jsToFloat", (void*)(size_t)&mbkwv_jsToFloat },
        { "wkeRunJsByFrame", (void*)(size_t)&mbkwv_wkeRunJsByFrame },
    };
    int i;
    if (!nm) return 0;
    for (i = 0; i < (int)(sizeof(k_tab) / sizeof(k_tab[0])); i++)
        if (strcmp(k_tab[i].n, nm) == 0) return k_tab[i].f;
    return 0;
}

#endif /* MBK_GEN_WEBVIEW_BIND_H */
