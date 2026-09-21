#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""diff_librec4.py —— 求解「8 字节额外增量」的插入点，给二进制直插方案定性。

思路：若文件里存在「在某点插入 8 字节」的操作，则存在 X 使
      base[X:] == caged[X+8:]。把所有满足的 X 找出来，最小的那个即插入点。
同时也报告：在 shift=8 假设下，哪些位置仍然不一致（=原地改写的字段，即偏移表/校验和）。
只读。
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
a = open(os.path.join(HERE, "projD_base.e"), "rb").read()
b = open(os.path.join(HERE, "projD_lib.e"), "rb").read()

print("Δ = %d B（新记录 = 4+59 = 63 B ⇒ 还多出 %d B）" % (len(b) - len(a), len(b) - len(a) - 63))

cands = [X for X in range(0, 700) if a[X:] == b[X + 8:]]
print("\n满足 base[X:] == caged[X+8:] 的 X：%s" % cands)
if cands:
    P = min(cands)
    print("  ⇒ 8 字节插入点 = base 偏移 %d" % P)
    print("  base  [%d,%d) = %s" % (max(0, P - 16), P + 8,
                                   " ".join("%02x" % x for x in a[max(0, P - 16):P + 8])))
    print("  caged [%d,%d) = %s" % (max(0, P - 16), P + 16,
                                   " ".join("%02x" % x for x in b[max(0, P - 16):P + 16])))
    print("  插入的 8 字节 = %s" % " ".join("%02x" % x for x in b[P:P + 8]))

# shift=8 下仍不一致的位置（前 700 B）→ 这些就是需要重算的字段
print("\n=== shift=8 下仍不一致的字节（[0,700)）===")
runs = []
for i in range(700):
    if a[i] != b[i + 8]:
        if runs and i == runs[-1][1]:
            runs[-1][1] = i + 1
        else:
            runs.append([i, i + 1])
for lo, hi in runs:
    print("  [%d,%d) %d B   base=%s   caged=%s"
          % (lo, hi, hi - lo,
             " ".join("%02x" % x for x in a[lo:hi]),
             " ".join("%02x" % x for x in b[lo + 8:hi + 8])))

print("\n=== 文件头前 32 B 对照（看有无总长/校验字段）===")
print("  base : %s" % " ".join("%02x" % x for x in a[:32]))
print("  caged: %s" % " ".join("%02x" % x for x in b[:32]))
