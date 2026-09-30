#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""在 .e 里找某些 32 位 ID 出现在哪个段、段内偏移多少、前后字节是什么。
用法: python tools/idwhere.py <file.e> 0x190D3F68 0x190FD47C ...
"""
import struct
import sys
from pathlib import Path

NAMES = {
    0x01007319: "用户信息", 0x02007319: "系统信息", 0x03007319: "程序段", 0x04007319: "资源段",
    0x05007319: "可丢失", 0x07007319: "结束", 0x08007319: "初始模块", 0x09007319: "编辑信息2",
    0x0A007319: "辅助1(事件)", 0x0B007319: "辅助2(公开性)", 0x0C007319: "模块记录", 0x0D007319: "易包1",
    0x0E007319: "过滤器", 0x0F007319: "模块记录2", 0x10007319: "辅助3", 0x11007319: "编译条件",
}


def sections(data):
    off = 8
    while off + 100 <= len(data):
        key = struct.unpack_from("<i", data, off + 8)[0] & 0xFFFFFFFF
        ln = struct.unpack_from("<i", data, off + 56)[0]
        yield key, off + 100, ln
        if key == 0x07007319:
            break
        off += 100 + ln


def main():
    path = Path(sys.argv[1])
    data = path.read_bytes()
    ids = [int(x, 0) for x in sys.argv[2:]]
    secs = list(sections(data))
    for i in ids:
        pat = struct.pack("<I", i & 0xFFFFFFFF)
        print(f"\n=== 0x{i:08X}")
        start = 0
        while True:
            k = data.find(pat, start)
            if k < 0:
                break
            start = k + 1
            for key, s, ln in secs:
                if s <= k < s + ln:
                    rel = k - s
                    ctx = data[max(s, k - 16):k + 20].hex(" ")
                    print(f"  {NAMES.get(key, hex(key)):<12} +{rel:<8} {ctx}")
                    break


if __name__ == "__main__":
    main()
