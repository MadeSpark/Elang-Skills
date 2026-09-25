#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""hook_stall_probe.py —— 直接量测「低级钩子链」的阻塞时长。

原理（关键）：
    `SendInput` 是**同步**调用 —— 注入的事件必须先被 win32k 的原始输入线程处理
    （**依次穿过整条 WH_KEYBOARD_LL / WH_MOUSE_LL 钩子链**）才返回。
    ⇒ 只要钩子链上任何一个持有者卡住，`SendInput` 就一起阻塞。
    ⇒ **测量一次 `SendInput` 的耗时 = 直接读出整条钩子链的阻塞时长。**
    不需要用户晃鼠标、不装钩子、不改设置。

注入内容是「+1px / -1px 各一次」⇒ 指针净位移为 0，肉眼不可见。
同时交叉核对 `GetLastInputInfo()` 的滞后，并记录每进程 CPU 以定位谁在忙。

⭐ 自动触发收尾：探针会在启动时记下所有 e.exe 的 pid，**一旦发现某个 e.exe 消失**
   （= 你关掉了那个易语言窗口），就在 `--after` 秒后自动结束并打印结论。
   ⇒ 你不需要掐时间，从容去关就行。

用法：
    python hook_stall_probe.py                       # 等 e.exe 退出，退出后 20s 收尾
    python hook_stall_probe.py --timeout 600 --after 20
    python hook_stall_probe.py --seconds 60          # 固定时长模式
