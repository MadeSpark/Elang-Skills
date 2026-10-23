# 交接：WebView2 灰屏根因修复 + 强制 Node 内核定案（2026-10-15）

本轮修完了一件真正卡住的事：**edgewebview（WebView2）内核的"灰屏/白屏"根因**。
顺带把上一轮悬着的"强制 Node 内核"查到定案。以下每条都带可复查的证据。

---

## 1. 一句话结论

| 项 | 结论 |
|---|---|
| **edgewebview 灰屏** | **已修复**。根因是 `MbKitWebView2OffscreenHost` 承载窗口被懒创建在 **WebView2 环境工作线程**上，导致 `WM_APP+73`（创建 controller）投进一个没人抽的消息队列。 |
| **修复后 edge 验收** | 冷启动 + 连续重载 **5/5 全通过**，Chromium 子窗口每轮都在、客户区恒为 `679×453`、全部窗口 `responsive`。 |
| **强制 Node 内核** | **定案：不可行（在本机 node.dll 上）**。不是"设置没生效"，而是 `wkeCreateWebView` 在 node.dll 内部**同步死循环**，把宿主 UI 线程彻底堵死。详见第 4 节。 |
| **重载第 5 轮后宿主 APPCRASH** | **与本轮改动无关**（12:43 就已存在同签名崩溃，我的改动在 23:07 之后）。详见第 5 节。 |

---

## 2. 灰屏根因与修复

### 2.1 根因链（有诊断日志逐行对应）

`%TEMP%\mbkit-webview-<pid>.log` 是 MbKit 自带的一行式诊断，能精确看出卡在哪一跳：

**修复前（PID 51112，灰屏）**：
```
detected WebView2 Runtime 155.0.4283.45
enable edgewebview kernel: ..., slots bound 181/252
CreateWebView view=...
SetHandle view=... host=001d158a valid=1
host 001d158a add WS_CLIPCHILDREN
env worker tid=39696 CoInitialize=0x00000000        ← 环境回调跑到工作线程
EnvDone tid=39696 env=36814508
                                                     ← 到此为止，之后什么都没有
```
**修复后（PID 48680，正常）**：
```
EnvDone async tid=42544 env=21614508                ← 回调回到属主线程
ensure_ctl async tid=42544 host=002115a4            ← 关键：这一步以前根本不执行
CreateCoreWebView2Controller returned hr=0x00000000
CtlDone::Invoke hr=0x00000000 ctl=216bc1c4 tid=42544
wire begin / map drive C..Y hr=0x00000000
fit parent 679 x 453 dpi=96 raster=1.000
virtual html served bytes=44768
NavDone ok=1 err=0 url=https://mbkit.local/page.html
```

### 2.2 机制

1. `ensure_env()` 调 `CreateCoreWebView2EnvironmentWithOptions`（异步）。
2. 环境完成回调 `EnvDone::Invoke` **可能运行在 WebView2 的工作线程**上。
3. 旧代码在回调里才第一次调 `hidden_host()` → `RegisterClassW` + `CreateWindowExW` 于是把窗口建在**工作线程**上。
4. 紧接着 `PostMessageW(hidden_host(), WM_APP+73, 0, 0)` —— 投给一个**工作线程自己不会去抽**的队列。
5. `WM_APP+73` 的处理体正是"遍历 live views 调 `ensure_ctl()`"。消息永不被处理 →
   controller 永不创建 → `Chrome_WidgetWin_*` 永不出现 → 内容区整片灰。
5. 对照：`edge_passed` 的旧日志里 `EnvDone async tid=47480`、`ensure_ctl async tid=47480`、
   `CtlDone::Invoke ... tid=47480` **全是同一个 tid**，所以那一版是好的 —— 环境回调
   回到哪个线程是时序相关的，所以表现为"有时好有时灰"。

### 2.3 改动（`src/mbkit/mbk_webview.cpp`）

把承载窗口**在属主线程上提前建好**，回调里只取句柄：

```cpp
// ensure_ctl() 内，必须在 ensure_env() 之前
hidden_host();                 // ← 新增：属主线程先建窗口
if (!ensure_env()) return 0;
```
```cpp
// EnvDone::Invoke() 内，补注释说明为什么不能在这里建窗口
PostMessageW(hidden_host(), WM_APP + 73, 0, 0);
```

改动共 2 处，均在 `src/mbkit/mbk_webview.cpp`。重新编译后 `MbKit.dll = 722,944 B`
（与历史上通过验收的 `edge_passed/MbKit147.dll` **同尺寸**，可作旁证）。

---

## 3. 本轮验证证据

### 3.1 冷启动
- 宿主 PID 48680，主窗口 `WTWindow 'Main'` `responsive=True`。
- `Chrome_WidgetWin_0` / `Chrome_WidgetWin_1`（标题「表情包管理」）/ `Chrome_RenderWidgetHostHWND`
  全部 `visible=True`、`responsive=True`，客户区 **679×453**。
