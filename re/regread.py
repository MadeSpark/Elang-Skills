#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Read EasyLanguage registry keys (read-only) to locate the enabled-library list."""
import sys
import io
import winreg

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")


def dump(root, path, depth=0, maxdepth=3):
    try:
        key = winreg.OpenKey(root, path, 0, winreg.KEY_READ)
    except OSError as e:
        print("  " * depth + "[%s] (cannot open: %s)" % (path, e))
        return
    with key:
        # values
        try:
            i = 0
            while True:
                try:
                    nm, val, typ = winreg.EnumValue(key, i)
                    sval = val if not isinstance(val, bytes) else val.hex()[:80]
                    print("  " * depth + "  VAL %s = %r (type=%d)" % (nm, sval, typ))
                    i += 1
                except OSError:
                    break
        except OSError:
            pass
        if depth >= maxdepth:
            return
        try:
            j = 0
            while True:
                try:
                    sub = winreg.EnumKey(key, j)
                    print("  " * depth + "SUB [%s]" % sub)
                    dump(root, path + "\\" + sub, depth + 1, maxdepth)
                    j += 1
                except OSError:
                    break
        except OSError:
            pass


HK = {"HKCU": winreg.HKEY_CURRENT_USER, "HKLM": winreg.HKEY_LOCAL_MACHINE}

ROOTS = [
    ("HKCU", r"Software\FlySky"),
    ("HKLM", r"SOFTWARE\Wow6432Node\FlySky"),
    ("HKLM", r"SOFTWARE\FlySky"),
    ("HKCU", r"Software\FlySky\E"),
    ("HKCU", r"Software\FlySky\E\Install"),
    ("HKLM", r"SOFTWARE\Wow6432Node\FlySky\E"),
]

for hive, path in ROOTS:
    print("\n==== %s\\%s ====" % (hive, path))
    dump(HK[hive], path, 0, 3)
