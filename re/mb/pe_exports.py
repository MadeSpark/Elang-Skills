# -*- coding: utf-8 -*-
"""pe_exports.py —— 读 PE 的导出表（名字 + 序号），判断导出名是否带 @N 修饰。

为 MbKit.dll 这类「要手工核对导出名」的场景准备（易语言 .DLL命令 依赖朴素名）。

用法：python pe_exports.py <文件.dll>
"""
import struct
import sys


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def cstr(b, o, limit=512):
    e = b.find(b"\0", o, o + limit)
    return b[o:e if e >= 0 else o + limit].decode("latin-1")


def main():
    path = sys.argv[1]
    b = open(path, "rb").read()
    pe = u32(b, 0x3C)
    assert b[pe:pe + 4] == b"PE\0\0", "不是 PE 文件"
    nsec = u16(b, pe + 6)
    optsz = u16(b, pe + 20)
    magic = u16(b, pe + 24)
    machine = u16(b, pe + 4)
    o = pe + 24
    if magic == 0x10B:          # PE32
        ddir = o + 96
    else:                       # PE32+
        ddir = o + 112
    edir_rva, edir_sz = u32(b, ddir), u32(b, ddir + 4)

    secs = []
    so = pe + 24 + optsz
    for i in range(nsec):
        s = so + i * 40
        name = b[s:s + 8].rstrip(b"\0").decode("latin-1")
        vsz, va, rsz, ro = (u32(b, s + 8), u32(b, s + 12),
                            u32(b, s + 16), u32(b, s + 20))
        secs.append((name, va, vsz, ro, rsz))

    def rva2off(rva):
        for _, va, vsz, ro, rsz in secs:
            if va <= rva < va + max(vsz, rsz):
                return ro + (rva - va)
        return None

    eo = rva2off(edir_rva)
    print("文件  : %s" % path)
    print("架构  : machine=0x%04X (%s)" % (machine, "x86" if machine == 0x14C else "x64"))
    if not edir_rva or not edir_sz or eo is None:
        print("** 无导出表（目录项为空）")
        return 1
    if u32(b, eo + 24) == 0 and u32(b, eo + 20) == 0:
        print("** 导出表存在但没有任何具名导出（可能被 --strip 掉了名字）")
        return 1

    nfun, nname = u32(b, eo + 20), u32(b, eo + 24)
    npt = rva2off(u32(b, eo + 32))
    names = []
    for i in range(nname):
        nr = u32(b, npt + i * 4)
        no = rva2off(nr)
        if no is not None:
            names.append(cstr(b, no))
    names.sort()

    deco = [n for n in names if "@" in n]
    print("导出数: %d（函数 %d / 具名 %d）" % (len(names), nfun, nname))
    print("带 @N 修饰的: %d %s" % (len(deco), deco[:8] if deco else ""))
    print("-" * 62)
    for n in names:
        print("   ", n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
