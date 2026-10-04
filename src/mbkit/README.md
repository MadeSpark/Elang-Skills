# src/mbkit —— MbKit：miniblink 常驻宿主层

解决「宿主 exe 卸载使用 miniblink 的插件 DLL 后，miniblink 再也初始化不了」的问题。

* 方案与实测结论：`docs/方案-MbKit-miniblink热重载（2026-09-23）.md`
* 易语言接入（`.DLL命令` 声明 + 用法）：`易语言接入说明.md`

---

## 目录

| 文件 | 角色 | 上生产？ |
|---|---|---|
| `mbkit.c` / `mbkit_api.h` | **常驻层本体 + 公共接口** | ✅ 这是交付物 |
| `mbload.h` | 动态装载器（模拟易语言 `.DLL命令` 的调用方式） | ✅ 参考用 |
| `mb_biz.c` | 业务薄层（可热重载）——接入参考实现 | ✅ 参考用 |
| `mb_legacy.c` | 旧做法对照 DLL（**只为复现问题**） | ❌ 别用 |
| `host_reload.c` | 压测宿主（6 个模式） | ❌ 测试用 |
| `build.sh` / `run_tests.sh` | 编译 / 一键验证 | — |

产物（32 位）：`MbKit.dll`（29 个朴素导出）、`biz_v1.dll`、`biz_v2.dll`、
`legacy_v1.dll`、`legacy_v2.dll`、`host_reload.exe`。

---

## 编译

```bash
bash build.sh
```

* 必须 **32 位 gcc**（与 `e.exe` 同架构）。
* ⚠️ 必须把 gcc 所在目录**前置进 `PATH`**，否则 gcc 找不到自己的 `cc1`/`as`/`ld`，
  表现是 **exit 1 且没有任何错误输出**。
* 可用环境变量覆盖：`GCC` / `MINGW_BIN` / `PY`（跑导出名核对用）。
* 关键编译参数（改之前先看注释，都有实测原因）：
  * `-Wl,--kill-at` —— 去掉 stdcall 的 `@N` 修饰，导出名必须朴素（易语言 `.DLL命令` 依赖朴素名）。
  * `-Wl,--image-base,0x62xxxxxx` —— 给各测试 DLL 不同基址，让「重载后落到不同地址」
    这一最坏情况**确定性**发生（否则 Windows 会映回同一地址，野指针可能"碰巧能用"）。
  * `-fexec-charset=GBK` —— 源码中文转 GBK 字节，日志在中文 Windows 控制台/记事本里可直接看。
  * 链接时 `-l` 必须排在源文件**之后**。

## 验证

```bash
bash run_tests.sh            # 证据落到 re/mbkit_run/evidence/
```

| 模式 | 期望 |
|---|---|
| `wildcard` | 3 / 0（自证「卸载后回调地址失效」可被检测） |
| `ansi` | 3 / 0（GBK 中文往返 + JS） |
| `paintprobe` | 四配置 paints 全 0（信息性：绘屏回调根本不触发） |
| `mbkit 5` | **20 / 0，退出码 0**，每轮 `kernel-init-count=1` |
| `fault` | **8 / 0**，`suppressed ≥ 1` |
| `legacy 2` | **崩溃，退出码 139**，停在第一条窗口消息 ← 这是预期结果 |

⚠️ 对照组的 **139 = SIGSEGV 是预期结果**，不是脚本失败。

---

## 设计要点（改代码前必读）

1. **常驻层自我 PIN**：`GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN)`。
   业务 DLL 怎么卸载都动不了它。代价：MbKit 自身不能热更新（刻意的）。
2. **内核由本层加载并 PIN**：路径解析顺序见
   `resolve_node_path()`，**包含 `<当前工作目录>\main\corn\node.dll`**，
   与用户现有 `.DLL命令` 相对路径的行为保持一致 —— **不改用户的依赖布局约定**。
3. **唯一进入内核的回调都定义在本模块内**（`on_paint` / `on_load_finish`），
   内核手里永远不会攥着别的模块的代码地址。
4. **转发前做三重存活校验**（`view_cb_alive`）：地址是否还属于已加载模块 / 模块句柄是否一致 /
   模块全路径是否一致。挡下时 `G.suppressed++`（`MbKit_SuppressedCount()` 可读）。
5. **本层不在窗口上留任何过程**：窗口交互交给内核自己（`wkeSetHandle`），
   避免复刻旧做法的「子类化残留」。
6. **热重载契约 = `MbKit_DetachAll()`**：注销回调 + 解绑窗口 + 销毁 view；
   **不** finalize 内核（重载才够轻）。

