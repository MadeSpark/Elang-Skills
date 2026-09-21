# -*- coding: utf-8 -*-
"""在 HKCU\\Software\\FlySky\\E 全树的值名/字符串值/二进制值里搜支持库痕迹。"""
import winreg

ROOT = r"Software\FlySky\E"
NEEDLES = [b"krnln", b".fne", b"RegEx", b"AutoLinker", b"etools", b"iext"]
NEEDLES_T = [n.decode("gbk", "ignore") for n in NEEDLES] + [n.decode("utf-8", "ignore") for n in NEEDLES]


def hit(data: bytes) -> str:
    for n in NEEDLES:
        i = data.find(n)
        if i != -1:
            ctx = data[max(0, i - 24): i + 48]
            return "HEX " + ctx.hex() + " | ASCII " + "".join(chr(c) if 32 <= c < 127 else "." for c in ctx)
    return ""


def walk(key_path, depth=0):
    pad = "  " * depth
    try:
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, key_path)
    except OSError:
        return
    i = 0
    try:
        while True:
            name, val, typ = winreg.EnumValue(k, i)
            i += 1
            if isinstance(val, bytes):
                h = hit(val)
                if h:
                    print(pad + "[" + name + "] (" + str(typ) + ") " + h)
            elif isinstance(val, str):
                if any(t in val for t in NEEDLES_T):
                    print(pad + "[" + name + "] STR " + val[:160])
    except OSError:
        pass
    i = 0
    try:
        while True:
            sub = winreg.EnumKey(k, i)
            walk(key_path + "\\" + sub, depth + 1)
            i += 1
    except OSError:
        pass


walk(ROOT)
print("done")