- 截图：`out_mbkit/ab_test/evidence/edge_fixed_coldstart.png` —— WebUI 完整渲染，
  「25 条结果」、表情缩略图全部出来了（连之前记录在案的"缩略图显示图片加载失败"也消失了）。
  （注：同目录的 `edge_main_fixed.png` 是被后来一次截图覆盖的旧件，看 `evidence/` 里那张。）
- 诊断：`NavDone ok=1 err=0 url=https://mbkit.local/page.html`，全程 0 条失败行。
- **终态复验（部署件冷启动，PID 40456）**：`CreateWebView=1 / CtlDone::Invoke hr=0x00000000=1 /
  NavDone ok=1 =1`，失败行 **0**；`Chrome_WidgetWin_1` 在、客户区 `679×453`、无 `responsive=False`。
  截图：`out_mbkit/ab_test/evidence/edge_fixed_coldstart.png`。窗口快照：
  `out_mbkit/ab_test/final_cold_windows.json`。

### 3.2 连续重载 5 次（点插件左侧「重载自身」）
| 轮次 | Chromium 子窗口 | 客户区 | 全窗口 responsive |
|---|---|---|---|
| 1 | OK | 679×453 | 是 |
| 2 | OK | 679×453 | 是 |
| 3 | OK | 679×453 | 是 |
| 4 | OK | 679×453 | 是 |
| 5 | OK | 679×453 | 是 |

**逐轮窗口快照**（`out_mbkit/ab_test/reload{0..5}_fixed_windows.json`，每轮 421 个窗口）：
每轮都同时存在 `Chrome_WidgetWin_0` 与 `Chrome_WidgetWin_1`，均 `visible=True`、
客户区 `679×453`，且 `responsive=False` 的窗口数为 **0**。
汇总见 `out_mbkit/ab_test/reload_results.json`。

**诊断日志**（该文件已被后续清理覆盖，结论由上面的窗口快照独立支撑）：
`CreateWebView=6`、`CtlDone::Invoke hr=0x00000000 =6`、`NavDone ok=1 =6`、失败行 **0 条**
（1 次冷启动 + 5 次重载）。

> 校验方法（可复现）：本轮全部结论都能只用保留下来的 `*_windows.json` 重新算一遍，不依赖日志。
> 判据 = 存在 `Chrome_WidgetWin_1` 且 `visible=True` 且 `client==[679,453]`，且无 `responsive=False`。

### 3.4 线程一致性（修复的直证）
终态冷启动（PID 40456）诊断日志 `out_mbkit/ab_test/evidence/final_coldstart_diag.log`：
```
EnvDone tid : 23312
ensure_ctl tid: 23312     ← 与 EnvDone 同线程，修复前这一步根本不会出现
CtlDone tid : 23312
errors      : 0
```
修复前（灰屏）对照：`EnvDone tid=39696`（工作线程），且**之后没有任何后续行**。

### 3.3 完整"换 DLL + 重载"验收
用脚本 `reload_verify` 同款流程重建插件（每轮改 `_按钮3_被单击` 里的循环次数以改 MD5）、
覆盖原名插件、点「重载自身」：**轮次 1–4 全部通过**（Chromium 在、679×453、responsive）。
第 5 轮宿主进程消失 —— 见第 5 节，属既有缺陷。

---

## 4. 强制 Node 内核：定案不可行

### 4.1 链路本身是通的
- 模块侧 `WKE设置node路径(路径, 失败原因, 强制使用node内核)` 完整，
  `.DLL命令 MbKit_SetForceNodeKernel` 在 `out_mbkit/miniblink_源码146/DLL.e.txt:1402`。
- C 侧 `MbKit_SetForceNodeKernel` → `mbkwv_set_force()`，导出在 `gen_fwd.def:265`。
- **注意：产品插件源码从未传第三参**（`窗口程序集_Main.form.e.txt:1353` 只有 1 个实参，
  第三参可空缺省=假）。本轮为了做 A/B 才在**临时副本**上把它改成 `, , 真` 并重编。

### 4.2 强制 Node 时的实测行为
trace 日志（`%TEMP%\mbkit-trace-2808.log`）显示 node 内核这条路**真的走通了**：
```
bind_all: enter (forced=1 cfgPath=[...\main\corn\node.dll])
bind_all: LoadLibraryA(node) -> 11850000 err=0
k_boot_load_only: bind_symbols ok
owner_init_here: G.Initialize() returned
MbKit_EnsureOwner -> 0 (ownerTid=42444)
```
插件日志同步显示 `初始化miniblink` → `node内核设置成功`（即 `WKE设置node路径` 返回真）。

