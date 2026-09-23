# 方案：miniblink 热重载 —— MbKit 常驻宿主层（2026-09-23）

> 用户场景：宿主 exe 载入「使用 miniblink 的业务 DLL」→ 业务 DLL 初始化内核并与窗口句柄
> 绑定 → 销毁 + 卸载 + 重新载入业务 DLL → **打不开**，必须结束宿主进程重启。
>
> 本文给出：**确定性复现** + **真因** + **已在同一套压测下验证通过的解法**。
>
> 约束：**不改用户项目「依赖统一放 `<工程>\main\corn\`」的约定**。内核仍从
> `main\corn\node.dll` 加载。

---

## 0. 结论

| | 旧做法（现状） | 新做法（MbKit） |
|---|---|---|
| 5 轮热重载压测 | **崩溃** | **20 通过 / 0 失败**，退出码 0 |
| 崩溃/失败点 | 卸载后**第一条窗口消息**就段错误（退出码 139） | 无 |
| 确定性 | 3/3 必崩，都停在同一行 | 3/3 全过 |
| 内核 initialize 次数 | — | **全程 1 次**（重载几乎零成本） |

**解法一句话**：把「内核 + 全部回调 + 窗口交互」搬进一个**自 PIN 的常驻层 `MbKit.dll`**，
业务 DLL 退化成薄封装、可以随便卸载重载。

---

## 1. 确定性复现（对照实验）

### 1.1 复刻了什么

`src/mbkit/mb_legacy.c` **逐条复刻**用户 `miniblink.e` 的写法：

| 用户源码的做法 | 对照 DLL 里的复刻 |
|---|---|
| `.DLL命令 ..., "main\corn\node.dll", "@wkeXXX"` 相对路径 | `LoadLibraryA("main\\corn\\node.dll")` |
| `WKE初始化` 里 `如果真 (wkeIsInitialize () ＝ 0)` 守卫 | `if (!f_IsInitialize()) { f_Initialize(); }` |
| `wkeOnPaintUpdated (m_WebView, &回调, 窗口句柄)`，回调在业务 DLL 内 | 同左（`legacy_paint`） |
| `_子类化`：`SetPropA(hwnd,"wkeEThis",…)` + `SetWindowLongW(…GWLP_WNDPROC…)` | `subclass_window()` |
| 恢复子类化是**条件式**（`如果真 (IsWindow(窗口) ≠ 0)`） | `unsubclass_window()` 里 `if (!IsWindow(h)) return;` |
| `销毁` 里**从不**调 `WKE卸载` | `Legacy_Detach(0)` 什么都不做 |

### 1.2 结果（3 次重复，全部一致）

```
$ bash src/mbkit/run_tests.sh
  legacy #1                exit=139  最后一行: 消息 1 ...
  legacy #2                exit=139  最后一行: 消息 1 ...
  legacy #3                exit=139  最后一行: 消息 1 ...
```

**退出码 139 = SIGSEGV**。三次都精确停在「卸载完成后发出的第一条窗口消息」——
也就是窗口过程链跳进了**已被释放的代码页**。

证据：`re/mbkit_run/evidence/ev_6_legacy_crash_{1,2,3}.log`（各 998 B / 24 行，内容一致）。

---

## 2. 真因

### 2.1 一个反直觉、但必须先纠正的事实

> ⚠️ **`wkeOnPaintUpdated` 在视图绑定窗口后根本不触发。**

我用 `paintprobe` 模式测绘了四种配置（证据 `ev_3_paintprobe.log`）：

| 配置 | 载入完成 | paints |
|---|---|---|
| 无窗口（headless） | ✅ | **0** |
| 屏幕外窗口（不披斗篷） | ✅ | **0** |
| 屏幕内窗口 + `DWMWA_CLOAK` | ✅ | **0** |
| **屏幕内、真正可见的窗口** | ✅ | **0** |

四种全是 0。内核用 `wkeSetHandle` 绑定窗口后**自己渲染**，从不回调应用的绘屏函数。

**这条事实的价值**：它排除了一个错误的排查方向（「绘屏回调是野指针来源」），
也说明**不能拿「绘屏回调不崩」当成功判据**（我第一版测试就踩了这个坑：3 秒绘屏风暴
根本没走到回调，判据是空的，已废弃）。真正会走到「内核 → 业务 DLL」的路径是下面两条。

### 2.2 两个真实的野指针来源

**来源甲：窗口子类化残留（本次崩溃的现场）**

```易语言
.如果真 (IsWindow (局部变量1) ≠ 0)      ← 窗口先销毁 / 直接卸载 ⇒ 整段跳过
    SetWindowLongW (局部变量1, GWLP_WNDPROC, 旧值)
    RemovePropA (局部变量1, "wkeEThis")
