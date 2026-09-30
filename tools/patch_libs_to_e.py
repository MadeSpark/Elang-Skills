#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""patch_libs_to_e.py —— 把「elang_addin 支持库记录」**字节级**插进 .e 的库表（B1 路径），
完全不经过 e2t/t2e 文本链 —— 供 mkcage 文本链跑不通的工程（引用了第三方支持库、
跨模块常量等）做无头编译前加壳用。

.e 库表二进制格式（2026-09-26 用多对 base/_ai 样本 diff 实测逆向）：

    [u16 库数量]
    [u32 记录长度][记录字节]        ← 记录 = key \r GUID \r 主版本 \r 次版本 \r GBK库名
    [u32 记录长度][记录字节]
    ……
    [u32 0]                          ← 终止符

    * 记录定界用 0x0d（\r）；krnln 记录恒为链首。
    * 插入一条记录 = 在终止符处替换为 [u32 新记录长度][新记录][u32 0]，同时 u16 数量 +1。
      实测纯插入 71 B（1→2 条），插入点之后全部字节原样后移，无需修任何偏移。
    * 自校验：u16 数量必须等于链走查所得记录数，否则拒绝改（宁可不加壳，不许改坏工程）。

用法
====
    python patch_libs_to_e.py <输入.e> <输出.e>
        [--key elang_addin] [--guid …] [--name …] [--major 1] [--minor 0]
        （缺省值 = mkcage.py 的 FALLBACK 常量；绝不改输入文件）

退出码：0 成功；1 失败（含自校验不过）。
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys

FALLBACK = {"key": "elang_addin", "guid": "7a1e4f22c3b0499e8d6a0011223344fe",
            "name": "AI调试宿主", "major": 1, "minor": 0}


def fail(msg: str) -> None:
    print("!! %s" % msg, file=sys.stderr)
    raise SystemExit(1)


def walk_chain(data: bytes, first_len_off: int):
    """从首个 [u32 len] 位置沿链走查。返回记录 (key, len_off) 列表；格式不合法抛 ValueError。"""
    recs = []
    p = first_len_off
    for _ in range(64):
        if p + 4 > len(data):
            raise ValueError("链越界")
        (n,) = struct.unpack_from("<I", data, p)
        if n == 0:
            return recs, p
        if not (8 <= n <= 512) or p + 4 + n > len(data):
            raise ValueError("记录长度 %d 不合理" % n)
        rec = data[p + 4: p + 4 + n]
        if rec.count(b"\r") != 4:
            raise ValueError("记录 \\r 数 = %d ≠ 4" % rec.count(b"\r"))
        recs.append((rec.split(b"\r")[0].decode("ascii", "replace"), p))
        p += 4 + n
    raise ValueError("链过长（>64 条）")


def find_lib_table(data: bytes):
    """定位库表：遍历 `krnln\\r` 出现位置，取「链走查通过」的那处。"""
    off = -1
    while True:
        off = data.find(b"krnln\r", off + 1)
        if off < 0:
            return None
        if off < 6:
            continue
        first_len_off = off - 4
        (n,) = struct.unpack_from("<I", data, first_len_off)
        if not (40 <= n <= 120):
            continue
        try:
            recs, term_off = walk_chain(data, first_len_off)
        except ValueError:
            continue
        return first_len_off, recs, term_off


def main() -> int:
    ap = argparse.ArgumentParser(description="字节级把支持库记录插进 .e 库表（B1，不走文本链）")
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--key", default=FALLBACK["key"])
    ap.add_argument("--guid", default=FALLBACK["guid"])
    ap.add_argument("--name", default=FALLBACK["name"])
    ap.add_argument("--major", type=int, default=FALLBACK["major"])
    ap.add_argument("--minor", type=int, default=FALLBACK["minor"])
    a = ap.parse_args()

    data = open(a.input, "rb").read()
    in_md5 = hashlib.md5(data).hexdigest()

    hit = find_lib_table(data)
    if not hit:
        fail("未定位到库表（找不到合法的 krnln 链）")
    first_len_off, recs, term_off = hit
    keys = [k for k, _ in recs]
    if a.key in keys:
        fail("工程已声明 %s（库表含 %s），无需加壳" % (a.key, keys))

    # u16 数量自校验
    (cnt,) = struct.unpack_from("<H", data, first_len_off - 2)
    if cnt != len(recs):
        fail("数量字段=%d ≠ 链走查记录数=%d，格式与预期不符，拒绝修改" % (cnt, len(recs)))

    rec = ("%s\r%s\r%d\r%d\r" % (a.key, a.guid, a.major, a.minor)).encode("ascii") \
        + a.name.encode("gbk")
    # 终止符 [u32 0] → [u32 len(rec)][rec][u32 0]；数量 +1
    out = (data[:term_off]
           + struct.pack("<I", len(rec)) + rec + struct.pack("<I", 0)
           + data[term_off + 4:])
    out = bytearray(out)
    struct.pack_into("<H", out, first_len_off - 2, cnt + 1)
    out = bytes(out)

    # 自校验：重新走查产物
    hit2 = find_lib_table(out)
    if not hit2:
        fail("产物自校验失败：库表走查不通过")
    first2, recs2, term2 = hit2
    if [k for k, _ in recs2] != keys + [a.key] or first2 != first_len_off:
        fail("产物自校验失败：记录序列 = %s" % [k for k, _ in recs2])
    (cnt2,) = struct.unpack_from("<H", out, first2 - 2)
    if cnt2 != len(recs2):
        fail("产物数量字段=%d ≠ 记录数=%d" % (cnt2, len(recs2)))
    if a.key.encode("ascii") not in out:
        fail("产物里找不到 Key ASCII")
    if hashlib.md5(out).hexdigest() == in_md5:
        fail("产物与输入相同")

    with open(a.output, "wb") as f:
        f.write(out)
    print("OK: %s" % a.output)
    print("  记录: %s → %s" % (keys, keys + [a.key]))
    print("  插入 %d B @ 终止符 %#x；数量 %d → %d；大小 %d → %d B"
          % (len(rec) + 8, term_off, cnt, cnt2, len(data), len(out)))
    print("  md5: %s → %s" % (in_md5, hashlib.md5(out).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
