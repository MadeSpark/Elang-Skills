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

实证：`JM下载_移植版.e` 有 **5 处** `.如果真结束`(@8/@12) 紧接 `.计次循环尾 ()`(@4/@8)，分别是
`下载本子` / `下载章节` / `API取明文` / `下载图片` / `取分带基准ID`，IDE 里流程线崩坏（红竖线、收口箭头错位）；
用户手动补 5 条 8/8/8/12/8 空格空行后恢复正常。
逐字节比对坏件 `JM下载_移植版.e` 与修复件 `JM下载_移植版_流程修复.e`，**全文差异只有这 5 条空白行**
（与 `SKILL.md` §1.2、`scripts/echeck.py` 的 `check_unwind_tail_gap` 文档串一致）。

## 一条命令复跑（自包含）

```bash
# 在仓库根目录：19 个文件级用例 + 1 个内存档 + GBK 管道自检 = 21 组，末行「全部通过」即合格
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python "re/规则⑥验收证据/run.py"

# 单跑某一例（fixtures/<用例名>/ 就是可组包的真实文本目录）
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python tools/echeck.py "re/规则⑥验收证据/fixtures/bad_reverse_explicit" --branch-only --json
```

`run.py` 断言 21 组（19 个文件级用例 + `unit_next_indent_gate` 内存档 + GBK 编码自检）；最近一次完整输出存于 `run_log.txt`（`run.py -v` 逐例原文）。
用例文本在 `fixtures/<用例名>/程序集1.static.e.txt`：**「缩进空行」是若干空格**（行尾空白有语义），
不要用会自动裁剪行尾空白的编辑器改这些文件。

> 历史记录：v1.3.8 那批 `before/partial/full` 样本来自已删除的 `.scratch_session/`；
> 本目录的 `before.json` … `gate_before_actual_e.json` 是当时跑出来的原始 JSON，只作留档。
> 现在 `before` 的等价形态由 `fixtures/jm_five`（5 处隐藏尾→显式尾）与 `fixtures/bad_unwind`（隐藏尾→隐藏尾）覆盖。

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
- `JM下载_移植版.e`：`流程分支: ✗ 错误 5`（exit 1）；`JM下载_移植版_流程修复.e`：`✓ 0 错误`（exit 0）。
- 全工作区回归（2026-10-27，437 个去重 `.e.txt` / 16137 处块尾相邻实例）：**非隐藏目录没有任何新增命中**——
-  基线 152 条错误与改前逐条一致，规则⑥ 仍只有 `miniblink\miniblink_只读拆包1025` 那 1 条旧的「隐藏尾→更浅隐藏尾」。
- 隐藏目录（`glob` 不扫，需显式传路径）：
-  - `.scratch_session` → 7 条：`before` 2 条（`格式化字节数`）、`bad_unwind` 1 条、`bad_reverse_explicit` 1 条（新负对照）、`mixed_*` 3 条规则③；`reverse_legal` 已不再命中。
-  - `.workbuddy/tmp/ck` → 46 条，其中 **27 条**是规则⑥：这些是 2026-10-20 那批「下载接管」A/B demo 的临时副本，
-    正是当年被误判为「合法嵌套收口」的形态 —— 新口径下它们**应该**被报出，这是修好漏报，不是新增误报。
- 稳定材料：**本目录已自包含**——`fixtures/`（19 个可组包用例）+ `run.py`（21 组断言）+ `run_log.txt`（最近一次完整输出）+ `corpus_diff.py` / `corpus_diff.txt`（全量对比，见 §全量误报扫描）；
  早期 `.scratch_session/` 已随仓库清理删除，本目录的 `*.json` / `*.exit.txt` / `*.stderr.txt` 是当时留下的原始输出。

## 用例清单（`run.py` 断言的全部 21 组）

