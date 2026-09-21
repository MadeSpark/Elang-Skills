#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""mkcage.py —— 给「用户的 .e」加壳：让它在被易语言 IDE 打开时自动加载我们的支持库。

原理
====
`e2txt` 的 `t2e` 读「支持库清单」的**唯一有效通道**是工程文本里的 lib 配置文件
（英文布局 `config/lib.config.json`；中文布局 `配置/支持库.config.json`）。
在文本 `类/*.e.txt` 里写 `.支持库 <key>` **不生效**。

所以加壳 = 把用户的 `.e` 转成文本 → 在 lib 配置里追加一条本库记录 → 转回 `.e`。
**全程离线**，不启动易语言 IDE、不碰任何 `*.e.txt`。

用法
====
    python mkcage.py <输入.e> [<输出_ai.e>]
      --fne <path>     本库 .fne 路径（默认：脚本同目录的 ../assets/elang_addin.fne）
      --json           结果以 JSON 打到 stdout
      -q, --quiet      不打印过程
    退出码：0 = 成功；非 0 = 失败。

成功判据（两道都要过）
======================
    ① 产物 `.e` 的 md5 ≠ 输入 `.e`
    ② 产物二进制里能找到本库 Key 的 ASCII 字节

库信息（Key/Guid/库名/版本）来源顺序
====================================
    ① `<fne 同名>.libinfo.txt`（UTF-8；由 `src/addin/build.sh` 生成并随技能发布）
    ② 同目录的 `offset_probe.exe --dump-libinfo <fne>`（开发环境）
    ③ 内置回退常量
这样 `.fne` 重建后 Guid 漂移会被自动带上，不会出现“加壳产物声明旧 Guid → 加载失败”。

依赖
====
    仅 Python 3.9+ 标准库；`e2txt` 需要可用（见下）。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))

# --- 内置回退常量（只在拿不到 .fne 库信息时使用）---------------------------- #
FALLBACK = {"key": "elang_addin", "guid": "7a1e4f22c3b0499e8d6a0011223344fe",
            "name": "AI调试宿主", "major": 1, "minor": 0}

# 默认 .fne：脚本同目录的 ../assets/elang_addin.fne（技能包里即 assets/）。
DEFAULT_FNE = os.path.normpath(os.path.join(HERE, os.pardir, "assets", "elang_addin.fne"))

# lib 配置文件的候选相对路径（英文布局优先，兼容中文布局）
CFG_CANDIDATES = [
    os.path.join("config", "lib.config.json"),
    os.path.join("配置", "支持库.config.json"),
]


def find_e2txt() -> str:
    """按 `E2TXT` 环境变量 → `PATH` 的顺序找 e2txt；都没有则报清晰错误。"""
    env = os.environ.get("E2TXT")
    if env:
        if os.path.isfile(env):
            return env
        which = shutil.which(env)
        if which:
            return which
        raise SystemExit("E2TXT 指向的文件不存在：%s" % env)
    which = shutil.which("e2txt")
    if which:
        return which
    raise SystemExit(
        "找不到 e2txt。请把 e2txt 加进 PATH，或设环境变量 E2TXT=<e2txt 可执行文件路径>。")


def md5f(p: str) -> str:
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(65536), b""):
            h.update(c)
    return h.hexdigest()


def _parse_kv(text: str) -> dict:
    kv = {}
    for ln in text.splitlines():
        if "=" in ln:
            k, _, v = ln.partition("=")
            kv[k.strip()] = v.strip()
    return kv


