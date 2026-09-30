# Debug Session: eide-freeze-crash
- **Status**: [OPEN]
- **Issue**: 易语言启动时窗口数秒无响应；退出后键盘鼠标卡顿 5-10 秒；编译有概率访问冲突闪退。
- **Debug Server**: http://127.0.0.1:7777/event
- **Log File**: .dbg/trae-debug-log-eide-freeze-crash.ndjson

## 本轮重启调查：500ms 验收

- 状态：[OPEN]，用户已批准 `.trae/documents/eide-input-latency-under-500ms-plan.md`。
- 本段以下历史记录保留供溯源，不代表当前结论。旧“iDraw 隔离”方案已被用户明确禁止，本轮完整保留 iDraw/CeTools。
- 历史间隔值与未经验证的注入不能证明“死钩子数量 × 300ms”。仅 SendInput 返回耗时也不是端到端交付时间。
- H1：IDE 钩子所属线程阻塞；H2：诊断消息泵自阻塞；H3：缺样本/输入空闲/分段误判；H4：系统调度或驱动延迟。均等待新的运行时证据。
- 现场：PID 73516 的线程数为 0，未将其判定为仍持有输入钩子；无运行中的采样器。原注入脚本未经有效验证，不再运行。
- 验收：原 e.exe 启动和正常退出全过程，鼠标及键盘延迟严格 <500ms；未覆盖、缺数据或探针异常均不得通过。
- 用户补充验收：连续数秒反复出现约 300ms 延迟同样不合格，不能仅以单事件 <500ms 判成功。
- 新物理取证：未安装额外低级钩子的情况下，启动鼠标消息最大排队约 313ms、关闭约 360ms，关闭期输入明显变稀疏。用户确认“仍连续卡顿数秒”，本轮明确未修复。尚无物理键盘样本。
- 用户要求测试窗口置顶，已落实；窗口遮挡/失焦会影响接收覆盖，必须单独记录。
- 用户要求工作区外变更先备份、明确验收前不得销毁回滚资料。当前安装目录 2714 项保护文件哈希未变，注册表未修改；临时 WPR 提权已获授权。

### PowerToys 对照与后续观测

- `pre-fix-20260927-021631-cycle1`：PowerToys 临时退出后，正常关闭 IDE PID 63224 仍失败。鼠标窗口接收峰值 605.5208ms，F24 按下 616.4538ms，松开 641.0359ms；每类 82 条均完整接收。SendInput 返回峰值仅约 308ms，不代表交付完成。
- 接收窗口 Timer 心跳最大间隔 3302.386ms，钩子泵 4611.1134ms，Raw Input 线程 53.6007ms，发送线程 319.5562ms。Timer 饥饿不能单独等同窗口完全停止处理输入；需补时间线和线程 ID。
- PowerToys 已恢复，备份 JSON 与当前配置哈希无差异。回滚资料 `.dbg/rollback-powertoys-20260927-021626` 保留，未经用户许可不删除。
- 旧 ETL `eide-input-system-0153.etl` 关闭区间 78.870–83.400s：CPU 自身采样热点为 `win32kfull!xxxWindowEvent`，并存在 `NtUserNotifyWinEvent`、USER 临界区等待。栈聚合计数不是 API 调用次数；该次关闭没有同步输入探针，不作精确因果关联。
- H1 仍待定位具体线程/组件；H2 仍需区分观察器停顿和消息 Timer 饥饿；H3 已确认历史单次峰值不足以验收，继续补连续时段；H4 尚未排除。仅退出 PowerToys 是不足以消除问题的对照结果，不代表它没有任何影响。
- 下一轮增加原生线程 ID、心跳停顿 QPC 区间和慢交付时段。以 100ms 为诊断慢事件线、100ms 为恢复合并间隔，明确区分“反复慢交付时段”与“完全无输入时长”；连续时段达到 500ms 不予通过。

### 同步 CPU 跟踪（022238）

