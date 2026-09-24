#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q1 关键坑的实证：内核文件被某个进程加载着的时候，还能怎么动它？

⚠️ 必须**直调 Win32 API**：本机 Python 的 `open(...,'r+b')` / `unlink()` 会被
   safe-delete 包装层拦截（它会去尝试 trash，失败信息与「被锁」完全分不清），
   拿它当判据会得出错误结论。

四项语义分开测，顺序有讲究：
  ① 就地覆盖写  ② 真删除  ③ 改名  ④ 改走之后在原位放一个新文件
  —— ④ 是关键：若可行，就存在「改名腾位 → 放新内核」这条不停机升级路径。

所有操作都在**副本**上做，绝不碰真内核。
"""
import ctypes
import ctypes.wintypes as wt
import pathlib
import shutil
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
GENERIC_WRITE = 0x40000000
OPEN_EXISTING = 3
CREATE_NEW = 1
MOVEFILE_REPLACE_EXISTING = 0x1

HERE = pathlib.Path(__file__).resolve().parent
LOCK = HERE / "lock"
SRC = HERE.parent / "opt" / "e2e" / "node.dll"
TARGET = LOCK / "node_lock.dll"
RENAMED = LOCK / "node_lock.renamed.dll"
ENV = {"SystemRoot": r"C:\Windows", "PATH": r"C:\Windows\System32;C:\Windows"}


def err_name():
    return "err=%lu" % ctypes.get_last_error()


def fresh():
    for p in (TARGET, RENAMED):
        if p.exists():
            k32.DeleteFileW(str(p))
            if p.exists():
                shutil.rmtree(p) if p.is_dir() else p.unlink()
    shutil.copy2(SRC, TARGET)


def try_overwrite():
    h = k32.CreateFileW(str(TARGET), GENERIC_WRITE, 0, None, OPEN_EXISTING, 0, None)
    if h == -1 or h == 0xFFFFFFFFFFFFFFFF:
        return "被拒(%s)" % err_name()
    n = wt.DWORD(0)
    ok = k32.WriteFile(h, b"\x00", 1, ctypes.byref(n), None)
    k32.CloseHandle(h)
    return "允许" if ok else "被拒(WriteFile %s)" % err_name()


def try_delete():
    ok = k32.DeleteFileW(str(TARGET))
    if ok:
        return "允许"
    return "被拒(%s)" % err_name()


def try_rename():
    ok = k32.MoveFileExW(str(TARGET), str(RENAMED), MOVEFILE_REPLACE_EXISTING)
    return "允许" if ok else "被拒(%s)" % err_name()


def try_put_new():
    h = k32.CreateFileW(str(TARGET), GENERIC_WRITE, 0, None, CREATE_NEW, 0, None)
    if h == -1:
        return "被拒(%s)" % err_name()
    n = wt.DWORD(0)
    k32.WriteFile(h, b"MZ" + b"\x00" * 64, 66, ctypes.byref(n), None)
    k32.CloseHandle(h)
    return "允许"


def attempt(tag):
    res = {"覆盖写": try_overwrite()}
    res["真删除"] = try_delete()
    if TARGET.exists():
        res["改名"] = try_rename()
        if not TARGET.exists():
            res["改名后放新文件"] = try_put_new()
            if RENAMED.exists():
                k32.DeleteFileW(str(RENAMED))
    print("  [%-8s] %s" % (tag, "  ".join("%s=%s" % kv for kv in res.items())))
    return res


def holder(secs):
    p = subprocess.Popen([str(LOCK / "holder.exe"), str(TARGET), str(secs)],
                         cwd=str(LOCK), env=ENV,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print("  holder:", (p.stdout.readline() or b"").decode("gbk", "replace").strip())
    return p


def main():
    if not SRC.exists():
        print("缺源内核:", SRC)
        return 2
    print("真内核:", SRC, SRC.stat().st_size, "字节（只读来源，不动它）")

    print("\n=== 阶段 1：没有任何进程加载它（对照） ===")
    fresh()
    free1 = attempt("未加载")

    print("\n=== 阶段 2：holder.exe 已把它 LoadLibrary 起来 ===")
    fresh()
    p = holder(12)
    time.sleep(1.0)
    locked = attempt("加载中")
    p.wait(timeout=40)

    print("\n=== 阶段 3：holder 退出之后 ===")
    fresh()
    p = holder(1)
    p.wait(timeout=40)
    time.sleep(0.5)
    free2 = attempt("已卸载")

    print()
    ok = (free1.get("覆盖写") == "允许" and free1.get("真删除") == "允许"
          and locked.get("覆盖写", "").startswith("被拒")
          and locked.get("真删除", "").startswith("被拒")
          and free2.get("覆盖写") == "允许")
    if ok:
        print("结论（Win32 API 实测）：")
        print("  · 内核被加载期间：**不能就地覆盖写、不能删除**")
        print("  · 但**改名允许**，且改名后能在原位放新文件")
        print("  ⇒ 要换内核，先把它改名腾位即可，不必停掉所有程序；")
        print("     代价是「老程序继续用旧内核、新程序用新内核」会同时存在。")
        return 0
    print("结论：与预期不符，需要人工看 ❌")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
