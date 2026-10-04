#!/usr/bin/env python3
"""极小 BMP(24bpp, 自底向上) -> PNG 转换器（本机无 PIL，纯 Python 手写）。
用法: python bmp2png.py in.bmp out.png
"""
import struct, sys, zlib


def bmp_to_png(src, dst):
    d = open(src, 'rb').read()
    assert d[:2] == b'BM', 'not a BMP'
    off = struct.unpack_from('<I', d, 10)[0]
    w, h = struct.unpack_from('<ii', d, 18)
    bpp = struct.unpack_from('<H', d, 28)[0]
    assert bpp == 24, 'only 24bpp supported, got %d' % bpp
    bottom_up = h > 0
    h = abs(h)
    stride = ((w * 3 + 3) // 4) * 4
    rows = []
    for y in range(h):
        sy = (h - 1 - y) if bottom_up else y
        line = d[off + sy * stride: off + sy * stride + w * 3]
        rgb = bytearray()
        for x in range(w):
            rgb += bytes((line[x * 3 + 2], line[x * 3 + 1], line[x * 3 + 0]))
        rows.append(b'\x00' + bytes(rgb))
    raw = b''.join(rows)

    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(dst, 'wb').write(png)
    print('%s -> %s  %dx%d' % (src, dst, w, h))


if __name__ == '__main__':
    bmp_to_png(sys.argv[1], sys.argv[2])
