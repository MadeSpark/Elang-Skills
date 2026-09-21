#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""verify_libsource2.py —— 判别「文本 `.支持库` 行无效」是否只是因为该库没装在 lib\。

first 实验（verify_libsource.py）结果：
  B 只改 配置/支持库.config.json  → .e 变化、含 key   ✅ 有效
  C 只在 class/*.e.txt 加 .支持库 → .e 与基线完全相同 ❌ 无效

但 C 用的 key 是 elang_addin（**未安装**在 lib\），而 B 的 json 自带 Name/Guid 无需解析库。
所以存在替代解释：**文本通道需要能解析到该库**，解析不到就静默丢弃。

判别法：换一个**已安装**的库（cncnv，lib\ 里实际存在）再走一次文本通道。
  E: 只改文本加 `.支持库 cncnv`   → .e 变了吗？含 'cncnv' 吗？
若 E 也无效 ⇒ 文本通道彻底不通，规范里那条写法应删除。
若 E 有效   ⇒ 文本通道可用但要求库已安装（脚本就要按此写）。

只读 demos/ 与 D:\Tools\e2txt\，产物落 re/libsource2/。
"""
import hashlib
import os
import shutil
import subprocess
import sys

E2TXT = os.environ.get("E2TXT") or r"D:\Tools\e2txt\e2txt.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(HERE)
SRC = os.path.join(WS, "demos", "01-C盘结构输出", "项目")
WORK = os.path.join(HERE, "libsource2")
TEXT_REL = os.path.join("代码", "程序集1.static.e.txt")
LIB_DIR = r"D:\ides\e\lib"


def md5f(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(65536), b""):
            h.update(c)
    return h.hexdigest()


def add_text_line(d, key):
    p = os.path.join(d, TEXT_REL)
    with open(p, "rb") as f:
        raw = f.read()
    bom = b"\xef\xbb\xbf"
    body = raw[len(bom):] if raw.startswith(bom) else raw
    txt = body.decode("utf-8", errors="replace")
    lines = txt.split("\r\n") if "\r\n" in txt else txt.split("\n")
    out, done = [], False
    for ln in lines:
        out.append(ln)
        if not done and ln.strip() == ".版本 2":
            out.append(".支持库 " + key)
            done = True
    with open(p, "wb") as f:
        f.write(bom)
        f.write("\r\n".join(out).encode("utf-8"))
    return done


def t2e(d, out_e):
    p = subprocess.run([E2TXT, "-mode", "t2e", "-src", d, "-dst", out_e, "-level", "2"],
                       capture_output=True, timeout=600)
    return ("SUCC:" in p.stdout.decode("gbk", "replace"),
            p.stderr.decode("gbk", "replace"))


def main():
    print("lib\\ 里已安装的相关库：")
    for k in ("cncnv", "dp1", "console", "iext", "elang_addin"):
        print("   %-14s %s" % (k, "存在" if os.path.exists(
            os.path.join(LIB_DIR, k + ".fne")) else "**不存在**"))

    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)

    base_e = os.path.join(WORK, "base.e")
    d0 = os.path.join(WORK, "base")
    shutil.copytree(SRC, d0)
    t2e(d0, base_e)
    b_md5, b_size = md5f(base_e), os.path.getsize(base_e)
    print("\n基线: %d B md5=%s" % (b_size, b_md5[:12]))

    for tag, key in (("E_cncnv_textonly", "cncnv"),
                     ("F_elangaddin_textonly", "elang_addin")):
        d = os.path.join(WORK, tag)
        shutil.copytree(SRC, d)
        ok_ins = add_text_line(d, key)
        out_e = os.path.join(WORK, tag + ".e")
        ok, se = t2e(d, out_e)
        size = os.path.getsize(out_e) if os.path.exists(out_e) else -1
        m = md5f(out_e) if os.path.exists(out_e) else "(无)"
        blob = open(out_e, "rb").read() if os.path.exists(out_e) else b""
        has = key.encode() in blob
        print("\n=== %s (key=%s, 插入成功=%s) ===" % (tag, key, ok_ins))
        print("   t2e SUCC=%s  stderr[错误]=%d" % (ok, se.count("[错误]")))
        print("   .e %d B md5=%s" % (size, m[:12]))
        print("   与基线: %s   含 '%s'=%s"
              % ("相同 ❌" if m == b_md5 else "不同", key, has))
        if "加载支持库失败" in se:
            print("   ⚠ stderr 出现『加载支持库失败』")
    return 0


if __name__ == "__main__":
    sys.exit(main())
