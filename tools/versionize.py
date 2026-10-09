#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""给 PE 产物写入 Windows 版本资源（RT_VERSION / VS_VERSIONINFO）。

为什么会有这个脚本
==================
易语言把工程「程序信息」编译成产物的 RT_VERSION。实测（2026-10-09）：

* 未开免打包的工程 `.e` → 无头静态编译 → **有** RT_VERSION；
* 开了**免打包（易包）**的工程 `.e`
  → 无论是否经过 QAQ-ETool 文本重组、无论 `--kind` 选哪个、无论中文还是英文布局，
  编译出来都**没有** RT_VERSION（资源目录里连类型 16 都没有）。

也就是说：**免打包工程经无头编译就是不写版本资源**（根因在编译链路，与源码无关），
靠重组或换编译方式都救不回来。
最干净的修法是在易语言 IDE 里打开 `.e` → 程序信息 → 勾选「写入版本信息」→ 填好 → 保存，
之后无头编译就会正常带出版本资源（未开免打包的工程就是这个状态）。

在没法动 IDE 的场合，本脚本用 **Windows 官方资源 API**（`BeginUpdateResource` /
`UpdateResource` / `EndUpdateResource`）把标准版本资源写进产物——资源编辑器用的就是
同一套 API，产物是**正常 PE**，不是打补丁式 hack。写完请务必用 `pever.py` 复查。

用法
====
    python versionize.py 产物.exe --file-version 1.2.3.0 --product-version 1.2.3.0 \\
        --product-name 我的产品名 --file-description 我的产品名 \\
        --company-name 作者名 --copyright 作者名 \\
        --original-filename 产物.exe [--lang 2052] [--comment "..."] [--dll]

    # 先看会写什么、不落盘：
    python versionize.py 产物.exe --product-name X --dry-run

