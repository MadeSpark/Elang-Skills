#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
a2_open_via_keys.py —— A2：用真实键盘助记符(Alt+T → L)打开『工具→支持库配置』，
再枚举其控件（判定是不是标准 Win32 控件、能否程序化枚举/勾选）。
只读：不点击、不改勾选；结束 WM_CLOSE + 精确 PID 杀。
"""
import ctypes as C
import ctypes.wintypes as wt
import subprocess
import time

u32 = C.windll.user32

E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
WM_SYSCOMMAND = 0x0112
SC_KEYMENU = 0xF100
WM_CLOSE = 0x0010
WM_KEYDOWN, WM_KEYUP = 0x0100, 0x0101
VK_MENU, VK_T, VK_L, VK_ESCAPE = 0x12, 0x54, 0x4C, 0x1B

KEYEVENTF_KEYUP = 0x0002
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
        res.append((hh, cname(hh), u32.GetDlgCtrlID(hh), wtext(hh)[:70],
                    bool(u32.IsWindowVisible(hh)), bool(u32.IsWindowEnabled(hh))))
        return True
    u32.EnumChildWindows(h, EnumProc(cb), 0)
    return res


def key(vk, up=False):
    u32.keybd_event(vk, 0, KEYEVENTF_KEYUP if up else 0, 0)


def alt_mnemonic(mn):
    key(VK_MENU); key(mn)
    time.sleep(0.05)
    key(mn, True); key(VK_MENU, True)
    time.sleep(0.15)


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
        time.sleep(2.0)
        print("main hwnd=0x%08X '%s'" % (hmain, wtext(hmain)))

        before = set(tops(pid).keys())

        # 让 e.exe 到前台（失败也无妨，再补 WM_SYSCOMMAND 触发 Alt）
        u32.SetForegroundWindow(hmain)
        u32.SetFocus(hmain)
        time.sleep(0.4)

        print("方式A：真实键 Alt+T 打开『工具』...")
        alt_mnemonic(VK_T)
        time.sleep(0.06)
        aftersub = set(tops(pid).keys())
        print("  新窗口(菜单弹出):", [(hex(h), tops(pid)[h][0], tops(pid)[h][1]) for h in aftersub - before] or "（无）")

        print("方式A：按 L 选『支持库配置』...")
        key(VK_L); key(VK_L, True)
        time.sleep(2.0)
        afterA = set(tops(pid).keys())
        newA = afterA - before
        print("  新顶层窗口:", [(hex(h),) + tops(pid)[h] for h in newA] or "（无）")

        if not newA:
            print("方式B：PostMessage(hMain, WM_SYSCOMMAND, SC_KEYMENU, 'T') 再按键 L ...")
            u32.PostMessageW(hmain, WM_SYSCOMMAND, SC_KEYMENU, ord("T"))
            time.sleep(0.5)
            u32.PostMessageW(hmain, WM_KEYDOWN, VK_L, 0)
            u32.PostMessageW(hmain, WM_KEYUP, VK_L, 0)
            time.sleep(2.0)
            newA = set(tops(pid).keys()) - before
            print("  新顶层窗口:", [(hex(h),) + tops(pid)[h] for h in newA] or "（无）")

        if not newA:
            print("[STOP] 未能打开『支持库配置』对话框（键盘助记符无效）")
            for h, v in tops(pid).items():
                print("   top 0x%08X class='%s' vis=%s '%s'" % (h, v[0], v[2], v[1]))
            return 6

        dlg = sorted(newA)[0]
        # 选一个“看起来最像对话框”的：优先 #32770
        cand = [h for h in newA if tops(pid)[h][0] == "#32770"]
        dlg = cand[0] if cand else dlg
        w = tops(pid)[dlg]
        print("\n--- 对话框 0x%08X class='%s' title='%s' ---" % (dlg, w[0], w[1]))
        kids = children(dlg)
        for (h, c, cid, t, vis, en) in kids:
            print("   ctrl 0x%08X class='%-22s' id=%-6d vis=%d en=%d text='%s'"
                  % (h, c, cid, vis, en, t))
        print("控件总数=%d" % len(kids))
        cls = {}
        for (_h, c, _i, _t, _v, _e) in kids:
            cls[c] = cls.get(c, 0) + 1
        print("控件 class 直方图:", cls)
        print("WM_CLOSE 关闭 ...")
        u32.PostMessageW(dlg, WM_CLOSE, 0, 0)
        time.sleep(0.5)
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