| 用例目录 | exit | errors | 抓的是什么 |
|---|---:|---:|---|
| `bad_unwind` | 1 | 1 | 隐藏 A → **更浅隐藏 B**，无空行 → 规则⑥ |
| `good_unwind` | 0 | 0 | 同上 + 8 空格空行 → 合法 |
| `bad_reverse_explicit` | 1 | 1 | 隐藏 A → **更浅显式尾**（`.计次循环尾 ()`），无空行 → 规则⑥（JM 形态） |
| `reverse_legal` | 0 | 0 | 同上 + 8 空格空行 → 合法 |
| `explicit_loop_legal` | 0 | 0 | A 是显式尾 → 不属规则⑥ |
| `explicit_loop_mixed` | 0 | 0 | 同上且带空行 → 仍不判 |
| `hidden_mixed` | 1 | 1 | 有空行但缩进错 → 规则③，规则⑥**不重复报**（断言报错文本里没有「更浅的」） |
| `good_two_blank` | 0 | 0 | A 后两条 8 空格空行 → 合法 |
| `cross_sub` | 0 | 0 | 跨 `.子程序` 边界不串判 |
| `else_marker` | 1 | 1 | 隐藏尾紧贴中段标记 → 规则⑤，规则⑥不重复 |
| `jm_five` | 1 | **5** | JM 五处形态复刻（`下载本子`/`下载章节`/`API取明文`/`下载图片`/`取分带基准ID`），应命中 5 条 |
| `jm_five_fixed` | 0 | 0 | 同上，每处补 A 同缩进空行 → 0 误报 |
| `bad_explicit_kinds` | 1 | 4 | 四种显式尾逐个命中：`.判断结束` / `.判断循环尾` / `.变量循环尾` / `.循环判断尾`（守 `TAIL_KIND` 不被改窄） |
| `bad_if_end_explicit` | 1 | 1 | A 是隐藏尾的另一个变体 `.如果结束` → 更浅显式尾 |
| `bad_unwind_chain` | 1 | 2 | 三层链 A1@12 → A2@8 → B@4 全无空行：两个 A 各报一条，互不吞报 |
| `chain_fixed_inner` | 1 | 2 | 只给内层 A2 补 8 空格：A1 仍报规则⑥；该空行相对 A1(12) 缩进不符 → 同时 1 条规则③（证明两道规则各行其责） |
| `gap_wrong_indent_explicit` | 1 | 1 | 有空行但缩进是 0 → 只报规则③，规则⑥ **0 条**（断言文本里没有「更浅的」） |
| `explicit_A_all_kinds` | 0 | 0 | 五种显式尾当 A（含「显式→显式」）→ 一律不判 |
| `crlf_bom_variant` | 1 | 1 | 同一坏样本另存为 **BOM + CRLF** → 结果一致（覆盖 `read_texts` 归一） |
| `unit_next_indent_gate`（不落盘） | — | — | 直接喂 `check_unwind_tail_gap`：B 同缩进 / B 更深 / A 是显式尾 / 已有空行 → 必须 **0 条**（锁住 `next_indent >= indent` 这道门） |
| `gbk_pipe_utf8_output`（不落盘） | 1 | — | `PYTHONIOENCODING=gbk` 重定向下 `_ensure_utf8_output()` 不崩、无 `Traceback`、`✗` 行完整 |

断言不只是条数，还校验：`branch.errors`/`branch.warnings`/`branch.passed` 与 `errors` 一致、`warnings` 恒为 0、
`branchOnly: true`、每条报错文本里**必须出现**和**不允许出现**的片段，以及「片段出现次数」
（`bad_unwind` 与 `hidden_mixed` 都是 exit 1 / 1 条错误，只有按消息特征串计数才分得清是规则⑥ 还是规则③，防重复报）。

`scripts/echeck.py` 的 **stdout 一律 UTF-8**（`_ensure_utf8_output()` 在 `main()` 开头把 `✓✗⚠` 无法编码的流重设为 UTF-8，
含 `--json` 模式）；外部调用方按 UTF-8 解码即可，不要依赖系统 locale。

## 全量误报扫描（2026-10-29）

退回 `tools/echeck.py` 前，用同一份语料对**旧脚本**与**新脚本**逐文件对比 `run_branch_checks()` 的输出：

| 项 | 结果 |
|---|---|
| 扫描 `.e.txt` 文件 | **3359**（`demos/` `src/` `QAQ-ETool/` `miniblink/` 等，跳过 `.git/.tmp/node_modules/__pycache__/.local`） |
| 旧脚本 / 新脚本 报错总数 | 151 / **151** |
| 命中文件数 | 79 / **79** |
| `only_old`（新脚本漏报） | **0** |
| `only_new`（新脚本新增误报） | **0** |

复跑（两条命令，`corpus_diff.py` 只比「位置 + 规则措辞」，不比后半句解释）：

```bash
git show HEAD~1:tools/echeck.py > "$TEMP/old_echeck.py"   # 或任意一份旧脚本
PYTHONIOENCODING=utf-8 PYTHONUTF8=1 python "re/规则⑥验收证据/corpus_diff.py" "$TEMP/old_echeck.py" tools/echeck.py
```

⇒ 扩到显式尾**没有**在现有语料里制造任何新命中（现有语料没有 JM 那种「隐藏尾 → 更浅显式尾且紧贴」的形态，
那种形态只在被删掉的 `JM下载_移植版.e` 里，故由 `fixtures/jm_five` 提供正向证据）。