.如果真结束
```

业务 DLL 的窗口过程被装进窗口链后一直有效。DLL 一卸载，**任何一条窗口消息**都会让
系统跳到那块已释放的代码页 ⇒ SIGSEGV。这就是「必须重启宿主 exe」的直接机制。

**来源乙：注册进内核的回调（真实会触发的是「载入完成」）**

`wkeOnLoadingFinish` 在每次页面载入结束时**必定**被调用（实测 `Biz_LoadCount=1`）。
若它的实现放在业务 DLL 里，卸载后下一次载入就是死。

**来源丙：`node.dll` 常驻，`wkeFinalize` 又无效**

`node.dll` 因加载方式与引用计数常驻进程（实测 `GetModuleHandleEx` 一直有效）；
而 `wkeFinalize()` 之后 `wkeIsInitialize()` **仍返回 1**（上一轮已实测）。
⇒ 内核全局状态跨 DLL 重载存活，旧状态（含旧回调）会一直留着。

---

## 3. 解法：MbKit 常驻宿主层

### 3.1 分层

```
宿主 exe
   │  LoadLibrary 一次，之后不再 Free
   ▼
MbKit.dll          ★ 常驻层
   │   · 自我 PIN（GET_MODULE_HANDLE_EX_FLAG_PIN）—— 业务 DLL 怎么卸载都动不了它
   │   · LoadLibrary + PIN 住 main\corn\node.dll（路径约定不变）
   │   · 唯一进入内核的回调（paint / loading-finish）都定义在**本模块内**
   │   · 每次转发前做「业务模块存活校验」，模块已卸载直接跳过
   │   · 不自己子类化窗口 —— 窗口交互交给内核自己，本层不在窗口上留任何过程
   ▼  纯 C / __stdcall / 29 个朴素导出
业务 DLL / 易语言模块      ← 薄封装，可任意卸载、重新载入
   只调 MbKit 接口 + 注册自己的事件函数
