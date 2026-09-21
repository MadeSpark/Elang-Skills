# -*- coding: utf-8 -*-
"""批量下载 AutoLinker 各版本 zip，解出 AutoLinker.fne，
并检测：① 是否含 --autolinker-headless-compile 功能串；② GetNewInf 的
RqSys/RqKrnl 版本门槛与 m_dwState（调 src/addin/dump_reqver.exe）。
"""
import io
import json
import os
import subprocess
import sys
import zipfile
import urllib.request

HERE = r"C:\Users\MadeSpark\Desktop\测试\re\_upstream_dl"
VD = os.path.join(HERE, "al_versions")
EXE = r"C:\Users\MadeSpark\Desktop\测试\src\addin\dump_reqver.exe"

data = json.load(io.open(os.path.join(HERE, "al_all.json"), encoding="utf-8"))
targets = []
for r in data:
    for a in r.get("assets", []):
        n = a["name"]
        if n.endswith(".zip") and "pdb" not in n.lower() and "Symbols" not in n:
            targets.append((r["tag_name"], a["browser_download_url"]))
            break

results = []
for tag, url in targets:
    zpath = os.path.join(VD, tag + ".zip")
    if not os.path.exists(zpath):
        try:
            urllib.request.urlretrieve(url, zpath)
        except Exception as e:
            results.append((tag, "DL_FAIL", str(e)[:60]))
            continue
    fne = None
    try:
        with zipfile.ZipFile(zpath) as z:
            for n in z.namelist():
                if n.lower().endswith(".fne"):
                    dest = os.path.join(VD, tag + ".fne")
                    with open(dest, "wb") as f:
                        f.write(z.read(n))
                    fne = dest
                    break
    except Exception as e:
        results.append((tag, "ZIP_FAIL", str(e)[:60]))
        continue
    if not fne:
        results.append((tag, "NO_FNE", ""))
        continue
    blob = open(fne, "rb").read()
    has_hc = b"--autolinker-headless-compile" in blob
    has_hc2 = b"autolinker-headless" in blob
    try:
        out = subprocess.run([EXE, fne], capture_output=True, timeout=60).stdout.decode("gbk", "replace")
        rq = [l for l in out.splitlines() if "RqSys" in l or "RqKrnl" in l or "m_dwState" in l or "BuildNumber" in l]
        rq_s = " | ".join(x.strip() for x in rq)
    except Exception as e:
        rq_s = "DUMP_FAIL " + str(e)[:40]
    results.append((tag, "headless=" + str(has_hc or has_hc2), rq_s))

for t in results:
    print(t[0], "|", t[1], "|", t[2])
