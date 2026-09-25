#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从探针工程里「按关键字裁剪」，用于二分定位让 e.exe 崩的写法。

用法: cut.py <源工程目录> <目标工程目录> <关键字1> [关键字2 ...]
规则：
  · 含任一关键字的「行」被删除（例如调用语句所在的行）；
  · 含任一关键字的「.子程序 块」整块删除（从 .子程序 行到下一个 .子程序 / 文件尾）；
  · 其余原样（保持 UTF-8+BOM 与 CRLF）。
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    keys = sys.argv[3:]
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    p = os.path.join(dst, "代码", "程序集1.static.e.txt")
    raw = open(p, "rb").read().decode("utf-8-sig")
    lines = raw.split("\r\n")

    out = []
    skip = False
    removed_sub, removed_line = [], 0
    for ln in lines:
        s = ln.strip()
        if s.startswith(".子程序"):
            skip = any(k in s for k in keys)
            if skip:
                removed_sub.append(s)
                continue
        elif s.startswith(".程序集"):
            skip = False
        if skip:
            continue
        if any(k in ln for k in keys):
            removed_line += 1
            continue
        out.append(ln)

    with open(p, "wb") as f:
        f.write(("\ufeff" + "\r\n".join(out)).encode("utf-8"))
    print("裁剪完成 -> %s" % dst)
    print("  删除子程序块 %d 个: %s" % (len(removed_sub), removed_sub))
    print("  删除散行 %d 行" % removed_line)


if __name__ == "__main__":
    raise SystemExit(main())
