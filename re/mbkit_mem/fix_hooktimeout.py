#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fix_hooktimeout.py —— 备份并删除 HKCU\\Control Panel\\Desktop\\LowLevelHooksTimeout

用户选择：删除该值（让 Windows 用它内置的默认 300ms）。
本脚本：
  1) 把 `HKCU\\Control Panel\\Desktop` 全部值 + 该键最后写入时间 备份到 idiag/reg_backup_desktop.json / .txt
  2) 生成一键还原文件 idiag/还原-LowLevelHooksTimeout.reg
  3) 删除 LowLevelHooksTimeout
  4) 回读自证（必须读不到才算成功）
⚠️ 该值在**会话初始化时**被读取 ⇒ 删除后需 **注销或重启** 才生效。
"""
import datetime
import json
import os
import sys
import winreg

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

IV = "Control Panel\\Desktop"
VALUE = "LowLevelHooksTimeout"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "idiag")
os.makedirs(OUT, exist_ok=True)


def dump_all(key):
    out = {}
    i = 0
    while True:
        try:
            name, val, typ = winreg.EnumValue(key, i)
        except OSError:
            break
        i += 1
        if isinstance(val, bytes):
            val = val.hex()
        out[name] = {"type": typ, "value": val}
    return out


# ---------- 1) 备份 ----------
with winreg.OpenKey(winreg.HKEY_CURRENT_USER, IV, 0,
                    winreg.KEY_READ | winreg.KEY_WRITE) as k:
    nsub, nval, lw = winreg.QueryInfoKey(k)
    lastwrite = (datetime.datetime(1601, 1, 1) +
                 datetime.timedelta(microseconds=lw // 10)).strftime("%Y-%m-%d %H:%M:%S")
    original = dump_all(k)

    print("备份：HKCU\\%s   子键=%d 值=%d  该键最后写入=%s" % (IV, nsub, nval, lastwrite))
    old = original.get(VALUE)
    if old is None:
        print("  ⚠️ 该值当前不存在（可能已被删除）⇒ 无需改动")
    else:
        print("  原值 %s = %s (type=%d)" % (VALUE, old["value"], old["type"]))

    with open(os.path.join(OUT, "reg_backup_desktop.json"), "w", encoding="utf-8") as f:
        json.dump({"key": "HKCU\\" + IV, "last_write": lastwrite, "values": original},
                  f, ensure_ascii=False, indent=2)

    # 一键还原 .reg（只还原这一个值）
    if old is not None:
        v = int(old["value"])
        regpath = os.path.join(OUT, "还原-LowLevelHooksTimeout.reg")
        with open(regpath, "w", encoding="utf-16") as f:
            f.write("Windows Registry Editor Version 5.00\r\n\r\n")
            f.write("[HKEY_CURRENT_USER\\Control Panel\\Desktop]\r\n")
            f.write("\"%s\"=dword:%08x\r\n" % (VALUE, v))
        print("  已写还原文件：%s   （双击即可把 %s 改回 %d）" % (regpath, VALUE, v))

    # ---------- 2) 删除 ----------
    if old is not None:
        winreg.DeleteValue(k, VALUE)
        print("  已删除 %s" % VALUE)

# ---------- 3) 回读自证 ----------
with winreg.OpenKey(winreg.HKEY_CURRENT_USER, IV, 0, winreg.KEY_READ) as k:
    left = dump_all(k)
now = left.get(VALUE)
if now is None:
    print("\n✅ 回读自证：%s 已不存在 ⇒ 系统将使用内置默认（300 ms）" % VALUE)
else:
    print("\n❌ 回读自证失败：%s 仍为 %s" % (VALUE, now))
    sys.exit(1)

print("⚠️ 需 **注销或重启** 才被 win32k 读取生效（已实测：该值在会话初始化时读取）。")