def dump_libinfo(fne: str) -> dict:
    """从 .fne 现取 (key, guid, name, major, minor)；见模块 docstring 的来源顺序。"""
    info = dict(FALLBACK, src="fallback")
    if not os.path.isfile(fne):
        info["src"] = "fallback (无 .fne: %s)" % fne
        return info

    # ① sidecar .libinfo.txt（UTF-8）
    sidecar = os.path.splitext(fne)[0] + ".libinfo.txt"
    if os.path.isfile(sidecar):
        kv = _parse_kv(open(sidecar, encoding="utf-8", errors="replace").read())
        if kv.get("GUID"):
            info.update({
                "key": kv.get("KEY") or FALLBACK["key"],
                "guid": kv["GUID"].lower(),
                "name": kv.get("NAME") or FALLBACK["name"],
                "major": int(kv.get("MAJOR") or FALLBACK["major"]),
                "minor": int(kv.get("MINOR") or FALLBACK["minor"]),
                "src": os.path.basename(sidecar),
            })
            return info

    # ② 同目录 offset_probe.exe（开发环境）
    probe = os.path.join(os.path.dirname(fne), "offset_probe.exe")
    if os.path.isfile(probe):
        try:
            p = subprocess.run([probe, "--dump-libinfo", fne],
                               capture_output=True, timeout=60)
            kv = _parse_kv(p.stdout.decode("gbk", errors="replace"))
            if kv.get("GUID"):
                info.update({
                    "key": kv.get("KEY") or FALLBACK["key"],
                    "guid": kv["GUID"].lower(),
                    "name": kv.get("NAME") or FALLBACK["name"],
                    "major": int(kv.get("MAJOR") or FALLBACK["major"]),
                    "minor": int(kv.get("MINOR") or FALLBACK["minor"]),
                    "src": "offset_probe.exe",
                })
                return info
        except Exception:                                          # noqa: BLE001
            pass
    return info


def run_e2txt(e2txt: str, mode: str, src: str, dst: str, extra=None):
    cmd = [e2txt, "-mode", mode, "-src", src, "-dst", dst, "-level", "2"]
    if extra:
        cmd += extra
    p = subprocess.run(cmd, capture_output=True, timeout=900)
    return (p.returncode,
            p.stdout.decode("gbk", errors="replace"),
            p.stderr.decode("gbk", errors="replace"))


def find_cfg(txt_dir: str):
    for rel in CFG_CANDIDATES:
        p = os.path.join(txt_dir, rel)
        if os.path.exists(p):
            return p
    for root, _dirs, files in os.walk(txt_dir):
        for fn in files:
            low = fn.lower()
            if low.endswith(".json") and ("lib" in low or "支持库" in fn):
                return os.path.join(root, fn)
    return None


def patch_cfg(cfg_path: str, info: dict):
    """追加本库记录；返回 (是否新增, 现有记录数, 是否已存在)。保留 BOM 与缩进风格。"""
    raw = open(cfg_path, "rb").read()
    had_bom = raw.startswith(b"\xef\xbb\xbf")
    with open(cfg_path, "r", encoding="utf-8-sig") as f:
        data = json.load(f)
    if not isinstance(data, list):
        raise SystemExit("lib 配置不是数组: %r" % type(data))
    existed = any(isinstance(it, dict) and it.get("Key") == info["key"] for it in data)
    if not existed:
        data.append({
            "CmdCount": 0,
            "Guid": info["guid"],
            "Key": info["key"],
            "MaxRefConstPos": 0,
            "MaxRefObjectPos": 0,
            "Name": info["name"],
            "Version": {"Major": info["major"], "Minor": info["minor"]},
        })
    with open(cfg_path, "w", encoding="utf-8-sig" if had_bom else "utf-8", newline="\n") as f:
        json.dump(data, f, ensure_ascii=False, indent=4, sort_keys=True)
        f.write("\n")
    return (not existed), len(data), existed


