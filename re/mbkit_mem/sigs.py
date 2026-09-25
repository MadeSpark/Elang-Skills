#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""统计 dll.e.txt 里指向 MbKit.dll 的 .DLL命令 的签名结构（参数类型 / 返回类型 / 传址 / @）。"""
import collections
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

PATH = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\MadeSpark\Desktop\测试\re\mb\kit\dll.e.txt"

lines = open(PATH, encoding="utf-8-sig").read().splitlines()


def split_slots(tail):
    """按半角逗号切分槽位，但 ', 参考 数组' / ', 传址' 这类是独立槽。"""
    return [s.strip() for s in tail.split(",")]


cmds = []
cur = None
for ln in lines:
    s = ln.strip()
    if s.startswith(".DLL命令"):
        body = s[len(".DLL命令"):].strip()
        parts = split_slots(body)
        name = parts[0]
        ret = parts[1] if len(parts) > 1 else ""
        dll = parts[2] if len(parts) > 2 else ""
        entry = parts[3] if len(parts) > 3 else ""
        cur = {"name": name, "ret": ret, "dll": dll, "entry": entry, "params": []}
        cmds.append(cur)
    elif s.startswith(".参数") and cur is not None:
        body = s[len(".参数"):].strip()
        parts = split_slots(body)
        pname = parts[0]
        ptype = parts[1] if len(parts) > 1 else ""
        flags = ",".join(parts[2:]).strip()
        cur["params"].append((pname, ptype, flags))

mbkit = [c for c in cmds if "MbKit" in c["dll"]]
other = [c for c in cmds if "MbKit" not in c["dll"]]
print("总 .DLL命令: %d ；指向 MbKit: %d ；其它: %d" % (len(cmds), len(mbkit), len(other)))
print()

# 返回类型统计
print("== 返回类型 ==")
for k, v in collections.Counter(c["ret"] or "(空)" for c in mbkit).most_common():
    print("  %-14s %d" % (k, v))
print()

# 参数类型统计
print("== 参数类型（出现次数） ==")
allp = [p for c in mbkit for p in c["params"]]
for k, v in collections.Counter(p[1] for p in allp).most_common():
    print("  %-16s %d" % (k or "(空)", v))
print()

print("== 参数修饰符 ==")
for k, v in collections.Counter(p[2] for p in allp).most_common():
    print("  %-16s %d" % (k or "(无)", v))
print()

# 签名族
def sig(c):
    return "(" + ",".join(p[1] + ("|" + p[2] if p[2] else "") for p in c["params"]) + ")->" + (c["ret"] or "void")

fam = collections.Counter(sig(c) for c in mbkit)
print("== 签名族（共 %d 种） ==" % len(fam))
for k, v in fam.most_common():
    print("  %-70s %d" % (k[:70], v))
print()

# @ 前缀
noc = [c for c in mbkit if not c["entry"].startswith("@")]
print("== entry 不以 @ 开头（非 cdecl）: %d ==" % len(noc))
for c in noc[:10]:
    print("   ", c["name"], c["entry"])
print()

# entry 名与 name 后缀不一致的
bad = [c for c in mbkit if c["entry"].lstrip("@").lower() not in c["name"].lower()]
print("== entry 名与命令名不匹配: %d ==" % len(bad))
for c in bad[:20]:
    print("   ", c["name"], "->", c["entry"])
print()

# 无参数命令
noargs = [c for c in mbkit if not c["params"]]
print("== 零参数命令: %d ==" % len(noargs))
for c in noargs:
    print("   ", c["name"], "->", c["entry"], "ret=", c["ret"] or "void")
