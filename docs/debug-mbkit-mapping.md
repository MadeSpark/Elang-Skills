# Debug Session: mbkit-mapping
- **Status**: [OPEN]
- **Issue**: 小栗子应用提示“宿主库（MbKit）载入失败：内置映像手工映射未成功”。

## Reproduction Target
1. DLL source: `D:\DevelopmentFolder\插件开发目录\E\插件\浏览框测试\小栗子应用.e`
2. Host: `D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\小栗子虚拟机.exe`
3. Expected: `WKE宿主自检()` reports a usable mapped image and `状态=1`.

## Hypotheses
| ID | Hypothesis | Expected Signal |
|---|---|---|
| A | The deployed miniblink module is stale. | Size/hash differs from the 421,582 B recovery build. |
| B | Fixed mapping pool is occupied but recovery validation fails. | Status 2, missing magic, invalid MZ, or empty slot 0. |
| C | MbKit image or mapping blob is absent/corrupt. | Image base 0 or empty mapping slots. |
| D | Runtime dependencies are absent from the host environment. | Host log shows a missing MbKit/node dependency. |
| E | The supplied source path is an IDE display path rather than a filesystem path. | Direct D: path resolves while the combined path does not. |

## Evidence
Pending collection.

- 进程内存实测 PID 25512：池槽0=0x6C2CA880、槽259=0x6C2C4A90、映像=0x6C2C0000、错误码=0、魔数=0x314B424D (MBK1)、映像首字=0x5A4D (MZ)。B/C/D 排除。
- 已确认根因：恢复分支把 MBK1 的 little-endian 十进制 0x314B424D 写错为 827408461；正确值为 827015757。因而第一道魔数校验恒失败。工作区模板已修正，待重生成模块与部署验证。

- 多插件并发补强：生成器现对占用池且魔数未写入的情况最多等待 50×10ms；超时将本模块状态复位为 0，允许后续调用重试，避免第二插件永久状态=2。
- 新产物：re\\mbkit_mem\\out_mem\\miniblink-hotfix.ec；待核对大小/哈希后再申请部署授权。

## Resolved (2026-09-27)
- **真正根因**：恢复分支的映像校验 `_内存读整数(基址,0) ≠ 23117` 读的是 **4 字节**（DOS 头实为 `4D 5A 90 00` = 0x00905A4D），与 2 字节常量 0x5A4D 比较恒不等 ⇒ 恢复分支**从未成功过**（此前修的魔数常量只是它前面的另一道错门）。
- 复现物证（单进程 load→挂载→FreeLibrary→load→自检，`_mbk_scratch/`）：gen2 恢复分支所有前提全部健在（池 State=0x1000、映像 Protect=0x40、映像头=0x00905A4D、槽值/魔数完好），仅校验恒假 → 状态=2、池=0x0，与用户报错签名一致。
- 修复：`位与(_内存读整数(_内存基址,0), 65535) ≠ 23117`（按低 16 位比较）。gen_module.py 已改，门禁 miniblink_ec_guard.txt 已加该语句。
- 验证：修复版模块（mzfix/miniblink.ec 423030 B）在探针里 gen2 附着成功 `池=0x50000000 状态=1`；已部署 D:\DevelopmentFolder\常用模塊\miniblink.ec（MD5 D818F7B2E2A71EE09AB5943CC202601C；旧版备份 _rollback/miniblink.ec.bak-20260927-mzfix前）。
- 待办：用户需在 IDE 里重新编译小栗子应用（模块在编译期嵌入）。
- 附带发现：池为 PAGE_READWRITE 而映射器在池内执行，进程开启 DEP（nxcompat）会 0xC0000005 —— 自编测试宿主需 `-Wl,--disable-nxcompat`；小栗子宿主未开 DEP 故无碍。

## ⚠️ 无头编译链路对本工程不可用（2026-09-27 实锤）