```

### 3.2 热重载契约

```
Attach  : MbKit_Init → MbKit_Create(hwnd, …) → MbKit_LoadHtml/LoadUrl
Detach  : MbKit_DetachAll()        ← 注销全部回调 + 解绑窗口 + 销毁全部 view
卸载    : FreeLibrary(业务 DLL)
重载    : 载入新版业务 DLL → 重新 Attach
```

`MbKit_DetachAll()` 之后内核**保持已初始化**（刻意不 finalize）⇒ 重载近乎零成本，
实测 5 轮下来 `kernel-init-count` 始终是 **1**。

### 3.3 存活校验（三重）

业务侧事件函数可能位于会被卸载的模块。每次转发前：

1. `GetModuleHandleExA(FROM_ADDRESS, cb)` —— 该地址还属于某个已加载模块吗？
2. 句柄是否与注册时一致？（防基址被别的模块占用）
3. 该模块**全路径**是否与注册时一致？（防同基址换了个模块）

任一不成立 ⇒ 判定野指针，**跳过调用并计数**（`MbKit_SuppressedCount`）。

### 3.4 内核路径解析（不动你的约定）

`MbKit_Init(NULL)` 时的查找顺序：

1. 环境变量 `MBKIT_NODE_DLL`
2. `<当前工作目录>\main\corn\node.dll` ← **与现有 `.DLL命令` 相对路径的行为一致**
3. `<宿主 exe 目录 及上溯 1~4 级>\main\corn\node.dll`
4. `<MbKit.dll 目录 及上溯 1~2 级>\main\corn\node.dll`
5. 裸名 `node.dll`（系统搜索序）

实测命中：`C:\Users\...\测试\main\corn\node.dll`（就是工作区里你复制的那份）。

---

## 4. 实测结果

内核：`node.dll` 33,600,512 B / x86 / 映像基址 `0x10000000` / **306 个 `wke*` 导出**
版本串：`wke258 wke version 1.02  blink build 98096  build time Wed Mar 10 09:29:26 2021`

| 模式 | 目的 | 结果 | 退出码 | 证据 |
|---|---|---|---|---|
| `wildcard` | 自证「卸载后回调地址失效」可被检测 | 3 / 0 | 0 | `ev_1_wildcard.log` |
| `ansi` | ANSI(GBK) 变体接口（中文往返 + JS） | 3 / 0 | 0 | `ev_2_ansi.log` |
| `paintprobe` | 测绘绘屏回调触发条件 | 四配置 paints 全 0 | 0 | `ev_3_paintprobe.log` |
| **`mbkit`** | **新架构 5 轮热重载** | **20 / 0** | **0** | `ev_4_mbkit_5rounds.log` |
| **`fault`** | **业务层忘记 Detach 就直接卸载** | **8 / 0** | **0** | `ev_5_fault_injection.log` |
| `legacy` | 旧做法对照 | 3/3 **崩溃** | **139** | `ev_6_legacy_crash_*.log` |
| `mbkit` ×3 | 稳定性复核 | 3 × (8 / 0) | 0/0/0 | `ev_7_mbkit_repeat_*.log` |

### 4.1 新架构 5 轮的关键读数

```
round 1/5  title="ROUND-1"  kit.dll=0x50090000  node.dll=0x781E0000  kernel-init-count=1
round 2/5  title="ROUND-2"  …(句柄全部未变)…                          kernel-init-count=1
round 3/5  title="ROUND-3"  …                                        kernel-init-count=1
round 4/5  title="ROUND-4"  …                                        kernel-init-count=1
round 5/5  title="ROUND-5"  …                                        kernel-init-count=1
SUMMARY  mbkit :  20 passed / 0 failed
```

每轮都 `LoadLibrary` 一个**不同基址**的业务 DLL（`biz_v1.dll`=0x62000000 /
`biz_v2.dll`=0x63000000，模拟「重新编译后落到不同地址」这一最坏情况），
`node.dll` 与 `MbKit.dll` 的模块句柄**始终不变**。

### 4.2 故障注入（最坏情况）的关键读数

```
step 1  Biz_LoadCount=1                          ← 确认「载入完成回调」真会触发
step 2  biz_on_event=0x77FD1480  view=500A748C
step 3  FreeLibrary(biz_v1)  ← ★ 故意不调 Detach
        卸载后：该地址已不属于任何已加载模块 => 内核手里的它就是野指针
路径甲  40 轮窗口消息后进程仍存活                 ← 常驻层不在窗口上留过程，无残留
路径乙  载入完成=1  新标题="ROUND-202"  suppressed=2（基线 0）
        ★ 常驻层挡下 2 次野回调 => 保护真实生效（内核安全）
