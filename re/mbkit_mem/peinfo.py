#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查 MbKit.dll 的 PE 结构，判定「内存映射它」需要做哪些事。

输出：首选基址 / SizeOfImage / 节表 / TLS 目录 / 重定位 / 导入表 / 导出表 / 是否有 .reloc。
"""
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    print("文件: %s  (%d 字节)" % (path, len(d)))
    e = u32(d, 0x3C)
    assert d[e:e + 4] == b"PE\0\0"
    machine = u16(d, e + 4)
    nsec = u16(d, e + 6)
    opt = e + 24
    magic = u16(d, opt)
    print("machine=0x%04X (%s)  sections=%d  optmagic=0x%04X" % (
        machine, "i386" if machine == 0x14C else "?", nsec, magic))
    image_base = u32(d, opt + 28)
    sizeof_image = u32(d, opt + 56)
    entry = u32(d, opt + 16)
    dllchar = u16(d, opt + 70)
    print("ImageBase=0x%08X  SizeOfImage=0x%X (%d)  AddressOfEntryPoint=0x%X  DllCharacteristics=0x%04X"
          % (image_base, sizeof_image, sizeof_image, entry, dllchar))
    ndd = u32(d, opt + 92)
    dd = opt + 96
    names = ["Export", "Import", "Resource", "Exception", "Security", "BaseReloc",
             "Debug", "Arch", "GlobalPtr", "TLS", "LoadConfig", "BoundImport",
             "IAT", "DelayImport", "CLR", "Reserved"]
    print("\n== 数据目录 ==")
    dirs = {}
    for i in range(min(ndd, 16)):
        rva = u32(d, dd + i * 8)
        size = u32(d, dd + i * 8 + 4)
        dirs[names[i]] = (rva, size)
        if rva:
            print("  %-12s rva=0x%08X size=0x%X" % (names[i], rva, size))

    secs = []
    sec0 = opt + u16(d, e + 20)
    print("\n== 节表 ==")
    for i in range(nsec):
        o = sec0 + i * 40
        nm = d[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", d, o + 8)
        chars = u32(d, o + 36)
        secs.append((nm, va, vsize, raw, rsize, chars))
        print("  %-8s va=0x%06X vsize=0x%06X raw=0x%06X rsize=0x%06X chars=0x%08X%s"
              % (nm, va, vsize, raw, rsize, chars, "  <== 可写" if chars & 0x80000000 else ""))

    def va2off(va):
        for nm, sva, vsize, raw, rsize, _ in secs:
            if sva <= va < sva + max(vsize, rsize):
                return raw + (va - sva)
        return None

    # TLS
    tls = dirs.get("TLS")
    print("\n== TLS ==")
    if tls and tls[0]:
        o = va2off(tls[0])
        s, e_, idx, cbs, z, cz = struct.unpack_from("<IIIIII", d, o)
        print("  StartRaw=0x%X EndRaw=0x%X AddressOfIndex=0x%X AddressOfCallBacks=0x%X ZeroFill=0x%X"
              % (s, e_, idx, cbs, z))
        print("  TLS 数据长度 = %d 字节；索引槽 rva=0x%X；回调表 rva=0x%X" % (e_ - s, idx, cbs))
        cb = va2off(cbs)
        if cb is not None:
            arr = []
            while True:
                v = u32(d, cb + 4 * len(arr))
                if v == 0:
                    break
                arr.append(v)
            print("  TLS 回调数 = %d  %s" % (len(arr), ["0x%X" % x for x in arr]))
    else:
        print("  (无)")

    # 导入
    imp = dirs.get("Import")
    print("\n== 导入表 ==")
    if imp and imp[0]:
        o = va2off(imp[0])
        i = 0
        while True:
            oft, tstamp, fwd, name_rva, first = struct.unpack_from("<IIIII", d, o + i * 20)
            if name_rva == 0 and first == 0:
                break
            no = va2off(name_rva)
            dll = d[no:d.index(b"\0", no)].decode("latin1")
            thunk = first or oft
            to = va2off(thunk)
            n, k = 0, 0
            fns = []
            while True:
                v = u32(d, to + k * 4)
                if v == 0:
                    break
                if not (v & 0x80000000):
                    fo = va2off(v)
                    fn = d[fo + 2:d.index(b"\0", fo + 2)].decode("latin1")
                    fns.append(fn)
                n += 1
                k += 1
            print("  %-16s %d 个: %s" % (dll, n, ", ".join(fns[:12]) + (" ..." if n > 12 else "")))
            i += 1
    else:
        print("  (无)")

    # 导出
    exp = dirs.get("Export")
    print("\n== 导出表 ==")
    if exp and exp[0]:
        o = va2off(exp[0])
        flags, tstamp, vmaj, vmin, name_rva, base, nfunc, nname, afunc, aname, aord = \
            struct.unpack_from("<IIHHIIIIIII", d, o)
        no = va2off(name_rva)
        print("  DLL名=%s  函数数=%d 具名=%d  序号基=%d"
              % (d[no:d.index(b"\0", no)].decode("latin1"), nfunc, nname, base))
        ao, an = va2off(afunc), va2off(aname)
        rows = []
        for k in range(nname):
            nm_rva = u32(d, an + k * 4)
            ordn = u16(d, ao + k * 2)
            fno = va2off(nm_rva)
            nm = d[fno:d.index(b"\0", fno)].decode("latin1")
            frva = u32(d, va2off(afunc) + ordn * 4)
            rows.append((nm, frva))
        print("  前 10 个: " + ", ".join("%s@0x%X" % r for r in rows[:10]))
        # 是否有转发导出
        fwd = [r for r in rows if exp[0] <= r[1] < exp[0] + exp[1]]
        print("  转发导出: %d" % len(fwd))
    else:
        print("  (无)")

    # 重定位
    rel = dirs.get("BaseReloc")
    print("\n== 重定位 ==")
    if rel and rel[0]:
        o = va2off(rel[0])
        tot, cnt = 0, 0
        p = 0
        while p < rel[1]:
            page, bsz = struct.unpack_from("<II", d, o + p)
            if bsz == 0:
                break
            cnt += 1
            tot += (bsz - 8) // 2
            p += bsz
        print("  块数=%d  条目数=%d  ⇒ %s" % (cnt, tot, "需要重定位" if tot else "无重定位项"))
    else:
        print("  (无 .reloc ⇒ 不能重定位，必须映射到首选基址)")


if __name__ == "__main__":
    raise SystemExit(main())
