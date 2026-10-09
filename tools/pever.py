#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""读取 PE 产物的 Windows 版本资源（RT_VERSION / VS_VERSIONINFO），零第三方依赖。

为什么需要它
============
「编译成功」不等于「发布就绪」。易语言工程把**程序信息**（`配置/用户.config.json` 的
`WriteVersion` / `ReleaseVersion` / `Name` / `Description` / `Author` / `Copyright` /
`Email`）编译成产物的 `RT_VERSION` 资源；一旦这条信息在源码往返（QAQ-ETool e2t→t2e、
多次重组）或工程改动中丢掉，产物照样编译成功、照样能跑，但 Windows「属性 → 详细信息」
里**所有字段全空**——只看文件大小和 mtime 的判据完全发现不了。

本模块就是那条判据：解析 PE 资源目录，取回 RT_VERSION 的字符串表与
VS_FIXEDFILEINFO 里的数值版本。

命令行
======
    python pever.py <产物.exe|.dll> [...] [--json] [--expect-version 3.1.3]
                    [--require] [--quiet]

退出码：0 = 全部满足要求；3 = 有产物缺版本资源或字段/版本不符；2 = 用法/文件错误。

API
===
    read_version_info(path) -> dict | None
        返回 {"fileVersion": "1.0.0.0", "productVersion": ..., "strings": {...},
              "translation": "080404B0", "rawSize": N}；没有 RT_VERSION 时返回 None。
    check_version_info(path, expect_version=None, require=False) -> dict
        返回 {"ok": bool, "present": bool, "missing": [...], "mismatch": [...]...}
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from pathlib import Path

# 发布时必须非空的字符串字段。
# 实测（易语言静态编译产物）：编译器只会写出「程序信息」里确实填了的项 ——
#   FileDescription ← Description、ProductName ← Name、LegalCopyright ← Copyright、
#   CompanyName ← Author；Author/Copyright 留空时这两项**根本不会出现在资源里**。
# 所以硬判据只取「每个工程都一定有」的四项；其余只作提醒，不判失败。
REQUIRED_STRINGS = (
    "FileDescription",
    "FileVersion",
    "ProductName",
    "ProductVersion",
)
RECOMMENDED_STRINGS = (
    "CompanyName",
    "LegalCopyright",
)

RT_VERSION = 16


# --------------------------------------------------------------------------
# PE 结构读取
# --------------------------------------------------------------------------
class _PE:
    def __init__(self, data: bytes):
        self.d = data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise ValueError("不是 PE 文件（缺 MZ 头）")
        e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
        if e_lfanew + 24 > len(data) or data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
            raise ValueError("不是 PE 文件（缺 PE\\0\\0 签名）")
        coff = e_lfanew + 4
        nsec, = struct.unpack_from("<H", data, coff + 2)
        opt_size, = struct.unpack_from("<H", data, coff + 16)
        opt = coff + 20
        magic, = struct.unpack_from("<H", data, opt)
        if magic == 0x20B:      # PE32+
            dd = opt + 112
        elif magic == 0x10B:    # PE32
            dd = opt + 96
        else:
            raise ValueError("未知的可选头 magic 0x%04X" % magic)
        rsrc_rva, rsrc_size = struct.unpack_from("<II", data, dd + 2 * 8)
        self.rsrc_rva, self.rsrc_size = rsrc_rva, rsrc_size
        sec = opt + opt_size
        self.sections = []
        for i in range(nsec):
            off = sec + i * 40
            if off + 40 > len(data):
                break
            name = data[off:off + 8].rstrip(b"\0")
            vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
            self.sections.append((name, vaddr, vsize, rawptr, rawsize))

    def off(self, rva: int) -> int | None:
        for _name, vaddr, vsize, rawptr, rawsize in self.sections:
            if vaddr <= rva < vaddr + max(vsize, rawsize):
                delta = rva - vaddr
                if delta < rawsize:
                    return rawptr + delta
        return None


def _wstr(buf: bytes, off: int) -> str:
    end = off
    while end + 1 < len(buf) and not (buf[end] == 0 and buf[end + 1] == 0):
        end += 2
    return buf[off:end].decode("utf-16-le", "replace")


def _align4(n: int) -> int:
    return (n + 3) & ~3


def _walk_version_block(buf: bytes, start: int, end: int, strings: dict,
                        keys: list | None = None) -> None:
    """递归遍历 VS_VERSIONINFO 的层级（StringFileInfo → StringTable → String）。"""
    off = start
    while off + 6 <= end:
        clen, cvlen, ctype = struct.unpack_from("<HHH", buf, off)
        if clen < 6 or off + clen > end:
            break
        cbuf = buf[off:off + clen]
        key = _wstr(cbuf, 6)
        if keys is not None:
            keys.append(key)
        voff = _align4(6 + (len(key) + 1) * 2)
        if ctype == 0 and cvlen:
            # 根节点的 Value = VS_FIXEDFILEINFO（52 字节）
            voff = _align4(voff + cvlen)
            _walk_version_block(buf, off + voff, off + clen, strings, keys)
        elif ctype == 1 and cvlen:
            strings.setdefault(key, _wstr(cbuf, voff))
            voff = _align4(voff + cvlen * 2)
            _walk_version_block(buf, off + voff, off + clen, strings, keys)
        else:
            _walk_version_block(buf, off + voff, off + clen, strings, keys)
        off += _align4(clen)


