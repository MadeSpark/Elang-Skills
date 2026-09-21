#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Check which module registers IDE window classes, and inspect krnln.fne / spec.fne
   for OutputDebugString / DBWIN usage (directly relevant to the P0 hypothesis)."""
import sys
import io
import os
import glob

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import pefile  # noqa: E402

TARGETS_STR = [b"ScintillaForEIDETools", b"CodeView", b"TipLwy", b"FindReplaceEx",
               b"LayerWindow", b"ColorMade", b"Menulwy", b"Setfd", b"ENewFrame"]
TARGETS_DEBUG = [b"OutputDebugString", b"DBWIN_BUFFER", b"DBWIN_DATA_READY",
                 b"DBWIN_MUTEX", b"DBWIN", b"WaitForDebugEvent", b"DebugActiveProcess"]

print("### Part 1: which file contains the IDE window-class names?")
for path in glob.glob(r"D:\ides\e\lib\*.fne") + [r"D:\ides\e\e.exe"]:
    try:
        data = open(path, "rb").read()
    except Exception:  # noqa: BLE001
        continue
    hits = [t.decode() for t in TARGETS_STR if t in data]
    if hits:
        print("  %-40s -> %s" % (os.path.basename(path), hits))

print("\n### Part 2: OutputDebugString / DBWIN usage in key libs")
for path in [r"D:\ides\e\lib\krnln.fne", r"D:\ides\e\lib\spec.fne",
             r"D:\ides\e\e.exe", r"D:\ides\e\lib\eAPI.fne", r"D:\ides\e\lib\console.fne"]:
    if not os.path.exists(path):
        print("  (missing)", path)
        continue
    print("\n-- %s --" % path)
    data = open(path, "rb").read()
    for t in TARGETS_DEBUG:
        cnt = data.count(t)
        if cnt:
            print("   str %-22s x%d" % (t.decode(), cnt))
    try:
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"]])
        found = []
        for e in getattr(pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in e.imports:
                if imp.name and any(x in imp.name for x in [b"OutputDebug", b"DebugActive", b"WaitForDebugEvent", b"ContinueDebugEvent", b"CreateFileMapping", b"MapViewOfFile", b"OpenFileMapping"]):
                    found.append("%s!%s" % (e.dll.decode(), imp.name.decode()))
        print("   imports:", found if found else "(none of interest)")
        pe.close()
    except Exception as ex:  # noqa: BLE001
        print("   pe error", ex)
