# 诊断：miniblink 热重载失败的真因（2026-09-23）

> 用户场景：宿主 exe 载入一个使用 miniblink 的插件 DLL → DLL 初始化 miniblink 后与窗口
> 句柄绑定 → 销毁 + 卸载 DLL + 重新载入 → **无法重新初始化**，必须重启宿主 exe。
>
> 目标：C++ 写浏览器 DLL（参考精易 EdgeView2 的功能面）→ 封装成 `.ec` 给易语言调用，
> 且**支持热重载**。

---

## 0. 一句话结论

**内核没问题，问题在「谁持有内核与回调」。**

实测证明 miniblink 内核本身**完全支持**重复初始化、重复建 WebView、以及卸载后重载；
用户在易语言里踩的坑来自**进程级资源生命周期**：
业务 DLL 把**回调函数指针**与**窗口子类化**注册进了内核/窗口，卸载 DLL 时这些指针
指向的代码页被释放，而 `node.dll` 又因加载方式常驻进程 —— 于是重载后内核里握着**野回调**。

---

## 1. 一手实测（真 node.dll，本机）

* 探针源码：`re/mb/mb_probe.c`（约 180 行 C，**零头文件依赖**，全用 `GetProcAddress`）
* 内核：`node.dll` 32,600,512 B（x86，2021-03-10 build，`blink 98096`）
* 方式：headless WebView（`wkeSetHeadlessEnabled`）+ 自泵消息循环，判据 = `wkeIsLoadingCompleted`

| 阶段 | 动作 | 结果 |
|---|---|---|
| A | 首次 `wkeInitialize` → `wkeCreateWebView` → 载入 HTML | ✅ `loadingCompleted=1` |
| B | 销毁 WebView 后**不 finalize**，直接再建一个 | ✅ `loadingCompleted=1` |
| C | `wkeFinalize` → 再 `wkeInitialize` → 再建 | ✅ `loadingCompleted=1` |
| D | `FreeLibrary` 后 `GetModuleHandle("node.dll")` | ✅ `NULL`（**真的卸载了**） |
| E | 重新 `LoadLibrary` + 完整重来 | ✅ `loadingCompleted=1` |

**汇总 5 通过 / 0 失败。**

> ⚠️ 两个必须记住的细节：
> 1. **`wkeFinalize()` 之后 `wkeIsInitialize()` 仍返回 1** —— 标志位根本没被清。
>    所以任何"靠 isInitialize 判断能否重来"的逻辑都不可靠。
> 2. `wkeGetVersionString()` **在 `wkeInitialize()` 之前调用会直接崩**（首次实验即 SIGSEGV）。
>    内核全局对象未建立时访问即崩。

---

## 2. 读源码得到的嫌疑点（`miniblink.e`，已 e2t 转文本）

源码位置：`re/mb/text/`（21 个文件；主类 `MB浏览框` 1732 行，DLL 命令 329 条）。

### 2.1 内核加载方式

```
.DLL命令 _DLL命令0A01010B_wkeInitialize, , "main\corn\node.dll", "@wkeInitialize"
```

**相对路径 + `.DLL命令`**。⇒ `node.dll` 的生命周期**不归业务 DLL 管**：
无论易语言是把它写进导入表（进程启动即加载、退出才卸）还是内部 `LoadLibrary` 后缓存句柄，
结果都一样 —— **业务 DLL 卸载动不了它**。这与"必须重启宿主 exe 才行"的现象一致。

### 2.2 初始化有守卫，且 finalize 无效

```易语言
.子程序 WKE初始化, , 公开
    .如果真 (wkeIsInitialize () ＝ 0)       ← 已初始化就整体跳过
        wkeInitialize ()
        WKE置易TAB切换焦点 ()
    .如果真结束

.子程序 WKE卸载, , 公开
    .如果真 (wkeIsInitialize () ≠ 0)
        wkeFinalize ()                      ← 实测：调了 isInitialize 仍为 1
    .如果真结束
```

⚠️ **`销毁` 里从不调 `WKE卸载`** —— 只销毁 WebView，内核一直活着。

### 2.3 回调指针指向「业务 DLL 自己的代码」★

```易语言
_局部变量5 ＝ _stdcall_to_cdecl (到整数 (&_wkePaintUpdatedCallback), 7)
wkeOnPaintUpdated (m_WebView, _局部变量5, 参数_窗口句柄)
```

`_wkePaintUpdatedCallback` 定义在 `class/程序集_callback.static.e.txt` ——它在**业务 DLL 内**。
miniblink 会把 paint 回调存进内核；**业务 DLL 一卸载，这个地址就是野指针**，
而内核（常驻）之后还会按需调用它。

### 2.4 窗口上挂了指向业务 DLL 数据的属性

```易语言
.SetPropA (窗口句柄, "wkeEThis", 数据指针)      ← _子类化
.RemovePropA (窗口句柄, "wkeEThis")             ← 销毁时恢复
```

