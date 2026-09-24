#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""极简 PE 解析：看导入表 / 导出表 / 子系统 / 是否 DLL。纯标准库。"""
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")


def rva2off(secs, rva):
    for va, vs, raw, rsz, name in secs:
        if va <= rva < va + max(vs, rsz):
            return raw + (rva - va)
    return None


def cstr(b, off):
    if off is None:
        return None
    e = b.find(b"\x00", off)
    return b[off:e].decode("latin1")


def parse(path):
    b = open(path, "rb").read()
    print("=" * 70)
    print(path, len(b), "字节")
    if b[:2] != b"MZ":
        print("  不是 PE")
        return
    e_lfanew = struct.unpack_from("<I", b, 0x3C)[0]
    assert b[e_lfanew:e_lfanew + 4] == b"PE\0\0", "PE 签名不符"
    coff = e_lfanew + 4
    machine, nsec, _, _, _, optsz, chars = struct.unpack_from("<HHIIIHH", b, coff)
    print("  machine=0x%04X  sections=%d  characteristics=0x%04X%s"
          % (machine, nsec, chars, "  [DLL]" if chars & 0x2000 else ""))
    opt = coff + 20
    magic = struct.unpack_from("<H", b, opt)[0]
    pe32plus = magic == 0x20B
    print("  optmagic=0x%03X (%s)" % (magic, "PE32+" if pe32plus else "PE32"))
    subsystem = struct.unpack_from("<H", b, opt + (0x5C if pe32plus else 0x44))[0]
    print("  subsystem=%d (%s)" % (subsystem, {2: "GUI", 3: "CONSOLE"}.get(subsystem, "?")))
    ddoff = opt + (0x70 if pe32plus else 0x60)
    ndd = struct.unpack_from("<I", b, opt + (0x6C if pe32plus else 0x5C))[0]
    secs = []
    so = opt + optsz
    for i in range(nsec):
        nm = b[so + i * 40:so + i * 40 + 8].rstrip(b"\x00").decode("latin1")
        vs, va, rsz, raw = struct.unpack_from("<IIII", b, so + i * 40 + 8)
        secs.append((va, vs, raw, rsz, nm))
    print("  节:", ", ".join("%s(va=%X,vs=%X)" % (s[4], s[0], s[1]) for s in secs))

    def dd(idx):
        rva, sz = struct.unpack_from("<II", b, ddoff + idx * 8)
        return rva, sz

    # 导出表 (index 0)
    er, es = dd(0)
    print("  --- 导出表 ---")
    if er and es:
        eo = rva2off(secs, er)
        nfun, nnam = struct.unpack_from("<II", b, eo + 0x14)[0], struct.unpack_from("<I", b, eo + 0x18)[0]
        naddr = struct.unpack_from("<I", b, eo + 0x1C)[0]
        names = struct.unpack_from("<I", b, eo + 0x20)[0]
        dllname = cstr(b, rva2off(secs, struct.unpack_from("<I", b, eo + 0x0C)[0]))
        print("    DLL 名: %r   导出函数 %d 个，具名 %d 个" % (dllname, nfun, nnam))
        no = rva2off(secs, names)
        ao = rva2off(secs, naddr)
        oo = rva2off(secs, struct.unpack_from("<I", b, eo + 0x1C + 4)[0]) if False else None
        for i in range(nnam):
            nr = struct.unpack_from("<I", b, no + i * 4)[0]
            ordinal = struct.unpack_from("<H", b, rva2off(secs, struct.unpack_from("<I", b, eo + 0x24)[0]) + i * 2)[0] if False else None
            fn = cstr(b, rva2off(secs, nr))
            print("      %s" % fn)
        # 按序号列地址
        for i in range(min(nfun, 12)):
            fr = struct.unpack_from("<I", b, ao + i * 4)[0]
            print("      [%2d] RVA=%X" % (i, fr))
    else:
        print("    (无)")

    # 导入表 (index 1)
    ir, isz = dd(1)
    print("  --- 导入表 ---")
    if ir and isz:
        io = rva2off(secs, ir)
        k = 0
        while True:
            oft, ts, fc, nrva, iat = struct.unpack_from("<IIIII", b, io + k * 20)
            if oft == 0 and nrva == 0 and iat == 0:
                break
            dll = cstr(b, rva2off(secs, nrva))
            names_out = []
            thunk = rva2off(secs, oft if oft else iat)
            j = 0
            while True:
                v = struct.unpack_from("<I", b, thunk + j * 4)[0]
                if v == 0:
                    break
                if v & 0x80000000:
                    names_out.append("#%d" % (v & 0xFFFF))
                else:
                    names_out.append(cstr(b, rva2off(secs, v) + 2))
                j += 1
            print("    %s: %s" % (dll, " ".join(names_out[:14]) + (" …" if len(names_out) > 14 else "")))
            k += 1
    else:
        print("    (无)")

    # TLS / 重定位 / 资源
    for idx, label in ((2, "资源"), (5, "重定位"), (9, "TLS"), (10, "LOAD_CONFIG"), (4, "安全")):
        r, s = dd(idx)
        print("  数据目录[%2d] %-12s rva=%X size=%X" % (idx, label, r, s))


for p in sys.argv[1:]:
    try:
        parse(p)
    except Exception as ex:
        print("解析失败", p, ex)
