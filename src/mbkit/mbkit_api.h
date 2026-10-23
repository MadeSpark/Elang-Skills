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
/* 调试文件开关：默认关闭。置为 1 后才允许创建 mbkit-trace、mbkit-webview、mb_veh 文件。 */
int          __stdcall MbKit_SetDebug(int on);
int          __stdcall MbKit_DebugEnabled(void);
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
/* 内核原始状态字：0=尚未加载 1=已就绪 2=已失败 0x7FFFFFFF=正在加载（诊断用） */
int          __stdcall MbKit_KernelState(void);
const char*  __stdcall MbKit_Diag(void);

/* --------------------------------------------------------------------------
 * edgewebview（WebView2）内核（v1.4.5）
 *
 *   内核选择规则（见 mbkit_fwd.c 的 bind_all）：
 *     1) 强制使用node内核 = 真                        → 一定走 node.dll
 *     2) 本机检测到 WebView2 Runtime（且未强制 node）  → 走 edgewebview（默认）
 *     3) 都不满足 / edgewebview 起不来                 → 走 node.dll（原路回退）
 *
 *   MbKit_HasWebViewEnv()     —— 本机是否装了 WebView2 Runtime（1=有）。只读探测。
 *   MbKit_KernelMode()        —— 当前实际内核：0=尚未加载 1=node.dll 2=edgewebview
 *   MbKit_SetForceNodeKernel()—— 「强制使用node内核」开关。只在内核尚未加载时
 *                                接受修改；加载后只能查询（返回 0）。
 *   MbKit_WebViewDiag()       —— edgewebview 的一行式诊断（Runtime 版本 / 启动结果）。
 * ------------------------------------------------------------------------*/
int          __stdcall MbKit_HasWebViewEnv(void);
int          __stdcall MbKit_KernelMode(void);
int          __stdcall MbKit_SetForceNodeKernel(int on);
const char*  __stdcall MbKit_WebViewDiag(void);

/* --------------------------------------------------------------------------
 * 内核启动：**异步**（2026-10-01）
 *
 *   MbKit_EnsureKernelInit() —— 只把「启动内核」投递到常驻线程 K 就返回，
 *                               **绝不阻塞调用方**（可安全地用在 DllMain /
 *                               宿主的 UI 线程里；同步等 LoadLibrary(node.dll)
 *                               在持有加载锁时会死锁）。
 *   MbKit_WaitKernel(ms)     —— 等启动完成；等待期间**抽本线程消息队列**
 *                               （见 mbk_thread.c 的 mbk_wait_pump），避免
 *                               「宿主线程被挡住、内核又要它处理窗口消息」的互等。
 *                               ms<=0 = 默认 20s。返回 MBKIT_OK / 失败码。
 *   MbKit_BootState()        —— 0=未启动 1=启动中 2=完成 3=失败（诊断用）
 *   MbKit_PumpCount()        —— 编组等待期间抽掉的消息数（诊断；>0 说明确实
 *                               在「边等边抽消息」，即该死锁场景被化解了）
 *   MbKit_WaitTimeouts()     —— 编组等待超时次数（诊断；正常恒为 0）
 * ------------------------------------------------------------------------*/
int          __stdcall MbKit_EnsureKernelInit(void);
int          __stdcall MbKit_WaitKernel(int ms);
int          __stdcall MbKit_BootState(void);
int          __stdcall MbKit_BootWaits(void);
/* 内部（**不导出**：gen_fwd.def 未列出）：从 wkeInitialize 返回至今的毫秒数；
 * 尚未初始化返回 -1。仅供编组层诊断用，回答「首次 load 时内核是否还没 warm」。 */
int          mbk_owner_init_ms(void);
int          __stdcall MbKit_PumpCount(void);
int          __stdcall MbKit_WaitTimeouts(void);

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

/* --------------------------------------------------------------------------
 * ★ v1.4.2 崩溃探针（卸载窗口取证）
 *
 * 场景：宿主热重载插件 = FreeLibrary(旧) → LoadLibrary(新)。两次之间那一小段
 * 「空窗期」里，只要有代码指针指向**已被卸载的映像**，就是访问违例 —— 而宿主
 * 自己的致命错误处理会直接把进程收掉（表现为 ExitCode=0、日志断在半句），
 * 什么都留不下。MbKit 已自钉（PIN），业务 DLL 卸载后它仍在进程里，于是由它
 * 挂一个向量异常处理器，把崩溃点、故障地址所在模块、调用栈写进日志。
 *
 * 用法：业务侧在**触发重载之前**调一次，然后去看返回的文件路径。
 * ------------------------------------------------------------------------*/
