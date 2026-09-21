#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""rt_big_test.py —— P0②：真实规模 .e 的 e2t→t2e 往返损失量化（纯离线，不启动 e.exe）。

对一份真实工程 .e 做：
  ① e2t  → 文本目录（记录 stdout/stderr 里的 [错误]/[警告] 条数）
  ② t2e  → 还原 .e（同上）
  ③ 对比 原件 vs 还原件：大小 / md5 / 前缀-后缀公共长度
只读原件（复制一份做输入），全程不碰用户原文件。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

E2TXT = r"D:\Tools\e2txt\e2txt.exe"
SRC = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\MadeSpark\Desktop\测试\测试.e"
WORK = sys.argv[2] if len(sys.argv) > 2 else r"C:\Users\MadeSpark\Desktop\测试\re\rt_big"

def md5f(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 16), b""):
            h.update(c)
    return h.hexdigest()

def run_e2txt(mode, src, dst):
    cmd = [E2TXT, "-mode", mode, "-src", src, "-dst", dst, "-level", "2"]
    if mode == "e2t":
        cmd += ["-enc", "UTF-8"]
    p = subprocess.run(cmd, capture_output=True, timeout=1800)
    so = p.stdout.decode("gbk", errors="replace")
    se = p.stderr.decode("gbk", errors="replace")
    return p.returncode, so, se

def tally(text):
    return {"err": text.count("[错误]"), "warn": text.count("[警告]"),
            "succ": text.count("SUCC:"), "fail": text.count("FAIL:")}

def common_prefix(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[i] == b[i]:
        i += 1
    return i

def common_suffix(a, b):
    n = min(len(a), len(b))
    i = 0
    while i < n and a[len(a) - 1 - i] == b[len(b) - 1 - i]:
        i += 1
    return i

def main():
    os.makedirs(WORK, exist_ok=True)
    src_copy = os.path.join(WORK, "src.e")
    if not os.path.isfile(src_copy) or md5f(src_copy) != md5f(SRC):
        shutil.copy2(SRC, src_copy)
    txt_dir = os.path.join(WORK, "text_raw")
    rt_e = os.path.join(WORK, "roundtrip.e")
    shutil.rmtree(txt_dir, ignore_errors=True)
    os.makedirs(txt_dir)          # -dst 父目录必须先建
    if os.path.isfile(rt_e):
        os.remove(rt_e)

    print("src=%s (%d B) md5=%s" % (SRC, os.path.getsize(SRC), md5f(src_copy)))

    rc1, so1, se1 = run_e2txt("e2t", src_copy, txt_dir)
    t1 = tally(so1 + se1)
    print("e2t rc=%s tally=%s" % (rc1, t1))
    # 错误样例（前 5 条）
    errs = [ln.strip() for ln in (so1 + se1).splitlines() if "[错误]" in ln][:5]
    for e in errs:
        print("  e2t错误样例: %s" % e[:200])
    n_txt = sum(len(fs) for _, _, fs in os.walk(txt_dir))
    print("text files=%d" % n_txt)

    rc2, so2, se2 = run_e2txt("t2e", txt_dir, rt_e)
    t2 = tally(so2 + se2)
    print("t2e rc=%s tally=%s" % (rc2, t2))
    errs2 = [ln.strip() for ln in (so2 + se2).splitlines() if "[错误]" in ln][:8]
    for e in errs2:
        print("  t2e错误样例: %s" % e[:200])

    a = open(src_copy, "rb").read()
    b = open(rt_e, "rb").read() if os.path.isfile(rt_e) else b""
    res = {
        "srcSize": len(a), "rtSize": len(b),
        "srcMd5": md5f(src_copy), "rtMd5": md5f(rt_e) if b else None,
        "e2t": {"rc": rc1, **t1, "textFiles": n_txt, "errSamples": errs},
        "t2e": {"rc": rc2, **t2, "errSamples": errs2},
        "prefix": common_prefix(a, b), "suffix": common_suffix(a, b),
    }
    with open(os.path.join(WORK, "result.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=2)
    print(json.dumps({k: res[k] for k in ("srcSize", "rtSize", "prefix", "suffix")},
                     ensure_ascii=False))
    return 0

if __name__ == "__main__":
    sys.exit(main())
