#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
snap_state.py —— 任务 A1/A3 的状态快照器

把「支持库选择状态」可能落脚的所有地方，完整、可字节级比对地拍快照：
  1) HKCU\\Software\\FlySky 全树（递归）：键名 / 值名 / 类型 / 长度 / 十六进制 dump
     另把每个 REG_BINARY 值单独落一份 .bin，便于逐字节 diff
  2) D:\\ides\\e\\lib\\ 递归清单（相对路径 + size + mtime）
  3) lib\\ 顶层 *.fne 文件名清单（排序）
  4) e.exe 的 PE 版本信息 + 文件大小/mtime

用法:  python snap_state.py <输出目录>
例:    python snap_state.py re/reg_before
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import sys
import winreg

E_EXE = r"D:\ides\e\e.exe"
LIB_DIR = r"D:\ides\e\lib"
REG_ROOT = r"Software\FlySky"


# ------------------------------------------------------------------ registry
def enum_reg(hive, path, out_lines, out_files, dirpath, depth=0):
    try:
        h = winreg.OpenKey(hive, path)
    except OSError as e:
        out_lines.append("(cannot open %s: %s)" % (path, e))
        return
    try:
        out_lines.append("KEY %s" % path)
        i = 0
        while True:
            try:
                name, val, typ = winreg.EnumValue(h, i)
                i += 1
            except OSError:
                break
            tn = {1: "REG_SZ", 2: "REG_EXPAND_SZ", 3: "REG_BINARY", 4: "REG_DWORD",
                  5: "REG_MULTI_SZ", 7: "REG_MULTI_SZ", 11: "REG_QWORD"}.get(typ, str(typ))
            if isinstance(val, (bytes, bytearray)):
                b = bytes(val)
                hexs = b.hex()
                out_lines.append("  VAL %-16s type=%s len=%d hex=%s" % (name, tn, len(b), hexs))
                safe = "".join(c if c.isalnum() or c in "._-" else "_" for c in name)
                fn = os.path.join(dirpath, "%s.bin" % safe)
                with open(fn, "wb") as f:
                    f.write(b)
                out_files.append(fn)
            elif isinstance(val, list):
                out_lines.append("  VAL %-16s type=%s values=%r" % (name, tn, val))
            else:
                out_lines.append("  VAL %-16s type=%s value=%r" % (name, tn, val))
        j = 0
        while True:
            try:
                sub = winreg.EnumKey(h, j)
                j += 1
            except OSError:
                break
            enum_reg(hive, path + "\\" + sub, out_lines, out_files, dirpath, depth + 1)
    finally:
        winreg.CloseKey(h)


def dump_registry(dirpath):
    lines, files = [], []
    for hive, hname in ((winreg.HKEY_CURRENT_USER, "HKCU"), (winreg.HKEY_LOCAL_MACHINE, "HKLM")):
        try:
            enum_reg(hive, REG_ROOT, lines, files, dirpath)
        except OSError as e:
            lines.append("(root %s\\%s failed: %s)" % (hname, REG_ROOT, e))
    with open(os.path.join(dirpath, "registry_dump.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return len(lines)


# ------------------------------------------------------------------ filesystem
def dump_lib(dirpath):
    rows = []
    for root, _dn, fn in os.walk(LIB_DIR):
        for f in fn:
            p = os.path.join(root, f)
            rel = os.path.relpath(p, LIB_DIR).replace("\\", "/")
            try:
                st = os.stat(p)
                rows.append((rel, st.st_size, int(st.st_mtime)))
            except OSError:
                rows.append((rel, -1, -1))
    rows.sort()
    with open(os.path.join(dirpath, "lib_listing.tsv"), "w", encoding="utf-8") as f:
        f.write("relpath\tsize\tmtime\n")
        for r in rows:
            f.write("%s\t%d\t%d\n" % r)
    top = sorted(x[0] for x in rows if "/" not in x[0] and x[0].lower().endswith(".fne"))
    with open(os.path.join(dirpath, "lib_fne_toplevel.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(top) + "\n")
    return len(rows), len(top)


# ------------------------------------------------------------------ exe version
def file_version(path):
    size = ctypes.windll.version.GetFileVersionInfoSizeA(path.encode("gbk"), None)
    if not size:
        return None
    buf = ctypes.create_string_buffer(size)
    if not ctypes.windll.version.GetFileVersionInfoA(path.encode("gbk"), 0, size, buf):
        return None
    out = {}
    ptr = ctypes.c_void_p()
    length = ctypes.c_uint()
    if ctypes.windll.version.VerQueryValueA(buf, b"\\", ctypes.byref(ptr), ctypes.byref(length)):
        ffi = ctypes.cast(ptr, ctypes.POINTER(ctypes.c_uint * 13)).contents
        out["FileVersionMS"] = "0x%08X" % ffi[2]
        out["FileVersionLS"] = "0x%08X" % ffi[3]
        out["FileVersion"] = "%d.%d.%d.%d" % (ffi[2] >> 16, ffi[2] & 0xFFFF,
                                              ffi[3] >> 16, ffi[3] & 0xFFFF)
    return out


def dump_exe(dirpath):
    st = os.stat(E_EXE)
    v = file_version(E_EXE)
    with open(os.path.join(dirpath, "e_version.txt"), "w", encoding="utf-8") as f:
        f.write("path=%s\nsize=%d\nmtime=%d\nversion=%s\n" % (E_EXE, st.st_size, int(st.st_mtime), v))
    return v


def main():
    if len(sys.argv) < 2:
        print("usage: snap_state.py <outdir>")
        return 2
    dirpath = sys.argv[1]
    os.makedirs(dirpath, exist_ok=True)
    nreg = dump_registry(dirpath)
    nlib, ntop = dump_lib(dirpath)
    v = dump_exe(dirpath)
    print("snapshot -> %s" % dirpath)
    print("  registry lines=%d" % nreg)
    print("  lib files=%d (top-level .fne=%d)" % (nlib, ntop))
    print("  e.exe version=%s" % v)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
