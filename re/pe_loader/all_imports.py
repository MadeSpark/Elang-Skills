#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""导出某个 PE 的**全部**导入函数名（按 DLL 分组），供 grep 分析。
用法: python all_imports.py <pe> [关键词...]
"""
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")


def rva2off(secs, rva):
    for va, vs, raw, rsz, _ in secs:
        if va <= rva < va + max(vs, rsz):
            return raw + (rva - va)
    return None


def cstr(b, off):
    if off is None:
        return None
    e = b.find(b"\x00", off)
    return b[off:e].decode("latin1")


def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    e_lfanew = struct.unpack_from("<I", b, 0x3C)[0]
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", b, coff + 2)[0]
    optsz = struct.unpack_from("<H", b, coff + 16)[0]
    opt = coff + 20
    pe32plus = struct.unpack_from("<H", b, opt)[0] == 0x20B
    ddoff = opt + (0x70 if pe32plus else 0x60)
    secs = []
    so = opt + optsz
    for i in range(nsec):
        vs, va, rsz, raw = struct.unpack_from("<IIII", b, so + i * 40 + 8)
        secs.append((va, vs, raw, rsz, ""))
    ir, _ = struct.unpack_from("<II", b, ddoff + 1 * 8)
    io = rva2off(secs, ir)
    total = 0
    buckets = {}
    k = 0
    while True:
        oft, ts, fc, nrva, iat = struct.unpack_from("<IIIII", b, io + k * 20)
        if oft == 0 and nrva == 0 and iat == 0:
            break
        dll = cstr(b, rva2off(secs, nrva))
        thunk = rva2off(secs, oft if oft else iat)
        j, names = 0, []
        while True:
            v = struct.unpack_from("<I", b, thunk + j * 4)[0]
            if v == 0:
                break
            names.append("#%d" % (v & 0xFFFF) if v & 0x80000000
                         else cstr(b, rva2off(secs, v) + 2))
            j += 1
        buckets[dll] = names
        total += len(names)
        k += 1
    print("导入 DLL 数 = %d，导入函数总数 = %d" % (len(buckets), total))
    for dll, names in buckets.items():
        print("  %-22s %d 个" % (dll, len(names)))
    if len(sys.argv) > 2:
        kws = [k.lower() for k in sys.argv[2:]]
        print("\n--- 命中关键词 ---")
        for dll, names in buckets.items():
            for n in names:
                if any(k in n.lower() for k in kws):
                    print("  %s!%s" % (dll, n))
    else:
        for dll, names in buckets.items():
            print("\n[%s]" % dll)
            for n in names:
                print("  " + n)


if __name__ == "__main__":
    main()
