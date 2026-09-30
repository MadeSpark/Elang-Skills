#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""`.e` 二进制探查器 —— P1-2 容器格式测绘的主力工具。

子命令:
  find   在 .e 里搜字符串/字节，支持 GBK / UTF-16LE / ASCII / 裸 hex
  dump   十六进制 + 双编码文本对照
  ctx    搜到目标后自动 dump 其周边（find + dump 的组合，最常用）
  scan   统计文件整体特征（高熵区/零区/可打印串密度），判断是否压缩

例:
  python tools/ebin.py ctx "测试源码/窗口属性无法解析.e" --str miniblink_test --before 96 --after 160
  python tools/ebin.py find "测试源码/窗口属性无法解析.e" --hex 0100010009000000
  python tools/ebin.py dump "测试源码/窗口属性无法解析.e" --at 0x1234 --len 256
"""

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PRINTABLE = set(range(0x20, 0x7F))


def resolve(p: str) -> Path:
    q = Path(p)
    if not q.is_absolute():
        q = ROOT / p
    if not q.exists():
        sys.exit(f"找不到文件: {q}")
    return q


def patterns_for(args) -> list:
    """把用户给的搜索目标转成 [(说明, bytes), ...]"""
    pats = []
    for s in args.str or []:
        pats.append((f"ascii:{s}", s.encode("latin-1", errors="replace")))
        try:
            g = s.encode("gbk")
            if g != s.encode("latin-1", errors="replace"):
                pats.append((f"gbk:{s}", g))
        except UnicodeEncodeError:
            pass
        pats.append((f"utf16le:{s}", s.encode("utf-16-le")))
    for h in args.hex or []:
        h2 = h.replace(" ", "").replace("_", "")
        pats.append((f"hex:{h2}", bytes.fromhex(h2)))
    return pats


def find_all(data: bytes, pat: bytes, limit: int) -> list:
    out, start = [], 0
    while len(out) < limit:
        i = data.find(pat, start)
        if i < 0:
            break
        out.append(i)
        start = i + 1
    return out


def render(data: bytes, start: int, end: int, mark: int = -1, marklen: int = 0):
    start = max(0, start)
    end = min(len(data), end)
    start -= start % 16
    for off in range(start, end, 16):
        chunk = data[off:off + 16]
        hexs = []
        for k, b in enumerate(chunk):
            a = off + k
            inmark = mark >= 0 and mark <= a < mark + marklen
            hexs.append(("[%02x]" % b) if inmark else (" %02x " % b))
        hexline = "".join(hexs)
        ascii_ = "".join(chr(b) if b in PRINTABLE else "." for b in chunk)
        try:
            gbk = chunk.decode("gbk", errors="replace").replace("\n", " ")
        except Exception:
            gbk = ""
        print(f"  {off:08x}  {hexline:<64}  |{ascii_:<16}|  {gbk}")


def cmd_find(path: Path, args):
    data = path.read_bytes()
    print(f"{path.name}  {len(data):,} B")
    for name, pat in patterns_for(args):
        hits = find_all(data, pat, args.limit)
        print(f"\n[{name}]  {len(pat)} B  ->  {len(hits)} 命中"
              + (f"（截断至 {args.limit}）" if len(hits) >= args.limit else ""))
        for h in hits:
            print(f"    0x{h:08x}  ({h})")


def cmd_ctx(path: Path, args):
    data = path.read_bytes()
    print(f"{path.name}  {len(data):,} B")
    for name, pat in patterns_for(args):
        hits = find_all(data, pat, args.limit)
        print(f"\n{'='*78}\n[{name}]  {len(hits)} 命中\n{'='*78}")
        for n, h in enumerate(hits, 1):
            print(f"\n--- #{n}  offset 0x{h:08x} ({h}) ---")
            render(data, h - args.before, h + len(pat) + args.after, h, len(pat))


def cmd_dump(path: Path, args):
    data = path.read_bytes()
    at = int(args.at, 0)
    print(f"{path.name}  {len(data):,} B   dump @0x{at:x} len {args.len}")
    render(data, at, at + args.len)


def cmd_scan(path: Path, args):
    data = path.read_bytes()
    n = len(data)
    zeros = data.count(0)
    printable = sum(1 for b in data if b in PRINTABLE)
    print(f"{path.name}  {n:,} B")
    print(f"  零字节      {zeros:>10,}  ({100.0*zeros/n:.1f}%)")
    print(f"  可打印ASCII {printable:>10,}  ({100.0*printable/n:.1f}%)")
    step = max(1, n // args.buckets)
    print(f"\n  分块特征（每块 {step:,} B）: Z=零占比 P=可打印占比 U=唯一字节数")
    for i in range(0, n, step):
        b = data[i:i + step]
        if not b:
            continue
        z = 100.0 * b.count(0) / len(b)
        p = 100.0 * sum(1 for x in b if x in PRINTABLE) / len(b)
        u = len(set(b))
        bar = "#" * int(p / 4)
        print(f"    0x{i:08x}  Z{z:5.1f}%  P{p:5.1f}%  U{u:>4}  {bar}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    sub = ap.add_argument_group("目标")
    ap.add_argument("--str", action="append", help="搜字符串（自动试 ascii/gbk/utf16le）")
    ap.add_argument("--hex", action="append", help="搜裸 hex，如 0100010009000000")
    ap.add_argument("--limit", type=int, default=40, help="每个模式最多列多少命中")
    ap.add_argument("--before", type=int, default=64)
    ap.add_argument("--after", type=int, default=128)
    ap.add_argument("--at", default="0")
    ap.add_argument("--len", type=lambda x: int(x, 0), default=256)
    ap.add_argument("--buckets", type=int, default=32)
    ap.add_argument("--mode", choices=("find", "ctx", "dump", "scan"),
                    default=None)
    args = ap.parse_args()

    path = resolve(args.file)
    mode = args.mode
    if mode is None:
        if args.at != "0":
            mode = "dump"
        elif args.str or args.hex:
            mode = "ctx"
        else:
            mode = "scan"

    {"find": cmd_find, "ctx": cmd_ctx, "dump": cmd_dump,
     "scan": cmd_scan}[mode](path, args)


if __name__ == "__main__":
    main()
