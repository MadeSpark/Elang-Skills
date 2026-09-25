#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""bmp2png.py —— 纯标准库 BMP(24/32 位) → PNG，用于把截图喂给多模态看图。
本机没有 PIL/Pillow，也不想装依赖，所以手写。

用法：python bmp2png.py in.bmp [out.png]
"""
import struct
import sys
import zlib


def convert(src, dst):
    d = open(src, "rb").read()
    assert d[:2] == b"BM", "不是 BMP"
    pixoff = struct.unpack_from("<I", d, 10)[0]
    hdrsz = struct.unpack_from("<I", d, 14)[0]
    w, h = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    comp = struct.unpack_from("<I", d, 30)[0]
    assert comp == 0, "只支持未压缩 BMP"
    assert bpp in (24, 32), "只支持 24/32 位，当前 %d" % bpp

    flip = h > 0
    ah = abs(h)
    px = bpp // 8
    rowin = ((w * bpp + 31) // 32) * 4

    raw = bytearray()
    for y in range(ah):
        sy = (ah - 1 - y) if flip else y
        line = d[pixoff + sy * rowin: pixoff + sy * rowin + w * px]
        out = bytearray()
        for x in range(w):
            b, g, r = line[x * px], line[x * px + 1], line[x * px + 2]
            out += bytes((r, g, b))
        raw += b"\x00" + out

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, ah, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
    png += chunk(b"IEND", b"")
    open(dst, "wb").write(png)
    return w, ah


if __name__ == "__main__":
    s = sys.argv[1]
    o = sys.argv[2] if len(sys.argv) > 2 else s.rsplit(".", 1)[0] + ".png"
    print("%s -> %s  %dx%d" % (s, o, *convert(s, o)))
