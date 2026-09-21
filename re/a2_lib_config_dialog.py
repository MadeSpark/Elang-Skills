#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
a2_lib_config_dialog.py —— 任务A2：判定「工具→支持库配置」对话框能否被程序化打开/枚举。

只读原则：
  · 只 GetMenu/GetSubMenu/GetMenuString 读菜单文本；
  · 只 PostMessage(hMain, WM_COMMAND, id, 0) 打开对话框；
  · 对话框只 EnumChildWindows + GetClassName/GetWindowText + GetDlgCtrlID 读控件；
  · **绝不**点击/改勾选；结束后 PostMessage(dlg, WM_CLOSE) 关窗 + 精确 PID 杀 e.exe。
"""
import ctypes as C
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

u32 = C.windll.user32
k32 = C.windll.kernel32

E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"

WM_COMMAND = 0x0111
WM_CLOSE = 0x0010

EnumWindowsProc = C.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
EnumChildProc = C.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def wtext(h):
    n = u32.GetWindowTextLengthW(h)
    buf = C.create_unicode_buffer(n + 2)
    u32.GetWindowTextW(h, buf, n + 1)
    return buf.value


def cname(h):
    buf = C.create_unicode_buffer(256)
    u32.GetClassNameW(h, buf, 255)
    return buf.value


def menu_str(hmenu, pos):
    n = u32.GetMenuStringW(hmenu, pos, None, 0, 0x00000400)  # MF_BYPOSITION
    if n <= 0:
        return ""
    buf = C.create_unicode_buffer(n + 2)
    u32.GetMenuStringW(hmenu, pos, buf, n + 1, 0x00000400)
    txt = buf.value
    # 去掉 & 快捷键标记与 \t
    return txt.replace("&", "")


def find_main(timeout=25.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        h = u32.FindWindowW("ENewFrame", None)
        if h:
            return h
        time.sleep(0.3)
    return None


def open_std_dialogs_of(hmain):
    """列出当前 e.exe 所有可见的 #32770 顶层对话框（辅助判断是否有启动对话框挡住）。"""
    out = []

    def cb(h, lp):
        if u32.IsWindowVisible(h):
            cls = cname(h)
            if cls in ("#32770",):
                out.append((h, cls, wtext(h)))
        return True

    u32.EnumWindows(EnumWindowsProc(cb), 0)
    return out


def main():
    proc = subprocess.Popen([E_EXE], cwd=E_DIR,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    pid = proc.pid
    try:
        print("launched e.exe pid=%d" % pid)
        hmain = find_main(25)
        if not hmain:
            print("[STOP] 未找到主窗口 ENewFrame")
            return 2
        print("main hwnd=0x%08X class='%s' text='%s'" % (hmain, cname(hmain), wtext(hmain)))
        time.sleep(2.0)  # 让 IDE 初始化完菜单

        hmenu = u32.GetMenu(hmain)
        print("GetMenu(hMain)=0x%08X" % (hmenu or 0))
        if not hmenu:
            print("[STOP] 主窗口没有标准 HMENU → 菜单可能是自绘/自定义控件，无法用 WM_COMMAND 直达")
            return 3

        n = u32.GetMenuItemCount(hmenu)
        print("top-level menu items = %d" % n)
        tools_pos = None
        for i in range(n):
            txt = menu_str(hmenu, i)
            sub = u32.GetSubMenu(hmenu, i)
            print("  [%d] '%s'  sub=0x%08X" % (i, txt, sub or 0))
            if "工具" in txt:
                tools_pos = i
        if tools_pos is None:
            print("[STOP] 顶层菜单中找不到『工具』")
            return 4

        sub = u32.GetSubMenu(hmenu, tools_pos)
        m = u32.GetMenuItemCount(sub)
        print("工具 子菜单项数 = %d" % m)
        target_id = None
        for j in range(m):
            txt = menu_str(sub, j)
            iid = u32.GetMenuItemID(sub, j)
            sep = (iid == 0xFFFFFFFF) or (iid == 0)
            print("  (%d) id=%s '%s'%s" % (j, ("0x%04X" % iid) if not sep else "----", txt,
                                           "  <== 命中支持库配置" if ("支持库" in txt) else ""))
            if ("支持库" in txt) and not sep:
                target_id = iid
        if target_id is None:
            print("[STOP] 工具菜单里找不到『支持库…』项")
            return 5

        # 记录已有 #32770（避免与启动对话框混淆）
        before = set(h for (h, _, _) in open_std_dialogs_of(hmain))
        print("PostMessage(hMain, WM_COMMAND, 0x%04X, 0)" % target_id)
        u32.PostMessageW(hmain, WM_COMMAND, target_id, 0)
        time.sleep(1.5)

        after = open_std_dialogs_of(hmain)
        newdlg = [t for t in after if t[0] not in before]
        print("新出现的顶层对话框:", [(hex(h), c, t) for (h, c, t) in newdlg] or "（无）")

        # 无论是否叫 #32770，都把可能是对话框的窗口枚举一遍
        targets = newdlg if newdlg else after
        if not targets:
            print("[STOP] 按 WM_COMMAND 后没有出现新顶层窗口（该菜单项可能不走 WM_COMMAND 路由）")
            return 6

        dlg = targets[0][0]
        print("\n--- 对话框 hwnd=0x%08X class='%s' title='%s' 控件枚举 ---"
              % (dlg, cname(dlg), wtext(dlg)))
        ctrl = []

        def ccb(h, lp):
            ctrl.append((h, cname(h), u32.GetDlgCtrlID(h), wtext(h)[:60]))
            return True

        u32.EnumChildWindows(dlg, EnumChildProc(ccb), 0)
        for (h, c, cid, t) in ctrl:
            print("   ctrl hwnd=0x%08X class='%s' id=%d text='%s'" % (h, c, cid, t))
        print("控件总数 = %d" % len(ctrl))

        # 关闭对话框（不点确定）
        print("WM_CLOSE 关闭对话框 ...")
        u32.PostMessageW(dlg, WM_CLOSE, 0, 0)
        time.sleep(0.5)
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                           capture_output=True, text=True)
            try:
                proc.wait(timeout=10)
            except Exception:
                pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
