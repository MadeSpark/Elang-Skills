#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""规则⑥ 文件级回归（自包含，不依赖 .scratch_session）。

用法：
    python re/规则⑥验收证据/run.py          # 跑全部用例，打印 PASS/FAIL 表
    python re/规则⑥验收证据/run.py -v       # 另外打印每个用例的报错原文
    python re/规则⑥验收证据/run.py --json   # 机器可读汇总

用例文本在 fixtures/<用例名>/程序集1.static.e.txt，都是可直接组包的真实片段。
⚠️ 「缩进空行」由若干空格组成（行尾空格有语义），别用会自动裁剪行尾空白的编辑器改。

期望值来自同目录 FINAL.txt / README.md：
  规则⑥ = A 是隐藏块尾（.如果结束 / .如果真结束），B 比 A 更浅（B.缩进 < A.缩进），
          A、B 之间没有「与 A 同缩进的空行」→ 错误。B 可以是隐藏尾，也可以是显式尾。
  A 是显式尾不判；A、B 之间已有空行时由规则③ 判空行缩进，规则⑥ 不重复报。

退出码：0 = 全部通过；1 = 有用例不一致。
"""

import io
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ECHECK = os.path.join(ROOT, "tools", "echeck.py")
FIXTURES = os.path.join(HERE, "fixtures")

# name, exit, errors, warnings, 必须出现的片段, 不允许出现的片段, {片段: 条数}
# 「片段: 条数」用来区分「是规则⑥ 还是规则③/⑤ 报的」——只看 (exit, errors) 分不出来。
CASES = [
    ("bad_unwind", 1, 1, 0,
     ["隐藏块尾「.如果真结束」与更浅的隐藏块尾「.如果真结束」", "没有空行"], [],
     {"之间没有空行": 1}),
    ("good_unwind", 0, 0, 0, [], [], {}),
    ("bad_reverse_explicit", 1, 1, 0,
     ["更浅的显式块尾「.计次循环尾」", "没有空行"], [],
     {"之间没有空行": 1}),
    ("reverse_legal", 0, 0, 0, [], [], {}),
    ("explicit_loop_legal", 0, 0, 0, [], [], {}),
    ("explicit_loop_mixed", 0, 0, 0, [], [], {}),
    ("hidden_mixed", 1, 1, 0, ["空行缩进"], ["更浅的"],
     {"空行缩进": 1, "之间没有空行": 0}),
    ("good_two_blank", 0, 0, 0, [], [], {}),
    ("cross_sub", 0, 0, 0, [], [], {}),
    ("else_marker", 1, 1, 0, ["中段标记"], ["更浅的"],
     {"中段标记": 1, "之间没有空行": 0}),
    ("jm_five", 1, 5, 0, ["更浅的显式块尾「.计次循环尾」"], [],
     {"之间没有空行": 5}),
    ("jm_five_fixed", 0, 0, 0, [], [], {}),
    # 四种显式尾逐个命中（守 TAIL_KIND 不被改窄）
    ("bad_explicit_kinds", 1, 4, 0,
     ["更浅的显式块尾「.判断结束」", "更浅的显式块尾「.判断循环尾」",
      "更浅的显式块尾「.变量循环尾」", "更浅的显式块尾「.循环判断尾」"], [],
     {"之间没有空行": 4}),
    # A 是隐藏尾的另一个变体 .如果结束
    ("bad_if_end_explicit", 1, 1, 0,
     ["隐藏块尾「.如果结束」与更浅的显式块尾「.计次循环尾」"], [],
     {"之间没有空行": 1}),
    # 三层链：两个 A 各报一条，互不吞报
    ("bad_unwind_chain", 1, 2, 0, ["与更浅的隐藏块尾", "与更浅的显式块尾"], [],
     {"之间没有空行": 2}),
    # 只给内层 A2 补空行：外层 A1 仍报⑥；那条 8 空格空行相对 A1 缩进错 → 同时 1 条③
    ("chain_fixed_inner", 1, 2, 0, ["空行缩进", "之间没有空行"], [],
     {"空行缩进": 1, "之间没有空行": 1}),
    # 有空行但缩进错（0 空格）→ 只报③，规则⑥ 不叠加
    ("gap_wrong_indent_explicit", 1, 1, 0, ["空行缩进"], ["更浅的"],
     {"空行缩进": 1, "之间没有空行": 0}),
    # 五种显式尾当 A（含显式→显式）→ 一律不判
    ("explicit_A_all_kinds", 0, 0, 0, [], ["更浅的"], {}),
    # BOM + CRLF 归一后与 bad_reverse_explicit 结果一致
    ("crlf_bom_variant", 1, 1, 0, ["更浅的显式块尾「.计次循环尾」"], [],
     {"之间没有空行": 1}),
]


def _run(argv, env_extra=None):
    """跑一次 echeck，返回 (退出码, 输出文本)。

    输出走文件重定向而不是管道：本机沙箱下子进程管道偶发 WinError 5，
    重定向到临时文件是稳定路径。"""
    env = dict(os.environ)
    env["PYTHONUTF8"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    if env_extra:
        env.update(env_extra)
    fd, out_path = tempfile.mkstemp(suffix=".txt", prefix="echeck_run_")
    try:
        with os.fdopen(fd, "wb") as fh:
            rc = subprocess.call([sys.executable, ECHECK] + argv,
                                 stdout=fh, stderr=subprocess.STDOUT, env=env)
        with io.open(out_path, "rb") as fh:
            raw = fh.read()
        return rc, raw.decode("utf-8", "replace")
    finally:
        try:
            os.remove(out_path)
        except OSError:
            pass


def _json_of(text):
    """从输出里取出 JSON 主体（echeck 只打印 JSON，这里容忍前后空行）。"""
    start = text.find("{")
    if start < 0:
        raise ValueError("输出里没有 JSON：" + text[:400])
    return json.loads(text[start:])


def check_case(name, want_exit, want_errors, want_warnings, need, forbid, counts, verbose):
    d = os.path.join(FIXTURES, name)
    if not os.path.isdir(d):
        return False, "缺用例目录 " + d, ""
    rc, text = _run([d, "--branch-only", "--json"])
    problems = []
    detail = text
    try:
        data = _json_of(text)
    except Exception as exc:
        return False, "JSON 解析失败：%s" % exc, text
    errs = data.get("errors") or []
    warns = data.get("warnings") or []
    branch = data.get("branch") or {}
    if rc != want_exit:
        problems.append("退出码 %d ≠ 期望 %d" % (rc, want_exit))
    if len(errs) != want_errors:
        problems.append("错误数 %d ≠ 期望 %d" % (len(errs), want_errors))
    if len(warns) != want_warnings:
        problems.append("警告数 %d ≠ 期望 %d" % (len(warns), want_warnings))
    if branch.get("errors") != len(errs):
        problems.append("branch.errors(%s) 与 errors 条数不一致" % branch.get("errors"))
    if branch.get("warnings") != 0:
        problems.append("branch.warnings 必须恒为 0，实际 %s" % branch.get("warnings"))
    if branch.get("passed") is not (len(errs) == 0):
        problems.append("branch.passed 与错误数不一致")
    if data.get("branchOnly") is not True:
        problems.append("不是 --branch-only 模式")
    joined = "\n".join(errs)
    for frag in need:
        if frag not in joined:
            problems.append("缺少片段：%s" % frag)
    for frag in forbid:
        if frag in joined:
            problems.append("不该出现的片段：%s" % frag)
    for frag, want in (counts or {}).items():
        got = joined.count(frag)
        if got != want:
            problems.append("「%s」出现 %d 次 ≠ 期望 %d 次" % (frag, got, want))
    if problems:
        return False, "；".join(problems), detail
    return True, "%d 错误 / %d 警告" % (len(errs), len(warns)), detail


def check_gbk_pipe():
    """回归 _ensure_utf8_output()：stdout 编码是 GBK 时也不能崩在 ✓✗⚠ 上。"""
    d = os.path.join(FIXTURES, "bad_unwind")
    rc, text = _run([d, "--branch-only"], {"PYTHONIOENCODING": "gbk"})
    problems = []
    if rc != 1:
        problems.append("GBK 管道下退出码 %d ≠ 1" % rc)
    if "Traceback" in text or "UnicodeEncodeError" in text:
        problems.append("GBK 管道下抛了异常：")
        problems.append(text.strip().splitlines()[-1] if text.strip() else "")
    if "✗" not in text:
        problems.append("GBK 管道下丢了「✗」报错行")
    if "流程分支: ✗ 错误 1" not in text:
        problems.append("GBK 管道下少了流程分支结论行")
    if problems:
        return False, "；".join(problems), text
    return True, "GBK stdout 下报错行完整保留", text


def check_unit_gate():
    """内存档：直接喂 check_unwind_tail_gap，锁住「B 更浅才判 / 有空行交给③」。

    这些形态在合法嵌套里造不出（相邻块尾的后者必然更浅），只能直接调函数。
    1) B 与 A 同缩进、2) B 比 A 更深 → 必须 0 条（防止判据被改成 > 或被删掉）；
    3) B 更浅 + 隐藏/显式 → 各 1 条；4) A 是显式尾 → 0 条；5) 已有空行 → 0 条。"""
    import importlib.util
    spec = importlib.util.spec_from_file_location("echeck_under_test", ECHECK)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    probes = [
        ("B 与 A 同缩进", ["    .如果真结束", "    .如果真结束"], 0),
        ("B 比 A 更深", ["    .如果真结束", "        .计次循环尾 ()"], 0),
        ("B 更浅（隐藏尾）", ["    .如果真结束", ".如果真结束"], 1),
        ("B 更浅（显式尾）", ["    .如果真结束", ".计次循环尾 ()"], 1),
        ("A 是显式尾", ["    .计次循环尾 ()", ".如果真结束"], 0),
        ("已有空行（归规则③）", ["    .如果真结束", "    ", ".计次循环尾 ()"], 0),
        ("A 混在语句中", ["    a ＝ 1", "    .如果真结束", "        .计次循环尾 ()"], 0),
    ]
    problems, lines = [], []
    for label, sample, want in probes:
        errs = []
        mod.check_unwind_tail_gap("synthetic.e.txt", sample, errs)
        lines.append("%-22s → %d 条（期望 %d）" % (label, len(errs), want))
        if len(errs) != want:
            problems.append("%s：%d 条 ≠ %d 条" % (label, len(errs), want))
    detail = "\n".join(lines)
    if problems:
        return False, "；".join(problems), detail
    return True, "7 个内存档全部符合", detail


def main(argv):
    verbose = "-v" in argv or "--verbose" in argv
    as_json = "--json" in argv
    if not os.path.isfile(ECHECK):
        print("找不到 echeck：%s" % ECHECK, file=sys.stderr)
        return 2
    rows, failed = [], 0
    for spec in CASES:
        ok, msg, detail = check_case(*spec[:6], counts=spec[6], verbose=verbose)
        if not ok:
            failed += 1
        rows.append({"case": spec[0], "ok": ok, "result": msg,
                     "expected": "exit=%d errors=%d warnings=%d" % spec[1:4]})
        if verbose or not ok:
            print("--- %s (%s) ---" % (spec[0], "PASS" if ok else "FAIL"))
            print(detail.rstrip())
    unit_ok, unit_msg, unit_detail = check_unit_gate()
    if not unit_ok:
        failed += 1
    rows.append({"case": "unit_next_indent_gate", "ok": unit_ok, "result": unit_msg,
                 "expected": "7 个内存档（同缩进/更深/显式A/已有空行 均 0 条）"})
    if verbose or not unit_ok:
        print("--- unit_next_indent_gate (%s) ---" % ("PASS" if unit_ok else "FAIL"))
        print(unit_detail)

    gbk_ok, gbk_msg, gbk_detail = check_gbk_pipe()
    if not gbk_ok:
        failed += 1
    rows.append({"case": "gbk_pipe_utf8_output", "ok": gbk_ok, "result": gbk_msg,
                 "expected": "exit=1 且 ✗ 行完整"})
    if verbose or not gbk_ok:
        print("--- gbk_pipe_utf8_output (%s) ---" % ("PASS" if gbk_ok else "FAIL"))
        print(gbk_detail.rstrip())

    if as_json:
        print(json.dumps({"passed": failed == 0, "failed": failed,
                          "total": len(rows), "rows": rows},
                         ensure_ascii=False, indent=2))
    else:
        print()
        print("%-24s %-6s %-28s %s" % ("用例", "结果", "期望", "实际"))
        print("-" * 88)
        for r in rows:
            print("%-24s %-6s %-28s %s" % (r["case"], "PASS" if r["ok"] else "FAIL",
                                           r["expected"], r["result"]))
        print("-" * 88)
        print("合计 %d 用例，失败 %d 条 → %s" % (len(rows), failed,
                                                "全部通过" if failed == 0 else "不通过"))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
