#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""并发窗口观察器：在编译进行中反复扫描「请输入欲编译到…」保存框的完整控件树。

为什么要单独写它：宿主库的 `DLG-DUMP` 只在**前 3 次**扫描时落盘，而那 3 次恰好都发生在
文件名编辑框还没被创建的时候（`edits=0`），所以我们始终没看到**真正承载文件名的那几个
控件**的类名与控件号。这个脚本在外面不停扫，一发现「有 Edit 的保存框」就立刻把整棵树
（类名 / 控件号 / 父类 / 文本 / 使能）落盘。

用法：先起编译（后台），再跑本脚本。
"""
import ctypes
import ctypes.wintypes as w
import pathlib
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

u = ctypes.windll.user32
OUT = pathlib.Path(__file__).resolve().parent / "dlg_watch.txt"
TARGET_TITLE = "请输入欲编译到"
DURATION = float(sys.argv[1]) if len(sys.argv) > 1 else 120.0

EnumProc = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)


def text(h, n=512):
    b = ctypes.create_unicode_buffer(n)
    u.GetWindowTextW(h, b, n)
    return b.value


def cls(h):
    b = ctypes.create_unicode_buffer(256)
    u.GetClassNameW(h, b, 256)
    return b.value


def children(h):
    out = []

    def cb(x, l):
        out.append(x)
        return True

    u.EnumChildWindows(h, EnumProc(cb), 0)
    return out


def tops():
    out = []

    def cb(x, l):
        out.append(x)
        return True

    u.EnumWindows(EnumProc(cb), 0)
    return out


def main() -> int:
    seen = set()
    hits = 0
    t0 = time.time()
    with OUT.open("w", encoding="utf-8") as fp:
        while time.time() - t0 < DURATION:
            for h in tops():
                if cls(h) != "#32770":
                    continue
                t = text(h)
                if TARGET_TITLE not in t:
                    continue
                ch = children(h)
                sig = (t, tuple((cls(c), u.GetDlgCtrlID(c), text(c, 200)) for c in ch))
                has_edit = any(c.lower() == "edit" for c in (cls(c) for c in ch))
                if not has_edit:
                    continue                     # 还没到「文件名框存在」的时刻
                if sig in seen:
                    continue
                seen.add(sig)
                hits += 1
                fp.write("=" * 90 + "\n")
                fp.write("[%.1fs] 保存框 title=%r 子控件=%d（含编辑框）\n"
                         % (time.time() - t0, t, len(ch)))
                for c in ch:
                    cc = cls(c)
                    if cc in ("Static", "ToolbarWindow32", "ReBarWindow32", "WorkerW",
                              "msctls_progress32", "TravelBand", "UpBand", "SeparatorBand",
                              "UniversalSearchBand", "Search Box",
                              "SearchEditBoxWrapperClass", "DirectUIHWND",
                              "Breadcrumb Parent", "Address Band Root", "ScrollBar"):
                        continue                  # 只留可能有用的
                    par = u.GetParent(c)
                    rr = w.RECT()
                    u.GetWindowRect(c, ctypes.byref(rr))
                    pr = w.RECT()
                    if par:
                        u.GetWindowRect(par, ctypes.byref(pr))
                    fp.write("   id=%-6d cls=%-18s par=%-16s en=%d rect=(%d,%d,%d,%d)%s txt=%r\n"
                             % (u.GetDlgCtrlID(c), cc,
                                cls(par) if par else "", u.IsWindowEnabled(c),
                                rr.left, rr.top, rr.right, rr.bottom,
                                " par_rect=(%d,%d,%d,%d)" % (pr.left, pr.top, pr.right, pr.bottom)
                                if par else "",
                                text(c, 200)))
                fp.flush()
            time.sleep(0.15)
    print("观察结束：抓到 %d 个「含编辑框的保存框」快照 -> %s" % (hits, OUT))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