def _fixed_file_info(buf: bytes) -> tuple[str, str]:
    """从根节点取 VS_FIXEDFILEINFO 的数值版本 → ("a.b.c.d", "a.b.c.d")。"""
    if len(buf) < 6:
        return "", ""
    total, vlen, vtype = struct.unpack_from("<HHH", buf, 0)
    if vtype != 0 or vlen < 52:
        return "", ""
    key = _wstr(buf, 6)
    voff = _align4(6 + (len(key) + 1) * 2)
    if voff + 52 > len(buf):
        return "", ""
    fv_ms, fv_ls, pv_ms, pv_ls = struct.unpack_from("<IIII", buf, voff + 8)
    fmt = lambda ms, ls: "%d.%d.%d.%d" % (ms >> 16, ms & 0xFFFF, ls >> 16, ls & 0xFFFF)
    return fmt(fv_ms, fv_ls), fmt(pv_ms, pv_ls)


def _find_version_resource(pe: _PE) -> bytes | None:
    """在资源目录里找 RT_VERSION（类型 16，或名为 "#16" 的命名项），返回其原始字节。"""
    if not pe.rsrc_rva:
        return None
    base = pe.off(pe.rsrc_rva)
    if base is None:
        return None

    def entries(dir_off: int):
        if dir_off + 16 > len(pe.d):
            return []
        nname, nid = struct.unpack_from("<HH", pe.d, dir_off + 12)
        out = []
        for i in range(nname + nid):
            eo = dir_off + 16 + i * 8
            if eo + 8 > len(pe.d):
                break
            name, dataoff = struct.unpack_from("<II", pe.d, eo)
            named = bool(name & 0x80000000)
            label = None
            if named:
                so = base + (name & 0x7FFFFFFF)
                if so + 2 <= len(pe.d):
                    ln, = struct.unpack_from("<H", pe.d, so)
                    label = pe.d[so + 2:so + 2 + ln * 2].decode("utf-16-le", "replace")
            out.append({"id": None if named else name, "label": label,
                        "sub": bool(dataoff & 0x80000000),
                        "off": dataoff & 0x7FFFFFFF})
        return out

    def is_version(e) -> bool:
        return e["id"] == RT_VERSION or (e["label"] or "").lstrip("#") == str(RT_VERSION)

    for t in entries(base):
        if not is_version(t) or not t["sub"]:
            continue
        for lang in entries(base + t["off"]):
            if not lang["sub"]:
                continue
            for leaf in entries(base + lang["off"]):
                if leaf["sub"]:
                    continue
                do = base + leaf["off"]
                if do + 16 > len(pe.d):
                    continue
                rva, size = struct.unpack_from("<II", pe.d, do)
                fo = pe.off(rva)
                if fo is None or size == 0:
                    continue
                return pe.d[fo:fo + size]
    return None


def read_version_info(path: str | Path) -> dict | None:
    """读产物的 RT_VERSION。没有该资源时返回 None。"""
    data = Path(path).read_bytes()
    pe = _PE(data)
    blob = _find_version_resource(pe)
    if not blob:
        return None
    strings: dict[str, str] = {}
    keys: list[str] = []
    _walk_version_block(blob, 0, len(blob), strings, keys)
    fv, pv = _fixed_file_info(blob)
    trans = next((k for k in keys if len(k) == 8 and all(
        c in "0123456789ABCDEFabcdef" for c in k)), "")
    return {
        "rawSize": len(blob),
        "fileVersion": strings.get("FileVersion") or fv,
        "productVersion": strings.get("ProductVersion") or pv,
        "fixedFileVersion": fv,
        "fixedProductVersion": pv,
        "translation": trans,
        "strings": strings,
        "resourceTypes": resource_types(path),
    }


def resource_types(path: str | Path) -> list[str]:
    """列出产物资源目录里的一级类型（排障用；能一眼看出 RT_VERSION 在不在）。"""
    pe = _PE(Path(path).read_bytes())
    if not pe.rsrc_rva:
        return []
    base = pe.off(pe.rsrc_rva)
    if base is None:
        return []
    nname, nid = struct.unpack_from("<HH", pe.d, base + 12)
    names = {1: "RT_CURSOR", 2: "RT_BITMAP", 3: "RT_ICON", 4: "RT_MENU", 5: "RT_DIALOG",
             6: "RT_STRING", 7: "RT_FONTDIR", 8: "RT_FONT", 9: "RT_ACCELERATOR",
             10: "RT_RCDATA", 11: "RT_MESSAGETABLE", 12: "RT_GROUP_CURSOR",
             14: "RT_GROUP_ICON", 16: "RT_VERSION", 24: "RT_MANIFEST"}
    out = []
    for i in range(nname + nid):
        eo = base + 16 + i * 8
        if eo + 8 > len(pe.d):
            break
        name, _ = struct.unpack_from("<II", pe.d, eo)
        if name & 0x80000000:
            so = base + (name & 0x7FFFFFFF)
            ln, = struct.unpack_from("<H", pe.d, so)
            out.append(pe.d[so + 2:so + 2 + ln * 2].decode("utf-16-le", "replace"))
        else:
            out.append(names.get(name, "#%d" % name))
    return out


