# `re/` 目录说明（逆向 / 实验 / 物证，非交付产物）

> 本目录是**实验与逆向工作区**：PoC 脚本、受控实验、物证都在这里。
> **里面的任何东西都不是交付产物**——技能成品在 `Releases/Elang-AiTools/`，工具真源在 `tools/`，宿主库源码在 `src/addin/`。
> 结论性文档在 `docs/`（方案 / 分析报告 / 交接）。本 README 只做导览，防止误把实验件当成品、或误删物证。

## 1. 物证（勿动；结论都靠它们复核）

| 路径 | 是什么 |
|---|---|
| `addin/evidence/` | **PoC 核心物证**：`D_trace_full.txt`（免登记加载全 trace）、`D_selfcheck.txt`、`run{A,B,C}_*`（PoC-2 正常/中断/异常三跑）、`PoC2_crashtrap_iControls.txt`（0xC0000005 崩溃物证）。原始 GBK，中文 grep 用 `.utf8.txt` 视图 |
| `addin/POC1-结果与阻塞.md`、`addin/POC1D-免登记成功与__stdcall根因.md` | PoC-1 / 任务 D 结论文档（`__stdcall` 根因在这里） |
| `addin/lib_restore_diff.txt`、`addin/src/`、`addin/elang_addin.fne` | lib\ 还原 diff 自证；AddIn 实验期源码（正式源码已提升到 `src/addin/`） |
| `projD_base.e` / `projD_lib.e` / `projD/` | 加壳通道受控实验的基线/产物（6747→6818 B，任务 D 用的 `.e`） |
| `ework_decl/probe.e` | 与 `projD_lib.e` 同 md5 的加壳产物副本 |
| `verify_libsource.py` / `verify_libsource2.py` | **加壳通道四组受控实验**（「改 config.json 才是输入」的反证来源） |
| `diff_librec.py` ~ `diff_librec4.py` | `.e` 支持库表二进制格式逆向（`u16 条数 + Key\rGuid\rMajor\rMinor\rName(GBK)`） |
| `reg.txt` / `reg_before/` / `reg_snapshot.py` | 注册表快照物证与工具 |
| `mods_noproj.txt` / `proj_out.txt` / `pylib/` | e.exe 加载集合观测（76/77）与 pefile 等实验依赖 |
| `skill_smoke/` | 技能端到端冒烟物证（注意：`runA_harness.txt`/`smokeA_harness.txt` 是**已修 bug 的旧证据**，看 `runA2/A3`） |
| `rt_batch/matrix.json` | **真实工程往返损失矩阵**（7 工程，v6 方案文档引用） |
| `rt_big/`、`rt_mc/` | 两个全链路实测的留证：加壳产物 `caged.e` + `*.debug.log`(+utf8) + `*_harness.txt`/`*_trace.txt`（`rt_big`=0.66MB 普通工程 → `ok`；`rt_mc`=1.49MB 内嵌双 .ec 模块工程 → `stopped`，编译 208ms） |
| `rt_ailian/result.json` | 1.23MB 样本工程往返数据点 |

## 2. 实验 / 逆向脚本（一次性，保留备查）

`analyze_e.py`、`classes_e.py`、`strings_e.py`、`strings2_e.py`、`find_classes.py`、`check_guid_bytes.py`、
`compare.py`、`probe_debug_io.py`、`regread.py`、`run_proj.py`、`run_enum.py`、`run_enum2.py`、`run_f5.py`、
`snap_state.py`、`sweep_state.sh`、`readgbk.py`、`mod2.py`、`opens_e_probe.py`、`opens_decl_e.py`、
`launch_ai.py`、`poc1_dualrun.py`、`poc1_launch_e.py`、`poc2_run.py`、`verify_claims.py`、`clean_stray_refs.py`、
`a2_*.py`（支持库配置 UI 自动化，**已判不可靠关闭**）、`rt_big_test.py`、`rt_batch.py`、`test_readlog_follow.py`（readlog 回归测试）。

## 3. 已归档（避免与成品混淆）

| 路径 | 说明 |
|---|---|
| `_archive/poc_mkcage.py` | **PoC 版加壳脚本**（成品是 `tools/mkcage.py` / 技能 `scripts/mkcage.py`）。2026-09-22 由 `re/mkcage.py` 改名归档——同名混淆源，交接文档 §8⑤ 点名处理 |

## 4. 纪律（同交接文档 §9）

- 物证文件**按运行唯一命名**（`--tag`），不预删、不覆盖；
- 涉及 e.exe 的实验只用精确 PID 终止，**绝不 `taskkill /F /IM e.exe`**；
- `D:\ides\e\` 只读（只允许投放/移除 `lib\elang_addin.fne` 并 diff 自证）；
- 本目录不进技能包、不进发布区（`sync-release.sh` 显式名单不含 `re/`）。

## 5. 上游技能搬运与无头编译实证（2026-09-22）

| 路径/文件 | 说明 |
|---|---|
| `addin_gate_exp/` | **AutoLinker 版本闸门受控实验**：V1（LBS_LIB_INFO2 位）/V2（RqSys3.7+RqKrnl5.3）/V3（仅 RqSys）/V4（仅 RqKrnl）四组变体的源码与 `.fne`，逐一证明旧 IDE 的三道独立拒载闸 |
| `al_window_probe.py` / `al_errdlg_probe.py` | 无头编译阻塞探针：枚举 e.exe 顶层窗口、抓「不能载入支持库」弹窗全文 |
| `al_screen.py` | 30 个 AutoLinker 历史版本批量下载 + 特征筛查（无头编译串 + LIB_INFO 门槛 dump） |
| `make_tests.py` / `make_mc_tests.py` | 控制台验证 / 置入代码（NOP、INT3）测试工程生成 |
| `fne_loadtest.py` / `webview2_check.py` / `reg_dump.py` / `reg_libsearch.py` | 排查辅助（64 位 Python 加载 32 位 fne 的误区、WebView2 检查、注册表排查） |
| `up_test/`、`epk_test/` | 实测工作目录（沙箱布局、semantic 编译产物）；可由上述脚本再生，已 gitignore 大文件 |
| `_upstream_e-language-skill/`（本地克隆，不入库） | 上游 [aiqinxuancai/e-language-skill](https://github.com/aiqinxuancai/e-language-skill)（MIT）参考本体 |
| `_upstream_dl/`（本地，不入库） | AutoLinker / e-packager 各版本下载与解包 |

关键结论沉淀：三道闸见 `re/addin_gate_exp/AutoLinker与无头编译.md` §1（实验区归档，不进技能包）；
NOP/INT3 实弹已改走自家静态编译链路复验，记录在 `docs/置入代码与机器码.md` §7。
（e-packager 通道 2026-09-22 按用户决策整体移除，其历史配方见 git 提交 b60e6b9。）
