#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""diff_librec2.py —— 精确定位 .e 加库时的「原地改写」字节（排除插入区）。

diff_librec.py 已知：
  · 库记录是**可读文本记录**：`u16 条数` + 每条 `u32 载荷长` + `Key\rGuid(32hex)\rMajor\rMinor\rName(GBK)`
      krnln        载荷 57 = 5+1+32+1+1+1+1+1+14      "系统核心支持库"
      elang_addin  载荷 59 = 11+1+32+1+1+1+1+1+10     "AI调试宿主"
  · 公共后缀从 base 的 700 / caged 的 771 开始 ⇒ **插入点 = base 偏移 700**（净 +71 B）
  · 但公共前缀只到 513 ⇒ `[513,700)` 里还有**原地改写**的字节

本脚本：列出 base 与 caged 在 [0,700) 内逐字节不同处，并给出两边的上下文，
       便于判断这些是校验和 / 长度 / 偏移表里的哪一种。
只读。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, "projD_base.e")
CAGED = os.path.join(HERE, "projD_lib.e")


def hexs(b, s, n):
    return " ".join("%02x" % x for x in b[s:s + n])


def main():
    a = open(BASE, "rb").read()
    b = open(CAGED, "rb").read()
    INS_AT = 700          # base 侧插入点
    DELTA = len(b) - len(a)

    # 自证：base[INS_AT:] == caged[INS_AT+DELTA:]
    same_tail = (a[INS_AT:] == b[INS_AT + DELTA:])
    print("自证 base[%d:] == caged[%d:] : %s" % (INS_AT, INS_AT + DELTA, same_tail))

    diffs = [i for i in range(INS_AT) if a[i] != b[i]]
    print("\n[0,%d) 内逐字节不同处共 %d 个：" % (INS_AT, len(diffs)))
    # 聚成连续段
    runs = []
    for i in diffs:
        if runs and i == runs[-1][1]:
            runs[-1][1] = i + 1
        else:
            runs.append([i, i + 1])
    for lo, hi in runs:
        print("\n  段 [%d, %d)  共 %d B" % (lo, hi, hi - lo))
        print("      base  : %-30s u32=%d" % (hexs(a, lo, hi - lo),
                                              int.from_bytes(a[lo:lo + 4], "little")))
        print("      caged : %-30s u32=%d" % (hexs(b, lo, hi - lo),
                                              int.from_bytes(b[lo:lo + 4], "little")))
        ctx = 16
        print("      上下文 前 : %s" % hexs(a, max(0, lo - ctx), ctx))
        print("      上下文 后 : %s" % hexs(a, hi, ctx))

    print("\n--- 插入区（base 没有、caged 有）---")
    print("  区间 = caged[%d,%d)  共 %d B" % (INS_AT, INS_AT + DELTA, DELTA))
    print("  hex   : %s" % hexs(b, INS_AT, DELTA))
    print("  latin1: %r" % b[INS_AT:INS_AT + DELTA])

    print("\n--- 条数字段候选（u16）---")
    for off in range(600, 660):
        if a[off:off + 2] == b"\x01\x00" and b[off:off + 2] == b"\x02\x00":
            print("  偏移 %d: base=0x0001  caged=0x0002  ← 条数从 1 变 2" % off)
    return 0


if __name__ == "__main__":
    sys.exit(main())
