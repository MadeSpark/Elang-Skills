#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""diff_librec3.py —— 以 'krnln' 为锚点对齐 base/caged，看清插入点前的 8 字节差从哪来。

已知锚点：
    base : 条数 01 00 → 载荷长 39 00 00 00 → 'krnln'   首字节 643
    caged: 条数 02 00 → 载荷长 39 00 00 00 → 'krnln'   首字节 651
两者相差 8 ⇒ 除了尾部多一条记录（63 B），插入点**之前**还多了 8 B。
本脚本把两段按 'krnln' 对齐并逐 16 字节对照，找出这 8 B 的归属。
只读。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
a = open(os.path.join(HERE, "projD_base.e"), "rb").read()
b = open(os.path.join(HERE, "projD_lib.e"), "rb").read()

K = b"krnln"
ka = a.find(K)
kb = b.find(K)
print("krnln: base@%d  caged@%d  Δ=%d" % (ka, kb, kb - ka))


def dump(buf, tag, start, n):
    print("\n=== %s [%d,%d) ===" % (tag, start, start + n))
    for off in range(start, start + n, 16):
        row = buf[off:off + 16]
        txt = "".join(chr(c) if 32 <= c < 127 else "." for c in row)
        print("  %5d  %-47s  %s" % (off, " ".join("%02x" % c for c in row), txt))


# 锚点前 64B、后 160B
dump(a, "base", ka - 64, 64)
dump(b, "caged", kb - 64, 128)

print("\n=== 逐字节对齐（base 偏移 → caged 偏移 = +%d）===" % (kb - ka))
same = 0
first = None
for i in range(ka - 64, ka):
    j = i + (kb - ka)
    if a[i] != b[j]:
        if first is None:
            first = i
        same += 1
print("  锚点前 64B 里，按 +%d 平移后仍不一致的字节数 = %d" % (kb - ka, same))
if first is not None:
    print("  最早不一致 base@%d = %02x , caged@%d = %02x"
          % (first, a[first], first + (kb - ka), b[first + (kb - ka)]))
else:
    print("  ⇒ 锚点前 64B **完全平移一致** ⇒ 那 8 字节的增加发生在更前面")

# 反向找：从锚点往前，base 与 caged 平移一致的最远起点
def alen(buf_a, buf_b, shift, start, stop):
    i = start
    while i > stop and buf_a[i - 1] == buf_b[i - 1 + shift]:
        i -= 1
    return i


p = alen(a, b, kb - ka, ka, 0)
print("\n  以 'krnln' 为锚点向左回溯，平移一致的最远起点 = %d" % p)
dump(a, "base(回溯段)", max(0, p - 32), min(64, ka - max(0, p - 32)))
print("\n  base  与 k 对齐的尾部字节: %s" % " ".join("%02x" % x for x in a[ka - 24:ka]))
print("  caged 与 k 对齐的尾部字节: %s" % " ".join("%02x" % x for x in b[kb - 24:kb]))
