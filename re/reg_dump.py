# -*- coding: utf-8 -*-
"""递归转储 HKCU\\Software\\FlySky\\E，找「已登记支持库」清单的存储位置。"""
import winreg

ROOT = r"Software\FlySky\E"
NEEDLES = ("krnln", "RegEx", "etools", "fne", "lib")


def walk(key_path, depth=0, max_depth=4):
    if depth > max_depth:
        return
    pad = "  " * depth
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path)
    except OSError as e:
        print(pad + "[open fail] " + key_path + " " + str(e))
        return
    i = 0
    try:
        while True:
            name, val, _t = winreg.EnumValue(k, i)
            s = str(val)
            mark = " <<<" if any(n in (name + s).lower() for n in NEEDLES) else ""
            print(pad + "  [" + name + "] = " + s[:160] + mark)
            i += 1
    except OSError:
        pass
    i = 0
    try:
        while True:
            sub = winreg.EnumKey(k, i)
            print(pad + "  <sub> " + sub)
            walk(key_path + "\\" + sub, depth + 1, max_depth)
            i += 1
    except OSError:
        pass


walk(ROOT)
