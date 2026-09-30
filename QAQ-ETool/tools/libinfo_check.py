#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""校验 qaq-libinfo.exe 与 ELibInfo 官方导出器（缓存结果）逐项一致。"""
import glob
import json
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "src/QAQ.ETool.Cli/bin/Release/net8.0/libcache"
TOOL = ROOT / "src/libinfo/qaq-libinfo.exe"
LIBDIR = Path(os.environ.get("E_LIBDIR", r"D:\ides\e\lib"))

bad = 0
for d in sorted(CACHE.iterdir()):
    key = d.name.split("_", 1)[1]
    ref = json.loads(next(d.glob("*.json")).read_text(encoding="utf-8"))
    out = Path(tempfile.gettempdir()) / f"qaq_cmp_{key}.json"
    subprocess.run([str(TOOL), str(LIBDIR / f"{key}.fne"), str(out)], check=True)
    me = json.loads(out.read_text(encoding="utf-8"))
    diffs = []

    def cmp(label, a, b):
        if a != b:
            diffs.append(f"{label}: 参考={str(a)[:90]} 我={str(b)[:90]}")

    cmp("Guid", ref["Guid"], me["Guid"])
    cmp("Name", ref["Name"], me["Name"])
    cmp("Version", ref["Version"], me["Version"])
    cmp("命令数", len(ref["Cmds"]), len(me["Cmds"]))
    for i, (a, b) in enumerate(zip(ref["Cmds"], me["Cmds"])):
        cmp(f"命令{i}", a["Name"], b["Name"])
        cmp(f"命令{i}参数数", len(a["Parameters"]), len(b["Parameters"]))
    cmp("类型数", len(ref["DataTypes"]), len(me["DataTypes"]))
    for i, (a, b) in enumerate(zip(ref["DataTypes"], me["DataTypes"])):
        cmp(f"类型{i}名", a["Name"], b["Name"])
        cmp(f"类型{i}成员名", [m["Name"] for m in a["Members"]], [m["Name"] for m in b["Members"]])
        cmp(f"类型{i}事件名", [m["Name"] for m in a["Events"]], [m["Name"] for m in b["Events"]])
        cmp(f"类型{i}方法", a["Methods"], b["Methods"])
        cmp(f"类型{i}Kind", a.get("Kind"), b.get("Kind"))
    cmp("常量数", len(ref["Constants"]), len(me["Constants"]))
    for i, (a, b) in enumerate(zip(ref["Constants"], me["Constants"])):
        cmp(f"常量{i}", (a["Name"], a.get("Value")), (b["Name"], b.get("Value")))
    print(f"{key:<16} 命令{len(me['Cmds']):>5} 类型{len(me['DataTypes']):>4} 常量{len(me['Constants']):>5}  差异 {len(diffs)}")
    for x in diffs[:6]:
        print("       ", x)
    bad += len(diffs)
print("总差异", bad)