def mkcage(user_e: str, out_e: str = None, fne: str = None, quiet: bool = False) -> dict:
    """加壳主流程；成功返回结果 dict，失败抛 SystemExit。"""

    def say(msg=""):
        if not quiet:
            print(msg)

    user_e = os.path.abspath(user_e)
    if not os.path.isfile(user_e):
        raise SystemExit("找不到输入: %s" % user_e)
    if out_e is None:
        stem, ext = os.path.splitext(user_e)
        out_e = stem + "_ai" + (ext or ".e")
    out_e = os.path.abspath(out_e)
    if fne is None:
        fne = DEFAULT_FNE
    fne = os.path.abspath(fne)

    e2txt = find_e2txt()
    info = dump_libinfo(fne)
    say("[0/4] e2txt = %s" % e2txt)
    say("      本库(%s): KEY=%s GUID=%s NAME=%s VER=%d.%d"
        % (info["src"], info["key"], info["guid"], info["name"], info["major"], info["minor"]))

    tmp = tempfile.mkdtemp(prefix="mkcage_")
    txt_dir = os.path.join(tmp, "text")
    try:
        say("[1/4] e2t: %s -> <tmp>/text" % user_e)
        rc, so, se = run_e2txt(e2txt, "e2t", user_e, txt_dir, ["-enc", "UTF-8"])
        if rc != 0 or "[错误]" in se or not os.path.isdir(txt_dir):
            raise SystemExit("e2t 失败 rc=%s\nSTDOUT:%s\nSTDERR:%s"
                             % (rc, so[-400:], se[-400:]))

        cfg = find_cfg(txt_dir)
        if not cfg:
            raise SystemExit("在文本目录里找不到支持库配置文件（lib.config.json / 支持库.config.json）")
        say("[2/4] 追加库记录: %s" % os.path.relpath(cfg, tmp))
        added, total, existed = patch_cfg(cfg, info)
        if existed:
            raise SystemExit("该工程已声明本库（Key=%s），无需加壳" % info["key"])
        say("      现有记录数=%d" % total)

        os.makedirs(os.path.dirname(out_e), exist_ok=True)
        say("[3/4] t2e -> %s" % out_e)
        rc, so, se = run_e2txt(e2txt, "t2e", txt_dir, out_e)
        # 此刻本机 lib\ 通常**没装**本库，t2e 会打一条
        #   「引入支持库失败！…[原因] 支持库不存在」——这是**预期且非致命**：
        #   它仍会把库记录写进 .e。真正判据见下面两道。
        if rc != 0 or "SUCC:" not in so or not os.path.isfile(out_e):
            raise SystemExit("t2e 失败 rc=%s\nSTDOUT:%s\nSTDERR:%s"
                             % (rc, so[-400:], se[-400:]))
        for ln in se.splitlines():
            if "[错误]" in ln:
                say("      (t2e 提示，非致命) %s" % ln.strip()[:160])

        m_user = md5f(user_e)
        m_out = md5f(out_e)
        has_key = info["key"].encode("ascii") in open(out_e, "rb").read()
        reasons = []
        if m_out == m_user:
            reasons.append("判据①失败：产物 md5 与输入相同")
        if not has_key:
            reasons.append("判据②失败：产物里找不到 Key 的 ASCII '%s'" % info["key"])
        ok = not reasons
        result = {
            "ok": ok,
            "input": user_e,
            "output": out_e,
            "inputMd5": m_user,
            "outputMd5": m_out,
            "libKey": info["key"],
            "guid": info["guid"],
            "libName": info["name"],
            "libVersion": "%d.%d" % (info["major"], info["minor"]),
            "libInfoSrc": info["src"],
            "changed": (m_out != m_user),
            "hasKeyAscii": has_key,
            "records": total,
        }
        say("[4/4] 验收：")
        say("      输入 %s  md5=%s" % (os.path.basename(user_e), m_user))
        say("      产物 %s  md5=%s" % (os.path.basename(out_e), m_out))
        say("      ① md5 不同 = %s   ② 含 Key '%s' = %s" % (m_out != m_user, info["key"], has_key))
        say("      == %s ==" % ("PASS" if ok else "FAIL"))
        if not ok:
            raise SystemExit("加壳验收未通过： " + "; ".join(reasons))
        say("产物: %s" % out_e)
        return result
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(
        description="给用户的 .e 加壳（声明我们的支持库），全程离线",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="输入 .e")
    ap.add_argument("output", nargs="?", help="输出 _ai.e（默认 <输入>_ai.e）")
    ap.add_argument("--fne", help="本库 .fne 路径（默认 ../assets/elang_addin.fne）")
    ap.add_argument("--json", action="store_true", dest="as_json", help="结果以 JSON 打到 stdout")
    ap.add_argument("-q", "--quiet", action="store_true", help="不打印过程")
    a = ap.parse_args()

    try:
        result = mkcage(a.input, a.output, a.fne, quiet=(a.quiet or a.as_json))
    except SystemExit as e:
        if a.as_json:
            print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False))
        else:
            print("!! %s" % e, file=sys.stderr)
        return 1
    if a.as_json:
        print(json.dumps(result, ensure_ascii=False))
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