step 4  新业务层照常载入 title="ROUND-102"
内核最终状态：kernel-init-count=1  live-view=0  suppressed=2
```

`suppressed=2` 是**保护确实生效的可数证据**：内核真的调了那个已失效的回调，
常驻层识别出「该地址不再属于任何已加载模块」并挡下了。

---

## 5. 接口清单（`MbKit.dll`，29 个导出，全部朴素名、`__stdcall`）

> ⚠️ **导出名不带 `@` 前缀**。易语言 `.DLL命令` 里 `@` 表示 `__cdecl`；
> MbKit 是 `__stdcall`，所以写**裸名**（`"MbKit_Init"`，不是 `"@MbKit_Init"`）。

| 分组 | 接口 |
|---|---|
| 生命周期 | `MbKit_Init` `MbKit_IsReady` `MbKit_NodePath` `MbKit_Version` `MbKit_LastError` |
| view | `MbKit_Create` `MbKit_Destroy` **`MbKit_DetachAll`** `MbKit_LiveCount` |
| 导航 | `MbKit_LoadUrl` `MbKit_LoadHtml` `MbKit_RunJs` `MbKit_Resize` `MbKit_SetZoom` `MbKit_SetHeadless` `MbKit_IsLoaded` `MbKit_GetTitle` `MbKit_ForcePaint` `MbKit_Pump` |
| ANSI(GBK) 变体 | `MbKit_LoadUrlA` `MbKit_LoadHtmlA` `MbKit_RunJsA` `MbKit_GetTitleA` |
| 诊断 | `MbKit_NodeModule` `MbKit_SelfModule` `MbKit_KernelInitCount` `MbKit_Pinned` `MbKit_SuppressedCount` `MbKit_AliveModule` |

事件（业务侧回调，`__stdcall`，签名 `void cb(void* user, int ev, void* p1, void* p2)`）：

| ev | 含义 | p1 |
|---|---|---|
| `MB_EV_PAINT` (1) | ⚠️ 实测**基本不会触发**（见 §2.1） | `MbPaintInfo*` |
| `MB_EV_LOADED` (2) | 载入完成（**必定触发**） | `(int)result` 0=成功 1=失败 2=取消 |
| `MB_EV_DETACH` (9) | 常驻层正在解绑 | — |

---

## 6. 易语言怎么接

见 **`src/mbkit/易语言接入说明.md`**（含可直接粘贴的 `.DLL命令` 声明与封装子程序）。

最小用法：

```易语言
MbKit_初始化 (真)
view ＝ MbKit_创建 (窗口句柄, 0, 0, 宽, 高, &事件回调, 0)
MbKit_载入页面A (view, html)
' …… 业务做完，准备卸载本模块前：
MbKit_全解绑 ()      ★ 这一个调用是热重载的关键
```

---

## 7. 边界与未做（诚实标注）

* ⚠️ **绘屏回调不触发**：本方案对「内核自己渲染」的常规用法成立。若你的场景需要
  **自己接管渲染**（离屏渲染 / 透明窗口 / 双缓冲 blit），需要单独实测走法，
  **本方案未覆盖**。
* ⚠️ **MbKit 被刻意 PIN 住 ⇒ 它自身不能热更新**。要换 MbKit 本身仍需重启宿主。
  这是刻意的取舍：常驻层稳定 = 业务层才能自由重载。
* ⚠️ 只提供 **32 位**版本（与 `e.exe` 同架构）。64 位宿主需另编。
* ⚠️ 只在**一份内核**上实测：`node.dll` 2021-03-10 build（blink 98096）。
  换内核版本需重跑 `run_tests.sh`。
* ❌ **`.ec` 易模块封装未做**（下一步）。目前交付的是 DLL + 可直接粘贴的声明。
* ❌ 未接 WebView2 路线。
* ❌ 未做「多 view / 多标签」的实测（接口支持，`MB_MAX_VIEW=64`）。

---

## 8. 文件清单

| 路径 | 说明 |
|---|---|
| `src/mbkit/mbkit.c` | 常驻层本体（自 PIN、内核加载、回调接管、存活校验、DetachAll） |
| `src/mbkit/mbkit_api.h` | 公共接口（事件号、返回码、函数原型） |
| `src/mbkit/mbload.h` | 动态装载器（模拟易语言 `.DLL命令` 的用法） |
| `src/mbkit/mb_biz.c` | 业务薄层（可热重载）——**接入参考实现** |
| `src/mbkit/mb_legacy.c` | 旧做法对照 DLL（**只为复现问题，不要用于生产**） |
| `src/mbkit/host_reload.c` | 压测宿主（6 个模式） |
| `src/mbkit/build.sh` | 32 位编译（含导出名与 image-base 核对） |
| `src/mbkit/run_tests.sh` | 一键跑全套验证并落证据 |
| `src/mbkit/易语言接入说明.md` | 易语言 `.DLL命令` 声明 + 用法 |
| `re/mbkit_run/evidence/` | 11 份实测证据日志 |
| `re/mb/pe_exports.py` | PE 导出名核对工具（查 `@N` 修饰） |

### 复现命令

```bash
cd src/mbkit
bash build.sh          # 编译（需 32 位 gcc 在 PATH 里）
bash run_tests.sh      # 跑全套，证据落到 re/mbkit_run/evidence/
```

⚠️ 编译前必须把 32 位 gcc 目录前置进 `PATH`，否则会 **exit 1 且没有任何错误输出**。