- 新统计 14 项自测通过。600ms 专用接收窗口延迟实测为约 629ms 慢交付时段，正确 FAIL，心跳间隔约 631ms；未启动 IDE 的这一轮只验证探针。
- 旧 PowerToys 对照 CSV 按新规则重算：退出期间鼠标反复慢交付 3208ms（44 条），按键按下 3284ms（46 条），松开 3350ms（47 条）。
- `.dbg/input-cpu-20260927-022238.etl` 与 `pre-fix-20260927-022245-cycle1` 为同一轮。IDE PID 44832，探针 PID 58432；接收线程 57808、钩子线程 69164、Raw 41832、发送 34400。
- ETL 内 IDE 生命周期 24.579171–45.258340s，正常关闭请求约 36.82s。退出期鼠标峰值 971ms、键盘松开 1086ms，反复慢交付约 7441ms；跟踪可能放大延迟，不替代无跟踪基线。
- `.dbg/input-cpu-022238-ide-profile.html` 的关闭 CPU 栈（Sampled Profile）：6616 样本，`xxxWindowEvent` 包含 5341 样本（80.73%），自身 4932；`NtUserNotifyWinEvent` 包含 5684。这些是采样计数，不是通知次数。树控件销毁及 iControls 栈存在，但尚未证明其为错误源。
- `.dbg/input-cpu-022238-sender.html`：发送线程的 SendInput 路径及 USER 临界区等待均存在，无充分证据将延迟全部归因于低级钩子超时。下一步采集 Contention 和 QueueEventMessage/RetrieveQueueEventMessage，定位等待方与事件接收线程。
- WPR 保存完成，零 lost buffers/events；iDraw 全部 2714 项哈希一致，未改注册表。首次 process 导出早于 trace 完全保存，不含有效进程；以 `input-cpu-022238-process-final.csv` 为准。首次 SampledProfile 过滤名无空格导致空报告，以事件正则 `Sampled.Profile` 生成的报告为准。

## Reproduction Steps
1. 正常启动易语言并观察启动阶段是否无响应。
2. 编译一个已知可编译工程。
3. 正常关闭易语言并观察键盘、鼠标卡顿时长。

## Hypotheses & Verification
| ID | Hypothesis | Likelihood | Effort | Evidence |
|----|------------|------------|--------|----------|
| A | 退出后的残留 e.exe 或线程阻塞输入/窗口消息链 | High | Low | Pending |
| B | AutoLinker.fne 在启动/保存框路径阻塞 IDE | High | Medium | Pending |
| C | 其余支持库或其依赖 DLL 造成启动/编译访问冲突 | Medium | Medium | Pending |
| D | 系统级输入驱动或覆盖层在 e.exe 退出时超时 | Medium | High | Pending |
| E | 卡顿与编译闪退为两个独立问题 | Medium | Medium | Pending |

## Log Evidence
- 采集器首次运行失败：残留 `e.exe` 的 `StartTime/CPU` 可能为空，原脚本直接调用
  `.ToString()` 导致 PowerShell 空引用异常。已将采集逻辑改为逐字段 try/catch；此改动仅影响
  调试采集，不影响易语言、插件或系统设置。
- `2026-09-25 13:55:06` PID `58552` 与 `13:57:10` PID `55928` 均为异常残留：
  `ThreadCount=0`、`HandleCount=0`、`WorkingSetSize=20480`、无命令行、无可执行路径、
  无进程所有者；父进程已退出。对两个 PID 的精确 `Stop-Process -Force` 均被系统拒绝访问。
- 在无正常 IDE 进程状态下启动 `D:\ides\e\e.exe`，未形成可用的正常实例，也未写入新的
  Application Error 事件。这与易语言单实例协调路径被上述无效进程对象阻塞相符。
- 重启后的受控基线：正常 demo 工程主窗口在 4526ms 出现且 Responding=True；
  CloseMainWindow 成功，进程退出耗时 7718ms。后续采样复测退出为 3561-4505ms。
