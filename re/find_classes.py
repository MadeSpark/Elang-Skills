#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys
import io
import os
import glob

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pefile  # noqa: E402

NEEDLES = [b"ScintillaForEIDETools", b"CodeView", b"TipLwy", b"FindReplaceEx",
           b"LayerWindow", b"ColorMade", b"Menulwy", b"Setfd", b"ENewFrame",
           b"Scintilla"]

print("### recursive search under D:\\ides\\e (lib + plugins)")
root = r"D:\ides\e"
count = 0
for dirpath, dirnames, filenames in os.walk(root):
    # skip huge/irrelevant trees
    if any(x in dirpath.lower() for x in ("\\help", "\\samples", "\\static_lib",
                                          "\\vreport", "\\项目存档", "\\setup", "\\wizard")):
        continue
    for fn in filenames:
        if not fn.lower().endswith((".fne", ".fnr", ".fnl", ".run", ".dll", ".exe")):
            continue
        p = os.path.join(dirpath, fn)
        try:
            if os.path.getsize(p) > 40 * 1024 * 1024:
                continue
            data = open(p, "rb").read()
        except Exception:  # noqa: BLE001
            continue
        hits = [n.decode() for n in NEEDLES if n in data]
        if hits:
            print("  %-60s -> %s" % (p, hits))
            count += 1
print("files with hits:", count)

print("\n### krnln.fnr runtime check")
for p in glob.glob(r"D:\ides\e\lib\krnln.fnr") + glob.glob(r"D:\ides\e\lib\*.fnr"):
    data = open(p, "rb").read()
    print(" ", os.path.basename(p), "OutputDebugString:", data.count(b"OutputDebugString"))
