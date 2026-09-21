#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys
import io
import re

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
data = open(r"D:\ides\e\e.exe", "rb").read()

# decode every string run in GBK and UTF-16LE, then filter to class-like tokens
def gbk_strings(data, minlen=3):
    out = []; i = 0; n = len(data); start = None; buf = bytearray()
    while i < n:
        b = data[i]
        if 0x20 <= b <= 0x7E or b in (9, 10, 13):
            if start is None: start = i
            buf.append(b); i += 1
        elif 0x81 <= b <= 0xFE and i + 1 < n and (0x40 <= data[i+1] <= 0xFE and data[i+1] != 0x7F):
            if start is None: start = i
            buf.append(b); buf.append(data[i+1]); i += 2
        else:
            if start is not None and len(buf) >= minlen:
                out.append((start, bytes(buf).decode("gbk", "replace")))
            start = None; buf = bytearray(); i += 1
    return out


def utf16_strings(data, minlen=3):
    out = []; i = 0; n = len(data); start = None; buf = bytearray()
    while i + 1 < n:
        lo, hi = data[i], data[i+1]
        cp = lo | (hi << 8)
        ok = (hi == 0 and 0x20 <= lo <= 0x7E) or (0x20 <= cp <= 0x7E) or (0x4E00 <= cp <= 0x9FFF)
        if ok:
            if start is None: start = i
            buf.append(lo); buf.append(hi); i += 2
        else:
            if start is not None and len(buf)//2 >= minlen:
                out.append((start, buf.decode("utf-16le", "replace")))
            start = None; buf = bytearray(); i += 1
    return out


allstr = gbk_strings(data) + utf16_strings(data)

# class-name candidates: whole-string tokens that look like window classes
pat = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_]*)$")
classes = {}
for off, s in allstr:
    if pat.match(s) and 3 <= len(s) <= 40:
        if s in ("ScintillaForEIDETools", "ENewFrame", "NewWindow", "FindReplaceEx",
                 "ColorMade", "Menulwy", "MenuWly", "TipLwy", "Setfd", "_EL_HideOwner",
                 "CodeView", "LayerWindow", "_EL_Label", "OutBar", "OutEdit", "OutWnd",
                 "OutputWnd", "EOutWnd", "EOutputBar", "EIDEOut"):
            classes.setdefault(s, []).append(off)

print("=== known/suspected class literals found ===")
for k in sorted(classes):
    print("  %-24s offsets=%s" % (k, [hex(x) for x in classes[k][:6]]))

print("\n=== tokens containing Out / Output / Debug / Trace ===")
seen = set()
for off, s in allstr:
    if any(x in s for x in ("Out", "Output", "Debug", "Trace")) and len(s) < 40 and s not in seen:
        seen.add(s)
        if pat.match(s):
            print("  0x%08X %s" % (off, s))

print("\n=== U16 strings containing 输出 (context) ===")
for off, s in utf16_strings(data):
    if "输出" in s and len(s) < 60:
        print("  0x%08X %s" % (off, s))