`--file-version` 只写 `3.1.3` 也认（自动补成 `3.1.3.0`）。退出码 0 成功、2 用法/IO 错、3 API 失败。
"""

from __future__ import annotations

import argparse
import ctypes
import json
import struct
import sys
from ctypes import wintypes
from pathlib import Path

RT_VERSION = 16
VS_FFI_SIGNATURE = 0xFEEF04BD
VS_FFI_STRUCVERSION = 0x00010000
VOS_NT_WINDOWS32 = 0x00040004
VFT_APP = 0x00000001
VFT_DLL = 0x00000002


def _pad4(b: bytes) -> bytes:
    return b + b"\x00" * ((4 - len(b) % 4) % 4)


def _ver_tuple(v: str) -> tuple[int, int, int, int]:
    parts = [p for p in str(v).split(".") if p != ""]
    out = []
    for p in parts[:4]:
        try:
            out.append(int(p))
        except ValueError:
            raise SystemExit("版本号里出现非数字段: %r" % v)
    while len(out) < 4:
        out.append(0)
    for n in out:
        if not 0 <= n <= 0xFFFF:
            raise SystemExit("版本号段超出 0..65535: %r" % v)
    return tuple(out)          # type: ignore[return-value]


def _block(key: str, value: bytes, value_len: int, vtype: int,
           children: bytes = b"") -> bytes:
    """拼一个 VS_VERSIONINFO 层：wLength/wValueLength/wType + UTF-16 key + value + 子块。"""
    head = struct.pack("<HHH", 0, value_len, vtype) + key.encode("utf-16-le") + b"\x00\x00"
    body = _pad4(head) + _pad4(value) + children
    total = len(body)
    return struct.pack("<HHH", total, value_len, vtype) + body[6:]


def build_version_resource(strings: dict, lang: int, code_page: int,
                           file_type: int, file_version: str, product_version: str) -> bytes:
    fv = _ver_tuple(file_version)
    pv = _ver_tuple(product_version)
    fixed = struct.pack(
        "<IIIIIIIIIIIII",
        VS_FFI_SIGNATURE, VS_FFI_STRUCVERSION,
        (fv[0] << 16) | fv[1], (fv[2] << 16) | fv[3],
        (pv[0] << 16) | pv[1], (pv[2] << 16) | pv[3],
        0x3F,                                   # FileFlagsMask
        0x00,                                   # FileFlags
        VOS_NT_WINDOWS32, file_type, 0, 0, 0)

    entries = b""
    for k, v in strings.items():
        if v is None or str(v) == "":
            continue
        val = str(v)
        data = val.encode("utf-16-le") + b"\x00\x00"
        entries += _block(k, data, len(val) + 1, 1)
    table_key = "%04X%04X" % (lang, code_page)
    table = _block(table_key, b"", 0, 1, entries)
    sfi = _block("StringFileInfo", b"", 0, 1, table)
    translation = struct.pack("<HH", lang, code_page)
    vfi = _block("VarFileInfo", b"", 0, 1, _block("Translation", translation, 1, 0))
    return _block("VS_VERSION_INFO", fixed, len(fixed), 0, sfi + vfi)


def _kernel32():
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    k32.BeginUpdateResourceW.restype = wintypes.HANDLE
    k32.UpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPVOID,
                                    wintypes.WORD, wintypes.LPVOID, wintypes.DWORD]
    k32.UpdateResourceW.restype = wintypes.BOOL
    k32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    k32.EndUpdateResourceW.restype = wintypes.BOOL
    return k32


def write_version_resource(path: str | Path, blob: bytes, lang: int = 2052) -> dict:
    p = Path(path)
    if not p.is_file():
        return {"ok": False, "error": "文件不存在: %s" % p}
    k32 = _kernel32()
    h = k32.BeginUpdateResourceW(str(p), False)
    if not h:
        return {"ok": False, "error": "BeginUpdateResourceW 失败 err=%d" % ctypes.get_last_error()}
    try:
        buf = ctypes.create_string_buffer(blob, len(blob))
        ok = k32.UpdateResourceW(h, ctypes.c_void_p(RT_VERSION), ctypes.c_void_p(1),
                                 lang, ctypes.cast(buf, wintypes.LPVOID), len(blob))
        if not ok:
            k32.EndUpdateResourceW(h, True)          # True = 放弃
            return {"ok": False, "error": "UpdateResourceW 失败 err=%d" % ctypes.get_last_error()}
    finally:
        if not k32.EndUpdateResourceW(h, False):
            return {"ok": False, "error": "EndUpdateResourceW 失败 err=%d" % ctypes.get_last_error()}
    return {"ok": True, "path": str(p), "bytes": len(blob), "lang": lang}


def main(argv=None) -> int:
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")          # type: ignore[union-attr]
        except Exception:                                 # noqa: BLE001
            pass
    ap = argparse.ArgumentParser(description="给 PE 产物写入 Windows 版本资源")
    ap.add_argument("path", help="目标 .exe / .dll（原地修改，建议先备份）")
    ap.add_argument("--file-version", default="1.0.0.0")
    ap.add_argument("--product-version", default=None, help="缺省与 --file-version 相同")
    ap.add_argument("--product-name", default="")
    ap.add_argument("--file-description", default="")
    ap.add_argument("--company-name", default="")
    ap.add_argument("--copyright", default="")
    ap.add_argument("--original-filename", default=None, help="缺省取目标文件名")
    ap.add_argument("--internal-name", default=None, help="缺省取目标文件名主干")
    ap.add_argument("--comment", default="")
    ap.add_argument("--lang", type=int, default=2052, help="语言 ID（默认 2052 简体中文）")
    ap.add_argument("--code-page", type=int, default=1200, help="代码页（默认 1200 = Unicode）")
    ap.add_argument("--dll", action="store_true", help="产物是 DLL（FileType=2）")
    ap.add_argument("--dry-run", action="store_true", help="只打印将要写入的字段")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)

    p = Path(a.path)
    fv = ".".join(str(x) for x in _ver_tuple(a.file_version))
    pv = ".".join(str(x) for x in _ver_tuple(a.product_version or a.file_version))
    strings = {
        "Comments": a.comment,
        "CompanyName": a.company_name,
        "FileDescription": a.file_description,
        "FileVersion": fv,
        "InternalName": a.internal_name or p.stem,
        "LegalCopyright": a.copyright,
        "OriginalFilename": a.original_filename or p.name,
        "ProductName": a.product_name,
        "ProductVersion": pv,
    }
    blob = build_version_resource(strings, a.lang, a.code_page,
                                  VFT_DLL if a.dll else VFT_APP, fv, pv)
    if a.dry_run:
        out = {"dryRun": True, "path": str(p), "bytes": len(blob),
               "fileVersion": fv, "productVersion": pv, "lang": a.lang,
               "strings": {k: v for k, v in strings.items() if v}}
        print(json.dumps(out, ensure_ascii=False, indent=2) if a.json
              else "\n".join("%-18s %s" % (k, v) for k, v in out["strings"].items()))
        return 0

    res = write_version_resource(p, blob, a.lang)
    res.update({"fileVersion": fv, "productVersion": pv,
                "strings": {k: v for k, v in strings.items() if v}})
    if a.json:
        print(json.dumps(res, ensure_ascii=False))
    else:
        print("写入成功: %s (%d 字节版本资源, 版本 %s)" % (p, len(blob), fv)
              if res["ok"] else "写入失败: %s" % res.get("error"))
    return 0 if res["ok"] else 3


if __name__ == "__main__":
    sys.exit(main())
