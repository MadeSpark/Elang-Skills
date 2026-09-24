/* ============================================================================
 * mbkit_api.h —— MbKit.dll 公共接口（miniblink 常驻宿主层）
 * ----------------------------------------------------------------------------
 * 设计目标：让「使用 miniblink 的业务 DLL / 易语言模块」可以被反复卸载、重新
 *           加载，而内核与窗口句柄的绑定不会残留野指针。
 *
 * 分层：
 *   宿主 exe ──LoadLibrary 一次，之后不再 Free──▶ MbKit.dll（常驻层）
 *                                                    │ 自己 PIN 在进程里
 *                                                    │ 持有 node.dll 与全部 wke 回调
 *                                                    ▼ 纯 C / __stdcall 接口
 *                              业务 DLL / 易语言模块 ──薄封装── 只注册自己的事件函数
 *
 * 约定：
 *   - 全部导出为 __stdcall，导出名**不带修饰**（编译时 -Wl,--kill-at）。
 *   - 内核 API 符号名以本机 node.dll 的真实导出表为准（306 个 wke* 符号）。
 *   - 业务侧传给 MbKit 的事件函数指针，允许位于「会被卸载的模块」里：
 *     MbKit 每次回调前都会做**模块存活校验**，模块已卸载则直接跳过。
 * ==========================================================================*/
#ifndef MBKIT_API_H
#define MBKIT_API_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 返回码 ---- */
#define MBKIT_OK            0
#define MBKIT_E_GENERIC    -1
#define MBKIT_E_NODLL      -2   /* 找不到 / 加载不了 node.dll */
#define MBKIT_E_SYMBOL     -3   /* node.dll 缺少所需导出 */
#define MBKIT_E_PARAM      -4
#define MBKIT_E_STATE      -5   /* 状态不对（未初始化 / view 已失效） */

/* ---- 事件类型（MbEventCb 的 ev 参数）---- */
#define MB_EV_PAINT        1    /* p1 = MbPaintInfo*
                                 * ⚠️ 实测（2026-09-23，本机 node.dll 2021 build）：
                                 * 视图一旦 wkeSetHandle 绑定到窗口，内核**自己**渲染，
                                 * 从不回调 wkeOnPaintUpdated —— 无窗口 / 离屏 / 披斗篷 /
                                 * 真正可见 四种配置实测 paints 全为 0。
                                 * 所以别把渲染逻辑压在这个事件上。 */
#define MB_EV_LOADED       2    /* p1 = (int)result  0=成功 1=失败 2=取消   ← 会真实触发 */
#define MB_EV_DETACH       9    /* 常驻层正在解绑，业务侧应停止一切渲染 */

/* 绘屏回调信息（MB_EV_PAINT 的 p1） */
typedef struct {
    int          x, y, cx, cy;
    unsigned int hdc;
} MbPaintInfo;

/* 事件回调（业务侧实现，__stdcall）
 *   user —— MbKit_Create 时传入的 user
 *   ev   —— MB_EV_xxx
 *   p1/p2—— 见各事件说明
 */
typedef void (__stdcall *MbEventCb)(void* user, int ev, void* p1, void* p2);

/* --------------------------------------------------------------------------
 * 生命周期
 * ------------------------------------------------------------------------*/

/* 初始化常驻层。nodeDllPathOrNull 非空时等价于先调一次 MbKit_SetNodePath，
 * 再加载内核；传 NULL / 空串时按下列顺序自动查找：
 *   1) 自定义路径（见下方 MbKit_SetNodePath）—— 显式设置即契约，设了没命中就报错
 *   2) <宿主 exe 目录>\node.dll              ← 默认位置（宿主程序根目录）
 *   3) <MbKit.dll 目录>\node.dll             ← 「两个文件放一起」的老布局
 *   4) <宿主 exe 目录 上溯 ≤5 级>\node.dll 与 \main\corn\node.dll
 *   5) <当前工作目录>\node.dll 与 \main\corn\node.dll
 *   6) 裸名 node.dll（走系统搜索序）
 * 幂等：重复调用只返回首次结果。node.dll 会被 PIN 住，**永不卸载**。 */
int          __stdcall MbKit_Init(const char* nodeDllPathOrNull);
/* 常驻层是否已就绪（内核已 initialize） */
int          __stdcall MbKit_IsReady(void);

/* --------------------------------------------------------------------------
 * 自定义内核位置（2026-09-24）
 *
 *   MbKit_SetNodePath(p)  —— p 为目录或 node.dll 完整文件名；空/NULL = 默认
 *                            （宿主程序根目录）。**只在内核尚未加载时接受**，
 *                            成功返回 1；内核已加载/已失败返回 0。
 *                            ★ 不触发内核加载（设置路径通常发生在启动早期）。
 *   MbKit_ConfigNodePath()—— 取当前配置值（未设置时为空串）。
 *   MbKit_KernelLoaded()  —— 内核是否已加载（不会触发加载）。
 *   MbKit_Diag()          —— 一行式自检：为什么/从哪儿进来的内核。
 *
 *   ★ 显式设置即契约：设了路径却没找到 node.dll 会**明确报错**，
 *     不会静默回退到别处 —— 避免"我设了路径却跑了另一个内核"这种难查问题。
 * ------------------------------------------------------------------------*/