- `ecompile.py`（mkcage＝e2t→改 lib 表→t2e）编译 小栗子应用.e 恒报 `错误(10022): 子程序"_OnGroup"…`，9/25 与 9/27 两次完全一致；但同一源码在用户 IDE 里编译正常（12:24 DLL 为证）⇒ 不是源码错，是往返链路缺陷。
- 对比「原始 .e 转出的文本 A」与「往返产物再转出的文本 B」（`_mbk_src/xlzy_audit{,_b}`）：**t2e 写回时把所有含模块常量（`#…`）的语句静默置为禁用（`' ` 前缀）**——初始化的 3 条 `置应用权限申请`、`_AppStart` 的 `返回(#启用响应_完成)`、4 处 `返回(#消息处理_忽略)`、`_OnPrivate/_OnGuildPush/_OnEvent` 里约 90 条 `.判断开始/.判断` 分支头全部被注释。`--check` 能「通过」是因为「整子程序无返回」触发易语言自动补 `返回 0`，恰好 `消息处理_忽略=0` 数值相同，属侥幸。
- **结论/红线：小栗子应用.e（以及任何重度使用模块常量的工程）禁止用 mkcage/ecompile 往返链路出产物**——即使编译通过，产物行为也已被静默改变。此类工程只能由用户 IDE 直接静态编译。`10022 _OnGroup` 报错 = 该缺陷的冰山一角，勿再当源码问题排查。
- 现场证据：`_mbk_src/`（xlzy_audit＝A、xlzy_audit_b＝B、xlzy_rt.e＝纯往返产物、xlzy_caged.e＝加壳产物）；加壳产物与纯往返产物代码区逐字节一致，仅差 elang_addin 库记录与索引位移。

## 阶段1 抓证：真宿主卡死现场（2026-09-27 19:25，hangcatch v3）

**现象链**（`_mbk_scratch/hang_report_192545.txt`、dmp 同目录）：
- 宿主每个世代：启动活跃 ~10-14s（逐个载入插件）→ **全进程硬冻结**（所有线程 RIP 恒定、CPU 增量=0.00）持续 **~30s** → 守护 protector.exe 杀掉重启（换 PID）→ 无限循环。与用户「卡死 30 秒自动重启」吻合。
- 冻结前线程数 46 → **169/177**（10.1s，node.dll 线程池爆出 ~120 个工作线程）→ 1.4s 内全体冻结。**node.dll 已完整映射进模块表**（@55470000+2048000）。

**主线程（TID 83436，ESP=0x1AF388）深扫 2KB 栈**（外→内）：
```
小栗子应用.dll+0x106370 → +0x222450 → +0x4C1D → +0x37D9D → +0x37D55
  → [0x8C 匿名帧空隙：MbKit 映像/池跳板帧，模块表不可见]
  → node.dll+0x5C1436
  → ntdll+0x9228E
  → 小栗子应用.dll+0x4BD60（疑似陈旧残留，待定）
  → KERNELBASE+0x156B42/0x16A3D0/0x156BD8 → ntdll 等待原语
```
- 其余 ~120 个 node 工作线程全部睡在 node.dll 内部同步点（+0x172D26/+0x1737FC 等）；宿主工作线程睡在自己的等待点。全进程无人在跑 ⇒ **初始化死锁**而非死循环。
- 等待原语不是 WaitForMultipleObjectsEx（参数区读出垃圾值）。
- 小栗子应用.dll（MD5 c58fc0c7 = 用户 18:24 IDE 编译版）以哈希名 `c58fc0c7...dll@17530000` 由宿主 LoadLibrary 正常加载。

**结论**：卡死点在「插件初始化链 → MbKit/内核 → node.dll 加载或初始化」内，主线程死等某同步对象，node 工作线程全部无法推进。收敛为两大候选：
- **H1'**：主线程在宿主 LoadLibrary(插件) 调用链内执行到 MbKit 映射器 → MbKit DllMain → LoadLibrary(node.dll) → node DllMain 创建工作线程 → 工作线程等 loader lock（被主线程加载链持有）→ 三环死锁；
- **H3'**：node.dll 初始化本身在等自己的线程池/辅助进程（与 loader lock 无关的内核内部死锁）。

**裁决手段**：模块插桩 trace（T0-T8，写入 `%TEMP%\mbkit_trace.log`）——gen_module.py 已加 `_追踪写` + 8 个阶段探针；若日志停在 `T5 调映射器前` ⇒ H1'，若 T6/T8 已过 ⇒ H3'。

