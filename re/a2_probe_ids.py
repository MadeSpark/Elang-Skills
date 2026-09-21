#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
a2_probe_ids.py —— 决定性诊断：向主窗口 PostMessage 一组菜单命令 ID，
逐个看是否弹出新的 #32770 对话框。用来区分
  (a) “WM_COMMAND 路由本身不通”        —— 所有 ID 都无反应
  (b) “路由通、但某些命令有前置条件”    —— 部分 ID 有反应
只读：不点击、不改勾选；结束 WM_CLOSE + 精确 PID 杀。
"""
import ctypes as C
import ctypes.wintypes as wt
import subprocess
import time

u32 = C.windll.user32
E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
WM_COMMAND = 0x0111
WM_CLOSE = 0x0010
EnumProc = C.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


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
        res.append((hh, cname(hh), u32.GetDlgCtrlID(hh), wtext(hh)[:60]))
        return True
    u32.EnumChildWindows(h, EnumProc(cb), 0)
    return res


CANDS = [
    (0x808A, "L.支持库配置"),
    (0x808C, "I.安装新的支持库"),
    (0x806C, "O.系统配置"),
    (0x8051, "M.菜单编辑器"),
    (0x80A2, "W.执行易向导"),
]


def main():
    proc = subprocess.Popen([E_EXE], cwd=E_DIR,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid = proc.pid
    try:
        print("launched e.exe pid=%d" % pid)
        hmain = None
        t0 = time.time()
        while time.time() - t0 < 25:
            hmain = u32.FindWindowW("ENewFrame", None)
            if hmain:
                break
            time.sleep(0.3)
        if not hmain:
            print("[STOP] no ENewFrame"); return 2
        time.sleep(2.5)
        print("main hwnd=0x%08X '%s'" % (hmain, wtext(hmain)))

        for (iid, name) in CANDS:
            before = set(tops(pid).keys())
            u32.PostMessageW(hmain, WM_COMMAND, iid, 0)
            time.sleep(1.8)
            after = tops(pid)
            new = [h for h in after if h not in before]
            dlg = [h for h in new if after[h][0] == "#32770" and after[h][2]]
            print("\nID=0x%04X (%s): 新增顶层=%d, 其中可见 #32770=%d"
                  % (iid, name, len(new), len(dlg)))
            for h in new:
                if after[h][2]:
                    print("     vis new 0x%08X '%s' '%s'" % (h, after[h][0], after[h][1]))
            for h in dlg:
                kids = children(h)
                print("     >>> 对话框 0x%08X '%s' 控件=%d" % (h, after[h][1], len(kids)))
                for (hh, c, cid, t) in kids:
                    print("         ctrl 0x%08X class='%s' id=%d text='%s'" % (hh, c, cid, t))
                u32.PostMessageW(h, WM_CLOSE, 0, 0)
                time.sleep(0.6)
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, text=True)
            try:
                proc.wait(timeout=10)
            except Exception:
                pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