int          __stdcall MbKit_SetNodePath(const char* pathOrDirOrNull);
const char*  __stdcall MbKit_ConfigNodePath(void);
int          __stdcall MbKit_KernelLoaded(void);
const char*  __stdcall MbKit_Diag(void);

/* 实际使用的内核全路径 / 版本串 / 最近一次错误 */
const char*  __stdcall MbKit_NodePath(void);
const char*  __stdcall MbKit_Version(void);
const char*  __stdcall MbKit_LastError(void);

/* --------------------------------------------------------------------------
 * WebView
 * ------------------------------------------------------------------------*/

/* 建 WebView 并绑定到窗口。
 *   hwnd    —— 目标窗口（可为 NULL：此时为「无窗口 / 离屏」用法，需自己取绘屏回调）
 *   x,y,w,h —— 位置尺寸（x,y 仅在该内核支持 SetHandleOffset 时生效）
 *   cb,user —— 事件回调，可为 NULL
 * 返回 view 句柄（不透明，可直接当整数用）；失败返回 NULL。 */
void*        __stdcall MbKit_Create(void* hwnd, int x, int y, int w, int h,
                                    MbEventCb cb, void* user);

/* 销毁单个 view（会先解绑回调与窗口，再销毁内核对象） */
int          __stdcall MbKit_Destroy(void* view);

/* ★ 热重载契约：解绑并销毁**全部** view。
 * 业务 DLL 卸载前必须调用。调用后内核保持已初始化（不 finalize），
 * 因此下一次 Attach 是廉价的。返回被清理的 view 数。 */
int          __stdcall MbKit_DetachAll(void);

/* 当前存活 view 数 */
int          __stdcall MbKit_LiveCount(void);

/* --------------------------------------------------------------------------
 * 导航与操作
 * ------------------------------------------------------------------------*/
int          __stdcall MbKit_LoadUrl(void* view, const wchar_t* url);
int          __stdcall MbKit_LoadHtml(void* view, const wchar_t* html);
long long    __stdcall MbKit_RunJs(void* view, const wchar_t* js);
int          __stdcall MbKit_Resize(void* view, int w, int h);
int          __stdcall MbKit_SetZoom(void* view, float factor);
int          __stdcall MbKit_SetHeadless(void* view, int on);
int          __stdcall MbKit_IsLoaded(void* view);
int          __stdcall MbKit_GetTitle(void* view, wchar_t* buf, int cch);
int          __stdcall MbKit_ForcePaint(void* view);

/* ANSI(GBK) 变体 —— 给易语言「文本型」用，免去手工拼 UTF-16 字节集 */
int          __stdcall MbKit_LoadUrlA(void* view, const char* url);
int          __stdcall MbKit_LoadHtmlA(void* view, const char* html);
int          __stdcall MbKit_RunJsA(void* view, const char* js);
int          __stdcall MbKit_GetTitleA(void* view, char* buf, int cch);

/* 抽干当前线程消息队列 ms 毫秒（宿主若无自己的消息循环可用它） */
int          __stdcall MbKit_Pump(int ms);

/* --------------------------------------------------------------------------
 * 诊断（用于自证「内核常驻 / 业务模块可任意卸载」）
 * ------------------------------------------------------------------------*/
unsigned int __stdcall MbKit_NodeModule(void);      /* node.dll 的 HMODULE */
unsigned int __stdcall MbKit_SelfModule(void);      /* MbKit.dll 的 HMODULE */
int          __stdcall MbKit_KernelInitCount(void); /* 真正执行 initialize 的次数 */
int          __stdcall MbKit_Pinned(void);          /* MbKit 是否已自 PIN */
int          __stdcall MbKit_SuppressedCount(void); /* 被存活校验挡下的野回调次数 ★保护生效的直证 */

/* 回调登记层（mbkit_cb.c）逐项计数。
 * ★ 2026-09-25：这几个函数以前只在 mbkit_cb.c 里定义、**头文件没有声明** ——
 *   直接后果是 gen_fwd.py 的导出清单（当时靠手写）漏了它们，
 *   而且任何其它 .c 想调用都编译不过。此处补齐声明，头文件即导出契约。
 * 排查「事件没反应 / 缩放不重绘（撕裂）」时看这四个数：
 *   被挡 = 0 表示没有回调被拦下（正常）；被挡持续增长即为异常。 */
int          __stdcall MbKit_CbCount(void);    /* 当前登记条数 */
int          __stdcall MbKit_CbLive(void);     /* 其中「业务身份仍然有效」的条数 */
int          __stdcall MbKit_CbHits(void);     /* 累计成功转发次数 */
int          __stdcall MbKit_CbBlocked(void);  /* 累计被挡下次数 */
int          __stdcall MbKit_DetachView(void* view);  /* 只解绑某一个 view 上的代理回调 */

/* 模块存活校验（业务事件函数指针是否仍指向已加载模块）
 * 供测试与业务侧自检使用：cbAddr 是函数地址，hOwner 是注册时的模块句柄（可为 0） */
int          __stdcall MbKit_AliveModule(unsigned int cbAddr, unsigned int hOwner);

#ifdef __cplusplus
}
#endif
#endif /* MBKIT_API_H */
