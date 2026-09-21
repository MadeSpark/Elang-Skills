#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Extract strings from a binary via GBK and UTF-16LE decoding; map to file offsets/RVA.
No `strings` tool available on this machine, so we implement it.
"""
import sys
import io
import re

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

PATH = r"D:\ides\e\e.exe"
MIN = 4


def rva_of_offset(pe, off):
    for sec in pe.sections:
        start = sec.PointerToRawData
        end = start + sec.SizeOfRawData
        if start <= off < end:
            return sec.VirtualAddress + (off - start)
    return None


def ascii_strings(data, minlen=MIN):
    """ASCII printable runs (subset of GBK)."""
    out = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, data):
        out.append((m.start(), m.group().decode("latin-1")))
    return out


def gbk_strings(data, minlen=MIN):
    """Both pure-ASCII and GBK multibyte runs."""
    out = []
    i = 0
    n = len(data)
    start = None
    buf = bytearray()
    while i < n:
        b = data[i]
        if 0x20 <= b <= 0x7E or b in (0x09, 0x0A, 0x0D):
            if start is None:
                start = i
            buf.append(b)
            i += 1
        elif 0x81 <= b <= 0xFE and i + 1 < n and (
                0x40 <= data[i + 1] <= 0xFE and data[i + 1] != 0x7F):
            if start is None:
                start = i
            buf.append(b)
            buf.append(data[i + 1])
            i += 2
        else:
            if start is not None and len(buf) >= minlen:
                out.append((start, bytes(buf).decode("gbk", errors="replace")))
            start = None
            buf = bytearray()
            i += 1
    if start is not None and len(buf) >= minlen:
        out.append((start, bytes(buf).decode("gbk", errors="replace")))
    return out


def utf16_strings(data, minlen=MIN):
    out = []
    i = 0
    n = len(data)
    start = None
    buf = bytearray()
    while i + 1 < n:
        lo = data[i]
        hi = data[i + 1]
        cp = lo | (hi << 8)
        ch_ok = (0x20 <= cp <= 0x7E) or (0x4E00 <= cp <= 0x9FFF) or (0x3000 <= cp <= 0x30FF)
        if hi == 0 and (0x20 <= lo <= 0x7E):
            ok = True
        elif ch_ok:
            ok = True
        else:
            ok = False
        if ok:
            if start is None:
                start = i
            buf.append(lo)
            buf.append(hi)
            i += 2
        else:
            if start is not None and len(buf) // 2 >= minlen:
                try:
                    out.append((start, buf.decode("utf-16le", errors="replace")))
                except Exception:  # noqa: BLE001
                    pass
            start = None
            buf = bytearray()
            i += 1
    return out


KEYWORDS = [
    "OutputDebug", "DBWIN", "CreateFileMapping", "CreateEvent", "共享",
    "调试", "输出", "断点", "单步", "Trace", "Debug",
    ".fne", "GetNewInf", "NES_", "NAS_", "AddIn", "Add-In", "plugin", "扩展",
    "RegisterClass", "支持库", "lib\\", "\\lib", "cmd", "Cmd", "command",
    "SOFTWARE\\", "Software\\", "注册", "命令行", "/run", "-run",
    "e.exe", "E.EXE", "项目", "prj", ".e", ".ec", "Main",
]


def main():
    with open(PATH, "rb") as f:
        data = f.read()
    import pefile
    pe = pefile.PE(PATH, fast_load=True)

    gb = gbk_strings(data)
    u16 = utf16_strings(data)
    asc = ascii_strings(data)
    print("counts: gbk=%d utf16le=%d ascii=%d" % (len(gb), len(u16), len(asc)))

    print("\n=== KEYWORD HITS (offset / RVA / enc / text) ===")
    for kw in KEYWORDS:
        hits = []
        for off, s in gb:
            if kw in s:
                hits.append((off, "GBK", s))
        for off, s in u16:
            if kw in s:
                hits.append((off, "U16", s))
        # dedup by (off, text)
        seen = set()
        uniq = []
        for h in sorted(hits):
            k = (h[0], h[2])
            if k in seen:
                continue
            seen.add(k)
            uniq.append(h)
        if not uniq:
            continue
        print("\n---- keyword: %r  (%d hits) ----" % (kw, len(uniq)))
        for off, enc, s in uniq[:40]:
            rva = rva_of_offset(pe, off)
            rs = ("0x%08X" % rva) if rva is not None else "   -     "
            txt = s.replace("\r", "\\r").replace("\n", "\\n")
            if len(txt) > 160:
                txt = txt[:160] + "..."
            print("  off=0x%08X rva=%s %-4s %s" % (off, rs, enc, txt))

    pe.close()


if __name__ == "__main__":
    main()
