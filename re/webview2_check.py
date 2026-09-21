# -*- coding: utf-8 -*-
"""检查 WebView2 运行时是否安装。"""
import winreg

PATHS = [
    (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients"),
    (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\EdgeUpdate\Clients"),
    (winreg.HKEY_CURRENT_USER, r"Software\Microsoft\EdgeUpdate\Clients"),
]
found = False
for root, path in PATHS:
    try:
        k = winreg.OpenKey(root, path)
    except OSError:
        continue
    i = 0
    try:
        while True:
            sub = winreg.EnumKey(k, i)
            i += 1
            if "F3017226" in sub:
                sk = winreg.OpenKey(root, path + "\\" + sub)
                try:
                    pv = winreg.QueryValueEx(sk, "pv")[0]
                except OSError:
                    pv = "?"
                print("WebView2 Runtime pv =", pv, "at", path)
                found = True
    except OSError:
        pass
if not found:
    print("WebView2 Runtime NOT FOUND")