**然后就卡死了**：插件日志停在 `node内核设置成功`，**从不出现** `开始载入表情包管理页面`。
`开始载入...` 是 `表情包管理.载入文件()` 打的，它在 `表情包管理.绑定()` **之后**；
所以卡点就是 `绑定()` 内部的 **`wkeCreateWebView` 同步调用**。

症状：**UI 线程被堵死**。逐窗测量（`out_mbkit/reload_acceptance/node_fixed_test_windows.json`）：

| 指标 | 强制 Node（PID 2808） | Edge（PID 40456，同一测量口径） |
|---|---|---|
| 窗口总数 | 417 | 421 |
| `responsive=False` | **415** | **0** |
| `Chrome_WidgetWin*` | **0** | 1（`679×453`，`visible=True`） |

（与上一轮观察一致："宿主 UI 卡住，日志停在 MbKit_EnsureOwner 后"。）

### 4.3 为什么这是 node.dll 的问题
- 不是内核没起来：`wkeInitialize` 已返回，符号全绑上，`ownerTid` 已记账。
- 不是 MbKit 编组问题：属主直通模式下 `MbKit_Create`→`k_create`→`G.CreateWebView()`
  就在调用线程上直调，中间没有额外等待。
- 不是我的修复引入的：修复只影响 WebView2 路径，node 路径完全不经过 `hidden_host()`。
- `main\corn\node.dll` 是 **2021-03-29、33,600,512 B** 的旧 miniblink；
  `wkeCreateWebView` 在这种"由插件回调线程直接调"的用法下进入死循环。
  若要继续攻，需要给 node.dll 换版本或改 MbKit 的创建时序；不属于本轮范围。

### 4.4 结论
**产品插件保持"非强制 Node"（即 Runtime 存在就走 edgewebview）是正确配置**，
不要为了"用上 node 内核"去改第三参 —— 一改就把宿主堵死。
上一轮打算"基于完整源码重编一个强制 Node 版模块"这条路可以停掉：
链路本来就是通的，问题在 node.dll 本身。

---

## 5. 重载第 5 轮后的宿主崩溃：既有缺陷，非本轮引入

| 时间 | 异常码 | 位移 | 模块 |
|---|---|---|---|
| **12:43:38** | `0xc0000005` | `0xc453c8b4` | unknown |
| **23:27:11**（本轮第 5 轮后） | `0xc0000005` | `0x7133c8b4` | unknown |
| 00:29:04 | `0x40000015` | `0x0005eca4` | MbKit144.dll |

- **12:43:38 就已有同签名崩溃**，而我对 `mbk_webview.cpp` 的改动在 **23:07 之后** →
  该崩溃**不是本轮改动引入的**。
- 低 16 位 `c8b4` 两次完全一致，说明是同一个固定逻辑点。
- 崩溃地址 `0x7133c8b4` 落在 `WLDP.DLL(0x71E80000+0x5A000)` 与 `iertutil.dll(0x71F60000)` 之间的
  **空洞**：该区间在 VEH 快照里没有任何已加载模块 → 典型的"跳到已卸载代码页"。
- 插件日志最后是 `_按钮3_被单击` 里循环打印的 `1 2 3 4`，紧接着就是崩溃 →
  崩溃发生在**卸载/重载流程**里，不是渲染中。
- **时间线硬证据**：`src/mbkit/mbk_webview.cpp` 的改动时间 = `2026-10-15 23:03:39`；
  12:43:38 那次同签名崩溃早于它约 **10 小时 20 分**。
- 结论：这是既有的**热重载 use-after-free**（MbKit 的 VEH 探针正是为它装的），
  与 WebView2 灰屏修复是两件独立的事。需要单独一轮排查（建议从 `Api.重载自身()` 的
  卸载路径与 WebView2 事件回调的存活校验入手）。
- **注意口径**：上一轮验收标准原文是"重复重载 **5 次不崩溃**即成功"。本轮
  §3.2 的 5 次重载**全部通过**；§3.3 的"换 DLL + 重载"则是第 1–4 轮通过、第 5 轮宿主消失。
  两者是不同强度的测试，不要混为一谈。

---

## 6. 当前部署状态（同名同路径，未改任何文件名）

| 文件 | 大小 | SHA256(前16) |
|---|---|---|
| `D:\DevelopmentFolder\常用模塊\miniblink.ec` | 941,030 | `f1b372035fdbd0ed` |
| `...\框架-爱丽\main\plugin\AILI·LIFE.dll` | 19,267,584 | `9ecf941a640c92c6` |
| `...\框架-爱丽\MbKit146.dll` | 722,944 | `a34e2a92e7fbe356` |
| `src\mbkit\MbKit.dll`（源本） | 722,944 | `a34e2a92e7fbe356` |

模块与插件均按**原名**部署，没有新增/改名任何 `.dll`（遵守"宿主全量加载 main\plugin\*.dll"的约束）。
`MbKit147.dll`（历史遗留、未被引用）已置隐藏，未删除。