恢复写成了**条件式**：

```易语言
.如果真 (IsWindow (局部变量1) ≠ 0)             ← 窗口已销毁时整段跳过！
    SetWindowLongW (局部变量1, GWLP_WNDPROC, 旧值)
    RemovePropA (局部变量1, "wkeEThis")
.如果真结束
```

⇒ 若宿主**先关窗口、后卸 DLL**（很常见），子类化与属性**残留**，
窗口过程仍指向已卸载 DLL 的代码。

### 2.5 根因链条（推定，与实测不冲突）

1. `node.dll` 常驻 → 内核全局状态跨 DLL 重载存活
2. 业务 DLL 卸载 → 它注册进内核的 paint 回调、挂在窗口上的 `wkeEThis` 属性/子类化
   全部变成**指向已释放代码页的野指针**
3. 重载 DLL 后 `WKE初始化` 被 `wkeIsInitialize()==1` 守卫挡住 → 不会真正重置内核
4. 内核带着旧野回调继续跑 → 新建的 WebView 渲染/消息链路崩坏 → 表现为「打不开 / 初始化不了」

---

## 3. 解法方向

### 方案 1（推荐）：内核宿主层「钉住自己」+ 业务侧薄封装

即用户要的 **C++ DLL** 的正确架构：

```
宿主 exe
   │  LoadLibrary 一次（不再 Free）
   ▼
MbKit.dll   ←── 常驻层：持有 miniblink、持有全部回调、做窗口子类化
   │            自身用 GET_MODULE_HANDLE_EX_FLAG_PIN 钉在进程里
   │            暴露纯 C 接口
   ▲
业务插件 DLL ←── 薄封装：只调 MbKit 接口 + 注册/注销自己的事件处理函数
                 卸载前调 MbDetach 解绑 → 卸载 → 重载 → 重新 Attach
```

* **关键点 1**：所有进入内核的回调（paint / wndproc / 各类 wke 回调）都定义在 `MbKit.dll` 内，
  业务 DLL 只注册**自己的数据处理函数指针**，且**卸载前必须注销**。
* **关键点 2**：`MbKit.dll` 用 `GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN, ...)`
  把自己钉住 ⇒ 业务 DLL 怎么卸载都不影响它。
* **关键点 3**：热重载契约 = `MbDetach()`（注销全部回调 + 恢复子类化 + 销毁 WebView）→ 卸载 → 重载 → `MbAttach()`。
* 内核可选 miniblink（现有、Win7 兼容）或 WebView2（现代、内核在独立进程、天然友好）。

### 方案 2：内核进程外（最强隔离，工作量最大）

浏览器跑在独立进程，业务侧只有 IPC 客户端（无状态）。热重载天然可行，且内核崩溃不拖垮宿主。
代价：离屏渲染（共享 DIB）+ 输入转发 + 窗口消息桥接。

### 方案 3：维持现状 + 严格注销（最小改动，风险最高）

只补「卸载前彻底注销」：paint 回调置 0、强制恢复子类化（不看 `IsWindow`）、
`wkeSetHandle(0)` + `wkeDestroyWebView`。**但 `node.dll` 常驻这点改不掉，
`wkeFinalize` 实测又无效**，能否干净复用内核**未经验证** —— 不推荐作为长期方案。

---

## 4. 目标功能面（参考用户给的 EdgeView2 对比表）

需覆盖：创建窗口/嵌入控件、多标签、Cookie 管理、代理、下载、JS 执行与互调、
资源拦截（`wkeNetHookRequest`）、截图、打印、开发者工具（`wkeSetDebugConfig`）、
UA/缩放/透明/无边框拖拽、离屏渲染、内存缓存开关等。
miniblink 的 `node.dll` 本机实测导出 **9,611 个符号，其中 306 个 `wke*`** —— API 面足够。

---

## 5. 未验证项（诚实标注）

* ❌ **「业务 DLL 卸载导致野回调」这一步我只做了源码分析，没有做双 DLL 精确复现实验**。
  规范做法：编译 `biz.dll`（注册回调+子类化）+ 宿主 exe（常驻加载 node.dll），
  卸载重载 `biz.dll`，观察是否复现 —— **建议在动手写 MbKit 前先做这一步**。
* ❌ 易语言 `.DLL命令` 的确切链接方式（静态导入 or 运行时 LoadLibrary）**未实测**。
  已备好实验：造含 `.DLL命令` 的最小工程编译后查导入表（`re/mb/pe_deps.py` 已写好）。
* ❌ 内核版本差异未测：本机有 3 个 `node.dll` 副本（2021/2021/2023，28~35 MB），
  仅测了 `GS框架开发区` 那份（2021-03-10）。
* ❌ WebView2 路线未做任何实测（未确认本机 Runtime 可用）。