**工具**：`_mbk_scratch/hangcatch.py`（v3）——跟踪最新宿主 PID、250ms 采样全线程 RIP、2s 静止判定卡死→先杀守护再抓证（主线程 2KB 深扫+模块归因）+ minidump + 收尾清理。已知限制：匿名内存帧不打印（只归因已加载模块与 MbKit 池区）。

## 阶段2-4：插桩定位 → 根因定罪 → 修复验证（2026-09-27 20:00-21:45）

### 阶段2 插桩部署
- 模块侧：gen_module.py 加 T0-T8 探针 + `_追踪写`（输出到池诊断文件），重生成 `D:\DevelopmentFolder\常用模塊\miniblink.ec`（门禁 miniblink_ec_guard.txt PASS，旧版已备份）。关键探针：T1R=池已存在时恢复附着复用（AILI·LIFE 先建池场景）。
- 插件侧：工作区 `_mbk_src\hang_e` 加 P 探针（`_追踪P` 追加写 `%TEMP%\mbkit_trace.log`，GBK）。
- **t2e 禁用三坑**（无头编译链路）：① `返回(#SDK常量)` 语句被静默禁用→10022 缺返回，须改字面量 `返回(0)`（主程序集 5 处已改）；② krnln `启动线程(...)` 语句被静默禁用；③ DLL 命令参数不接受 `&子程序` 实参（10044）。绕开：精易 `线程_启动(&子程序, 0, 线程ID)` 编译通过。
- 插桩 DLL 三代宿主验证全绿（T1-T8 + P-1~P0 全通）。

### 阶段3 根因定罪
- **幽灵建池者**：`AILI·LIFE.dll`（MBK1=1 / wkeInitialize=5 特征字节扫描确认）在宿主逐个载入插件时**先于本插件**用旧版 miniblink.ec 建池 → 本插件走 T1R 恢复附着复用旧池（三代宿主全绿）。
- **死锁机制**：node 的**首次 wkeInitialize**（爆发 169+ 工作线程）在宿主主线程载入链内同步执行 = loader-lock 级死锁——19:25 冻结现场主线程栈：`KERNELBASE+0x156BD8/0x16A3D0/0x156B42 等待链`，169 线程全冻。
- 裸进程（`_mbk_scratch/loader.exe` 热重载模拟器）同路径全新初始化 T1-T8 全通 ⇒ 排除映射器/池阶段（H1' 映射器分支），死锁条件=「主线程载入链内 + node 首次初始化」同时成立；池已就绪时 wkeInitialize 幂等不死。

### 阶段4 修复迭代
- **异步线程方案（试验A）**：`线程_启动(&_延迟初始化线程,0,线程ID)` + 延时 2000ms——编译通过、异步链 P1T→P4 全通，**但主线程随后冻结**（KERNELBASE 等待链与 19:25 同构，嫌疑精易 `线程_启动` 磁盘实现，未定罪）。放弃。
- **主线程延时错峰（试验B，最终方案）**：`_按钮1_被单击` 开头 `延时(2000)` 后同步执行原 wke 链。21:25 验证：P1→P1W(精确2000ms)→P2→P4 全通，**主线程回到消息循环**（RIP=win32u.dll，USER32/宿主exe 帧 = 正常空闲 GUI 形态），宿主稳定运行 279s 守护未重启 ⇒ **死锁纯属载入期时机窗口，延时错峰即可修复**。
- 21:25 报告 verdict=HANG@288s 为 hangcatch 空闲误判（主线程 win32u + 181 worker 闲置 ntdll；守护全程未触发）——**判读铁律：看主线程 RIP**（win32u/GetMessage=空闲；KERNELBASE+小栗子应用.dll 帧=真冻）+ 守护是否触发 + trace 是否推进。
- 启动期 85740→98948→44772 三次换 PID 发生在前 9 秒、且 85740 连 T0 都未执行 = 守护/宿主启动竞争噪音，与 miniblink 无关。

### 回归验证（21:35-21:38，`_mbk_scratch/regress3.py`）
3 轮 `hangcatch --timeout 60` 全绿：每轮 trace 完整链 `T0→T1R→P-1→P-2→P0→P1→P1W→P2→P4`，单代宿主无重启，verdict 全部 **no-hang**，主线程 RIP=win32u（空闲）。

