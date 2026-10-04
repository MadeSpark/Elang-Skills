# 修复报告：miniblink 模块「宿主启动期载入插件 DLL 就卡死」（2026-10-01）

> 读者：接手的人 / 用户本人。本文自包含。
> 交付物：`out_mbkit/miniblink.e`、`out_mbkit/miniblink.ec`（内嵌修好的 `MbKit141.dll` **v1.4.1**）
> + C 侧源码改动（`src/mbkit/`）。
>
> **2026-10-02 续修（v1.4 / v1.4.1）见文末第 6、7 节** —— 那份 v1.3 交付虽然让宿主不再
> 「未响应」，但页面渲染不出来（窗口一片空白、点不动）；而且它把渲染修好之后，
> DllMain 死锁又回来了。两件事都在第 6、7 节里给了一手证据和最终修法。

---

## 0. 一句话结论

**卡死的根因是 MbKit 的「内核启动」是同步的，而插件是在 `DllMain`（加载锁）里调进来的。**

- 宿主 `LoadLibrary` 插件 DLL → 插件 `_启动子程序`（= `DllMain` / `DLL_PROCESS_ATTACH`）→ 调
  `WKE初始化` → MbKit 要启动内核（`LoadLibraryA(node.dll)`，33 MB）。
- **新线程必须等 `DllMain` 返回、加载锁释放后才真正开跑**（`LdrInitializeThunk` 要做线程 attach、
  跑各模块的 `DLL_THREAD_ATTACH`）。所以在 `DllMain` 里
  ① 等 K 建好消息队列、② 把任务 `PostThreadMessage` 给 K、③ 等 K 把 node.dll 加载完 ——
  **三件事都不可能成功**，只能一轮轮超时，宿主界面全程「未响应」。
- 实测（本仓 C 夹具，可复跑）：旧 MbKit **第 1 轮卡 10,016 ms、第 2 轮卡 60,016 ms，`node.dll` 始终为 0**
  —— 也就是「插件永远起不来，宿主每次启动都卡几十秒」。

修法（C 侧，v1.3）：

| # | 改动 | 作用 |
|---|---|---|
| ① | **内核异步启动**：`MbKit_EnsureKernelInit` 只把「启动内核」投递到 K 就返回（`MbKit_WaitKernel` 才等） | 在 `DllMain` / 宿主 UI 线程里调 miniblink **不再阻塞** |
| ② | **任务投递不再依赖 K 的线程消息队列**：改成「事件 + 任务链」；`mbk_kernel_start` **不再等 K 就绪** | `DllMain` 里线程起不来也照样能投递，DllMain 一返回 K 就把排队的活做掉 |
| ③ | **编组等待带消息泵**（`MsgWaitForMultipleObjects` + `PeekMessage/DispatchMessage`，`WM_QUIT` 原样归还） | 消除「宿主 UI 线程被我们挡住、内核又要它处理窗口消息」的互等死锁 |
| ④ | **失败默认不弹模态框**（改 `MBKIT_SHOW_ERRORBOX=1` 才弹），错误进 `MbKit_LastError` / `WKE宿主自检` | 宿主启动期不会突然冒一个「等点击」的框（在用户眼里同样是卡死） |
| ⑤ | 自检新增「内核启动 / 等启动次数 / 编组等待抽消息数 / 等待超时次数」 | 真机上能直接看出卡在哪一环 |

模块侧（易语言）：

| # | 改动 | 作用 |
|---|---|---|
| ⑥ | 内嵌的 MbKit 从 09-24 的 56,320 B 版换成 **v1.3（115,200 B）** | 上面 ①~⑤ 才真正生效 |
| ⑦ | 修 `错误(10066)`：`WKE网络添加HTTP头字段到URL请求` 把无返回值的 `.DLL命令` 当 `返回 (...)` 表达式用 | 模块源码链进 **DLL** 工程时的编译错误（exe 工程不报） |
| ⑧ | 新增公开命令 `WKE等待内核就绪` / `WKE取内核启动状态` / `WKE全解绑` / `WKE取等待统计` | 启动期可确定性等内核；热重载前有明确契约（`WKE全解绑`） |
| ⑨ | `_确保宿主库已载入` 载入后核对 MbKit 版本串；`WKE宿主自检` 版本不符时把警告顶到第一行 | 「多个插件各带一份 MbKit」时只有先载入的那份生效 —— 这是热重载里最难查的一类问题，现在会自己说出来 |
| ⑩ | `WKE设置node路径` 补上**第二个参数「失败原因」**（参考可空），与插件现用签名一致 | 你现有插件源码里是 `WKE设置node路径 (路径, 失败原因)`（两参）—— 不补这个参数，新模块会让插件**编译不过**（"只要 1 个参数，给了 2 个"） |