## ★ 2026-10-01 改动：内核**异步**启动 + 编组等待**带消息泵**（修宿主启动期卡死）

现象：宿主（聊天机器人框架）**启动时自动载入插件 DLL**，插件在 `_启动子程序`
（= `DllMain` / 加载锁）里调 miniblink → **宿主整个界面未响应**。

根因（实测，见 `docs/修复-miniblink启动期卡死（2026-10-01）.md`）：

* 旧版 `MbKit_EnsureKernelInit` 是**同步**的：等 K 建好消息队列 → `PostThreadMessage`
  投递启动任务 → 等 K 把 `node.dll`（33 MB）`LoadLibrary` 完。
* 而在 `DllMain` 里，**新线程要等 `DllMain` 返回、加载锁释放后才真正开跑**
  （`LdrInitializeThunk` 要做线程 attach 并跑各模块的 `DLL_THREAD_ATTACH`）。
  ⇒ 上面三步全部只能超时：旧版第 1 轮卡 10,015 ms、第 2 轮卡 60,016 ms，`node.dll` 始终为 0。

改法（`mbk_thread.c` / `mbkit.c` / `mbkit_fwd.c`）：

| 点 | 现在 |
|---|---|
| K 的任务投递 | **事件 + 任务链**（`task_push` / `task_pop`），不再依赖 K 的线程消息队列；`mbk_kernel_start` 记完 tid **立刻返回**，不等 K 就绪 |
| 内核启动 | `MbKit_EnsureKernelInit` 只**投递**启动任务（`mbk_kernel_post`）就返回；要碰内核的调用先走 `MbKit_WaitKernel(ms)` |
| 启动期导出 | `wkeInitialize` / `wkeInitializeEx` / `wkeIsInitialize` 只把启动踢起来就返回（`wkeIsInitialize` 回 0=尚未初始化），因此易语言的 `WKE初始化` **不再阻塞** |
| 等待 | `mbk_wait_pump`：`MsgWaitForMultipleObjects` + 抽本线程消息（`WM_QUIT` 原样归还）；`MBKIT_NO_PUMP` 可关；`DllMain(DETACH)` 里强制关 |
| 失败提示 | `fatal()` **默认不弹模态框**（弹框在宿主启动期 = 用户眼里的卡死），改 `MBKIT_SHOW_ERRORBOX=1` |
| 诊断 | `MbKit_BootState/BootWaits/PumpCount/WaitTimeouts` + `MbKit_Diag` 新增「内核启动」一行 |

新增导出：`MbKit_EnsureKernelInit`（语义改为异步）、`MbKit_WaitKernel`、`MbKit_BootState`、
`MbKit_BootWaits`、`MbKit_PumpCount`、`MbKit_WaitTimeouts`。
`gen_fwd.py` 重生成后 `gen_fwd.def` 已同步（`build_mbkit.cmd` 可单独编 MbKit.dll）。

## ★★ 2026-10-01 第二改（v1.4）：内核属主 = **调用线程**（修「窗口出来了但网页永远不渲染」）

v1.3 把内核启动搬到常驻线程 K 上、并把所有 `wke*` 调用编组到 K，解决了「启动期卡死」，
但**渲染是废的**：窗口在、进程不卡，网页却是空白，`wkeIsLoadingCompleted()==0`、`wkeGetTitle()==NULL`。

### 实测证据（本机 blink 内核 `node.dll`，同一线程对照）

| 配置 | `wkeLoadHTMLW`/`wkeLoadW` 结果 | viewDC BitBlt 回读 |
|---|---|---|
| **K 模式**（v1.3：`wkeInitialize` 在 K 上、调用编组到 K） | `IsLoadingCompleted=0`、`title=NULL`、URL=NULL（**页面从没加载**） | 1.35% 非背景（空白） |
| **属主直通**（v1.4：`wkeInitialize` + 全部调用都在同一线程） | `IsLoadingCompleted=1`、`title=LOCAL-FILE-OK` | **22.08% 非白像素 / 57488 个蓝色条像素**（页面真的画出来了） |
| 裸调 `node.dll`（同一线程，对照组） | `succeeded=1` | 有画面 |

⇒ **miniblink 要求「内核主线程 == 宿主窗口线程 == 有消息循环的那个线程」**。
宿主（框架 / 易语言插件）的调用恰好来自那个线程，所以把属主钉在**调用线程**即可。

### 改法（`mbkit.c` / `mbk_thread.c` / `mbkit_fwd.c`）

