#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
a2_lib_config_dialog2.py —— A2 加强版：精确命中『工具→L.支持库配置』并用多种方式尝试打开，
再按“e.exe 本进程的所有顶层窗口 diff（不限 class）”判断是否弹出了对话框。

只读：只读菜单文本；只 PostMessage(WM_COMMAND)；只读控件；不动任何勾选；
结束 PostMessage(WM_CLOSE) + 精确 PID 杀。
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


def menu_str(hm, pos):
    n = u32.GetMenuStringW(hm, pos, None, 0, 0x00000400)
    if n <= 0:
        return ""
    b = C.create_unicode_buffer(n + 2)
    u32.GetMenuStringW(hm, pos, b, n + 1, 0x00000400)
    return b.value.replace("&", "")


def wpid(h):
    p = wt.DWORD(0)
    u32.GetWindowThreadProcessId(h, C.byref(p))
    return p.value


def top_windows_of(pid):
    out = {}

    def cb(h, lp):
        if wpid(h) == pid:
            out[h] = (cname(h), wtext(h), bool(u32.IsWindowVisible(h)))
        return True

    u32.EnumWindows(EnumProc(cb), 0)
    return out


def dump_children(h):
    res = []

    def cb(hh, lp):
        res.append((hh, cname(hh), u32.GetDlgCtrlID(hh), wtext(hh)[:60],
                    bool(u32.IsWindowVisible(hh))))
        return True

    u32.EnumChildWindows(h, EnumProc(cb), 0)
    return res


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
            print("[STOP] no ENewFrame")
            return 2
        time.sleep(2.0)
        print("main hwnd=0x%08X '%s'" % (hmain, wtext(hmain)))

        hm = u32.GetMenu(hmain)
        tools_pos = None
        for i in range(u32.GetMenuItemCount(hm)):
            if "工具" in menu_str(hm, i):
                tools_pos = i
        sub = u32.GetSubMenu(hm, tools_pos)
        ids = {}
        for j in range(u32.GetMenuItemCount(sub)):
            txt = menu_str(sub, j)
            iid = u32.GetMenuItemID(sub, j)
            ids[j] = (iid, txt)
            print("  工具(%d) id=0x%04X '%s'" % (j, iid, txt))
        target = None
        for j, (iid, txt) in ids.items():
            if "支持库配置" in txt:
                target = iid
                break
        print("==> 命中 id=0x%04X（支持库配置）" % (target or 0))

        before = top_windows_of(pid)
        print("打开前 e.exe 顶层窗口数=%d" % len(before))

        # 方式1：PostMessage
        u32.PostMessageW(hmain, WM_COMMAND, target, 0)
        time.sleep(2.0)
        new = [h for h in top_windows_of(pid) if h not in before]
        print("PostMessage 后新增顶层窗口:", [(hex(h),) + top_windows_of(pid)[h] for h in new] or "（无）")

        if not new:
            # 方式2：PostMessage 到 MDIClient 的父(帧) 亦无效时，试 SendMessageNotify
            print("再试：直接对帧 SendMessage(WM_COMMAND)（可能阻塞=模态）...")
            import threading
            done = {"ok": False}

            def sender():
                u32.SendMessageW(hmain, WM_COMMAND, target, 0)
                done["ok"] = True

            th = threading.Thread(target=sender, daemon=True)
            th.start()
            time.sleep(2.5)
            new = [h for h in top_windows_of(pid) if h not in before]
            print("SendMessage 后新增顶层窗口:", [(hex(h),) + top_windows_of(pid)[h] for h in new] or "（无）")
            print("SendMessage 是否已返回(未阻塞)=", done["ok"])

        if not new:
            print("[STOP] WM_COMMAND 两种方式都未弹出新顶层窗口 → 可能不走标准命令路由")
            # 列一下 e.exe 当前所有顶层窗口，便于判断
            for h, v in top_windows_of(pid).items():
                print("   top 0x%08X class='%s' vis=%s '%s'" % (h, v[0], v[2], v[1]))
            return 6

        dlg = new[0]
        w = top_windows_of(pid)[dlg]
        print("\n--- 对话框 0x%08X class='%s' title='%s' ---" % (dlg, w[0], w[1]))
        kids = dump_children(dlg)
        for (h, c, cid, t, vis) in kids:
            print("   ctrl 0x%08X class='%s' id=%d vis=%d text='%s'" % (h, c, cid, vis, t))
        print("控件总数=%d" % len(kids))
        print("WM_CLOSE ...")
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