---

## 1. 复现与对比（一手数据，可复跑）

夹具：`work_mb/repro/bizdllmain.c`（32 位 DLL，**在 `DllMain` 里调 `MbKit_Init`**）
+ `work_mb/repro/dllmain_host.c`（宿主，只测 `LoadLibrary` 耗时）。

```
cd work_mb
repro\dllmain_host.exe abd_old\bizdllmain.dll   # 旧 MbKit（111,104 B，09-25 版）
repro\dllmain_host.exe abd_new\bizdllmain.dll   # 新 MbKit（115,200 B，v1.3）
```

| 轮次 | 旧 MbKit | 新 MbKit |
|---|---|---|
| 第 1 轮 `LoadLibrary` | **10,015 ms**，`node.dll=0` | **0 ms** |
| 第 2 轮 | **60,000 ms**，`node.dll=0` | **0 ms**，`node.dll=0x78D80000` ← 内核已在后台起来 |
| 第 3 轮 | — | **0 ms**，`node.dll=0x78D80000` |

证据文件：`work_mb/evidence/A_old_MbKit_DllMain.txt`、`B_new_MbKit_DllMain.txt`。

> 机制旁证：旧版第 1 轮的 10,015 ms 正是 `mbk_kernel_start` 里「等 K 建好消息队列」的 10s 上限；
> 第 2 轮 60,000 ms 是编组等待的 60s 上限。两个数字都精确落在代码里的超时常数上。

## 2. 其它已做的行为回归（全部通过）

| 场景 | 夹具 | 结果 |
|---|---|---|
| 有消息循环的宿主（`Init→Create→LoadUrl`） | `repro/test_host.exe sequential` | 0 / 32 / 94 ms，view=1 |
| **无消息循环**的宿主（`_AppStart` 型） | `... nopump` | 0 / 204 / 93 ms，view=1 |
| 边抽消息边调 | `... pump` | 0 / 47 / 94 ms，view=1 |
| 另一个线程调、主线程抽消息 | `... worker` | 0 / 187 ms，view=1 |
| **易语言控制台探针**（真实模块 + 真实 node.dll，`_启动子程序` 里没有消息循环） | `work_mb/bin/mdprobe_k.exe` | 设置路径 0 ms、绑定 156 ms、载入 187 ms；自检显示 `内核启动: 已完成 / 编组等待抽消息 3 条 / 等待超时 0 次`；进程正常结束 |
| 文本层 | `echeck.py` / `rtcheck.py` | echeck ✓（0 错误；1 条告警来自模块自带的 help 程序集，与本次改动无关）；rtcheck 双次往返**收敛**、两次 t2e 均 0 错误 |
| 易模块产物 | `ecompile.py --kind ecom` | `status=ok`；头 8 字节 `CNWTEPRG`；反向抽取（`e2t`）成功、0 处无法解析；`MbKit_WaitKernel/DetachAll/BootState/Version/PumpCount` 入口名均在 DLL 表里 |
| 部署件（`out_mbkit/miniblink.e`） | `ecompile.py --check` | `status=syntax-ok`（易语言编译器静态检查全过） |
| **与插件同形的调用**（`WKE设置node路径 (路径, 失败原因)` 两参） | `work_mb/bin/mdprobe2.exe` | 编译通过、运行正常：首次设置返回真、失败原因空；随后故意把路径设成不存在的目录 —— 第二次返回真（内核尚未加载），内核因此**明确加载失败**（没有静默回退），`WKE宿主自检` 里给出原文原因与「内核启动: 失败」，进程不卡不崩（证据 `work_mb/evidence/D_probe_2param_apifail.txt`） |

---

## 3. 你（用户）侧的验收步骤

1. 用新建工程导入 `out_mbkit/miniblink.ec`：**IDE 不崩、不报 `错误(10022)`、不报「本模块程序未经编译，不能载入」**。
2. 用新模块重新编译你的插件（`浏览框测试\小栗子应用.e`）：模块的内嵌 MbKit 已是 v1.3。
3. 在小栗子里做两轮：
   - 启动宿主（会自动载入并启用插件）→ 界面应当**不再未响应**；调 `WKE宿主自检 ()` 应看到
     `内核启动 : 已完成`、`编组等待抽消息 N 条`、`等待超时 0 次`。
   - **不重启宿主**再重载一次插件（硬重载）→ 仍能绑定 + 载入页面。
   - 若某次仍卡，请把 `WKE宿主自检 ()` 的原文发我：`等待超时 > 0` 就是仍然有人在加载锁里调
     miniblink（把首次调用挪出 `_启动子程序`/另开线程即可）。
