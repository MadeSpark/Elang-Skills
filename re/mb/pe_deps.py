# -*- coding: utf-8 -*-
"""PE 导入表 / 导出表速查（纯 Python，不依赖 dumpbin）。

用途：
  1) 看易语言编译出的 exe 是否把 `.DLL命令` 指向的 DLL 写进了【导入表】——
     若是，则该 DLL 由 Windows 加载器在进程启动时加载、进程退出才卸载，任何
     FreeLibrary 都卸不掉它（这正是「热重载后内核状态还在」的根因）。
  2) 看 node.dll 导出了哪些 wke* 符号（确认内核接口）。
"""
import struct, sys, os

def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]

def rva2off(secs, rva):
    for name, va, vsz, raw, rsz in secs:
        if va <= rva < va + max(vsz, rsz):
            return raw + (rva - va)
    return None

def cstr(b, o, n=256):
    e = b.find(b"\0", o)
    if e < 0 or e - o > n: e = o + n
    return b[o:e].decode("latin1", "replace")

def parse(path):
    b = open(path, "rb").read()
    if b[:2] != b"MZ": return None
    pe = u32(b, 0x3C)
    if b[pe:pe+4] != b"PE\0\0": return None
    nsec = u16(b, pe+6)
    optsz = u16(b, pe+20)
    opt = pe + 24
    magic = u16(b, opt)
    is64 = (magic == 0x20B)
    # DataDirectory: PE32 -> opt+96, PE32+ -> opt+112
    ddoff = opt + (112 if is64 else 96)
    imp_rva, imp_sz = u32(b, ddoff+8), u32(b, ddoff+12)
    exp_rva, exp_sz = u32(b, ddoff), u32(b, ddoff+4)
    secs = []
    so = opt + optsz
    for i in range(nsec):
        s = so + i*40
        secs.append((b[s:s+8].rstrip(b"\0").decode("latin1"),
                     u32(b, s+12), u32(b, s+8), u32(b, s+20), u32(b, s+16)))
    out = {"is64": is64, "imports": [], "exports": [], "machine": u16(b, pe+4)}
    # imports
    if imp_rva:
        o = rva2off(secs, imp_rva)
        if o:
            while True:
                oft_rva, ts, fc, nm_rva, ft_rva = struct.unpack_from("<IIIII", b, o)
                if nm_rva == 0 and ft_rva == 0: break
                dll = cstr(b, rva2off(secs, nm_rva) or 0)
                funcs = 0
                base = oft_rva if oft_rva else ft_rva   # 绑定时 IAT 已被改写，用 OFT
                fo = rva2off(secs, base) if base else None
                if fo:
                    k = 0
                    while True:
                        v = struct.unpack_from("<I", b, fo + k*4)[0]
                        if v == 0: break
                        funcs += 1; k += 1
                        if k > 20000: break
                out["imports"].append((dll, funcs))
                o += 20
    # exports
    if exp_rva:
        o = rva2off(secs, exp_rva)
        if o:
            nnames = u32(b, o+24)
            names_rva = u32(b, o+32)
            no = rva2off(secs, names_rva)
            for i in range(min(nnames, 20000)):
                nr = u32(b, no + i*4)
                eo = rva2off(secs, nr)
                if eo: out["exports"].append(cstr(b, eo, 128))
    return out

def main():
    for path in sys.argv[1:]:
        if not os.path.isfile(path):
            print("!! 不存在:", path); continue
        r = parse(path)
        if not r:
            print("!! 非 PE:", path); continue
        print("=" * 70)
        print("文件:", path, " (%.2f MB)" % (os.path.getsize(path)/1048576))
        print("  架构:", "x64" if r["is64"] else "x86", " machine=0x%04X" % r["machine"])
        print("  --- 导入表（%d 个 DLL）---" % len(r["imports"]))
        for dll, n in sorted(r["imports"], key=lambda x: -x[1]):
            print("     %-44s %4d 个函数" % (dll, n))
        ex = r["exports"]
        print("  --- 导出表（%d 个符号）---" % len(ex))
        wke = [e for e in ex if e.startswith("wke")]
        print("     其中 wke* : %d 个" % len(wke))
        if wke: print("     样例:", ", ".join(wke[:14]))

main()
