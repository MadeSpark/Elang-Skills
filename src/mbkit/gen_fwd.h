/* 本文件由 gen_fwd.py 自动生成 —— 请勿手改。
 * 数据来源：易语言模块「miniblink」的 .DLL命令 声明表（e2t 产物）。
 * 重新生成：python gen_fwd.py <清单.json>
 */
#ifndef MBK_GEN_FWD_H
#define MBK_GEN_FWD_H

/* _mbk_slots[] 覆盖全部 API（含被 C 侧接管者），下标见 MBK_IDX_* */
#define MBK_ALL_COUNT 256
#define MBK_FWD_COUNT 233   /* 其中走汇编转发桩的 */

/* 由 mbkit_fwd.c 提供 */
extern void* mbk_slots[MBK_ALL_COUNT];
extern int   mbk_ensure(void);

static const char* const mbk_fwd_names[MBK_ALL_COUNT]
    __attribute__((unused)) = {
    "JsBindGetter",
    "JsBindSetter",
    "jsArg",
    "jsArgCount",
    "jsArgType",
    "jsArrayBuffer",
    "jsBindFunction",
    "jsBoolean",
    "jsCall",
    "jsCallGlobal",
    "jsDeleteObjectProp",
    "jsDouble",
    "jsEmptyArray",
    "jsEmptyObject",
    "jsEvalExW",
    "jsEvalW",
    "jsFalse",
    "jsFloat",
    "jsFunction",
    "jsGC",
    "jsGet",
    "jsGetArrayBuffer",
    "jsGetAt",
    "jsGetData",
    "jsGetGlobal",
    "jsGetKeys",
    "jsGetLastErrorIfException",
    "jsGetLength",
    "jsGetWebView",
    "jsGlobalObject",
    "jsInt",
    "jsIsArray",
    "jsIsBoolean",
    "jsIsFalse",
    "jsIsFunction",
    "jsIsJsValueValid",
    "jsIsNull",
    "jsIsNumber",
    "jsIsObject",
    "jsIsString",
    "jsIsTrue",
    "jsIsUndefined",
    "jsIsValidExecState",
    "jsNull",
    "jsObject",
    "jsSet",
    "jsSetAt",
    "jsSetGlobal",
    "jsSetLength",
    "jsStringW",
    "jsThrowException",
    "jsToBoolean",
    "jsToDouble",
    "jsToFloat",
    "jsToInt",
    "jsToStringW",
    "jsToTempStringW",
    "jsTrue",
    "jsTypeOf",
    "jsUndefined",
    "wkeAddDirtyArea",
    "wkeAddPluginDirectory",
    "wkeCanGoBack",
    "wkeCanGoForward",
    "wkeCreateStringW",
    "wkeCreateWebView",
    "wkeCreateWebWindow",
    "wkeDeleteString",
    "wkeDestroyWebView",
    "wkeDestroyWebWindow",
    "wkeEditorCopy",
    "wkeEditorCut",
    "wkeEditorDelete",
    "wkeEditorPaste",
    "wkeEditorRedo",
    "wkeEditorSelectAll",
    "wkeEditorUnSelect",
    "wkeEditorUndo",
    "wkeEnableWindow",
    "wkeFinalize",
    "wkeFireContextMenuEvent",
    "wkeFireKeyDownEvent",
    "wkeFireKeyPressEvent",
    "wkeFireKeyUpEvent",
    "wkeFireMouseEvent",
    "wkeFireMouseWheelEvent",
    "wkeFireWindowsMessage",
    "wkeGC",
    "wkeGetBlinkMainThreadIsolate",
    "wkeGetCaretRect",
    "wkeGetContentHeight",
    "wkeGetContentWidth",
    "wkeGetCookie",
    "wkeGetCookieW",
    "wkeGetCursorInfoType",
    "wkeGetFrameUrl",
    "wkeGetGlobalExecByFrame",
    "wkeGetHeight",
    "wkeGetHostHWND",
    "wkeGetMediaVolume",
    "wkeGetName",
    "wkeGetSource",
    "wkeGetString",
    "wkeGetStringW",
    "wkeGetTitle",
    "wkeGetTitleW",
    "wkeGetURL",
    "wkeGetUserAgent",
    "wkeGetUserKeyValue",
    "wkeGetVersion",
    "wkeGetVersionString",
    "wkeGetViewDC",
    "wkeGetWebView",
    "wkeGetWebViewForCurrentContext",
    "wkeGetWidth",
    "wkeGetWindowHandle",
    "wkeGetZoomFactor",
    "wkeGlobalExec",
    "wkeGoBack",
    "wkeGoForward",
    "wkeInitialize",
    "wkeInitializeEx",
    "wkeInsertCSSByFrame",
    "wkeIsAwake",
    "wkeIsCookieEnabled",
    "wkeIsDirty",
    "wkeIsDocumentReady",
    "wkeIsInitialize",
    "wkeIsLoading",
    "wkeIsLoadingCompleted",
    "wkeIsLoadingFailed",
    "wkeIsLoadingSucceeded",
    "wkeIsMainFrame",
    "wkeIsProcessingUserGesture",
    "wkeIsWebRemoteFrame",
    "wkeJsBindFunction",
    "wkeJsBindGetter",
    "wkeJsBindSetter",
    "wkeKillFocus",
    "wkeLayoutIfNeeded",
    "wkeLoadFileW",
    "wkeLoadHTML",
    "wkeLoadHTMLW",
    "wkeLoadHtmlWithBaseUrl",
    "wkeLoadURL",
    "wkeLoadURLW",
    "wkeLoadW",
    "wkeMoveToCenter",
    "wkeMoveWindow",
    "wkeNetAddHTTPHeaderFieldToUrlRequest",
    "wkeNetCancelRequest",
    "wkeNetCancelWebUrlRequest",
    "wkeNetChangeRequestUrl",
    "wkeNetContinueJob",
    "wkeNetCreateWebUrlRequest",
    "wkeNetGetExpectedContentLength",
    "wkeNetGetFavicon",
    "wkeNetGetHTTPHeaderField",
    "wkeNetGetHttpStatusCode",
    "wkeNetGetMIMEType",
    "wkeNetGetPostBody",
    "wkeNetGetRequestMethod",
    "wkeNetGetResponseUrl",
    "wkeNetGetUrlByJob",
    "wkeNetHoldJobToAsynCommit",
    "wkeNetHookRequest",
    "wkeNetOnResponse",
    "wkeNetSetData",
    "wkeNetSetHTTPHeaderField",
    "wkeNetSetMIMEType",
    "wkeNetSetURL",
    "wkeNetStartUrlRequest",
    "wkeOnAlertBox",
    "wkeOnConfirmBox",
    "wkeOnConsole",
    "wkeOnCreateView",
    "wkeOnDidCreateScriptContext",
    "wkeOnDocumentReady",
    "wkeOnDocumentReady2",
    "wkeOnDownload",
    "wkeOnDownloadFinish",
    "wkeOnDownloadProgress",
    "wkeOnDownloadStart",
    "wkeOnLoadUrlBegin",
    "wkeOnLoadUrlEnd",
    "wkeOnLoadingFinish",
    "wkeOnMouseOverUrlChanged",
    "wkeOnNavigation",
    "wkeOnPaintUpdated",
    "wkeOnPromptBox",
    "wkeOnTitleChanged",
    "wkeOnURLChanged",
    "wkeOnURLChanged2",
    "wkeOnWillMediaLoad",
    "wkeOnWillReleaseScriptContext",
    "wkeOnWindowClosing",
    "wkeOnWindowDestroy",
    "wkePaint",
    "wkePaint2",
    "wkePerformCookieCommand",
    "wkePostURL",
    "wkePostURLW",
    "wkeReload",
    "wkeRepaintIfNeeded",
    "wkeResize",
    "wkeResizeWindow",
    "wkeRunJS",
    "wkeRunJSW",
    "wkeRunJsByFrame",
    "wkeSetCookie",
    "wkeSetCookieEnabled",
    "wkeSetCookieJarFullPath",
    "wkeSetCookieJarPath",
    "wkeSetCspCheckEnable",
    "wkeSetDebugConfig",
    "wkeSetDeviceParameter",
    "wkeSetDirty",
    "wkeSetDownloadDir",
    "wkeSetDragDropEnable",
    "wkeSetDragEnable",
    "wkeSetDragFiles",
    "wkeSetEditable",
    "wkeSetFileSystem",
    "wkeSetFocus",
    "wkeSetHandle",
    "wkeSetHandleOffset",
    "wkeSetHeadlessEnabled",
    "wkeSetLocalStorageFullPath",
    "wkeSetMediaVolume",
    "wkeSetMemoryCacheEnable",
    "wkeSetMouseEnabled",
    "wkeSetName",
    "wkeSetNavigationToNewWindowEnable",
    "wkeSetNpapiPluginsEnabled",
    "wkeSetProxy",
    "wkeSetResourceGc",
    "wkeSetStringW",
    "wkeSetTouchEnabled",
    "wkeSetTransparent",
    "wkeSetUserAgent",
    "wkeSetUserAgentW",
    "wkeSetUserKeyValue",
    "wkeSetViewNetInterface",
    "wkeSetViewProxy",
    "wkeSetWindowTitleW",
    "wkeSetZoomFactor",
    "wkeShowDevtools",
    "wkeShowWindow",
    "wkeSleep",
    "wkeStopLoading",
    "wkeUpdate",
    "wkeUtilSerializeToMHTML",
    "wkeVisitAllCookie",
    "wkeWake",
    "wkeWebFrameGetMainFrame",
    "wkeWebFrameGetMainWorldScriptContext",
};