const char*  __stdcall MbKit_InstallVehProbe(void);   /* 返回日志全路径（空串=失败） */
int          __stdcall MbKit_VehHits(void);           /* 探针累计捕获次数 */
int          __stdcall MbKit_InstallMsgTrace(void);   /* WH_GETMESSAGE 跟踪当前线程消息 */

/* ── 热重载安全（v1.4.2）─────────────────────────────────────────────
 * 宿主 `重载自身` = FreeLibrary(旧插件) → LoadLibrary(新插件)，两次之间有空窗
 * 期。只要还有指针指向旧映像（窗口过程 / 内核回调 / 宿主内部记下的地址），跳
 * 进去就是访问违例，宿主致命错误处理直接收掉进程（ExitCode=0，日志断在半句）。
 * 从插件侧无法保证那些指针都被清干净（内核、user32、宿主代码都可能记着），
 * 但可以让**那段映像不被卸载**：钉住（PIN = 加一个引用）之后 FreeLibrary 只减
 * 引用计数，映像留在进程里 ⇒ 所有悬挂指针仍指向有效代码 ⇒ 空窗期不再有命案。
 * 代价（有意为之）：每次重载多留一份旧映像（约几 MB），进程退出才回收。
 * 用法：在**宿主可能卸载本插件之前**调用一次（例如重载按钮的第一行）。
 * 返回：非 0 = 被钉住的模块句柄；0 = 失败（调用点所在模块无法反查）。 */
unsigned int __stdcall MbKit_PinCaller(void);               /* 钉住「调用者」所在模块（=本插件） */
unsigned int __stdcall MbKit_PinAddress(unsigned int addr); /* 钉住指定地址所属模块 */

/* ── 僵尸窗口清理（v1.4.2，热重载命案的直接凶手）──────────────────────
 * 插件的按钮/窗体，窗口过程编译在插件 DLL 自己的映像里。宿主 `重载自身` 卸载
 * 插件映像时**并不销毁插件的窗口** ⇒ 窗口还挂在窗口链上，其窗口过程却已落在被
 * 释放的代码页里。此后任何消息（miniblink 离屏重绘 tick 约 16ms 一次）被 user32
 * 派发给它，就是「跳进已卸载代码」⇒ 访问违例 ⇒ 宿主致命错误处理收掉进程。
 * 本命令枚举本进程所有窗口，把**窗口过程落在 MEM_FREE 区域**（映像确实已卸载、
 * 过程永远不可能再正确执行）的窗口销毁掉；落在已提交内存里的一律不动。
 * 建议在**重载之前**调用一次。返回被销毁的僵尸窗口数。 */
int __stdcall MbKit_KillZombieWindows(void);
int __stdcall MbKit_ZombieScanned(void);      /* 上次扫描到的窗口总数 */

/* ── 销毁「窗口过程还在调用者映像里」的窗口（v1.4.2，热重载的真正解药）──
 * 探针实测：插件的按钮窗口过程就在插件 DLL 映像里
 * （`Button 标题=重载插件 [proc=<插件 md5 名>.dll+0xF0E84]`）。插件映像被卸载后，
 * 这些窗口就是下一次消息派发的坟墓。本命令用返回地址反查**调用者所在模块**的映像
 * 范围（即本插件 DLL），把窗口过程落在该范围内的窗口全部销毁 —— 卸载前调一次即可。
 * 返回被销毁的窗口数；找不到调用者模块返回 -1。 */
int __stdcall MbKit_KillCallerWindows(void);
int __stdcall MbKit_CallerWindowScanned(void);  /* 上次扫描到的窗口总数 */

/* 诊断：回调代理因「回调地址已经不在内存里（插件映像被宿主卸载）」而拦下的次数。
 * 热重载/卸载发生时它会立刻从 0 上涨 —— 这正是旧版「每 128 次才查一次」漏掉的路径。 */
int __stdcall MbKit_CbRegionBlocked(void);

#ifdef __cplusplus
}
#endif
#endif /* MBKIT_API_H */