| 点 | 现在（v1.4） |
|---|---|
| 默认模式 | **属主直通**：`boot_begin()` 在**当前调用线程上同步**完成 `LoadLibrary(node.dll)` + 绑符号 + `wkeInitialize()`，随后 `g_ownerTid=当前线程`、`g_kMode=0` |
| 同步为什么不死锁 | 死锁只发生在「同步等**别的**线程 `LoadLibrary`」（旧版事故：调用方持加载锁）；**本线程自己 `LoadLibrary` 用的是递归加载锁**，不会死锁。真实宿主里 `node.dll` 往往已被宿主自己的 webview 加载过，这里只是引用计数 +1（实测 0~1 ms） |
| 内核调用 | `mbk_kernel_run` 在 `!g_kMode && 当前线程==g_ownerTid` 时**就地执行**（不再编组到 K） |
| 宿主已初始化过 | `wkeIsInitialize()` 已经为 1 时**不再重复 initialize**，只把当前线程记成属主（框架进程内很常见） |
| 旧 K 模式 | 保留为逃生通道：`MBKIT_K=1`（离屏 / 无窗口场景）。自检里会标明「K 模式（编组；页面渲染不了，仅离屏可用）」 |
| 消息泵 | **默认关闭**（`MBKIT_PUMP=1` 才开）。真机实测编组等待超时 0 次、从不需要抽消息；而抽消息会在插件调用中途把宿主自己的消息派发进去（一次调用抽掉 253 条），正是「窗口不卡但也没反应」的观感来源 |

### 业务侧（易语言模块）配套修补

| 修补 | 原因 |
|---|---|
| `绑定` 可重复调用：`m_WebView ≠ 0` 时**先 `销毁 ()` 再重建**（原来直接 `返回 (假)`） | 热重载时调用方拿到假却仍会继续 `载入`，把 0 当 view 交给内核 → 崩溃 / 白屏 |
| 载入 / 运行JS 家族 13 处：`m_WebView = 0` 时直接 `返回` | 同上，`wke*` 内部解引用空指针 |
| **宿主库文件名版本化：`MbKit.dll` → `MbKit141.dll`**（264 处 `.DLL命令` 库名 + 释放路径） | 见下面「第二个坑」——不这么做，本模块的修复**在真实宿主里根本不会生效** |

### ★★ 第二个坑（比渲染 bug 更隐蔽）：别的插件用旧版 `MbKit.dll` 抢了基名

真机验证时出现怪事：宿主里 `WKE设置node路径` 返回**假**（错误文案是 v1.3 时代的
「内核已经加载…」），`WKE宿主自检 ()` 也报告**内核属主 = K 模式、编组等待抽消息 83 条**
—— 也就是说进程里跑的**根本不是**这份 v1.4。

原因：

* 易语言 `.DLL命令` 声明的是**库基名**（`"MbKit.dll"`），运行时按基名 `LoadLibrary`；
* Windows 对已载入模块是**按基名命中、不看路径**的；
* 宿主里 `AILI·LIFE.dll`（同一个 miniblink 模块的旧版本，内嵌 **MbKit/1.2**）加载顺序排在前面，
  于是进程里第一份 `MbKit.dll` 是旧版 —— 本模块后面所有调用都走那份陌生实现。

⇒ 把基名改成 **`MbKit141.dll`**：新旧模块同进程共存、互不干扰。
（`src/mbkit/MbKit.dll` 这个**磁盘文件名不用改**，它只是被当成资源写进 `.ec`；
真正决定抢占关系的是**释放到磁盘时的文件名**和 `.DLL命令` 里的库名。）

排查### ★★ 第四个坑：模块「同版本」判据是尺寸一致，新版永远不生效（2026-10-02）

模块侧 _释放宿主库文件 用「磁盘文件尺寸 == 内嵌资源尺寸」判断是不是同一版本。
117 KB 的宿主库两次构建极易同尺寸：实测 v1.4（同步初始化、DllMain 死锁那版）与 v1.4.1
都是 **117,248 B** ⇒ 旧的 v1.4 被当成同版本留在盘上，修好的 v1.4.1 一次都没跑起来，
而且自检/日志/界面看不出异常。

三道保险（已落地）：

1. 宿主库文件名带**精确版本号**（MbKit.dll → MbKit14.dll → MbKit141.dll），
   由 work_mb/mk_modfixed.py 从 src/mbkit/MbKit.dll 的版本串自动派生；
2. 模块里的版本自证串精确到小版本（“MbKit/1.4.1”），不再用前缀匹配；
3. 交付自检比对 **sha256**：释放出的文件必须等于内嵌镜像
   （实测 707bf4b7361e… == 707bf4b7361e…）。