- 关闭期间单个 e.exe 线程连续运行约 3.3 秒，每 250ms 消耗 200-280ms CPU；其他线程
  主要为 `EventPairLow` / `UserRequest` 等待。该进程与线程均为 `Normal` 优先级，
  demo 工程未加载任何 `.fne` 模块。
- WER 对编译闪退的直接证据：`CeTools.dll 3.7.13.1` 同时出现 `c0000005` 与
  `c0000409(BEX)`；`ntdll.dll` 的访问冲突发生在其后，不能作为独立根因。
- WPR 系统级性能记录被当前用户权限策略拒绝（0xc5585011），未生成 trace 文件。
- 自动输入延迟实测：无 IDE 进程时，F24 合成按键 60 次平均 4.246ms、最大 18.652ms；
  IDE 关闭期间平均 261.520ms、最大 623.801ms。确认系统输入路径被退出中的 e.exe 拖慢。
- 已将 `HKCU\Control Panel\Desktop\LowLevelHooksTimeout` 写为 `100`，写前注册表快照在
  `re/_rollback/eide-input-hook-timeout/desktop-before-20260925-155400.reg`。同会话立即复测
  仍为平均 346.638ms、最大 619.898ms，表明该策略需重启/注销后才会被新钩子读取。
- 重启后验证（runId=post-fix）：demo 工程 2787ms 出现主窗口且 Responding=True；
  退出 4169ms。退出期间合成输入延迟平均 45.972ms、最大 249.605ms，相比修前
  平均 261.520ms、最大 623.801ms，平均延迟下降约 82%。

## Verification Conclusion
假设 A 已确认：崩溃后无效 e.exe 进程对象阻塞新 IDE 启动；重启已清除。
假设 B 已排除：AutoLinker 当前不存在，且最小 demo 未加载任何 FNE。
假设 C 已确认：iDraw 的 CeTools.dll 是编译闪退直接来源，iDraw 已隔离。
假设 E 已确认：退出卡顿与编译闪退为两个独立问题。历史只读取证（2026-09-24）已确认
`LowLevelHooksTimeout=25000`，是 Windows 默认 300ms 的 83 倍；这使任意失效的全局
低级键鼠钩子可以冻结输入 5-10 秒。该值本轮已再次删除，待重启后按默认 300ms 生效。
iDraw 保持隔离以避免已证实的 CeTools.dll 编译访问冲突。

- 干净基线（重启后、elang_addin 与 iDraw 已隔离）：正常 demo 工程主窗口 4526ms 出现且 Responding=True；CloseMainWindow 成功，进程退出耗时 7718ms。关闭卡顿独立于已隔离的两个扩展，需抓取 IDE 退出阶段的系统级等待证据。

- 10ms 重启后对 测试.e 自动复测：主窗口 4984ms 且 Responding=True；20.109 秒内未正常退出（测试脚本已仅按自身 PID 收尾）。输入平均 89.297ms、最大 642.021ms。结论：10ms 策略无改善峰值且显著破坏正常退出，必须撤回。

- 临时进程降优先级（BelowNormal）对 测试.e 的对照为负：启动 Responding=False，退出 18.663s，输入平均 93.579ms、最大 659.665ms。该设置仅用于测试进程，退出后自动消失；不能作为修复。

- 最终有效自动回归（删除 LowLevelHooksTimeout 覆盖值并重启后，测试.e）：三轮关闭分别 1933ms/2047ms/1835ms；关闭期 SendInput 峰值 14.57ms/22.12ms/9.91ms，三轮均 0 个 >150ms 阻塞。退出键鼠冻结已复现为消失。
- 无头编译即检查尝试：tools/ecompile.py --check 在临时投放 D:\ides\e\lib\elang_addin.fne 时被 TRAE Sandbox PermissionError 拦截（exit 3）；IDE 未启动，安装目录未更改。该限制不影响已完成的退出卡顿回归。
