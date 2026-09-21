#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""verify_libsource.py —— 受控实验：判定 t2e 读「支持库清单」的真实输入通道。

背景：两条互相矛盾的说法
  (甲) 文本里的 `.支持库 <key>` 是输入，`配置/支持库.config.json` 是 t2e 生成的产物
  (乙) 直接改 `配置/支持库.config.json` 就能让 t2e 把库写进 .e

对照四组（同一源文本，唯一变量是改了哪里）：
  A 基线      —— 什么都不改
  B 只改 json —— 配置/支持库.config.json 里加 elang_addin
  C 只改文本 —— 代码/程序集1.static.e.txt 的 `.版本 2` 后插一行顶格 `.支持库 elang_addin`
  D 都改

每组报：产物 .e 的大小 / md5 / 是否含 ASCII 'elang_addin'；
以及 t2e 结束后源目录里 config.json 与文本是否**仍保留**该项（→ 判断 输入 or 产物）。

只读 demos/ 与 D:\Tools\e2txt\，全部产物落在 re/libsource/。
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

E2TXT = os.environ.get("E2TXT") or r"D:\Tools\e2txt\e2txt.exe"
HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(HERE)
SRC = os.path.join(WS, "demos", "01-C盘结构输出", "项目")
WORK = os.path.join(HERE, "libsource")

LIBKEY = "elang_addin"
LIBGUID = "7a1e4f22c3b0499e8d6a0011223344fe"
LIBNAME = "AI调试宿主"

TEXT_REL = os.path.join("代码", "程序集1.static.e.txt")
CFG_REL = os.path.join("配置", "支持库.config.json")


def md5f(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def patch_config(d):
    """在 配置/支持库.config.json 里追加一项（保留 BOM，sort_keys 与原文件一致）"""
    p = os.path.join(d, CFG_REL)
    with open(p, "r", encoding="utf-8-sig") as f:
        data = json.load(f)
    if not any(it.get("Key") == LIBKEY for it in data):
        data.append({
            "CmdCount": 0,
            "Guid": LIBGUID,
            "Key": LIBKEY,
            "MaxRefConstPos": 0,
            "MaxRefObjectPos": 0,
            "Name": LIBNAME,
            "Version": {"Major": 1, "Minor": 0},
        })
    with open(p, "w", encoding="utf-8-sig", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=4, sort_keys=True)
        f.write("\n")
    return p


def patch_text(d):
    """在 代码/程序集1.static.e.txt 的 `.版本 2` 之后插一行顶格 `.支持库 elang_addin`"""
    p = os.path.join(d, TEXT_REL)
    raw = open(p, "rb").read()
    bom = b"\xef\xbb\xbf"
    body = raw[len(bom):] if raw.startswith(bom) else raw
    txt = body.decode("utf-8", errors="replace")
    lines = txt.split("\r\n") if "\r\n" in txt else txt.split("\n")
    if any(ln.strip() == ".支持库 " + LIBKEY for ln in lines):
        return p, "already"
    out = []
    done = False
    for ln in lines:
        out.append(ln)
        if not done and ln.strip() == ".版本 2":
            out.append(".支持库 " + LIBKEY)
            done = True
    if not done:
        return p, "NO_VER_LINE"
    with open(p, "wb") as f:
        f.write(bom)
        f.write("\r\n".join(out).encode("utf-8"))
    return p, "ok"


def run_t2e(src_dir, dst_e):
    p = subprocess.run([E2TXT, "-mode", "t2e", "-src", src_dir, "-dst", dst_e,
                        "-level", "2"], capture_output=True, timeout=600)
    so = p.stdout.decode("gbk", errors="replace")
    se = p.stderr.decode("gbk", errors="replace")
    return ("SUCC:" in so), so, se


def state_of(d):
    """t2e 之后，源目录里两项还在不在"""
    cfg_p = os.path.join(d, CFG_REL)
    txt_p = os.path.join(d, TEXT_REL)
    in_cfg = False
    if os.path.exists(cfg_p):
        try:
            with open(cfg_p, "r", encoding="utf-8-sig") as f:
                in_cfg = any(it.get("Key") == LIBKEY for it in json.load(f))
        except Exception as e:
            in_cfg = "PARSE_ERR:%s" % e
    in_txt = False
    if os.path.exists(txt_p):
        t = open(txt_p, "rb").read().decode("utf-8", errors="replace")
        in_txt = ("支持库 " + LIBKEY) in t
    return in_cfg, in_txt


CASES = [("A_base", False, False), ("B_jsononly", True, False),
         ("C_textonly", False, True), ("D_both", True, True)]


def main():
    print("e2txt  =", E2TXT)
    print("源文本 =", SRC)
    if not os.path.isdir(SRC):
        print("!! 源目录不存在"); return 1
    if os.path.exists(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)

    results = {}
    for name, do_cfg, do_txt in CASES:
        d = os.path.join(WORK, name)
        shutil.copytree(SRC, d)
        note = []
        if do_cfg:
            patch_config(d); note.append("改 config.json")
        if do_txt:
            _, st = patch_text(d); note.append("改文本(%s)" % st)
        out_e = os.path.join(WORK, name + ".e")
        ok, so, se = run_t2e(d, out_e)
        size = os.path.getsize(out_e) if os.path.exists(out_e) else -1
        m = md5f(out_e) if os.path.exists(out_e) else "(无产物)"
        has = False
        if os.path.exists(out_e):
            has = (LIBKEY.encode() in open(out_e, "rb").read())
        in_cfg, in_txt = state_of(d)
        errs = se.count("[错误]")
        results[name] = dict(size=size, md5=m, has=has, in_cfg=in_cfg, in_txt=in_txt)
        print("\n=== %s  [%s] ===" % (name, "、".join(note) or "不改"))
        print("   t2e SUCC=%s  stderr[错误]=%d" % (ok, errs))
        print("   .e 大小=%s  md5=%s  含'%s'=%s" % (size, m, LIBKEY, has))
        print("   t2e 后 源目录: config里有=%s  文本里有=%s" % (in_cfg, in_txt))

    print("\n================ 结论判据 ================")
    a = results["A_base"]
    for name in ("B_jsononly", "C_textonly", "D_both"):
        r = results[name]
        same_as_a = (r["md5"] == a["md5"])
        print("  %-11s 产物与基线%s | 含库key=%s | md5=%s"
              % (name, "相同" if same_as_a else "不同", r["has"], r["md5"][:12]))
        print("               → 该通道%s t2e 的输入"
              % ("**是**" if (not same_as_a and r["has"]) else "**不是**"))
    print("\n  基线自证：A 的 .e 是否等于 demos/01 原 .e？ %s"
          % ("是（说明 demos 原 .e 就是 t2e 产物）"
             if a["md5"] == md5f(os.path.join(SRC, "代码.e")) else "否"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
