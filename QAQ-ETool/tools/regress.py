#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""QAQ-ETool 对 e2txt 基线的回归：三夹具 × 英/中布局，逐文件比对。

用法: python tools/regress.py [--only 子串] [--show N] [--layout en|cn|both]
"""
import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EXE = ROOT / "src/QAQ.ETool.Cli/bin/Release/net8.0/QAQ-ETool.exe"
FIX = ROOT / "测试源码"
BASE = ROOT / "tests/baseline"
OUT = ROOT / "tests/out"


def run(fixture: Path, layout: str):
    dst = OUT / fixture.stem / layout
    args = [str(EXE), "-mode", "e2t", "-src", str(fixture), "-dst", str(dst)]
    if layout == "cn":
        args += ["-ns", "2"]
    t = time.time()
    p = subprocess.run(args, capture_output=True)
    return dst, time.time() - t, p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default=None)
    ap.add_argument("--show", type=int, default=6)
    ap.add_argument("--layout", default="en")
    ap.add_argument("--all", type=int, default=0)
    a = ap.parse_args()
    layouts = ["en", "cn"] if a.layout == "both" else [a.layout]
    for f in sorted(FIX.glob("*.e")):
        if a.only and a.only not in f.name:
            continue
        for lay in layouts:
            dst, dt, p = run(f, lay)
            err = p.stderr.decode("utf-8", "replace")
            nerr = sum(1 for l in err.splitlines() if l.startswith("[错误]"))
            print(f"\n{'#' * 70}\n# {f.stem} [{lay}]  exit={p.returncode}  {dt:.2f}s  [错误]{nerr}")
            for l in err.splitlines()[:8]:
                print("#   " + l)
            sys.stdout.flush()
            cmd = [sys.executable, str(ROOT / "tools/cmpdir.py"), str(BASE / f.stem / lay), str(dst),
                   "--show", str(a.show), "--ctx", "1",
                   # 基线（e2txt）写 .etprj，QAQ-ETool 写 .qtprj；两者都只差 Source 路径，不比
                   "--skip", "etprj", "--skip", "qtprj"]
            if a.all:
                cmd += ["--all", str(a.all)]
            subprocess.run(cmd)


if __name__ == "__main__":
    main()
