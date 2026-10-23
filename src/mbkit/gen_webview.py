#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_webview.py —— 由「易语言 .DLL命令 声明表」生成 edgewebview 内核的绑定表。

输入：fwd_list.json（与 gen_fwd.py 同源）
输出：gen_webview_bind.h —— 两部分
    ① IMPL 名单里每一项的 **extern 原型**（签名与 gen_fwd.py 生成的
       K 侧执行体 tr_xxx 的强制转型**逐字一致** —— 这是硬约束：
       mbk_slots[i] 会被 tr_xxx 按 `(retc (__cdecl *)(args))c->real` 调用，
       宽度/参数个数对不上就是栈错位或返回值高位脏数据）；
    ② `mbkwv_lookup(name)` —— 名字 -> 实现函数指针；不在名单里的返回 NULL，
       调用方（mbk_webview.cpp 的 mbkwv_bind）保持槽位不动，
       由 mbkit_fwd.c 的安全桩兜底。

实现清单 IMPL 就是 mbk_webview.cpp 里真正写了代码的那些 API；
少了会在链接期报 undefined reference，多写会在这里报错 —— 两边对不上立刻炸。
"""
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------------------
# 与 gen_fwd.py 保持一致的类型映射（两处必须同步，否则槽位签名错位）
# ---------------------------------------------------------------------------
RET_KIND = {"": 0, "整数型": 1, "逻辑型": 1, "文本型": 1, "字节集": 1,
            "短整数型": 1, "字节型": 1, "长整数型": 2, "小数型": 3,
            "双精度小数型": 4}


def c_type(t):
    if t == "长整数型":
        return "long long"
    if t == "双精度小数型":
        return "double"
    if t == "小数型":
        return "float"
    return "unsigned"


def sig(cmd):
    ps = cmd.get("params", [])
    rt = cmd.get("ret", "")
    rct = c_type(rt) if rt else "void"
    argl = ", ".join("%s a%d" % (c_type(p[1]), i) for i, p in enumerate(ps))
    return rct, (argl if argl else "void")


# ---------------------------------------------------------------------------
# 实现清单：mbk_webview.cpp 里真正提供实现的 wke API
# ---------------------------------------------------------------------------
IMPL = [
    # ---- 生命周期 / 版本 ----
    "wkeIsInitialize", "wkeFinalize", "wkeUpdate",
    "wkeGetVersion", "wkeGetVersionString",
    # ---- 视图 ----
    "wkeCreateWebView", "wkeDestroyWebView", "wkeSetHandle", "wkeSetHandleOffset",
    "wkeGetHostHWND", "wkeGetWindowHandle",
    "wkeResize", "wkeGetWidth", "wkeGetHeight",
    "wkeGetContentWidth", "wkeGetContentHeight",
    "wkeRepaintIfNeeded", "wkeGetViewDC",
    "wkeSetZoomFactor", "wkeGetZoomFactor",
    "wkeSetFocus", "wkeKillFocus",
    "wkeSetTransparent", "wkeSetEditable", "wkeSetName", "wkeGetName",
    "wkeIsAwake", "wkeSleep", "wkeWake",
    "wkeSetHeadlessEnabled", "wkeLayoutIfNeeded",
    "wkePaint", "wkePaint2", "wkeAddDirtyArea", "wkeIsDirty", "wkeSetDirty",
    "wkeMoveToCenter", "wkeMoveWindow", "wkeResizeWindow",
    "wkeShowWindow", "wkeEnableWindow", "wkeSetWindowTitleW",
    # ---- 导航 ----
    "wkeLoadW", "wkeLoadURL", "wkeLoadURLW",
    "wkeLoadHTML", "wkeLoadHTMLW", "wkeLoadHtmlWithBaseUrl",
    "wkeLoadFileW", "wkeStopLoading", "wkeReload",
    "wkeGetURL", "wkeGetTitle", "wkeGetTitleW",
    "wkeIsLoading", "wkeIsLoadingCompleted", "wkeIsLoadingFailed",
    "wkeIsLoadingSucceeded", "wkeIsDocumentReady",
    "wkeCanGoBack", "wkeGoBack", "wkeCanGoForward", "wkeGoForward",
    "wkeRunJS", "wkeRunJSW", "wkeGetSource",
    # ---- wkeString / 用户键值 ----
    "wkeCreateStringW", "wkeDeleteString", "wkeGetStringW", "wkeGetString",
    "wkeSetStringW", "wkeSetUserKeyValue", "wkeGetUserKeyValue",
    # ---- 回调登记（22 个拦截项 + 网络响应）----
    "wkeOnAlertBox", "wkeOnConfirmBox", "wkeOnConsole", "wkeOnCreateView",
    "wkeOnDidCreateScriptContext", "wkeOnDocumentReady", "wkeOnDocumentReady2",
    "wkeOnDownload", "wkeOnLoadUrlBegin", "wkeOnLoadUrlEnd", "wkeOnLoadingFinish",
    "wkeOnDownloadFinish", "wkeOnDownloadProgress", "wkeOnDownloadStart",
    "wkeSetDownloadDir",
    "wkeOnMouseOverUrlChanged", "wkeOnNavigation", "wkeOnPaintUpdated",
    "wkeOnPromptBox", "wkeOnTitleChanged", "wkeOnURLChanged", "wkeOnURLChanged2",
    "wkeOnWillMediaLoad", "wkeOnWillReleaseScriptContext",
    "wkeOnWindowClosing", "wkeOnWindowDestroy", "wkeNetOnResponse",
    # ---- UA / 配置 / 开关 ----
    "wkeSetUserAgent", "wkeSetUserAgentW", "wkeGetUserAgent",
    "wkeSetDebugConfig", "wkeSetCookieJarPath", "wkeSetCookieJarFullPath",
    "wkeSetLocalStorageFullPath", "wkeSetProxy", "wkeSetViewProxy",
    "wkeSetCspCheckEnable", "wkeSetViewNetInterface",
    "wkeSetNavigationToNewWindowEnable", "wkeSetMouseEnabled",
    "wkeSetTouchEnabled", "wkeSetDragEnable", "wkeSetDragDropEnable",
    "wkeSetMemoryCacheEnable", "wkeSetNpapiPluginsEnabled",
    "wkeSetResourceGc", "wkeSetDeviceParameter", "wkeAddPluginDirectory",
    "wkeSetMediaVolume", "wkeGetMediaVolume", "wkeSetFileSystem", "wkeGC",
    # ---- 输入事件（WebView2 自己处理，本层只保证不崩）----
    "wkeFireKeyDownEvent", "wkeFireKeyUpEvent", "wkeFireKeyPressEvent",
    "wkeFireMouseEvent", "wkeFireContextMenuEvent", "wkeFireMouseWheelEvent",
    "wkeFireWindowsMessage", "wkeGetCaretRect",
    # ---- 编辑器 ----
    "wkeEditorSelectAll", "wkeEditorUnSelect", "wkeEditorCopy", "wkeEditorCut",
    "wkeEditorPaste", "wkeEditorDelete", "wkeEditorUndo", "wkeEditorRedo",
    # ---- 框架 / JS 句柄 ----
    "wkeGlobalExec", "wkeGetGlobalExecByFrame", "wkeWebFrameGetMainFrame",
    "wkeIsMainFrame", "wkeIsWebRemoteFrame", "wkeGetFrameUrl",
    "wkeInsertCSSByFrame", "wkeGetCursorInfoType",
    "wkeUtilSerializeToMHTML", "wkeIsProcessingUserGesture",
    "wkePerformCookieCommand", "wkeVisitAllCookie", "wkeShowDevtools",
    "wkeGetWebView", "wkeGetWebViewForCurrentContext",
    "wkeWebFrameGetMainWorldScriptContext",
    # ---- Cookie ----
    "wkeGetCookie", "wkeGetCookieW", "wkeSetCookie",
    "wkeIsCookieEnabled", "wkeSetCookieEnabled",
    # ---- jsValue（让「运行JS → jsToStringW」链在 edge 下也通）----
    "jsUndefined", "jsNull", "jsTrue", "jsFalse", "jsInt", "jsStringW",
    "jsToStringW", "jsToTempStringW", "jsEvalW", "jsEvalExW",
    "jsIsString", "jsIsUndefined",
    "jsBindFunction", "wkeJsBindFunction", "jsArg", "jsArgCount", "jsArgType",
    "jsTypeOf", "jsToInt", "jsToBoolean", "jsToDouble", "jsToFloat",
    "wkeRunJsByFrame",
]

HEADER = """/* 本文件由 gen_webview.py 自动生成 —— 请勿手改。
 * 数据来源：易语言模块「miniblink」的 .DLL命令 声明表（e2t 产物，与 gen_fwd.py 同源）。
 * 重新生成：python gen_webview.py <清单.json>
 */
