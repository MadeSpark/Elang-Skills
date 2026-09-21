#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
a2_with_project.py —— A2：**先打开一个工程**，再 PostMessage(WM_COMMAND,0x808A=支持库配置)，
枚举对话框控件，判定「能否程序化打开 + 控件是否标准」。
（前置发现：0x808A 在没有工程时是 no-op；0x806C 系统配置能开，证明 WM_COMMAND 路由本身是通的。）
只读：不点击“确定/勾选”，只枚举；结束 WM_CLOSE + 精确 PID 杀。
"""
import ctypes as C
import ctypes.wintypes as wt
import os
import shutil
import subprocess
import sys
import time

u32 = C.windll.user32
E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
WM_COMMAND = 0x0111
WM_CLOSE = 0x0010
EnumProc = C.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_E = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "projD_base.e")
ID = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x808A


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    b = C.create_unicode_buffer(n + 2)
    u32.GetWindowTextW(h, b, n + 1)
    return b.value


def cname(h):
    b = C.create_unicode_buffer(256)
    u32.GetClassNameW(h, b, 255)
    return b.value


def wpid(h):
    p = wt.DWORD(0)
    u32.GetWindowThreadProcessId(h, C.byref(p))
    return p.value


def tops(pid):
    out = {}

    def cb(h, lp):
        if wpid(h) == pid:
            out[h] = (cname(h), wtext(h), bool(u32.IsWindowVisible(h)))
        return True
    u32.EnumWindows(EnumProc(cb), 0)
    return out


def children(h):
    res = []

    def cb(hh, lp):
        res.append((hh, cname(hh), u32.GetDlgCtrlID(hh), wtext(hh)[:70],
                    bool(u32.IsWindowVisible(hh)), bool(u32.IsWindowEnabled(hh))))
        return True
    u32.EnumChildWindows(h, EnumProc(cb), 0)
    return res


def main():
    work = os.path.join(HERE, "ework_a2")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    ecopy = os.path.join(work, "proj.e")
    shutil.copy2(SRC_E, ecopy)
    proc = subprocess.Popen([E_EXE, ecopy], cwd=E_DIR,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid = proc.pid
    try:
        print("launched e.exe pid=%d with project '%s'" % (pid, os.path.basename(SRC_E)))
        hmain = None
        t0 = time.time()
        while time.time() - t0 < 30:
            hmain = u32.FindWindowW("ENewFrame", None)
            if hmain:
                break
            time.sleep(0.4)
        if not hmain:
            print("[STOP] no ENewFrame"); return 2
        print("main hwnd=0x%08X '%s'" % (hmain, wtext(hmain)))
        # 给 IDE 足够时间把工程加载完、菜单使能
        time.sleep(6.0)

        before = tops(pid)
        print("title after project load: '%s'" % wtext(hmain))
        v32770_before = set(h for h, v in before.items() if v[0] == "#32770" and v[2])
        print("打开前【可见】#32770 =", [(hex(h), before[h][1]) for h in v32770_before] or "（无）")
        print("PostMessage(hMain, WM_COMMAND, 0x%04X, 0)" % ID)
        u32.PostMessageW(hmain, WM_COMMAND, ID, 0)
        time.sleep(3.0)
        after = tops(pid)
        new = [h for h in after if h not in before]
        v32770_after = set(h for h, v in after.items() if v[0] == "#32770" and v[2])
        appeared_vis = v32770_after - v32770_before
        print("新增顶层窗口:", [(hex(h),) + after[h] for h in new] or "（无）")
        print("打开后【新出现可见】#32770 =", [(hex(h), after[h][1]) for h in appeared_vis] or "（无）")
        dlg = list(appeared_vis) if appeared_vis else \
              [h for h in v32770_after if after[h][1]]
        if not dlg:
            print("[STOP] 未弹出 #32770 对话框")
            return 6
        print("==> 候选对话框:", [(hex(h), after[h][1]) for h in dlg])
        h = dlg[0]
        kids = children(h)
        print("\n--- 对话框 0x%08X '%s' 控件=%d ---" % (h, after[h][1], len(kids)))
        hist = {}
        for (hh, c, cid, t, vis, en) in kids:
            print("   ctrl 0x%08X class='%-20s' id=%-6d vis=%d en=%d text='%s'"
                  % (hh, c, cid, vis, en, t))
            hist[c] = hist.get(c, 0) + 1
        print("class 直方图:", hist)
        u32.PostMessageW(h, WM_CLOSE, 0, 0)
        time.sleep(0.6)
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, text=True)
            try:
                proc.wait(timeout=10)
            except Exception:
                pass
        shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
