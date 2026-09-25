#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""reg_forensic.py —— 取证：找出 LowLevelHooksTimeout 被谁改的、还改了哪些「优化」项。

只读，不写任何注册表。
"""
import datetime
import sys
import winreg

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def filetime_to_dt(ft):
    if not ft:
        return None
    try:
        return datetime.datetime(1601, 1, 1) + datetime.timedelta(microseconds=ft // 10)
    except Exception:
        return None


def dump_key(root, path, label=None):
    print("=" * 78)
    print("KEY  %s\\%s" % (root, path))
    try:
        k = winreg.OpenKey(getattr(winreg, root), path, 0, winreg.KEY_READ)
    except FileNotFoundError:
        print("  (不存在)")
        return
    except PermissionError:
        print("  (无权限读取)")
        return
    with k:
        try:
            sk, sv, lw = winreg.QueryInfoKey(k)
            dt = filetime_to_dt(lw)
            print("  子键=%d  值=%d  该键最后一次写入=%s" % (
                sk, sv, dt.strftime("%Y-%m-%d %H:%M:%S") if dt else "?"))
        except Exception as e:
            print("  QueryInfoKey 失败:", e)
        for i in range(1024):
            try:
                name, val, typ = winreg.EnumValue(k, i)
            except OSError:
                break
            tname = {1: "REG_SZ", 2: "REG_EXPAND_SZ", 3: "REG_BINARY", 4: "REG_DWORD",
                     7: "REG_MULTI_SZ", 11: "REG_QWORD"}.get(typ, "type%d" % typ)
            if isinstance(val, bytes):
                val = "<%d bytes>" % len(val)
            print("    %-28s %-14s = %s" % (name, tname, val))


def hunt(root, path, needle="LowLevelHooksTimeout"):
    """在整个键树下搜索某值名。"""
    hits = []
    try:
        k = winreg.OpenKey(getattr(winreg, root), path, 0, winreg.KEY_READ)
    except Exception:
        return hits
    stack = [(path, k)]
    while stack:
        p, kk = stack.pop()
        try:
            n = winreg.QueryInfoKey(kk)[1]
            for i in range(n):
                try:
                    nm, val, typ = winreg.EnumValue(kk, i)
                except OSError:
                    break
                if needle.lower() in nm.lower():
                    hits.append((p, nm, val, typ))
            for i in range(winreg.QueryInfoKey(kk)[0]):
                try:
                    sub = winreg.EnumKey(kk, i)
                    stack.append((p + "\\" + sub, winreg.OpenKey(kk, sub, 0, winreg.KEY_READ)))
                except OSError:
                    pass
        except OSError:
            pass
        finally:
            try:
                kk.Close()
            except Exception:
                pass
    return hits


print("########## A. 直接相关：低级钩子超时 ##########")
dump_key("HKEY_CURRENT_USER", r"Control Panel\Desktop\LowLevelHooksTimeout".rsplit("\\", 1)[0])
for r, p in (("HKEY_CURRENT_USER", r"Control Panel\Desktop"),
             ("HKEY_LOCAL_MACHINE", r"SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System"),
             ("HKEY_LOCAL_MACHINE", r"SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer")):
    dump_key(r, p)

print()
print("########## B. 全盘搜索 LowLevelHooksTimeout（含 HKLM 各处） ##########")
for r, p in (("HKEY_CURRENT_USER", ""), ("HKEY_LOCAL_MACHINE", "")):
    try:
        hits = hunt(r, p)
    except Exception as e:
        print("  %s 搜索失败: %s" % (r, e))
        continue
    for hp, nm, val, typ in hits:
        print("  %s\\%s  ->  %s = %s (type=%d)" % (r, hp, nm, val, typ))
    if not hits:
        print("  %s: 未命中" % r)

print()
print("########## C. 其它经典「游戏优化」项（这些会直接损害输入/前台响应） ##########")
dump_key("HKEY_LOCAL_MACHINE",
         r"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile")
dump_key("HKEY_LOCAL_MACHINE",
         r"SYSTEM\CurrentControlSet\Control\PriorityControl")
dump_key("HKEY_LOCAL_MACHINE",
         r"SYSTEM\CurrentControlSet\Control\Session Manager\Memory Management")
dump_key("HKEY_LOCAL_MACHINE",
         r"SYSTEM\CurrentControlSet\Control\Session Manager\Executive")

print()
print("########## D. 谁在开机自启（Run 项里找优化/外设/远程控制软件） ##########")
for r, p in (("HKEY_CURRENT_USER", r"Software\Microsoft\Windows\CurrentVersion\Run"),
             ("HKEY_LOCAL_MACHINE", r"SOFTWARE\Microsoft\Windows\CurrentVersion\Run")):
    dump_key(r, p)
