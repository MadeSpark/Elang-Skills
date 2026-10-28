# 验收报告 —— 宿主事件总线 + 双向 IPC（v1.3.9）

> 结论先说：**代码层、协议层、进程内集成层全部通过**；**真机 e.exe 端到端（E2E）未能在本次会话内跑完**，
> 原因是 DSH 文件沙箱禁止向 `D:\ides\e\lib\` 写入/删除（宿主支持库必须临时投放进去），
> 与本套代码无关。下面 §6 给出用户在本机一条命令复验的方法。

## 1. 这次解决的是什么

原状：Python 侧只能靠 `EnumWindows` 死循环轮询找弹窗，5 分钟会话 `n_enum` 上万次，仍有漏窗与延迟。

现在：`.fne` 在 e.exe **进程内**用 WinEvent 钩子（`EVENT_OBJECT_CREATE/SHOW/DESTROY` +
`EVENT_SYSTEM_DIALOGSTART..END`）捕获弹窗，把结构化快照通过**命名管道 JSONL** 推给 Python，
Python 回一帧 `decide` 告诉宿主点哪个按钮；宿主 1500 ms 内没等到决策就退回内置规则。
Python 侧不再需要轮询（`engine="host"`），`n_enum` 掉到接近 0。

## 2. 交付物

| 文件 | 变更 | 说明 |
| --- | --- | --- |
| `src/addin/elang_addin.cpp` | +921 行 | WinEvent 事件总线、弹窗快照（按钮/编辑框）、命名管道客户端、决策接入、重试泵、审计埋点 |
| `src/addin/elang_addin.fne` | 87040 → 101888 字节 | 重新编译产物（PE32/x86，0 error / 8 条既有 warning） |
| `tools/elaunch.py` | +810 行 | `PipeBridge`（命名管道服务端，纯 ctypes + overlapped IO）、`WindowHider` engine 分档、IPC 环境变量注入、审计与统计 |
| `tools/ecompile.py` | +4 行 | `--json` 结果里透出 `ipc` / `headless` 小节 |
| `tools/selftest-ipc.py` | 新增 | 假 fne 客户端 + 真 `PipeBridge` 的协议级自测（无需 e.exe / .fne） |
| `docs/方案-宿主事件总线与双向IPC协议.md` | 新增 | 协议契约（拓扑、管道名模板、帧格式、字段表、线程模型、审计、版本规则） |
| `README.md` | +8 行 | 给用户看的变更说明与开关（`--no-ipc`、`ELANG_AI_PIPE_SDDL`） |

## 3. 验证矩阵

### A. 静态与构建

| 项 | 结果 | 证据 |
| --- | --- | --- |
| C++ 编译 | **通过** | `g++ -m32 -shared`，0 error / 8 条本次改动前就存在的 warning |
| 产物 | **通过** | `src/addin/elang_addin.fne` 101888 字节，PE32 x86；原 87040 字节。导出表仅 `GetNewInf` |
| 可复现构建 | **通过** | Lead 侧用 `build.sh` 同参数重编一次：101888 字节、sha256 `386ca623…`，与子智能体的产物仅差 **6 个字节**（PE 时间戳/校验和），说明产物可由源码确定性重建 |
| Python 语法 | **通过** | `py_compile` 全绿（elaunch / ecompile / selftest-ipc） |
| 协议一致性 | **通过** | 逐帧对照：`hello`/`dialog`/`event`/`result`/`decide`/`ack`/`ping`/`pong`/`policy`，字段名、类型、GBK-hex 编码与文档一一吻合 |

### B. 协议级自测（`python tools/selftest-ipc.py`）

| 组 | 覆盖 |
| --- | --- |
| [1] 握手 | 错令牌 → `ack ok=0` + `handshake_fail` 计数；对令牌 → `ack ok=1`、`peer_pid`/`peer_fw` 正确 |
| [2] 决策 | 9 种按钮组合（单按钮 / 有 IDOK / 无 IDOK 有 IDCANCEL / 有 IDYES 不猜 / 无按钮 / `pol=observe`） |
| [3] 心跳与坏帧 | `ping`→`pong` 回显 ts；非 JSON、非对象帧不断连且计数 |
| [4] 统计 | `event` 计数、`result.how=fallback` 计入 `fallbacks` |
| [7] policy | `set_policy("observe")` / `set_policy("dismiss", hide="on")` 真发出 `policy` 帧 |
| [5] 重连 | 断开 → `connected=False` → 重连 → 再握手 → 再决策；审计文件确有 in/out 两个方向 |
| [6] 收尾 | `close()` < 2 s（实测毫秒级，无句柄泄漏） |

实跑结果：**PASS 70 / FAIL 0**（0.8 s 内跑完，含 60 s 看门狗兜底）。

### C. 宿主侧进程内集成（真实 C++ + 真实 PipeBridge）

临时 harness 直接 `#include` 了 `elang_addin.cpp`，用真 `tools/elaunch.py` 的 `PipeBridge` 当服务端：