def _norm(v: str) -> tuple:
    parts = [p for p in str(v or "").split(".") if p != ""]
    out = []
    for p in parts[:4]:
        try:
            out.append(int(p))
        except ValueError:
            out.append(0)
    while len(out) < 4:
        out.append(0)
    return tuple(out)


def check_version_info(path: str | Path, expect_version: str | None = None,
                       require: bool = False) -> dict:
    """按发布要求校验一个产物。

    require=True 时「完全没有 RT_VERSION」= 失败；False 时缺失只记录不判失败
    （但仍会把 present=False 报出来）。给了 expect_version 就一定会判失败。
    """
    p = Path(path)
    res: dict = {"path": str(p), "present": False, "ok": True,
                 "missing": [], "mismatch": [], "warnings": [], "fields": {}}
    if not p.is_file():
        res["ok"] = False
        res["missing"].append("产物不存在")
        return res
    try:
        info = read_version_info(p)
    except Exception as exc:                                    # noqa: BLE001
        res["ok"] = False
        res["error"] = "解析 PE 版本资源失败: %s" % exc
        return res
    if not info:
        res["present"] = False
        res["resourceTypes"] = resource_types(p)
        if require or expect_version:
            res["ok"] = False
            res["missing"].append(
                "产物没有 RT_VERSION 资源（Windows「属性→详细信息」全空）。"
                "当前资源类型: %s" % ", ".join(res["resourceTypes"]))
        return res

    res["present"] = True
    res["fields"] = {
        "FileVersion": info["fileVersion"],
        "ProductVersion": info["productVersion"],
        "ProductName": info["strings"].get("ProductName", ""),
        "FileDescription": info["strings"].get("FileDescription", ""),
        "CompanyName": info["strings"].get("CompanyName", ""),
        "OriginalFilename": info["strings"].get("OriginalFilename", ""),
        "LegalCopyright": info["strings"].get("LegalCopyright", ""),
        "translation": info["translation"],
    }
    if require or expect_version:
        for k in REQUIRED_STRINGS:
            if not str(res["fields"].get(k) or "").strip():
                res["missing"].append(k)
        for k in RECOMMENDED_STRINGS:
            if not str(res["fields"].get(k) or "").strip():
                res["warnings"].append("%s 为空（易语言只在「程序信息」填了才写出该字段）" % k)
    if expect_version:
        want = _norm(expect_version)
        for k in ("FileVersion", "ProductVersion"):
            got = _norm(res["fields"].get(k))
            if got != want:
                res["mismatch"].append("%s=%s（期望 %s）" % (
                    k, res["fields"].get(k) or "(空)",
                    ".".join(str(x) for x in want)))
    res["ok"] = not (res["missing"] or res["mismatch"])
    return res


def main(argv=None) -> int:
    # 控制台是 GBK（cp936）时，✗/✓ 之类字符会让 print 直接抛 UnicodeEncodeError。
    # 统一退化为可替换输出，脚本永远不因为「打印」而失败。
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")          # type: ignore[union-attr]
        except Exception:                                 # noqa: BLE001
            pass
    ap = argparse.ArgumentParser(description="读取/校验 PE 产物的 Windows 版本资源")
    ap.add_argument("paths", nargs="+", help="要检查的 .exe / .dll")
    ap.add_argument("--expect-version", help="期望版本（如 3.1.3）；给了就强制校验")
    ap.add_argument("--require", action="store_true",
                    help="要求必须有 RT_VERSION（没有即失败）")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args(argv)

    results = [check_version_info(p, a.expect_version, a.require) for p in a.paths]
    ok = all(r["ok"] for r in results)
    if a.json:
        print(json.dumps({"ok": ok, "results": results}, ensure_ascii=False, indent=2))
    elif not a.quiet:
        for r in results:
            if r["present"]:
                f = r["fields"]
                print("%-34s %-4s FileVersion=%-10s ProductVersion=%-10s ProductName=%s"
                      % (Path(r["path"]).name, "OK" if r["ok"] else "BAD",
                         f.get("FileVersion") or "(空)", f.get("ProductVersion") or "(空)",
                         f.get("ProductName") or "(空)"))
            else:
                print("%-34s %-4s 无 RT_VERSION  资源类型: %s"
                      % (Path(r["path"]).name, "BAD" if not r["ok"] else "-",
                         ", ".join(r.get("resourceTypes") or [])))
            for m in r["missing"]:
                print("    x 缺失: %s" % m)
            for m in r["mismatch"]:
                print("    x 不符: %s" % m)
            for m in r.get("warnings") or []:
                print("    ! 提醒: %s" % m)
            if r.get("error"):
                print("    ✗ %s" % r["error"])
    return 0 if ok else 3


if __name__ == "__main__":
    sys.exit(main())
