#!/usr/bin/env python
# -*- coding: utf-8 -*-
import sys
import io
import re

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
PATH = r"D:\ides\e\e.exe"


def gbk_strings(data, minlen=4):
    out = []
    i, n = 0, len(data)
    start = None
    buf = bytearray()
    while i < n:
        b = data[i]
        if 0x20 <= b <= 0x7E or b in (0x09, 0x0A, 0x0D):
            if start is None:
                start = i
            buf.append(b)
            i += 1
        elif 0x81 <= b <= 0xFE and i + 1 < n and (0x40 <= data[i + 1] <= 0xFE and data[i + 1] != 0x7F):
            if start is None:
                start = i
            buf.append(b); buf.append(data[i + 1]); i += 2
        else:
            if start is not None and len(buf) >= minlen:
                out.append((start, bytes(buf).decode("gbk", errors="replace")))
            start = None; buf = bytearray(); i += 1
    if start is not None and len(buf) >= minlen:
        out.append((start, bytes(buf).decode("gbk", errors="replace")))
    return out


def utf16_strings(data, minlen=4):
    out = []
    i, n = 0, len(data)
    start = None
    buf = bytearray()
    while i + 1 < n:
        lo, hi = data[i], data[i + 1]
        cp = lo | (hi << 8)
        ok = (hi == 0 and 0x20 <= lo <= 0x7E) or (0x20 <= cp <= 0x7E) or (0x4E00 <= cp <= 0x9FFF)
        if ok:
            if start is None:
                start = i
            buf.append(lo); buf.append(hi); i += 2
        else:
            if start is not None and len(buf) // 2 >= minlen:
                out.append((start, buf.decode("utf-16le", errors="replace")))
            start = None; buf = bytearray(); i += 1
    return out


KW = ["OutputDebug", "CreateFileMapping", "DBWIN", "MapViewOfFile", "MEMORY",
      "输出工具条", "输出窗口", "输出面板", "系统输出", "工作夹", "output", "Output",
      "Afx", "ELang", "Wizard", "MainFrame", "MainWnd", "主窗口", "Class", "类",
      "FindWindow", "SendMessage", "PostMessage", "RegisterWindowMessage", "WM_",
      "GetCommandLine", "CommandLine", "\\cmd", "-", "/", "cmdline", "arg",
      "SetWindowsHookEx", "GetProcAddress", "LoadLibrary", "CreateRemoteThread",
      "WinExec", "ShellExecute", "DebugActiveProcess"]


def main():
    data = open(PATH, "rb").read()
    gb = gbk_strings(data)
    u16 = utf16_strings(data)
    for kw in KW:
        hits = []
        for off, s in gb:
            if kw in s:
                hits.append((off, "GBK", s))
        for off, s in u16:
            if kw in s:
                hits.append((off, "U16", s))
        seen = set()
        uniq = []
        for h in sorted(hits, key=lambda x: (x[2], x[0])):
            k = (h[0], h[2])
            if k in seen:
                continue
            seen.add(k)
            uniq.append(h)
        if not uniq:
            continue
        print("\n==== %r (%d) ====" % (kw, len(uniq)))
        for off, enc, s in uniq[:30]:
            print("  0x%08X %-4s %s" % (off, enc, s[:130].replace("\r", "\\r").replace("\n", "\\n")))


if __name__ == "__main__":
    main()
