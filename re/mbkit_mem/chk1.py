#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""chk1.py —— 无头语法检查一个工程，并把**全部原始输出**落盘供人读。

为什么不复用 ladder 的判定：`ok = "语法检查通过" in out` 依赖 stdout 的中文
能否被正确解码；一旦 e2t/ecompile 的输出混了编码，就会把「通过」误判成「崩溃」。
本脚本一律落盘原始字节 + 两种编码的解读，并额外提取 ecompile 自己的结论行与
harness 里的 e_self_exit，三者互相印证。

用法：python chk1.py <工程目录> [标签]
"""
import io
import os
import re
import subprocess
import sys
import time

PY = sys.executable
ECOMPILE = r"C:\Users\MadeSpark\.workbuddy\skills\elang-build\scripts\ecompile.py"
HERE = os.path.dirname(os.path.abspath(__file__))
LOGDIR = os.path.join(HERE, "chk_logs")
os.makedirs(LOGDIR, exist_ok=True)


def main():
    if len(sys.argv) < 2:
        print("用法：python chk1.py <工程目录> [标签]")
        return 2
    proj = os.path.abspath(sys.argv[1])
    tag = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(proj)
    stamp = time.strftime("%H%M%S")
    wd = os.path.join(LOGDIR, "wk_%s_%s" % (tag, stamp))
    os.makedirs(wd, exist_ok=True)

    print("工程   = %s" % proj)
    print("工作区 = %s" % wd)
    p = subprocess.run([PY, ECOMPILE, proj, "--check", "--workdir", wd],
                       capture_output=True)
    raw = (p.stdout or b"") + (b"\n---STDERR---\n" + p.stderr if p.stderr else b"")
    io.open(os.path.join(wd, "raw.bin"), "wb").write(raw)
    for enc in ("utf-8", "gbk"):
        try:
            t = raw.decode(enc)
        except Exception:
            t = raw.decode(enc, errors="replace")
        io.open(os.path.join(wd, "out.%s.txt" % enc), "w", encoding="utf-8",
                newline="").write(t)

    txt = raw.decode("utf-8", errors="replace")
    if "\ufffd" in txt:
        txt = raw.decode("gbk", errors="replace")

    print("\n---- ecompile 进程返回码 = %r ----" % p.returncode)
    verdicts = [l.strip() for l in txt.split("\n")
                if re.search(r"语法检查通过|编译成功|编译完成|编译失败|失败|\bOK\b|SUCC|ERROR|\[错误\]", l)]
    print("---- 结论/错误行（%d 条）----" % len(verdicts))
    for l in verdicts[:40]:
        print("   " + l[:170])

    hs = [f for f in os.listdir(wd) if f.endswith("_harness.txt")]
    for f in hs:
        t = io.open(os.path.join(wd, f), encoding="utf-8", errors="replace").read()
        m = re.search(r"观测:\s*(.+)", t)
        print("\n---- harness %s ----" % f)
        print("   " + (m.group(1).strip() if m else "(无观测行)"))
        m2 = re.search(r"e_self_exit=(\S+)", t)
        print("   e_self_exit = %s" % (m2.group(1) if m2 else "-"))

    print("\n原始输出已落盘：%s" % wd)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
