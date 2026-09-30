#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""目录比对：QAQ-ETool 产物 vs e2txt 基线（逐文件、逐行）。

用法:
  python tools/cmpdir.py <基线目录> <我们的目录> [--show N] [--only 子串] [--ctx 2]
输出：文件集合差异、逐字节一致数、每个不一致文件的首个差异行（带上下文）。
"""
import argparse
import sys
from pathlib import Path


def lines_of(p: Path):
    b = p.read_bytes()
    if b.startswith(b"\xef\xbb\xbf"):
        b = b[3:]
    try:
        t = b.decode("utf-8")
    except UnicodeDecodeError:
        t = b.decode("gbk", errors="replace")
    return t.split("\r\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("base")
    ap.add_argument("ours")
    ap.add_argument("--show", type=int, default=15, help="最多展示多少个不一致文件")
    ap.add_argument("--only", default=None)
    ap.add_argument("--ctx", type=int, default=2)
    ap.add_argument("--skip", action="append", default=[], help="忽略路径含该子串的文件")
    ap.add_argument("--all", type=int, default=0, help="每个文件列出前 N 处差异行（不止首差）")
    a = ap.parse_args()
    base, ours = Path(a.base), Path(a.ours)

    def files(d):
        return {str(p.relative_to(d)).replace("\\", "/"): p for p in d.rglob("*") if p.is_file()}

    fb, fo = files(base), files(ours)
    if a.only:
        fb = {k: v for k, v in fb.items() if a.only in k}
        fo = {k: v for k, v in fo.items() if a.only in k}
    for s in a.skip:
        fb = {k: v for k, v in fb.items() if s not in k}
        fo = {k: v for k, v in fo.items() if s not in k}

    only_b = sorted(set(fb) - set(fo))
    only_o = sorted(set(fo) - set(fb))
    common = sorted(set(fb) & set(fo))
    same = [k for k in common if fb[k].read_bytes() == fo[k].read_bytes()]
    diff = [k for k in common if k not in set(same)]

    print(f"基线 {len(fb)} 文件 / 我们 {len(fo)} 文件 / 共有 {len(common)} / 逐字节一致 {len(same)} / 不一致 {len(diff)}")
    if only_b:
        print(f"\n只在基线 ({len(only_b)}):")
        for k in only_b[:40]:
            print("   ", k)
    if only_o:
        print(f"\n只在我们 ({len(only_o)}):")
        for k in only_o[:40]:
            print("   ", k)

    # 按不一致行数排序，先看差得少的（通常是系统性小问题）
    stats = []
    for k in diff:
        lb, lo = lines_of(fb[k]), lines_of(fo[k])
        n = sum(1 for i in range(max(len(lb), len(lo)))
                if (lb[i] if i < len(lb) else None) != (lo[i] if i < len(lo) else None))
        stats.append((n, k, lb, lo))
    stats.sort(key=lambda x: (x[0], x[1]))
    if stats:
        print(f"\n不一致文件（按差异行数升序，展示前 {a.show}）:")
    for n, k, lb, lo in stats[: a.show]:
        i = 0
        while i < min(len(lb), len(lo)) and lb[i] == lo[i]:
            i += 1
        print(f"\n--- {k}  差异行 {n}  (基线 {len(lb)} 行 / 我们 {len(lo)} 行)  首差 @行{i + 1}")
        if a.all:
            import difflib
            shown = 0
            for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, lb, lo, autojunk=False).get_opcodes():
                if tag == "equal":
                    continue
                print(f"   [{tag}] 基{i1 + 1}-{i2} 我{j1 + 1}-{j2}")
                for x in lb[i1:i2][:3]:
                    print(f"      基 {x!r}")
                for x in lo[j1:j2][:3]:
                    print(f"      我 {x!r}")
                shown += 1
                if shown >= a.all:
                    break
            continue
        for j in range(max(0, i - a.ctx), min(max(len(lb), len(lo)), i + a.ctx + 1)):
            b = lb[j] if j < len(lb) else "<EOF>"
            o = lo[j] if j < len(lo) else "<EOF>"
            if b == o:
                print(f"   {j + 1:>5}   {b!r}")
            else:
                print(f"   {j + 1:>5} 基 {b!r}")
                print(f"   {j + 1:>5} 我 {o!r}")


if __name__ == "__main__":
    main()
