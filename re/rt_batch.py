#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""rt_batch.py —— P0② 批量实测：多个真实工程 e2t→t2e 往返损失矩阵（纯离线）。

每个工程：e2t（-level 2）→ 文本里查 `未知名称` 占位 → t2e → 体积偏差。
只读原件（先复制副本），结果落 <work>/matrix.json。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

E2TXT = r"D:\Tools\e2txt\e2txt.exe"
WORK = r"C:\Users\MadeSpark\Desktop\测试\re\rt_batch"

SAMPLES = [
    r"C:\Users\MadeSpark\Desktop\测试\测试.e",
    r"D:\ides\e\samples\矢量图形\教学课件\爱莲说.e",
    r"D:\ides\e\samples\行业应用\贷款管理系统\贷款管理.e",
    r"D:\ides\e\samples\中小学教学课件\中学电路虚拟实验室\中学电路虚拟实验室.e",
    r"D:\ides\e\samples\办公软件\易之表增强版\易之表增强版3.3.e",
    r"D:\ides\e\samples\易向导\OPenGL向导\OPenGL向导.e",
    r"C:\Users\MadeSpark\Desktop\杂物\萌尘框架官方版\萌尘框架官方版_UI自适应版.e",
]

def md5f(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 16), b""):
            h.update(c)
    return h.hexdigest()

def run(mode, src, dst):
    cmd = [E2TXT, "-mode", mode, "-src", src, "-dst", dst, "-level", "2"]
    if mode == "e2t":
        cmd += ["-enc", "UTF-8"]
    p = subprocess.run(cmd, capture_output=True, timeout=1800)
    return (p.returncode,
            p.stdout.decode("gbk", errors="replace"),
            p.stderr.decode("gbk", errors="replace"))

def main():
    os.makedirs(WORK, exist_ok=True)
    rows = []
    for i, src in enumerate(SAMPLES):
        name = os.path.splitext(os.path.basename(src))[0]
        w = os.path.join(WORK, "p%d" % i)
        shutil.rmtree(w, ignore_errors=True)
        os.makedirs(os.path.join(w, "text"))      # -dst 父目录先建
        shutil.copy2(src, os.path.join(w, "src.e"))
        txt = os.path.join(w, "text")
        rt = os.path.join(w, "rt.e")
        row = {"name": name, "srcSize": os.path.getsize(src)}
        try:
            rc1, so1, se1 = run("e2t", os.path.join(w, "src.e"), txt)
            row["e2tRC"] = rc1
            row["e2tErr"] = (so1 + se1).count("[错误]")
            ph = 0
            for root, _d, fs in os.walk(txt):
                for fn in fs:
                    if fn.endswith(".txt"):
                        ph += open(os.path.join(root, fn), "rb").read().count("未知名称".encode("utf-8"))
            row["placeholders"] = ph
            if rc1 == 0:
                rc2, so2, se2 = run("t2e", txt, rt)
                row["t2eRC"] = rc2
                row["t2eErr"] = (so2 + se2).count("[错误]")
                row["succ"] = "SUCC:" in (so2 + se2)
                row["rtSize"] = os.path.getsize(rt) if os.path.isfile(rt) else None
                if row["rtSize"]:
                    row["delta"] = row["rtSize"] - row["srcSize"]
                    row["deltaPct"] = round(100.0 * row["delta"] / row["srcSize"], 2)
        except Exception as e:                                 # noqa: BLE001
            row["error"] = str(e)[:200]
        rows.append(row)
        print("%-24s %9d B  e2t=%s err=%s ph=%s  rt=%s d=%s" % (
            name[:24], row["srcSize"], row.get("e2tRC"), row.get("e2tErr"),
            row.get("placeholders"), row.get("rtSize"), row.get("delta")))
        shutil.rmtree(w, ignore_errors=True)
    with open(os.path.join(WORK, "matrix.json"), "w", encoding="utf-8") as f:
        json.dump(rows, f, ensure_ascii=False, indent=2)
    return 0

if __name__ == "__main__":
    sys.exit(main())
