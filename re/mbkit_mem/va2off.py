#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""读取 PE 的节表，把 VA 映射到文件偏移并 dump 一段字节（用于看 易语言 的 .DLL命令 分派器）。"""
import struct
import sys

sys.stdout.reconfigure(encoding="utf-8")


def sections(path):
    d = open(path, "rb").read()
    e_lfanew = struct.unpack_from("<I", d, 0x3C)[0]
    assert d[e_lfanew:e_lfanew + 4] == b"PE\0\0", "not PE"
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", d, coff + 2)[0]
    opt_size = struct.unpack_from("<H", d, coff + 16)[0]
    opt = coff + 20
    image_base = struct.unpack_from("<I", d, opt + 28)[0]
    sec0 = opt + opt_size
    secs = []
    for i in range(nsec):
        o = sec0 + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, va, rsize, raw = struct.unpack_from("<IIII", d, o + 8)
        secs.append((name, va, vsize, raw, rsize))
    return d, image_base, secs


def va2off(secs, va):
    for name, sva, vsize, raw, rsize in secs:
        if sva <= va < sva + max(vsize, rsize):
            return raw + (va - sva)
    return None


def main():
    path = sys.argv[1]
    d, base, secs = sections(path)
    print("image_base = 0x%08X" % base)
    for name, sva, vsize, raw, rsize in secs:
        print("  %-8s va=0x%08X vsize=0x%06X raw=0x%08X rsize=0x%06X" % (name, sva, vsize, raw, rsize))
    for spec in sys.argv[2:]:
        va_s, ln_s = spec.split(":")
        va, ln = int(va_s, 16), int(ln_s, 16)
        off = va2off(secs, va)
        print("\n== VA 0x%08X -> off 0x%08X len 0x%X ==" % (va, off or 0, ln))
        if off is None:
            print("  (not in any section)")
            continue
        blob = d[off:off + ln]
        # 反汇编式打印：每 16 字节 + 简易 ff25 识别
        for i in range(0, len(blob), 16):
            chunk = blob[i:i + 16]
            hexs = " ".join("%02X" % b for b in chunk)
            note = ""
            if len(chunk) >= 6 and chunk[0] == 0xFF and chunk[1] == 0x25:
                tgt = struct.unpack_from("<I", chunk, 2)[0]
                note = "  ; jmp [0x%08X]" % tgt
            elif len(chunk) >= 5 and chunk[0] == 0xE8:
                rel = struct.unpack_from("<i", chunk, 1)[0]
                note = "  ; call 0x%08X" % (va + i + 5 + rel)
            print("  %08X: %-47s%s" % (va + i, hexs, note))


if __name__ == "__main__":
    raise SystemExit(main())
