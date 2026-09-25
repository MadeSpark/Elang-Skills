#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""scan_stages.py —— 按阶段从小到大逐个做无头语法检查，遇到第一个崩溃就停。

每次检查都要真实启动一次 e.exe（用户那边会感到一次 5~10 秒整机卡顿），
所以：① 顺序从小到大；② 一崩就停（后面的阶段必然继承同样的失败，没必要再试）。
"""
import io
import os
import re
import subprocess
import sys
import time

PY = sys.executable
ECOMPILE = r"C:\Users\MadeSpark\.workbuddy\skills\elang-build\scripts\ecompile.py"
HERE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.path.join(HERE, "stage_logs")
os.makedirs(LOGDIR, exist_ok=True)


def run_check(proj, tag):
    wd = os.path.join(LOGDIR, "wk_%s_%s" % (tag, time.strftime("%H%M%S")))
    os.makedirs(wd, exist_ok=True)
    p = subprocess.run([PY, ECOMPILE, proj, "--check", "--workdir", wd],
                       capture_output=True)
    raw = (p.stdout or b"") + (b"\n---STDERR---\n" + p.stderr if p.stderr else b"")
    io.open(os.path.join(wd, "raw.bin"), "wb").write(raw)
    txt = raw.decode("utf-8", errors="replace")
    if "\ufffd" in txt:
        txt = raw.decode("gbk", errors="replace")
    io.open(os.path.join(wd, "out.txt"), "w", encoding="utf-8", newline="").write(txt)

    exitcode = None
    for f in os.listdir(wd):
        if f.endswith("_harness.txt"):
            t = io.open(os.path.join(wd, f), encoding="utf-8", errors="replace").read()
            m = re.search(r"e_self_exit=(\S+)", t)
            if m:
                exitcode = m.group(1)
    # 判据以「e.exe 自己的退出码」为准（0xC0000005 = 解析期崩），
    # 不再依赖 stdout 里能否匹配到中文结论串（那会因编码误判）。
    crashed = (exitcode in ("3221225477", "-1073741819", "0xC0000005"))
    passed = ("语法检查通过" in txt)
    return {"passed": passed, "crashed": crashed, "exit": exitcode,
            "wd": wd, "tail": txt.strip().split("\n")[-1][:100] if txt.strip() else ""}


def main():
    STAGES = [
        ("wrap", "demo_proj_swrap", "258 个包装子程序（一条都不调用）"),
        ("load", "demo_proj_sload", "+ 内存载入宿主库（手工映射 + 填 258 个函数地址槽）"),
        ("win",  "demo_proj_swin",  "+ 建窗口（CreateWindowExA + 预注册类 #32770）"),
        ("wke",  "demo_proj_swke",  "+ 建浏览框 / 绑主窗口句柄 / 载网页"),
        ("full", "demo_proj_sfull", "+ 消息循环 + 贴图 + 取标题（全量）"),
    ]
    # 允许只跑指定阶段：python scan_stages.py wke full
    if len(sys.argv) > 1:
        want = set(sys.argv[1:])
        STAGES = [s for s in STAGES if s[0] in want]
    print("阶段   结果     e_self_exit    说明")
    print("-" * 96)
    last_pass = None
    first_crash = None
    for name, proj, desc in STAGES:
        d = os.path.join(HERE, proj)
        if not os.path.isdir(d):
            print("%-6s 跳过（目录不存在）" % name)
            continue
        r = run_check(d, name)
        verdict = "通过" if r["passed"] else ("❌崩溃" if r["crashed"] else "未判定")
        print("%-6s %-8s %-14s %s" % (name, verdict, r["exit"] or "-", desc))
        sys.stdout.flush()
        if r["passed"]:
            last_pass = name
        elif r["crashed"]:
            first_crash = name
            print("   → 崩溃现场日志：%s" % r["wd"])
            break
    print()
    if first_crash:
        print("★ 最后一个通过 = %s ；第一个崩溃 = %s" % (last_pass or "(无)", first_crash))
    else:
        print("★ 全部通过（last_pass=%s）" % last_pass)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
