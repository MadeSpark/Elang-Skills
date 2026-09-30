#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""占位符差分定位器 —— 夹出 e2txt 失败的精确触发点。

用法:
  python tools/phdiff.py                    # 列出各夹具占位符种类分布
  python tools/phdiff.py --ctx 窗口属性无法解析  # 打印占位符所在行及上下文
  python tools/phdiff.py --diff 窗口属性无法解析 新增 文字文件   # 靶子 vs 对照 的差集
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = ROOT / "tests" / "baseline"
REPORT = BASELINE / "report.json"

PLACEHOLDER_RE = re.compile(r"<\?.{0,80}?\?>")


def load():
    if not REPORT.exists():
        sys.exit(f"没有基线报告，先跑 tools/baseline.py：{REPORT}")
    return json.loads(REPORT.read_text(encoding="utf-8"))


def cmd_list(rep):
    for stem, rec in rep["fixtures"].items():
        r = rec["e2t"].get("en")
        if not r or not r.get("ok"):
            continue
        ph = r["placeholders"]
        print(f"\n=== {stem} ===  占位符总数 {ph['total']}")
        for kind, n in sorted(ph["kinds"].items(), key=lambda x: -x[1]):
            print(f"  {n:>5} x  {kind}")
        print("  分布（文件 -> 条数）:")
        for f, per in sorted(ph["by_file"].items(),
                             key=lambda x: -sum(x[1].values())):
            print(f"    {sum(per.values()):>4}  {f}")


def cmd_diff(rep, target, control):
    t = rep["fixtures"][target]["e2t"]["en"]["placeholders"]
    c = rep["fixtures"][control]["e2t"]["en"]["placeholders"]
    print(f"靶子   {target}: {t['total']}")
    print(f"对照   {control}: {c['total']}")
    print("\n--- 种类差异 (靶子 - 对照) ---")
    keys = set(t["kinds"]) | set(c["kinds"])
    for k in sorted(keys):
        tv, cv = t["kinds"].get(k, 0), c["kinds"].get(k, 0)
        mark = "  <== 多出" if tv > cv else ""
        print(f"  靶{tv:>5}  照{cv:>5}   {k}{mark}")
    print("\n--- 只在靶子里出现的文件 ---")
    only = set(t["by_file"]) - set(c["by_file"])
    for f in sorted(only):
        print(f"  {sum(t['by_file'][f].values()):>4}  {f}  {t['by_file'][f]}")


def cmd_ctx(rep, stem, layout, before, after):
    d = BASELINE / stem / layout
    if not d.is_dir():
        sys.exit(f"找不到基线产物目录: {d}")
    hits = 0
    for p in sorted(d.rglob("*")):
        if not p.is_file() or p.suffix.lower() not in (".txt", ".json", ".etprj"):
            continue
        try:
            text = p.read_text(encoding="utf-8-sig")
        except (UnicodeDecodeError, OSError):
            continue
        lines = text.splitlines()
        for i, line in enumerate(lines):
            if not PLACEHOLDER_RE.search(line):
                continue
            hits += 1
            rel = str(p.relative_to(d)).replace("\\", "/")
            print(f"\n{'-'*70}\n#{hits}  {rel}:{i+1}")
            lo = max(0, i - before)
            hi = min(len(lines), i + after + 1)
            for j in range(lo, hi):
                mark = ">>" if j == i else "  "
                print(f"  {mark} {j+1:>5} | {lines[j]}")
    print(f"\n共 {hits} 行含占位符")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--diff", nargs=2, metavar=("靶子", "对照"))
    ap.add_argument("--ctx", metavar="夹具名")
    ap.add_argument("--layout", default="en")
    ap.add_argument("--before", type=int, default=6)
    ap.add_argument("--after", type=int, default=3)
    args = ap.parse_args()

    rep = load()
    if args.diff:
        cmd_diff(rep, *args.diff)
    elif args.ctx:
        cmd_ctx(rep, args.ctx, args.layout, args.before, args.after)
    else:
        cmd_list(rep)


if __name__ == "__main__":
    main()