#ifndef MBK_GEN_WEBVIEW_BIND_H
#define MBK_GEN_WEBVIEW_BIND_H

#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif
"""


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fwd_list.json")
    data = json.load(io.open(src, encoding="utf-8"))

    by = {}
    for c in data:
        by.setdefault(c["name"], c)
    names = sorted(by)

    unknown = [n for n in IMPL if n not in by]
    if unknown:
        raise SystemExit("gen_webview.py: IMPL 里有声明表没有的名字：%s" % ", ".join(unknown))

    out = [HEADER]

    # ---- ① 原型 ----
    out.append("/* ---- 实现原型（定义在 mbk_webview.cpp，必须与本签名逐字一致）---- */\n")
    for n in IMPL:
        rct, argl = sig(by[n])
        out.append("extern %s mbkwv_%s(%s);\n" % (rct, n, argl))
    out.append("\n")

    out.append("""#ifdef __cplusplus
}
#endif

/* ---- ② 名字 -> 实现指针 ----
 * 不在名单里的返回 0：调用方保持槽位不动，由 mbkit_fwd.c 的安全桩兜底。 */
static void* mbkwv_lookup(const char* nm)
{
    static const struct { const char* n; void* f; } k_tab[] = {
""")
    for n in IMPL:
        out.append('        { "%s", (void*)(size_t)&mbkwv_%s },\n' % (n, n))
    out.append("""    };
    int i;
    if (!nm) return 0;
    for (i = 0; i < (int)(sizeof(k_tab) / sizeof(k_tab[0])); i++)
        if (strcmp(k_tab[i].n, nm) == 0) return k_tab[i].f;
    return 0;
}

#endif /* MBK_GEN_WEBVIEW_BIND_H */
""")

    p = os.path.join(HERE, "gen_webview_bind.h")
    io.open(p, "w", encoding="utf-8", newline="\n").write("".join(out))
    print("gen_webview_bind.h 生成完毕：实现 %d 项 / 声明 %d 项" % (len(IMPL), len(names)))


if __name__ == "__main__":
    main()