## e2txt 工具链闭环（2026-09-28）——修复源码正式出产物，推翻旧红线

### 用户报告的解包缺陷（实锤+定罪）
- e2txt v1.2（JimStone 2020）`e2t` 解包 小栗子应用.e 报 4 条 `解析【类型】名称 失败！[支持库] 20994`（类型 09/03/04 = 窗口/按钮/图片框成员引用），产物中 4 处语句变成 `' 图片框1.宽度 ＝ 设置窗口.<?未知名称:支持库不存在?>`。
- 根因：e2txt 内嵌 BaseELangIDE.dll 的支持库知识太旧，认不出新式「窗口类型成员」引用；把 fne 拷到 `D:\Tools\e2txt\lib\` 无效（知识内嵌，与磁盘 lib 无关）。写方向（e2t）残废，**读方向（t2e）正常**——t2e 按 form.json 组件属性表解析成员名。

### 三条工具路线终局对比
| 路线 | 解包 | 回包/编译 | 结论 |
|---|---|---|---|
| e-packager v1.2.13 | 完美（lib 就位零占位符） | 全量重建死于模块全局函数/窗口名实参 | 弃用 |
| e2txt 直通（不改文本） | 4 占位符 | 396 错（模块常量 102 语句丢失） | 弃用 |
| **e2txt + 文本修复** | 同上 | **t2e err_count=0** | ✅ 采用 |

### e2txt 修复链（`_mbk_src\e2t_rt\`，txt_fix 文本 → out4.e）
1. 模块常量 → 字面数值：全表 1095 项（98 唯一常量）按名字边界正则替换，先 grep `“[^”]*#` 确认字符串字面量无 `#`。**这一步同时破解了旧 §「无头编译链路不可用」红线的根因**（t2e 静默禁用含 `#模块常量` 语句）——字面量化后语句全部存活。
2. 4 处占位符 → 限定形式真文（`图片框1.宽度 ＝ 设置窗口.宽度`）；t2e 不可编码形式实测：裸 `标题 ＝`、`＝ 宽度`（未知的变量）、事件子程序调用 `_设置窗口_尺寸被改变 ()`（未知的方法类型）→ 内联展开。
3. 两处装饰性 `标题 ＝ “miniblink_test”` 删除（form.json 根控件 ExtensionData `0E000000`+ASCII 已含设计时标题）。
4. 核心修复：`_设置窗口_创建完毕` 插入 `延时 (2000)` + `_按钮1_被单击 ()`。
- 双次往返验证：out4.e 重解包 **零占位符**；权限申请字面值 1/25/{52,53} 存活；`返回 (#…)` 清零；分支头完好。`, 数组` 后缀为 e2t 回读显示口径（bit3），IDE 不按数组处理。

### 编译部署与宿主验证（12:19-12:31）
- `ecompile.py --check` → **syntax-ok**；`--kind static_dll` → 成功，`小栗子应用_out4.dll` 2,379,776 B（MD5 2FB00EADF7830561833D45A6BC896A8B）。已部署 `框架-爱丽\main\plugin\小栗子应用.dll`（旧版备份 `.bak-0928-e2txt链`，MD5 13238AA3EDE5D6AD989F2004F315164E）。
- 宿主实测两轮：新 DLL 载入成功（`载入耗时:468ms`，日志 12:22:55）；第一轮 hangcatch 90s verdict=HANG@51.1s = **空闲误判**（主线程 win32u 消息泵、最后 8 采样 CPU 仍 +0.00/+0.05 交替、WM 参数区垃圾——符合 §判读铁律）；严格阈值 `--stable 40` 复测 **no-hang**（75s）。
- 启动期前 9 秒 2-3 次换 PID 在两轮均复现且发生在插件载入前 = 宿主自身启动竞争噪音（佐证旧 §L6）。
- **注意**：设置窗口仅在用户点插件菜单（`_ControlPanel`→`_menuA`→`载入 (设置窗口)`）时才载入，wke 链不在启动路径上——启动日志无 `node设置` 输出属正常。最终功能验证待用户点菜单实测。