这个坑的抓手：`WKE宿主自检 ()` 会显示「内核属主」的模式文案与「编组等待抽消息」计数
—— 属主直通（v1.4 默认）不该有 K 模式字样，也不该有抽消息计数。

### ★★ 第三个坑：同步初始化把 DllMain 死锁装回来了（v1.4.1 修）

v1.4 默认「首次调用就同步 `LoadLibrary(node.dll)` + `wkeInitialize`」—— 这条路在
**DllMain**（易语言 DLL 的 `_启动子程序`，加载锁由本线程持有）里必然死锁，
也就是用户最初报的那个症状又回来了。回归夹具：

```
work_mb/repro/dllmain_kit.c → dllmain_kit.dll   （DllMain 里 SetNodePath + wkeInitialize）
work_mb/repro/loadprobe.c   → loadprobe.exe     （宿主视角 LoadLibrary + 计时 + 打印自检）
```

| 版本 | `loadprobe.exe dllmain_kit.dll` 结果 |
|---|---|
| v1.4 同步版 | **300 秒不返回**（死锁复现） |
| v1.4.1 | `LoadLibrary 0 ms`、`ProbeInitMs=0`、`bootState=1`；加载锁外首次碰内核 343 ms → `bootState=2`、属主=调用线程、view 建成功 |

修法 = **把「初始化类」和「真碰内核类」分开**：

* 初始化类（`wkeInitialize`/`wkeInitializeEx`/`wkeIsInitialize`/`wkeFinalize`/
  `MbKit_EnsureKernelInit`/`MbKit_SetNodePath`）：`boot_begin()` 只把 `g_bootState`
  标成 1（已登记）就返回，**不碰 node.dll**。
* 真碰内核类：`mbk_marshal_exec` 发现还没有属主时调 **`MbKit_EnsureOwner()`**，
  在当前调用线程上就地完成 `LoadLibrary` + `wkeInitialize`（首次约 350 ms）。
* `MbKit_WaitKernel()` 在属主模式下 = 「现在就在本线程建起来」（阻塞但正确）。
* 版本串 `MbKit/1.4.1 (owner-thread, DllMain-safe)`；自检里属主未定时会显示
  「未初始化（已登记启动；首个内核调用时在调用线程上建起来）」。

### 配套的 C 复现工程（`work_mb/repro/`）

`paintflow.c`（模块真实绑定+绘制路径：`wkeSetHandle` → 子类化 → `WM_PAINT` → `wkeRepaintIfNeeded`
→ `BitBlt(wkeGetViewDC)`）、`rebind.c`（**热重载重绑**：销毁旧 view 后在同一窗口重建，两次渲染像素一致）、
`mbprobe.c`（载入路径矩阵：MbKit thunk vs 直调 node.dll 逐条对照）。


## 实测踩到的坑（别重复踩）

* `wkeGetVersionString()` 必须在 `wkeInitialize()` **之后**调用，否则直接崩。
* `wkeFinalize()` 之后 `wkeIsInitialize()` **仍返回 1** —— 标志位根本没清，
  别用 `wkeIsInitialize` 判断「能不能重来」。
* `wkeOnPaintUpdated` 在 view 绑定窗口后**从不触发**（四配置实测全 0），
  不要拿它当渲染入口、也不要拿"它没崩"当成功判据。
  > 2026-10-01 补充：该结论只对**离屏/无消息循环**的配置成立。属主直通（内核主线程=窗口线程）
  > 下实测**会触发**（`paintflow.exe`：内核回调 1 次 → 窗口 `InvalidateRect` 1 次 → `WM_PAINT` 里
  > `BitBlt` 2 次）。它仍然不是渲染入口 —— 模块真正的渲染是 `WM_PAINT` 里
  > `wkeRepaintIfNeeded` + `BitBlt(wkeGetViewDC)`。
* **别把 `MbKit_Create` 返回的 `MBVIEW*` 当内核 handle 用**：`MbKit_Create` 返回的是本层的包装指针，
  只有 `MbKit_*` API 认它；`wke*` thunk 要的是内核原始 handle（`wkeCreateWebView()` 的返回值）。
  混用不会报错，但 `wkeLoadW(包装指针,…)` 会**静默不加载**（排查时极易误判成"MbKit 的载入坏了"）。
* `wkeSetHandle` 的第二个参数是 `HWND`（不是 offset）；offset 走 `wkeSetHandleOffset`。
* 32 位 gcc + `-shared` 时，MinGW 默认 `--enable-auto-image-base` 会把输出路径哈希成
  映像基址 —— 输出名写成相对名（别带目录）才能得到稳定基址。