/* 名字 -> _mbk_slots 下标（拦截层用它取内核真函数） */
#define MBK_IDX_JsBindGetter                           0
#define MBK_IDX_JsBindSetter                           1
#define MBK_IDX_jsArg                                  2
#define MBK_IDX_jsArgCount                             3
#define MBK_IDX_jsArgType                              4
#define MBK_IDX_jsArrayBuffer                          5
#define MBK_IDX_jsBindFunction                         6
#define MBK_IDX_jsBoolean                              7
#define MBK_IDX_jsCall                                 8
#define MBK_IDX_jsCallGlobal                           9
#define MBK_IDX_jsDeleteObjectProp                    10
#define MBK_IDX_jsDouble                              11
#define MBK_IDX_jsEmptyArray                          12
#define MBK_IDX_jsEmptyObject                         13
#define MBK_IDX_jsEvalExW                             14
#define MBK_IDX_jsEvalW                               15
#define MBK_IDX_jsFalse                               16
#define MBK_IDX_jsFloat                               17
#define MBK_IDX_jsFunction                            18
#define MBK_IDX_jsGC                                  19
#define MBK_IDX_jsGet                                 20
#define MBK_IDX_jsGetArrayBuffer                      21
#define MBK_IDX_jsGetAt                               22
#define MBK_IDX_jsGetData                             23
#define MBK_IDX_jsGetGlobal                           24
#define MBK_IDX_jsGetKeys                             25
#define MBK_IDX_jsGetLastErrorIfException             26
#define MBK_IDX_jsGetLength                           27
#define MBK_IDX_jsGetWebView                          28
#define MBK_IDX_jsGlobalObject                        29
#define MBK_IDX_jsInt                                 30
#define MBK_IDX_jsIsArray                             31
#define MBK_IDX_jsIsBoolean                           32
#define MBK_IDX_jsIsFalse                             33
#define MBK_IDX_jsIsFunction                          34
#define MBK_IDX_jsIsJsValueValid                      35
#define MBK_IDX_jsIsNull                              36
#define MBK_IDX_jsIsNumber                            37
#define MBK_IDX_jsIsObject                            38
#define MBK_IDX_jsIsString                            39
#define MBK_IDX_jsIsTrue                              40
#define MBK_IDX_jsIsUndefined                         41
#define MBK_IDX_jsIsValidExecState                    42
#define MBK_IDX_jsNull                                43
#define MBK_IDX_jsObject                              44
#define MBK_IDX_jsSet                                 45
#define MBK_IDX_jsSetAt                               46
#define MBK_IDX_jsSetGlobal                           47
#define MBK_IDX_jsSetLength                           48
#define MBK_IDX_jsStringW                             49
#define MBK_IDX_jsThrowException                      50
#define MBK_IDX_jsToBoolean                           51
#define MBK_IDX_jsToDouble                            52
#define MBK_IDX_jsToFloat                             53
#define MBK_IDX_jsToInt                               54
#define MBK_IDX_jsToStringW                           55
#define MBK_IDX_jsToTempStringW                       56
#define MBK_IDX_jsTrue                                57
#define MBK_IDX_jsTypeOf                              58
#define MBK_IDX_jsUndefined                           59
#define MBK_IDX_wkeAddDirtyArea                       60
#define MBK_IDX_wkeAddPluginDirectory                 61
#define MBK_IDX_wkeCanGoBack                          62
#define MBK_IDX_wkeCanGoForward                       63
#define MBK_IDX_wkeCreateStringW                      64
#define MBK_IDX_wkeCreateWebView                      65
#define MBK_IDX_wkeCreateWebWindow                    66
#define MBK_IDX_wkeDeleteString                       67
#define MBK_IDX_wkeDestroyWebView                     68
#define MBK_IDX_wkeDestroyWebWindow                   69
#define MBK_IDX_wkeEditorCopy                         70
#define MBK_IDX_wkeEditorCut                          71
#define MBK_IDX_wkeEditorDelete                       72
#define MBK_IDX_wkeEditorPaste                        73
#define MBK_IDX_wkeEditorRedo                         74
#define MBK_IDX_wkeEditorSelectAll                    75
#define MBK_IDX_wkeEditorUnSelect                     76
#define MBK_IDX_wkeEditorUndo                         77
#define MBK_IDX_wkeEnableWindow                       78
#define MBK_IDX_wkeFinalize                           79
#define MBK_IDX_wkeFireContextMenuEvent               80
#define MBK_IDX_wkeFireKeyDownEvent                   81
#define MBK_IDX_wkeFireKeyPressEvent                  82
#define MBK_IDX_wkeFireKeyUpEvent                     83
#define MBK_IDX_wkeFireMouseEvent                     84
#define MBK_IDX_wkeFireMouseWheelEvent                85
#define MBK_IDX_wkeFireWindowsMessage                 86
#define MBK_IDX_wkeGC                                 87
#define MBK_IDX_wkeGetBlinkMainThreadIsolate          88
#define MBK_IDX_wkeGetCaretRect                       89
#define MBK_IDX_wkeGetContentHeight                   90
#define MBK_IDX_wkeGetContentWidth                    91
#define MBK_IDX_wkeGetCookie                          92
#define MBK_IDX_wkeGetCookieW                         93
#define MBK_IDX_wkeGetCursorInfoType                  94
#define MBK_IDX_wkeGetFrameUrl                        95
#define MBK_IDX_wkeGetGlobalExecByFrame               96
#define MBK_IDX_wkeGetHeight                          97
#define MBK_IDX_wkeGetHostHWND                        98
#define MBK_IDX_wkeGetMediaVolume                     99
#define MBK_IDX_wkeGetName                           100
#define MBK_IDX_wkeGetSource                         101
#define MBK_IDX_wkeGetString                         102
#define MBK_IDX_wkeGetStringW                        103
#define MBK_IDX_wkeGetTitle                          104
#define MBK_IDX_wkeGetTitleW                         105
#define MBK_IDX_wkeGetURL                            106
#define MBK_IDX_wkeGetUserAgent                      107
#define MBK_IDX_wkeGetUserKeyValue                   108
#define MBK_IDX_wkeGetVersion                        109
#define MBK_IDX_wkeGetVersionString                  110
#define MBK_IDX_wkeGetViewDC                         111
#define MBK_IDX_wkeGetWebView                        112
#define MBK_IDX_wkeGetWebViewForCurrentContext       113
#define MBK_IDX_wkeGetWidth                          114
#define MBK_IDX_wkeGetWindowHandle                   115
#define MBK_IDX_wkeGetZoomFactor                     116
#define MBK_IDX_wkeGlobalExec                        117
#define MBK_IDX_wkeGoBack                            118
#define MBK_IDX_wkeGoForward                         119
#define MBK_IDX_wkeInitialize                        120
#define MBK_IDX_wkeInitializeEx                      121
#define MBK_IDX_wkeInsertCSSByFrame                  122
#define MBK_IDX_wkeIsAwake                           123
#define MBK_IDX_wkeIsCookieEnabled                   124
#define MBK_IDX_wkeIsDirty                           125
#define MBK_IDX_wkeIsDocumentReady                   126
#define MBK_IDX_wkeIsInitialize                      127
#define MBK_IDX_wkeIsLoading                         128
#define MBK_IDX_wkeIsLoadingCompleted                129
#define MBK_IDX_wkeIsLoadingFailed                   130
#define MBK_IDX_wkeIsLoadingSucceeded                131
#define MBK_IDX_wkeIsMainFrame                       132
#define MBK_IDX_wkeIsProcessingUserGesture           133
#define MBK_IDX_wkeIsWebRemoteFrame                  134
#define MBK_IDX_wkeJsBindFunction                    135
#define MBK_IDX_wkeJsBindGetter                      136
#define MBK_IDX_wkeJsBindSetter                      137
#define MBK_IDX_wkeKillFocus                         138
#define MBK_IDX_wkeLayoutIfNeeded                    139
#define MBK_IDX_wkeLoadFileW                         140
#define MBK_IDX_wkeLoadHTML                          141
#define MBK_IDX_wkeLoadHTMLW                         142
#define MBK_IDX_wkeLoadHtmlWithBaseUrl               143
#define MBK_IDX_wkeLoadURL                           144
#define MBK_IDX_wkeLoadURLW                          145
#define MBK_IDX_wkeLoadW                             146
#define MBK_IDX_wkeMoveToCenter                      147
#define MBK_IDX_wkeMoveWindow                        148
#define MBK_IDX_wkeNetAddHTTPHeaderFieldToUrlRequest 149
#define MBK_IDX_wkeNetCancelRequest                  150
#define MBK_IDX_wkeNetCancelWebUrlRequest            151
#define MBK_IDX_wkeNetChangeRequestUrl               152
#define MBK_IDX_wkeNetContinueJob                    153
#define MBK_IDX_wkeNetCreateWebUrlRequest            154
#define MBK_IDX_wkeNetGetExpectedContentLength       155
#define MBK_IDX_wkeNetGetFavicon                     156
#define MBK_IDX_wkeNetGetHTTPHeaderField             157
#define MBK_IDX_wkeNetGetHttpStatusCode              158
#define MBK_IDX_wkeNetGetMIMEType                    159
#define MBK_IDX_wkeNetGetPostBody                    160
#define MBK_IDX_wkeNetGetRequestMethod               161
#define MBK_IDX_wkeNetGetResponseUrl                 162
#define MBK_IDX_wkeNetGetUrlByJob                    163
#define MBK_IDX_wkeNetHoldJobToAsynCommit            164
#define MBK_IDX_wkeNetHookRequest                    165
#define MBK_IDX_wkeNetOnResponse                     166
#define MBK_IDX_wkeNetSetData                        167
#define MBK_IDX_wkeNetSetHTTPHeaderField             168
#define MBK_IDX_wkeNetSetMIMEType                    169
#define MBK_IDX_wkeNetSetURL                         170
#define MBK_IDX_wkeNetStartUrlRequest                171
#define MBK_IDX_wkeOnAlertBox                        172
#define MBK_IDX_wkeOnConfirmBox                      173
#define MBK_IDX_wkeOnConsole                         174
#define MBK_IDX_wkeOnCreateView                      175
#define MBK_IDX_wkeOnDidCreateScriptContext          176
#define MBK_IDX_wkeOnDocumentReady                   177
#define MBK_IDX_wkeOnDocumentReady2                  178
#define MBK_IDX_wkeOnDownload                        179
#define MBK_IDX_wkeOnDownloadFinish                  180
#define MBK_IDX_wkeOnDownloadProgress                181
#define MBK_IDX_wkeOnDownloadStart                   182
#define MBK_IDX_wkeOnLoadUrlBegin                    183
#define MBK_IDX_wkeOnLoadUrlEnd                      184
#define MBK_IDX_wkeOnLoadingFinish                   185
#define MBK_IDX_wkeOnMouseOverUrlChanged             186
#define MBK_IDX_wkeOnNavigation                      187
#define MBK_IDX_wkeOnPaintUpdated                    188
#define MBK_IDX_wkeOnPromptBox                       189
#define MBK_IDX_wkeOnTitleChanged                    190
#define MBK_IDX_wkeOnURLChanged                      191
#define MBK_IDX_wkeOnURLChanged2                     192
#define MBK_IDX_wkeOnWillMediaLoad                   193
#define MBK_IDX_wkeOnWillReleaseScriptContext        194
#define MBK_IDX_wkeOnWindowClosing                   195
#define MBK_IDX_wkeOnWindowDestroy                   196
#define MBK_IDX_wkePaint                             197
#define MBK_IDX_wkePaint2                            198
#define MBK_IDX_wkePerformCookieCommand              199
#define MBK_IDX_wkePostURL                           200
#define MBK_IDX_wkePostURLW                          201
#define MBK_IDX_wkeReload                            202
#define MBK_IDX_wkeRepaintIfNeeded                   203
#define MBK_IDX_wkeResize                            204
#define MBK_IDX_wkeResizeWindow                      205
#define MBK_IDX_wkeRunJS                             206
#define MBK_IDX_wkeRunJSW                            207
#define MBK_IDX_wkeRunJsByFrame                      208
#define MBK_IDX_wkeSetCookie                         209
#define MBK_IDX_wkeSetCookieEnabled                  210
#define MBK_IDX_wkeSetCookieJarFullPath              211
#define MBK_IDX_wkeSetCookieJarPath                  212
#define MBK_IDX_wkeSetCspCheckEnable                 213
#define MBK_IDX_wkeSetDebugConfig                    214
#define MBK_IDX_wkeSetDeviceParameter                215
#define MBK_IDX_wkeSetDirty                          216
#define MBK_IDX_wkeSetDownloadDir                    217
#define MBK_IDX_wkeSetDragDropEnable                 218
#define MBK_IDX_wkeSetDragEnable                     219
#define MBK_IDX_wkeSetDragFiles                      220
#define MBK_IDX_wkeSetEditable                       221
#define MBK_IDX_wkeSetFileSystem                     222
#define MBK_IDX_wkeSetFocus                          223
#define MBK_IDX_wkeSetHandle                         224
#define MBK_IDX_wkeSetHandleOffset                   225
#define MBK_IDX_wkeSetHeadlessEnabled                226
#define MBK_IDX_wkeSetLocalStorageFullPath           227
#define MBK_IDX_wkeSetMediaVolume                    228
#define MBK_IDX_wkeSetMemoryCacheEnable              229
#define MBK_IDX_wkeSetMouseEnabled                   230
#define MBK_IDX_wkeSetName                           231
#define MBK_IDX_wkeSetNavigationToNewWindowEnable    232
#define MBK_IDX_wkeSetNpapiPluginsEnabled            233
#define MBK_IDX_wkeSetProxy                          234
#define MBK_IDX_wkeSetResourceGc                     235
#define MBK_IDX_wkeSetStringW                        236
#define MBK_IDX_wkeSetTouchEnabled                   237
#define MBK_IDX_wkeSetTransparent                    238
#define MBK_IDX_wkeSetUserAgent                      239
#define MBK_IDX_wkeSetUserAgentW                     240
#define MBK_IDX_wkeSetUserKeyValue                   241
#define MBK_IDX_wkeSetViewNetInterface               242
#define MBK_IDX_wkeSetViewProxy                      243
#define MBK_IDX_wkeSetWindowTitleW                   244
#define MBK_IDX_wkeSetZoomFactor                     245
#define MBK_IDX_wkeShowDevtools                      246
#define MBK_IDX_wkeShowWindow                        247
#define MBK_IDX_wkeSleep                             248
#define MBK_IDX_wkeStopLoading                       249
#define MBK_IDX_wkeUpdate                            250
#define MBK_IDX_wkeUtilSerializeToMHTML              251
#define MBK_IDX_wkeVisitAllCookie                    252
#define MBK_IDX_wkeWake                              253
#define MBK_IDX_wkeWebFrameGetMainFrame              254
#define MBK_IDX_wkeWebFrameGetMainWorldScriptContext 255

#define MBK_INTERCEPT_COUNT 23
/* 内核真函数取值宏 */
#define MBK_REAL(idx) (mbk_slots[idx])

#endif