4. 可选：`WKE等待内核就绪 (5000)` 放在你希望「确认内核可用」的位置；插件被卸载前调一次
   `WKE全解绑 ()`（热重载契约）。

---

## 4. 已知边界（诚实标注）

1. **模块源码不要直接内联进 DLL 工程**（易语言 DLL 编译模式对「公开子程序 + 可空参数」有额外限制，
   实测会报 `错误(10061) 被取地址的子程序"WkeA2W"的参数 2 不能具有"可空"标志`）。
   正确做法与你现有插件一致：**把 `miniblink.ec` 作为易模块导入**。本次交付的 `.ec` 就是这样产出的。
2. **不要再和「内存映射版」混用**。手工映射的 MbKit 不在加载器链表里，会和 `LoadLibrary` 进来的
   这份并存成两个独立实例（两套内核状态）。本模块现在会在 `WKE宿主自检 ()` 里报出「已载入另一份
   MbKit」的警告，但它拦不住混用。
3. 内核（node.dll）一旦加载就刻意常驻、**不可更换**（这是热重载成立的前提）；要换内核只能重启宿主。
4. 「宿主整个界面未响应」这一类问题里，**插件自身的耗时操作**同样是原因之一
   （小栗子 SDK 明确写着「请勿在启用/启动回调里做耗时操作」）。模块这一侧现在已经不阻塞了，
   但你在 `_AppStart` 里放的 `延时 (2000)` 之类仍会占住宿主的线程。
5. 本次没有在你的真机宿主里做最终回归（需要你那边跑）；我这边能控制的复现件（C 夹具 + 易语言
   探针）全部通过。

---

## 5. 回滚

`out_mbkit/miniblink.e` / `.ec` 的旧版都在 git 里（提交 `83ed9c7` 及其后）：

```bash
git checkout 83ed9c7 -- out_mbkit/miniblink.e out_mbkit/miniblink.ec
```

C 侧源码改动只动了 `src/mbkit/`（`mbk_thread.c`、`mbkit.c`、`mbkit_fwd.c`、`mbkit_api.h`）
与新增的 `src/mbkit/build_mbkit.cmd`；`git diff` 可逐条回看。

---

## 6. 2026-10-02 续修之一（v1.4）：页面在宿主里渲染不出来

### 6.1 现象

v1.3 交付后宿主不再「未响应」了，但用户反馈：**窗口任何操作都做不了，内嵌的 webui 也没成功
加载，任务栏都关不掉窗口**。抓图（`work_mb/repro/evidence_plugin_only_0.png`）显示：标题栏 + 按钮
+ 一整块纯灰客户区（非背景像素 4.39%）。

### 6.2 根因（实测 A/B）

| 配置 | 结果 |
|---|---|
| K 模式（`wkeInitialize` 在常驻线程 K 上执行、所有调用编组到 K） | `wkeLoadHTMLW` 之后 `IsLoadingCompleted == 0`、`wkeGetTitleW == NULL`、截图一片空白 —— **页面从来没加载** |
| 同一进程裸用（`LoadLibrary` + `wkeInitialize` + `wkeCreateWebView` + `wkeSetHandle` + `wkeLoadHTMLW` 全在同一线程） | `succeeded=1`，屏幕截图能看到渲染内容 ✅ |

⇒ miniblink 要求「**内核主线程 == 窗口线程 == 有消息循环的线程**」。宿主（框架/易语言插件）的
调用恰好来自那个线程，所以**把内核属主设成调用线程**即可（v1.4「属主直通」）。

A/B 夹具：`work_mb/repro/paintflow.c`（属主直通 22.08% 非白 / 57,488 蓝色像素 / `IsLoaded=1` /
`title=LOCAL-FILE-OK`）与 K 模式对照组（1.35% 非白、永不完成）。
证据图：`work_mb/evidence/2026-10-01_模块绘制路径_属主直通_渲染成功.png`。

### 6.3 改动

