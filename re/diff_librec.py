#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""diff_librec.py —— 分析「给 .e 追加一条支持库记录」在二进制层面到底改了什么。

为什么做：加壳目前走 `e2t → 改 配置/支持库.config.json → t2e`，而 e2t/t2e 往返
对**真实大工程是有损的**（e2txt-cli 技能里有实测：1.23 MB → 1.00 MB + 385 条错误）。
如果「向 .e 直接插入库记录」的二进制格式能被看懂，就能做**无损**加壳。

样本（同一源文本，唯一差别是库表里多一项 elang_addin）：
    re/projD_base.e   6747 B   基线
    re/projD_lib.e    6818 B   多了 elang_addin

输出：公共前/后缀、插入区间、原地改动区间、插入内容的多编码渲染、
      krnln 与 elang_addin 记录的结构对照。
只读，不写任何 .e。
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BASE = os.path.join(HERE, "projD_base.e")
CAGED = os.path.join(HERE, "projD_lib.e")

KR_GUID_ASCII = b"d09f2340818511d396f6aaf844c7e325"        # krnln
EL_GUID_ASCII = b"7a1e4f22c3b0499e8d6a0011223344fe"        # elang_addin


def hexs(b, start, n):
    return " ".join("%02x" % x for x in b[start:start + n])


def render(b):
    """把一段字节按 可变长记录 渲染成可读文本"""
    out = []
    out.append("      hex : %s" % hexs(b, 0, min(len(b), 64)))
    out.append("      latin1: %s" % repr(b[:64]))
    try:
        out.append("      gbk   : %s" % repr(b.decode("gbk", errors="replace")[:40]))
    except Exception:
        pass
    return "\n".join(out)


def lcp(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[i] == b[i]:
        i += 1
    return i


def lcs(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[len(a) - 1 - i] == b[len(b) - 1 - i]:
        i += 1
    return i


def main():
    a = open(BASE, "rb").read()
    b = open(CAGED, "rb").read()
    print("base  = %6d B  %s" % (len(a), BASE))
    print("caged = %6d B  %s" % (len(b), CAGED))
    print("Δ     = %+d B" % (len(b) - len(a)))

    p = lcp(a, b)
    s = lcs(a, b)
    print("\n公共前缀长度 = %d (0x%X)" % (p, p))
    print("公共后缀长度 = %d (0x%X)" % (s, s))
    ins_a_from, ins_a_to = p, len(a) - s
    ins_b_from, ins_b_to = p, len(b) - s
    print("base  的差异区 [%d, %d)  共 %d B" % (ins_a_from, ins_a_to, ins_a_to - ins_a_from))
    print("caged 的差异区 [%d, %d)  共 %d B" % (ins_b_from, ins_b_to, ins_b_to - ins_b_from))
    print("  → 若只靠前后缀判定，则『净插入』%d B，『原地改写』%d B"
          % (ins_b_to - ins_b_from - (ins_a_to - ins_a_from),
             min(ins_a_to - ins_a_from, ins_b_to - ins_b_from)))

    print("\n--- base 差异区原始字节（最多 96B）---")
    print(render(a[ins_a_from:ins_a_from + 96]))
    print("\n--- caged 差异区原始字节（最多 160B）---")
    print(render(b[ins_b_from:ins_b_from + 160]))

    # 关键锚点定位
    print("\n--- 锚点 ---")
    for label, needle, buf, other in (("krnln ASCII", b"krnln", b, a),
                                      ("elang_addin ASCII", b"elang_addin", b, a),
                                      ("krnln GUID(ascii)", KR_GUID_ASCII, b, a),
                                      ("elang_addin GUID(ascii)", EL_GUID_ASCII, b, a)):
        i = buf.find(needle)
        j = other.find(needle)
        print("  %-24s caged@%s  base@%s" % (label, i if i >= 0 else "—", j if j >= 0 else "—"))
        if i >= 0:
            lo = max(0, i - 24)
            print("       caged 上下文[%d,%d): %s" % (lo, i + len(needle) + 24,
                                                      hexs(buf, lo, len(needle) + 48)))

    # 原始二进制 GUID（16 字节）是否存在
    print("\n--- 原始 16 字节 GUID 探测 ---")
    for label, g in (("krnln", bytes.fromhex("d09f2340818511d396f6aaf844c7e325")),
                     ("elang_addin", bytes.fromhex("7a1e4f22c3b0499e8d6a0011223344fe"))):
        print("  %-12s 正向@%s  反向@%s" % (label, b.find(g), b.find(g[::-1])))

    # 尾部对照（库表可能在文件尾部）
    print("\n--- 尾部 96B 对照 ---")
    print("  base : %s" % hexs(a, len(a) - 96, 96))
    print("  caged: %s" % hexs(b, len(b) - 96, 96))
    return 0


if __name__ == "__main__":
    sys.exit(main())
