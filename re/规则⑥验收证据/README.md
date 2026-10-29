# 规则⑥ 文件级验收材料（稳定副本）

版本：commit `6c67eff`（v1.3.8）；**2026-10-27 二次定案**（规则⑥ 扩到显式尾，见 §二次定案）。

规则⑥口径（2026-10-27 起）：**A 是隐藏块尾**（`.如果结束` / `.如果真结束`），**B 比 A 更浅**（`B.缩进 < A.缩进`），
且 A、B 之间没有与 A 同缩进的空行 → 错误。B 可以是隐藏尾，也可以是显式尾
（`.计次循环尾` / `.判断结束` / `.判断循环尾` / `.变量循环尾` / `.循环判断尾`）。
A 是显式块尾时不判（显式尾自己会画可见收尾线）。

## 二次定案（2026-10-27）

| 判据 | 旧口径（2026-10-20 → 10-24） | 新口径（2026-10-27 起） |
|---|---|---|
| B 的类型 | 只算隐藏块尾 | 隐藏尾 / 显式尾都算 |
| 判据核心 | A、B 都是隐藏尾 | 只看缩进：B 比 A 更浅 |
| `demos/下载接管` A/B demo | 据此判定「显式尾紧贴合法」 | 该结论错：无弹框、块模型行数完整 ≠ 流程线画对 |

实证：`JM下载_移植版.e` 有 4 处 `.如果真结束`(@8/@12) 紧接 `.计次循环尾 ()`(@4/@8)，
IDE 里流程线崩坏（红竖线、收口箭头错位）；用户手动补 4 条 8/8/8/12 空格空行后恢复正常。
逐字节比对坏件 `JM下载_移植版.e` 与修复件 `JM下载_移植版_流程修复.e`，**全文差异只有这 4 条空白行**。

## 指定命令

```bash
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Releases/Elang-AiTools/skills/elang-ai-coding/scripts/echeck.py .scratch_session/before --branch-only --json
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Releases/Elang-AiTools/skills/elang-ai-coding/scripts/echeck.py .scratch_session/full --branch-only --json
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Releases/Elang-AiTools/skills/elang-ai-coding/scripts/echeck.py .scratch_session/fixtures/bad_unwind --branch-only --json
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Releases/Elang-AiTools/skills/elang-ai-coding/scripts/echeck.py .scratch_session/fixtures/reverse_legal --branch-only --json
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Releases/Elang-AiTools/skills/elang-ai-coding/scripts/echeck.py .scratch_session/fixtures/bad_reverse_explicit --branch-only --json
```

| 样本 | exit | errors | warnings | 说明 |
|---|---:|---:|---:|---|
| before | 1 | 2 | 0 | 两处都在 `格式化字节数` |
| partial | 0 | 0 | 0 | 两处已补 8 空格 |
| full | 0 | 0 | 0 | 全量修复 |
| bad_unwind | 1 | 1 | 0 | 隐藏 A → 更浅隐藏 B，无空行（规则⑥） |
| good_unwind | 0 | 0 | 0 | 隐藏 A + 8 空格 |
| reverse_legal | 0 | 0 | 0 | 隐藏尾 → 更浅**显式**尾，**带** 8 空格空行 → 合法（2026-10-27 修正：此前该样本没有空行，被误判为合法） |
| bad_reverse_explicit | 1 | 1 | 0 | 隐藏尾 → 更浅**显式**尾，**不带**空行 → 规则⑥命中（JM 真实形态） |
| explicit_loop_legal | 0 | 0 | 0 | 显式循环尾 → 隐藏尾：A 是显式尾，不属规则⑥ |
| explicit_loop_mixed | 0 | 0 | 0 | 同上 |
| hidden_mixed | 1 | 1 | 0 | 规则③命中（隐藏 A 的空行缩进错），不是规则⑥ |
| good_two_blank | 0 | 0 | 0 | 隐藏 A + 两条 8 空格 |

## ecompile 门禁

```bash
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python Releases/Elang-AiTools/skills/elang-build/scripts/ecompile.py .scratch_session/before --check --json --workdir .scratch_session/evidence/gate_work
```

实际：进程 exit `3`；`branchGate.ok=false`、`branchGate.exitCode=1`、`branch.errors=2`、`warnings=0`；顶层 JSON 没有 `logFile`（编译器未启动）。

同一门禁对**实际未修复的 `.e`**（`_流程修复_修复前.e`）也实测过：`gate_before_actual_e.json` / `gate_before_actual_e.exit.txt`，exit 3、2 条错误、无 `logFile`。

## 基线 / 边界

- 大项目基线：规则⑥ 0；流程分支 14 错误；总错误 180 / 总警告 56。
- demos 01/02/03：规则⑥ 0（A 后面都带 1 条同缩进空行）。
- 「**显式**块尾 → 更浅尾」仍不判：A 是显式块尾就不进规则⑥（显式尾自己会画可见收尾线）。
- `JM下载_移植版.e`：`流程分支: ✗ 错误 4`（exit 1）；`JM下载_移植版_流程修复.e`：`✓ 0 错误`（exit 0）。
- 全工作区回归（2026-10-27，437 个去重 `.e.txt` / 16137 处块尾相邻实例）：**非隐藏目录没有任何新增命中**——
-  基线 152 条错误与改前逐条一致，规则⑥ 仍只有 `miniblink\miniblink_只读拆包1025` 那 1 条旧的「隐藏尾→更浅隐藏尾」。
- 隐藏目录（`glob` 不扫，需显式传路径）：
-  - `.scratch_session` → 7 条：`before` 2 条（`格式化字节数`）、`bad_unwind` 1 条、`bad_reverse_explicit` 1 条（新负对照）、`mixed_*` 3 条规则③；`reverse_legal` 已不再命中。
-  - `.workbuddy/tmp/ck` → 46 条，其中 **27 条**是规则⑥：这些是 2026-10-20 那批「下载接管」A/B demo 的临时副本，
-    正是当年被误判为「合法嵌套收口」的形态 —— 新口径下它们**应该**被报出，这是修好漏报，不是新增误报。
- 稳定材料：`.scratch_session/`（完整文本目录与 fixtures）；本目录保存 JSON/exit/README。