| 层 | 改动 |
|---|---|
| C（`mbkit.c`） | `owner_init_here()`：首次调用内联做 `LoadLibraryA(node.dll)` + 钉住 + `bind_symbols()` + `wkeInitialize()`，记账 `g_ownerTid = GetCurrentThreadId()`；`boot_begin` 默认走这条（同步） |
| C（`mbk_thread.c`） | `mbk_kernel_run`：属主线程上调用**就地执行**（回调重入安全），不再编组 |
| C（`mbkit_fwd.c`） | 诊断文案区分「属主直通（可渲染）」/「K 模式（渲染不了）」；编组计数在属主模式下不再虚报「跨线程」 |
| 模块（易语言） | `绑定` 可重复调用（先 `销毁 ()` 再重建）；载入 / 运行JS 家族 13 处加「视图为 0 直接返回」 |
| 模块 | `WKE设置node路径` 幂等：同路径重复调返回**真**（真机里否则第二次启用会整段跳过绑定与载入） |

### 6.4 真机验收（宿主 `D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\小栗子虚拟机.exe`）

自检原文（宿主进程内）：

```
内核启动     : 已完成    等启动次数 0   编组等待抽消息 0 条   等待超时 0 次
内核属主     : 线程 53712（属主直通（内核主线程=调用线程，可渲染））
```

日志：`node设置成功` → `加载: https://www.madespark.cc/` → bootstrap/css/icon/js 全部请求；
看板截图 `work_mb/repro/host_real_v141_window.png`（窗口 `miniblink_test` 里完整显示页面，
客户区非背景 43.41%），插件窗口内 view DC 的全页快照
`work_mb/repro/host_viewDC_snapshot.png`（788×3113，就是页面真实内容）。
热重载复绑：`work_mb/repro/rebind.exe` 第 1 次与第 2 次绑定**逐像素一致**
（22.08% 非白 / 57,488 蓝色像素 / `IsLoaded=1`）。

---

## 7. 2026-10-02 续修之二（v1.4.1）：两个「修好了却又回来」的坑

### 7.1 坑 A：别的插件用旧版宿主库抢了 DLL 基名（让上面的修复完全不生效）

真机里出现怪事：`WKE设置node路径` 返回**假**（错误文案还是 v1.3 时代的），自检报告
「内核属主 = K 模式 / 编组等待抽消息 83 条」—— **进程里跑的根不是 v1.4**。

原因：

* 易语言 `.DLL命令` 声明的是**库基名**（`"MbKit.dll"`），运行时按基名 `LoadLibrary`；
* Windows 对已载入模块是**按基名命中、不看路径**的；
* 真机上 `AILI·LIFE.dll`（同一个 miniblink 模块的旧版本，内嵌 **MbKit/1.2**）加载顺序排在前面，
  于是进程里第一份 `MbKit.dll` 是旧版，本模块之后所有调用都走那份陌生实现。

修法：**宿主库文件名版本化** —— 释放与声明都用版本化的名字（v1.4.1 起是 `MbKit141.dll`，
由 `work_mb/mk_modfixed.py` 按内嵌宿主库的版本串自动生成；`DLL.e.txt` 264 处库名 + 释放路径 +
诊断文案 + 版本自证串一起改）。改完真机立刻正常：自检变成「属主直通」，页面渲染出来。
这条也是「多个插件各带一份 MbKit」这一类问题的**通用解法**：版本化基名，让新旧共存。

#### 7.1.1 ★ 连带发现的坑：模块「同版本」判据是**尺寸一致**，会让新版永远不生效

模块侧 `_释放宿主库文件` 的逻辑是「文件尺寸 == 内嵌资源尺寸 ⇒ 视为同一版本，不重写」。
117 KB 的宿主库，两次构建**极易同尺寸** —— 实测 v1.4（同步初始化、DllMain 会死锁的那版）
与 v1.4.1 都是 **117,248 B**，于是：

* 宿主目录里那份旧的 v1.4 被当成「同一版本」留在盘上；
* 改好的 v1.4.1 **一次都没跑起来**，而自检、日志、界面**看不出任何异常**
  （v1.4 也能渲染页面，只是 DllMain 会死锁）。

**修复是否生效，曾经取决于一个跟修复无关的尺寸比较** —— 本轮最阴的坑。三道保险：

1. **文件名带精确版本号**（`MbKit141.dll`）：新版一定是「文件不存在 → 释放 → 生效」；
   脚本从版本串自动派生，杜绝「忘了改文件名」。