"""
import argparse
import ctypes
import ctypes.wintypes as w
import json
import os
import statistics
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

TH32CS_SNAPPROCESS = 0x00000002
MAX_PATH = 260
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
INPUT_MOUSE = 0
MOUSEEVENTF_MOVE = 0x0001
MAGIC = 0x504B4257                      # 'WBPK' 用于标记本探针注入的事件


class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [("cbSize", w.UINT), ("dwTime", w.DWORD)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", w.LONG), ("dy", w.LONG), ("mouseData", w.DWORD),
                ("dwFlags", w.DWORD), ("time", w.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    _fields_ = [("type", w.DWORD), ("mi", MOUSEINPUT)]


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ProcessID", w.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", w.DWORD),
                ("cntThreads", w.DWORD), ("th32ParentProcessID", w.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", w.DWORD),
                ("szExeFile", ctypes.c_wchar * MAX_PATH)]


class FILETIME(ctypes.Structure):
    _fields_ = [("dwLowDateTime", w.DWORD), ("dwHighDateTime", w.DWORD)]


u32.SendInput.argtypes = [w.UINT, ctypes.POINTER(INPUT), ctypes.c_int]
u32.SendInput.restype = w.UINT
u32.GetLastInputInfo.argtypes = [ctypes.POINTER(LASTINPUTINFO)]
u32.GetCursorPos.argtypes = [ctypes.POINTER(w.POINT)]
u32.GetForegroundWindow.restype = w.HWND
k32.GetTickCount64.restype = ctypes.c_ulonglong
k32.CreateToolhelp32Snapshot.restype = w.HANDLE
k32.OpenProcess.restype = w.HANDLE
k32.CloseHandle.argtypes = [w.HANDLE]

_JIGGLE = (INPUT * 2)(
    INPUT(INPUT_MOUSE, MOUSEINPUT(1, 0, 0, MOUSEEVENTF_MOVE, 0, MAGIC)),
    INPUT(INPUT_MOUSE, MOUSEINPUT(-1, 0, 0, MOUSEEVENTF_MOVE, 0, MAGIC)),
)


def send_input_ms():
    t = time.perf_counter()
    n = u32.SendInput(2, _JIGGLE, ctypes.sizeof(INPUT))
    d = (time.perf_counter() - t) * 1000.0
    return d, n


def last_input_lag_ms():
    li = LASTINPUTINFO()
    li.cbSize = ctypes.sizeof(LASTINPUTINFO)
    if not u32.GetLastInputInfo(ctypes.byref(li)):
        return None
    return (k32.GetTickCount64() & 0xFFFFFFFF) - li.dwTime & 0xFFFFFFFF


def cursor_pos():
    p = w.POINT()
    return (p.x, p.y) if u32.GetCursorPos(ctypes.byref(p)) else None


def fg_title():
    h = u32.GetForegroundWindow()
    if not h:
        return ""
    b = ctypes.create_unicode_buffer(512)
    u32.GetWindowTextW(h, b, 512)
    return b.value


def enum_procs():
    out = {}
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if not snap or snap == w.HANDLE(-1).value:
        return out
    try:
        e = PROCESSENTRY32W()
        e.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        ok = k32.Process32FirstW(snap, ctypes.byref(e))
        while ok:
            out[int(e.th32ProcessID)] = e.szExeFile
            ok = k32.Process32NextW(snap, ctypes.byref(e))
    finally:
        k32.CloseHandle(snap)
    return out


def proc_time_ms(pid):
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return None
    try:
        c, e, kt, ut = FILETIME(), FILETIME(), FILETIME(), FILETIME()
        if not k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e),
                                   ctypes.byref(kt), ctypes.byref(ut)):
            return None
        return (((kt.dwHighDateTime << 32) | kt.dwLowDateTime) +
                ((ut.dwHighDateTime << 32) | ut.dwLowDateTime)) / 10000.0
    finally:
        k32.CloseHandle(h)


def eset(names):
    return sorted(p for p, n in names.items() if n.lower() == "e.exe")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=0, help="固定时长模式（秒）；0 = 等 e.exe 退出")
    ap.add_argument("--timeout", type=int, default=900, help="最长等待（秒）")
    ap.add_argument("--after", type=int, default=20, help="检测到 e.exe 退出后再采样多少秒")
    ap.add_argument("--interval", type=float, default=0.1)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    out = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "idiag", "stall_%s.csv" % time.strftime("%H%M%S"))
    os.makedirs(os.path.dirname(out), exist_ok=True)

    ncpu = k32.GetActiveProcessorCount(ctypes.c_ushort(0xFFFF)) or os.cpu_count() or 1
    names = enum_procs()
    base_e = eset(names)
    print("=" * 78)
    print("钩子链阻塞探针   逻辑核=%d   采样间隔=%.0fms" % (ncpu, a.interval * 1000))
    print("启动时的 e.exe = %s" % (base_e or "（无）"))
    if a.seconds:
        print("模式：固定 %d 秒" % a.seconds)
    else:
        print("模式：⭐ 等到某个 e.exe 消失（=你关掉了那个易语言窗口）后再采样 %d 秒；"
              "最长 %d 秒" % (a.after, a.timeout))
    print("⇒ 你现在就可以从容地去关那个易语言窗口，探针会自己发现并收尾。")
    print("   （探针只做一次性注入 1px 鼠标抖动，净位移 0，不影响你操作）")
    print("=" * 78)

    rows = []
    prev_cpu, prev_tick, last_cpu = {}, None, 0.0
    last_top, last_total = "", 0.0
    last_names = 0.0
    # 预热：前几次注入会偏大（钩子持有者线程首次被唤醒/换入），属启动伪影 ⇒ 丢弃
    warm = [send_input_ms()[0] for _ in range(6)]
    time.sleep(0.3)
    print("  预热 SendInput 耗时 = %s ms（偏大属启动伪影，已丢弃）\n"
          % ", ".join("%.1f" % x for x in warm))
    t0 = time.perf_counter()
    exit_ev = None
    stop_at = None
    while True:
        now = time.perf_counter()
        el = now - t0
        if stop_at is not None and el >= stop_at:
            break
        if stop_at is None:
            if a.seconds and el >= a.seconds:
                break
            if el >= a.timeout:
                print("  ⏱ 达到最长等待 %ds，未观察到 e.exe 退出。" % a.timeout)
                break

        sq = time.perf_counter()
        send_ms, nsent = send_input_ms()
        overhead = (time.perf_counter() - sq) * 1000.0
        lag = last_input_lag_ms()
        tick = k32.GetTickCount64()
        cur = cursor_pos()
        fg = fg_title()

        if now - last_names > 2.0:
            names = enum_procs()
            last_names = now
        e_now = eset(names)
        if exit_ev is None and base_e and len(e_now) < len(base_e):
            gone = sorted(set(base_e) - set(e_now))
            exit_ev = {"t_s": round(el, 2), "gone": gone, "left": e_now}
            print("\n  ⭐⭐ [%.2fs] 检测到 e.exe 退出：%s   （剩余 %s）⇒ 再采样 %ds 后收尾\n"
                  % (el, gone, e_now, a.after))
            stop_at = el + a.after

        if prev_tick is None:
            prev_tick = tick
        if now - last_cpu >= 0.5:
            dt = tick - prev_tick
            prev_tick = tick
            pct = {}
            for pid in list(names.keys()):
                t = proc_time_ms(pid)
                if t is None:
                    continue
                o = prev_cpu.get(pid)
                if o is not None and dt > 0:
                    pct[pid] = max(0.0, (t - o) / dt / ncpu * 100.0)
                prev_cpu[pid] = t
            tp = sorted(pct.items(), key=lambda kv: -kv[1])[:3]
            last_top = " | ".join("%s(%d)=%.0f%%" % (names.get(p, "?"), p, v) for p, v in tp)
            last_total = round(sum(pct.values()), 1)
            last_cpu = now

        rows.append({"t_ms": int(el * 1000), "send_ms": round(send_ms, 2),
                     "sent": nsent, "overhead_ms": round(overhead, 2), "lag_ms": lag,
                     "cursor": cur, "cpu_total": last_total, "top": last_top,
                     "e_pids": e_now, "fg": fg})
        if send_ms > 150 or (lag or 0) > 800:
            print("  [%7.2fs] ⛔ SendInput 阻塞 %8.1f ms   lag=%6d ms   总CPU %5.1f%%   %s"
                  % (el, send_ms, lag or -1, last_total, last_top))
            sys.stdout.flush()

        rest = a.interval - (time.perf_counter() - now)
        if rest > 0:
            time.sleep(rest)

    with open(out, "w", encoding="utf-8") as f:
        f.write("t_ms,send_ms,sent,overhead_ms,lag_ms,cx,cy,cpu_total,top,e_pids,fg\n")
        for r in rows:
            f.write("%d,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n" % (
                r["t_ms"], r["send_ms"], r["sent"], r["overhead_ms"], r["lag_ms"],
                r["cursor"][0] if r["cursor"] else "", r["cursor"][1] if r["cursor"] else "",
                r["cpu_total"], r["top"].replace(",", ";"),
                "|".join(map(str, r["e_pids"])), r["fg"].replace(",", ";")))

    # ------------------------- 分析 -------------------------
    print("\n" + "=" * 78)
    print("结论")
    print("=" * 78)
    sends = [r["send_ms"] for r in rows if r["sent"] == 2]
    med = statistics.median(sends) if sends else float("nan")
    p99 = sorted(sends)[int(len(sends) * 0.99)] if sends else float("nan")
    print("  采样 %d 点；注入成功 %d 次。" % (len(rows), len(sends)))
    print("  SendInput 耗时：中位 = %.2f ms   常态 99 分位 = %.2f ms   峰值 = %.1f ms" % (
        med, p99, max(sends) if sends else -1))
    print("  ⇒ 中位数就是「钩子链在正常状态的额外延迟」；峰值 = 卡死时长。")

    segs, cur = [], None
    for i, r in enumerate(rows):
        s = r["send_ms"]
        if cur is not None:
            cur["to"] = r["t_ms"]
            cur["max"] = max(cur["max"], s)
            if r["top"]:
                cur["top"] = r["top"]
            if s <= 150:
                segs.append(cur)
                cur = None
            continue
        hist = [x["send_ms"] for x in rows[max(0, i - 10):i]]
        if s > 150 and hist and min(hist) < 30:
            cur = {"from": r["t_ms"], "to": r["t_ms"], "max": s, "top": r["top"],
                   "base": min(hist), "e": r["e_pids"]}
    if cur:
        segs.append(cur)

    if not segs:
        print("\n  ❌ 未观察到任何「SendInput 阻塞 > 150ms」的区间 ⇒ 本次没复现。")
    for s in segs:
        print("\n  ⛔ 输入管线阻塞：%.2fs ~ %.2fs  持续 %d ms  峰值 %.1f ms" % (
            s["from"] / 1000.0, s["to"] / 1000.0, s["to"] - s["from"], s["max"]))
        print("     阻塞前的基线耗时 = %.2f ms  ⇒ 「从正常突然变成秒级」= 真卡死而非空闲" % s["base"])
        print("     该区间内 CPU 前三 = %s" % s["top"])
        if exit_ev:
            rel = (s["from"] - exit_ev["t_s"] * 1000) / 1000.0
            print("     ⭐ 相对 e.exe 退出（%.2fs）的偏移 = %+.2f s" % (exit_ev["t_s"], rel))
        print("     该区间 e.exe = %s" % s["e"])
    if exit_ev:
        print("\n  e.exe 退出事件：%.2fs  消失的 pid = %s  剩余 = %s" % (
            exit_ev["t_s"], exit_ev["gone"], exit_ev["left"]))
    else:
        print("\n  （全程未观察到 e.exe 退出）")
    with open(out.replace(".csv", ".segments.json"), "w", encoding="utf-8") as f:
        json.dump({"exit": exit_ev, "segments": segs, "median_send_ms": med,
                   "p99_send_ms": p99}, f, ensure_ascii=False, indent=2)
    print("\n原始数据：%s" % out)


if __name__ == "__main__":
    main()
