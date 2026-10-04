# -*- coding: utf-8 -*-
"""端到端验证输入通路：**真鼠标点击** 客户端窗口 → 转发到辅助进程 → 内核 → 页面 JS。

判据（三条都要成立）：
  1. 客户端日志里出现 "客户区屏幕原点=(x,y)"（知道往哪点）
  2. 辅助进程日志里出现 [in] WM_LBUTTONDOWN/WM_LBUTTONUP（输入真的过桥了）
  3. 页面 JS 把点击次数写进 document.title，辅助进程回传，客户端日志里 title 变成 "CLICK 3"

用法: python verify_input.py
"""
import ctypes
import ctypes.wintypes as wt
import os
import re
import subprocess
import sys
import time

u32 = ctypes.windll.user32
HERE = os.path.dirname(os.path.abspath(__file__))
NODE = r"D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\main\corn\node.dll"
URL = "file:///" + HERE.replace("\\", "/") + "/spike.html"
TAG = "in0"
CLI_LOG = os.path.join(HERE, "spike_cli_%s.log" % TAG)
HOST_LOG = os.path.join(HERE, "spike_host_%s.log" % TAG)


def read(path):
    if not os.path.exists(path):
        return ""
    raw = open(path, "rb").read()
    for enc in ("utf-8", "gbk"):          # 新代码是 UTF-8；老日志是 GBK
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "ignore")


def wait_for(path, needle, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if needle in read(path):
            return True
        time.sleep(0.2)
    return False


def parse_origin(log):
    m = re.findall(r"客户区屏幕原点=\((\d+),(\d+)\)", log)
    return (int(m[-1][0]), int(m[-1][1])) if m else None


def click(hwnd, x, y):
    """把鼠标消息**投递给客户端窗口**（不走真实桌面）：
    这就是客户端桩实际要走的那条路 —— 窗口过程 → 输入环 → 辅助进程 → wkeFireWindowsMessage。
    不用真鼠标是因为测试环境里测试窗口被 IDE 盖住，真点击会落到别人的窗口上。"""
    lp = (y & 0xFFFF) << 16 | (x & 0xFFFF)
    r = []
    r.append(u32.PostMessageW(hwnd, 0x0200, 0, lp))          # WM_MOUSEMOVE
    time.sleep(0.03)
    r.append(u32.PostMessageW(hwnd, 0x0201, 0x0001, lp))     # WM_LBUTTONDOWN (MK_LBUTTON)
    time.sleep(0.05)
    r.append(u32.PostMessageW(hwnd, 0x0202, 0, lp))          # WM_LBUTTONUP
    return r


def main():
    u32.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
    u32.FindWindowW.restype = ctypes.c_void_p
    u32.PostMessageW.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                 ctypes.c_size_t, ctypes.c_ssize_t]
    u32.PostMessageW.restype = ctypes.c_int
    u32.GetWindowThreadProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(wt.DWORD)]
    u32.IsWindow.argtypes = [ctypes.c_void_p]

    for f in (CLI_LOG, HOST_LOG):
        if os.path.exists(f):
            os.remove(f)

    exe = os.path.join(HERE, "spike_cli.exe")
    p = subprocess.Popen([exe, NODE, URL, "700", "420", "14000", TAG,
                          "--bmp", os.path.join(HERE, "shot_in0")], cwd=HERE)
    print("client pid=%d" % p.pid)

    if not wait_for(CLI_LOG, "客户区屏幕原点=", 15):
        print("!! 没等到客户端窗口"); return 1
    time.sleep(0.8)

    hwnd = 0
    m = re.findall(r"窗口 hwnd=([0-9a-fA-F]+)", read(CLI_LOG))
    if m:
        hwnd = int(m[-1], 16)                       # 客户端自己打的 hwnd，最可靠
    if not hwnd or not u32.IsWindow(hwnd):
        hwnd = u32.FindWindowW("MbKitSpikeCliWnd", None)
    owner = wt.DWORD(0)
    u32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
    print("客户端窗口 hwnd=0x%08X  owner-pid=%d (client pid=%d) IsWindow=%d"
          % (hwnd, owner.value, p.pid, u32.IsWindow(hwnd)))
    if owner.value != p.pid:
        print("!! hwnd 不属于客户端进程，投递必然无效"); return 1

    for i in range(3):
        r = click(hwnd, 350, 200)
        print("  第 %d 次点击: PostMessage 返回 %s" % (i + 1, r))
        time.sleep(0.6)

    try:
        p.wait(timeout=40)
    except subprocess.TimeoutExpired:
        p.kill()

    log = read(CLI_LOG)
    hlog = read(HOST_LOG)
    host_ok = ("WM_LBUTTONDOWN" in hlog) or ("0x0201" in hlog)
    titles = re.findall(r'title="([^"]*)"', log)
    got3 = any("CLICK 3" in t for t in titles)
    n_fwd = re.findall(r"输入转发=(\d+)", log)
    print("\n== 证据 ==")
    print("  1) 客户端转发后的输入事件总数: %s" % (n_fwd[-1] if n_fwd else "?"))
    print("  2) 辅助进程日志里出现鼠标按下: %s" % ("是" if host_ok else "否"))
    print("  3) 客户端日志里的 title 序列: %s" % (titles[-4:] or "(无)"))
    print("  4) 页面 JS 收到 3 次点击:     %s" % ("是" if got3 else "否"))
    ok = host_ok and got3
    print("\n结论: %s" % ("PASS ✔ 输入通路打通（窗口过程 → 跨进程 → 页面 JS → 回传）" if ok else "FAIL ✘"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