2. **版本自证串精确到小版本**：模块里从 `“MbKit/1.4”` 改成 `“MbKit/1.4.1”`，前缀匹配不再把
   1.4 与 1.4.1 混为一谈（旧的前缀匹配正是让旧版蒙混过关的帮凶）。
3. **交付自检比对 hash**：释放出来的文件 sha256 必须等于模块内嵌镜像的 sha256 ——
   实测宿主目录 `MbKit141.dll` = `707bf4b7361e…`、模块内嵌镜像 = `707bf4b7361e…` ✅
   （就是靠这一步先发现坑、再证明修好的。）

### 7.2 坑 B：为了让页面能渲染而改同步初始化，把 DllMain 死锁装回去了

v1.4 默认把「首次调用」做成同步 `LoadLibrary(node.dll)` + `wkeInitialize`。
一旦首次调用发生在 `DllMain`（易语言 DLL 的 `_启动子程序`）里，加载锁由本线程持有，
加载器互等 —— 正是第 0 节那个老事故。

回归夹具（可复跑）：

```
work_mb/repro/dllmain_kit.c  → dllmain_kit.dll   （DllMain 里：SetNodePath + wkeInitialize）
work_mb/repro/loadprobe.c    → loadprobe.exe     （宿主视角 LoadLibrary + 计时 + 打印自检）
```

* v1.4（同步版）：`loadprobe.exe dllmain_kit.dll` **300 秒不返回** ⇒ 死锁复现 ✅（问题确认）
* v1.4.1（下面这个修法）：`LoadLibrary 用时 0 ms`、`ProbeInitMs=0`、`bootState=1`；
  随后（加载锁外）首次碰内核 343 ms、`bootState=2`、属主 = 调用线程、view 创建成功 ✅

最终修法（**分两级**，`mbkit.c` + `mbkit_fwd.c`）：

| 调用类别 | 行为 |
|---|---|
| **初始化类**：`wkeInitialize` / `wkeInitializeEx` / `wkeIsInitialize` / `wkeFinalize` / `MbKit_EnsureKernelInit` / `MbKit_SetNodePath` | 只**登记**启动请求（`MbKit_BootState()==1`）后立即返回，**不 LoadLibrary、不 wkeInitialize** ⇒ DllMain 里安全（0 ms） |
| **真碰内核类**：建视图 / 载入 / 取版本 / 调整大小 … | 首次在当前调用线程上调用 `MbKit_EnsureOwner()`：`LoadLibrary(node.dll)` + `wkeInitialize` 就地完成，内核属主 = 该线程 ⇒ 能渲染 |

这样两件事同时成立：**DllMain 里不干重活**（用户最初的症状不回潮）、**内核属主 = 业务线程**（页面能渲染）。
`MbKit_WaitKernel` 在属主模式下语义变成「现在就在本线程把内核建起来」（阻塞但正确），
文档已注明不要放 `DllMain`。

### 7.3 交付版本与文件

| 文件 | 大小 | 说明 |
|---|---|---|
| `src/mbkit/MbKit.dll`（= 内嵌镜像） | 117,248 B | 版本串 `MbKit/1.4.1 (owner-thread, DllMain-safe)` |
| `out_mbkit/miniblink.e` | 365,177 B | 模块源码（264 处 `MbKit141.dll` 声明 + 上述补丁） |
| `out_mbkit/miniblink.ec` | 333,511 B | 已编译易模块，头 `CNWTEPRG`；e2t 反查「22 个文件 / 267,662 字节 / 无法解析 0 处」 |
| 部署位置 | — | `D:\DevelopmentFolder\常用模塊\miniblink.ec`（用户模块目录）；插件 `main\plugin\小栗子应用.dll` |

### 7.4 仍未做/未验证（诚实标注）

* **框架「重新加载插件」按钮**没做端到端点击验证：热重载契约由 C 夹具 `rebind.exe`（同窗口
  二次绑定逐像素一致）与模块侧「`绑定` 可重复调用」补丁覆盖；框架 UI 里那一下由用户在
  「插件管理 → 重新加载」里点一次即可复验。
* 宿主目录里那份**旧的 `框架-爱丽\MbKit.dll`（v1.3，115,200 B）**我删不掉：用户自己的宿主实例
  （pid 61132）把它加载着，文件被占用。v1.4.1 起不再使用 `MbKit.dll` 这个基名，**留着无害**，
  但建议在宿主全部关闭后删掉，避免混淆。
* `.ec` 只能由易语言 IDE 编译（`ecompile.py --kind ecom`）；`t2e` 组不出 `.ec`，这条流程限制没变。