---

## 7. 回退点与回退方法

本轮所有实验产物集中在 `out_mbkit/ab_test/`，回退不需碰其它目录。

```
out_mbkit/ab_test/backups/
  AILI-LIFE.before_ab_20261015_230135.dll   ← 本轮之前的插件（灰屏版）
  miniblink.before_ab_20261015_230135.ec    ← 本轮之前的模块（935,717，10-14 DPI 版）
  source.before_ab_20261015_230135.e        ← 本轮之前的插件 .e
  MbKit146.before_fix.dll                   ← 修复前的内核 722,432
  _常量28010234.image.dat.before_fix        ← 修复前的模块资源 blob
  app_plugin_sets.before_reload5.db         ← 宿主启用库备份
out_mbkit/ab_test/
  plugin_edge_fixed.dll     ← 本轮定稿的 edge 插件（当前生产件）
  plugin_node.dll           ← 只改了第三参的 node A/B 版（会卡死，不要部署）
  miniblink_fixed.ec        ← 本轮定稿模块（= 已部署的 miniblink.ec）
  rounds.py / make_node_src.py / reload_results.json / reload*_fixed_windows.json
  edge_main_fixed.png       ← 修复后的成品截图
```

完整回退（回到本轮之前的状态）：
```powershell
Copy-Item out_mbkit\ab_test\backups\AILI-LIFE.before_ab_20261015_230135.dll `
          'D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\main\plugin\AILI·LIFE.dll' -Force
Copy-Item out_mbkit\ab_test\backups\miniblink.before_ab_20261015_230135.ec `
          'D:\DevelopmentFolder\常用模塊\miniblink.ec' -Force
```
（但注意：那样会**回到灰屏**。）

---

## 8. 复现/复测命令

```powershell
# 1) 编内核（改了 mbk_webview.cpp 之后）
$env:PATH='C:\msys64\usr\bin;C:\msys64\mingw32\bin;'+$env:PATH
& 'C:\msys64\usr\bin\bash.exe' ./build.sh          # 在 src\mbkit 下

# 2) 把内核塞进模块资源 → 组包 → 编模块
Copy-Item src\mbkit\MbKit.dll 'out_mbkit\miniblink_源码146\资源\_常量28010234.image.dat' -Force
& $Q -mode t2e -src out_mbkit\miniblink_源码146 -dst out_mbkit\ab_test\miniblink_fixed.e
python $COMP out_mbkit\ab_test\miniblink_fixed.e out_mbkit\ab_test\miniblink_fixed.ec `
       --kind ecom --timeout 300 --headless hide --json

# 3) 编插件（模块是外部引用，资源在编译期链入）
& $Q -mode t2e -src out_mbkit\ab_test\src_deployed -dst out_mbkit\ab_test\plugin_fixed.e
python $COMP out_mbkit\ab_test\plugin_fixed.e out_mbkit\ab_test\plugin_fixed.dll `
       --kind static_dll --timeout 400 --headless hide --json `
       --stamp-version-info --expect-version 1.0.0.0 `
       --stamp-product-name "AILI·LIFE" --stamp-description "AILI·LIFE" --stamp-company "MadeSpark"
```

**两个实测坑**：
1. `ecompile.py` 遇到"確認另存新檔"弹窗会 fail（`status=failed`）。这是 IDE 的
   `ComDlg32\LastVisitedPidlMRU` 记住了 e.exe 的路径所致，清掉 `e.exe` 的 MRU 条目后即好
   （见 `out_mbkit/ab_test/` 里本轮用的清理片段）。
2. `-src` 传 `miniblink_源码146` 做 `t2e` 组包是**逐字节可逆**的
   （实测 t2e 产物与 `out_mbkit\miniblink.e` SHA256 完全相同）。
   但**不要**把 `miniblink_源码146\代码\*` 整个拷进插件的 `模块\miniblink\` ——
   那是上一轮 37 处"未知数据类型"翻车的根因（插件内嵌模块是私有类型不同的另一份拷贝）。

---

## 9. 下一步建议（按优先级）

1. **热重载 use-after-free**（第 5 节）。这是目前唯一会让宿主进程消失的问题，
   优先级高于其它一切。已知线索：`0xc0000005` + 低 16 位 `c8b4` + 跳到已卸载代码页 +
   发生在卸载/重载流程中；12:43 与 23:27 两次同签名。
2. **node 内核**：若要真正支持，需换一份新的 `node.dll`（当前 2021-03-29 / 33.6 MB），
   或改为在独立线程上创建 view。当前不建议投入。
3. 可选清理：`%TEMP%\mbkit-webview-*.log`、宿主目录下 `mb_veh_*.log`（最大一份 469 MB）
   已积累很多，确认无引用后可归档。