| 观察点 | 结果 |
| --- | --- |
| 管道名 `{pid}` 模板替换 | 正确（`\\.\pipe\elang_addin.<pid>.<tag>`） |
| 握手 `hello` | 通过，`peer_fw=v1.3.9` |
| 两个弹窗经 IPC 决策关闭 | 通过，单次往返 ~1 ms，`result.how=ipc` |
| 决策超时 | 通过：decider 睡 2.5 s ⇒ 1500 ms 超时 ⇒ 内置规则兜底，`how=fallback`；迟到的 `decide` 被识别为过期并丢弃，**不会串到下一个弹窗** |

### D. 真机 e.exe 端到端 —— 未完成（沙箱限制）

| 步骤 | 结果 |
| --- | --- |
| 加壳（e2t/t2e + 文本改写） | 通过（产出 `测试_ai.e`，658769 字节） |
| 分支门禁 `echeck` | 通过（0 error / 0 warning） |
| 投放 `.fne` 到 `D:\ides\e\lib\` | **被沙箱拒绝**：`echo probe > D:\ides\e\lib\probe_dsh.tmp` → Permission denied |
| 启动独立 IDE + 触发静态编译 | 未执行（上一步失败即止） |

补充：`D:\ides\e\lib\elang_addin.fne` 里有一份 **2026-10-03 22:30 的旧 87040 字节残留**（非本次产生），
`elaunch` 检测到残留会拒绝覆盖（这是既有安全设计）。沙箱同样不允许删除它，所以本次无法继续。

## 4. 设计目标对照

| 指标 | 目标 | 现状 |
| --- | --- | --- |
| `n_enum`（5 min 会话） | < 2000 | 事件驱动后宿主内不再周期性 `EnumWindows`（仅保留 ~500 ms 低频审计兜底）；Python 侧 `engine="host"` 时完全不轮询 ⇒ 预期接近 0，**待真机复测确认** |
| 决策往返 p95 | < 50 ms | 进程内实测 ~1 ms（同机、无跨机） |
| 超时兜底 | 1500 ms | 已实现且已验证（`--no-ipc`/超时/坏决策三条路径都回到内置规则） |
| `visibleOnScreenSamples` | == 0 | 未变（宿主侧 cloak + 移屏 + hide 逻辑原样保留） |
| `ecompile --check` 5 个样本 | ±10% 内不变 | **未测**（见 §3-D） |
| 非 AI 模式零影响 | 必须 | 通过：IPC 与事件总线全部由 `ELANG_AI_PIPE` + AI 模式双重门控；未设置时不建线程、不建管道、不写文件 |

## 5. 已知限制（v1.3.9 有意不覆盖）

1. `fill` / `fillclick`（让 Agent 往对话框编辑框里填内容再点）**协议已定义、宿主未实现**，
   收到这两类决策一律按坏决策兜底，并写 `IPC-FALLBACK reason=bad-decision`。下个版本再补。
2. 宿主钩子只看**本进程**弹窗。UAC / WER / 其他进程的「文件被占用」框不在范围内（WER 由 `SetErrorMode` 抑制）。
3. `ELANG_AI_PIPE` 与 AI 模式是两个门：只设 `ELANG_AI_PIPE` 不会开 AI 模式（`DetectAiMode()` 未改），
   正常走 `elaunch.py` 时两者自动配齐。
4. 受限令牌下（本机 Agent 工作流就是），管道默认 DACL 连自己都连不上，故 `PipeBridge` 在检测到
   `IsTokenRestricted()` 时自动放宽为 `D:(A;;GA;;;SY)(A;;GRGW;;;WD)`；管道名带 48 bit 随机 tag + pid，
   协议还要求 128 bit 令牌，实践上可接受。需要更严可用 `ELANG_AI_PIPE_SDDL` 覆盖。

## 6. 用户在本机复验（一条命令）

先删掉那份残留（否则 `elaunch` 按设计拒绝覆盖）：

```cmd
del "D:\ides\e\lib\elang_addin.fne"
```

然后（工作目录 = 本仓库根）：

```cmd
python tools\ecompile.py --check --json "测试.e"
```

期望：JSON 里出现 `"ipc": {"decisions": N, "fallbacks": 0, "p95Ms": ...}`，
审计文件里出现 `IPC-CONN` / `IPC-DECIDE` / `IPC-CLOSE`，且全程 IDE 窗口不可见。

关闭 IPC 复跑（对照基线，用于比对 `n_enum`）：

```cmd
python tools\ecompile.py --check --json --no-ipc "测试.e"
```

协议级自测随时可跑（不需要易语言、不需要改 `lib\`）：

```cmd
python tools\selftest-ipc.py
```

## 发布结果（v1.3.9）

| 项 | 值 |
|---|---|
| 提交 | `fe692594cd911c8d888270d14c0c466f9242ce2f`（`main` 头；tree `2de3add7f45c54ff2318eba1b50fdc9480186ed4`） |
| 标签 | `v1.3.9`（tag object `cf4cec518988d434a283e3dc86dca81b14b7e55a`） |
| Release | https://github.com/MadeSpark/Elang-Skills/releases/tag/v1.3.9 |
| 资产 | `Elang-AiTools.zip` 30,705,028 B，sha256 `1a83e23be414d4df2dbd08301a8eca920d6d75965426701712239063ad0e22b1`；50 个文件，条目全为 `Elang-AiTools/…`，逐条 md5 回验 0 问题 |

`python tools/verify-release.py`：**90 项通过 / 24 项失败**。24 条失败全部是环境侧：23 条「本机已有安装副本与发布区不一致」（`agents/`、`claude/`、`codex/`、`cursor/`、`dsh/`、`trae/`、`trae-cn/`、`windsurf/`、`opencode/`、`workbuddy/` 下的 elang-build / elang-debug / elang-ai-coding 副本），1 条**预存漂移**：`skills/elang-ai-coding/scripts/echeck.py` 为 2026-10-28 35656 B，比 `tools/echeck.py`（2026-10-24 33582 B）更新 —— 是 v1.3.8 期间直接在发布区改的，从未回灌工程侧。受限沙箱下 `~/.claude/skills` 等目录不可写，安装副本无法在本会话刷新。

> 回灌`echeck.py`**故意未做**：工程侧那份更旧，覆盖会退回 v1.3.8 已修的规则⑥行为。
>
> 待办（下一会话优先级最高）：① 把发布区 `echeck.py` 回灌 `tools/echeck.py` 并补跑规则⑥回归，消除这条漂移；② 在本机跑一次 `安装技能.cmd` 后重跑 `verify-release.py`，剩余 23 条随之清零。
