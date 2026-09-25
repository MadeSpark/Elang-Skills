#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""input_blackout_probe.py —— **完全被动**的「整机输入黑洞」探针。

不装钩子、不注入任何输入、不改任何设置，只做只读采样：

  ① `GetLastInputInfo()` → 系统「最后一次处理完输入」的时刻。
     正常时它会紧跟当前时间（活跃时 lag < 50ms）；
     **win32k 的原始输入线程被低级钩子卡住时它会停止推进** ⇒ lag 单调上涨到秒级 = 输入黑洞。
  ② `GetCursorPos()`  → 冻结期鼠标指针位置不再变化，恢复后可能一次跳很远。
  ③ 每进程 CPU 占用（GetProcessTimes 差分）+ 总 CPU → 定位**是谁在忙**。
  ④ 前台窗口标题 + e.exe 存活情况：捕获「e.exe 退出」这一时刻。
  ⑤ 本线程自身的调度抖动（sleep 抖动）→ 用于区分「输入被卡」还是「整机 CPU 被抢」。

用法：
    python input_blackout_probe.py --seconds 90
    python input_blackout_probe.py --seconds 90 --wait-enter   # 回车后才开始计时

**复现要求**：开始后请
   1) 保存好工程；
   2) 关掉那个易语言窗口（最小化/关闭工程/退出 IDE 都行）；
   3) **卡住的这几秒里请持续晃动鼠标 + 乱敲空格**（否则探针无法区分「你没动」和「系统被卡」）；
   4) 结束前不用管别的。
"""
import argparse
import ctypes
import ctypes.wintypes as w
import json
import os
import sys
import time

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

TH32CS_SNAPPROCESS = 0x00000002
MAX_PATH = 260
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000


class LASTINPUTINFO(ctypes.Structure):
    _fields_ = [("cbSize", w.UINT), ("dwTime", w.DWORD)]


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ProcessID", w.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", w.DWORD),
                ("cntThreads", w.DWORD), ("th32ParentProcessID", w.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", w.DWORD),
                ("szExeFile", ctypes.c_wchar * MAX_PATH)]


class FILETIME(ctypes.Structure):
    _fields_ = [("dwLowDateTime", w.DWORD), ("dwHighDateTime", w.DWORD)]


u32.GetLastInputInfo.argtypes = [ctypes.POINTER(LASTINPUTINFO)]
u32.GetCursorPos.argtypes = [ctypes.POINTER(w.POINT)]
u32.GetForegroundWindow.restype = w.HWND
u32.GetWindowTextW.argtypes = [w.HWND, ctypes.c_wchar_p, ctypes.c_int]
k32.GetTickCount64.restype = ctypes.c_ulonglong
k32.CreateToolhelp32Snapshot.restype = w.HANDLE
k32.OpenProcess.restype = w.HANDLE
k32.CloseHandle.argtypes = [w.HANDLE]


def last_input_lag_ms():
    """返回 (lag_ms, TICK32)。lag 为「距最后一次被处理的输入」过去的毫秒数。"""
    li = LASTINPUTINFO()
    li.cbSize = ctypes.sizeof(LASTINPUTINFO)
    if not u32.GetLastInputInfo(ctypes.byref(li)):
        return None, None
    tick32 = k32.GetTickCount64() & 0xFFFFFFFF
    return (tick32 - li.dwTime) & 0xFFFFFFFF, tick32


def cursor_pos():
    p = w.POINT()
    if u32.GetCursorPos(ctypes.byref(p)):
        return (p.x, p.y)
    return None


def fg_title():
    h = u32.GetForegroundWindow()
    if not h:
        return ""
    buf = ctypes.create_unicode_buffer(512)
    u32.GetWindowTextW(h, buf, 512)
    return buf.value


def enum_procs():
    """返回 {pid: name}"""
    out = {}
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == w.HANDLE(-1).value or snap is None:
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
    """返回 (kernel+user) 毫秒，失败返回 None"""
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return None
    try:
        c, e, kt, ut = FILETIME(), FILETIME(), FILETIME(), FILETIME()
        if not k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e),
                                   ctypes.byref(kt), ctypes.byref(ut)):
            return None
        kk = (kt.dwHighDateTime << 32) | kt.dwLowDateTime
        uu = (ut.dwHighDateTime << 32) | ut.dwLowDateTime
        return (kk + uu) / 10000.0        # 100ns → ms
    finally:
        k32.CloseHandle(h)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=90)
    ap.add_argument("--interval", type=float, default=0.1)
    ap.add_argument("--cpu-every", type=float, default=0.5,
                    help="每进程 CPU 的采样间隔（秒）；调大可减小探针自身开销")
    ap.add_argument("--wait-enter", action="store_true")
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    out = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "idiag", "probe_%s.csv" % time.strftime("%H%M%S"))
    os.makedirs(os.path.dirname(out), exist_ok=True)

    if a.wait_enter:
        input(">>> 按回车开始采样（然后就去关那个易语言窗口）...")

    ncpu = k32.GetActiveProcessorCount(ctypes.c_ushort(0xFFFF))   # ALL_PROCESSOR_GROUPS
    if not ncpu or ncpu < 0:
        ncpu = os.cpu_count() or 1
    rows = []
    prev_cpu = {}          # pid -> ms
    prev_tick = None
    names = enum_procs()
    last_names = 0.0
    prev_cursor = cursor_pos()
    last_cpu = 0.0
    last_top, last_total = "", 0.0
    print("采样 %d 秒，间隔 %.0f ms，逻辑核 %d；写出 %s" % (a.seconds, a.interval * 1000, ncpu, out))
    print("请开始关易语言窗口，并在卡住时**持续晃鼠标 + 敲空格**。\n")

    t0 = time.perf_counter()
    while True:
        now = time.perf_counter()
        if now - t0 >= a.seconds:
            break
        tick = k32.GetTickCount64()
        lag, tick32 = last_input_lag_ms()

        # 本线程 sleep 抖动（区分「输入被卡」vs「整机 CPU 被抢」）
        expect = a.interval
        tt = time.perf_counter()
        time.sleep(a.interval)
        actual = time.perf_counter() - tt
        jitter = (actual - expect) * 1000.0

        cur = cursor_pos()
        moved = (cur != prev_cursor)
        prev_cursor = cur

        if now - last_names > 2.0:
            names = enum_procs()
            last_names = now

        # CPU 差分（降频采样：轮询数百进程很贵，不能每 100ms 做一次）
        if prev_tick is None:
            prev_tick = tick
        if now - last_cpu >= a.cpu_every:
            dt = tick - prev_tick
            prev_tick = tick
            cpu_pct = {}
            for pid in list(names.keys()):
                t = proc_time_ms(pid)
                if t is None:
                    continue
                old = prev_cpu.get(pid)
                if old is not None and dt > 0:
                    cpu_pct[pid] = max(0.0, (t - old) / dt / ncpu * 100.0)
                prev_cpu[pid] = t
            top = sorted(cpu_pct.items(), key=lambda kv: -kv[1])[:3]
            last_top = " | ".join("%s(%d)=%.0f%%" % (names.get(p, "?"), p, v) for p, v in top)
            last_total = round(sum(cpu_pct.values()), 1)
            last_cpu = now
        top_s, total = last_top, last_total

        enames = [p for p, n in names.items() if n.lower() == "e.exe"]
        rows.append({
            "t_ms": int((time.perf_counter() - t0) * 1000),
            "lag_ms": lag, "jitter_ms": round(jitter, 1),
            "cursor": cur, "moved": moved,
            "cpu_total": round(total, 1), "top": top_s,
            "e_pids": enames, "fg": fg_title(),
        })
        # ⚠️ 只有「紧邻的上 1 秒里系统还很活跃（lag<150ms）」之后 lag 突然涨到秒级，
        #    才判为输入黑洞 —— 排除「用户只是没在动鼠标/键盘」这一空闲假阳性。
        hist = [x["lag_ms"] for x in rows[-11:-1] if x["lag_ms"] is not None]
        active_before = bool(hist) and min(hist) < 150
        if lag is not None and lag > 600 and active_before:
            print("  [%7.1fs] ⛔ 输入滞后 %5d ms  （前1秒最小滞后 %d ms ⇒ 之前很活跃）"
                  "  本线程抖动 %6.1f ms  总CPU %5.1f%%  %s  e.exe=%s"
                  % (rows[-1]["t_ms"] / 1000, lag, min(hist), jitter, total, top_s, enames))
        sys.stdout.flush()

    with open(out, "w", encoding="utf-8") as f:
        f.write("t_ms,lag_ms,jitter_ms,cursor_x,cursor_y,moved,cpu_total,top,e_pids,fg\n")
        for r in rows:
            f.write("%d,%s,%s,%s,%s,%d,%s,%s,%s,%s\n" % (
                r["t_ms"], r["lag_ms"], r["jitter_ms"],
                r["cursor"][0] if r["cursor"] else "", r["cursor"][1] if r["cursor"] else "",
                1 if r["moved"] else 0, r["cpu_total"],
                r["top"].replace(",", ";"), "|".join(map(str, r["e_pids"])),
                r["fg"].replace(",", ";")))

    # ---------------- 分析（门槛：必须「之前很活跃」，排除空闲假阳性） ----------------
    print("\n" + "=" * 78)
    print("分析：输入黑洞区间")
    print("=" * 78)
    lags = [r["lag_ms"] for r in rows if r["lag_ms"] is not None]
    n_active = sum(1 for x in lags if x < 150)
    print("  全程 %d 个采样点，其中 lag<150ms（=你在活跃操作）的有 %d 个（%.0f%%）" % (
        len(lags), n_active, 100.0 * n_active / max(1, len(lags))))
    if n_active < 10:
        print("  ⚠️ 活跃采样太少 ⇒ 本次复现没法判定。请在卡住期间**持续晃动鼠标 + 敲空格**。")
    segs = []
    cur = None
    for i, r in enumerate(rows):
        lag = r["lag_ms"] or 0
        if cur is not None:
            cur["to"] = r["t_ms"]
            cur["cursor"] = r["cursor"]
            cur["max"] = max(cur["max"], lag)
            cur["jmax"] = max(cur["jmax"], r["jitter_ms"])
            if r["e_pids"] != cur["e"]:
                cur.setdefault("e_change", []).append((list(cur["e"]), list(r["e_pids"])))
                cur["e"] = list(r["e_pids"])
            if r["top"]:
                cur["top"] = r["top"]
            if lag <= 600:
                segs.append(cur)
                cur = None
            continue
        hist = [x["lag_ms"] for x in rows[max(0, i - 10):i] if x["lag_ms"] is not None]
        if lag > 600 and hist and min(hist) < 150:
            cur = {"from": r["t_ms"], "to": r["t_ms"], "max": lag, "e": list(r["e_pids"]),
                   "top": r["top"], "jmax": r["jitter_ms"],
                   "cursor0": r["cursor"], "active_before_ms": min(hist)}
    if cur:
        segs.append(cur)

    if not segs:
        print("  （未观察到合格的「输入黑洞」区间）")
    for s in segs:
        dur = s["to"] - s["from"]
        jump = ""
        if s.get("cursor0") and s.get("cursor"):
            jump = "  指针位移=%.0fpx" % (((s["cursor"][0] - s["cursor0"][0]) ** 2 +
                                       (s["cursor"][1] - s["cursor0"][1]) ** 2) ** 0.5)
        print("\n  ⛔ 输入黑洞：%.1fs ~ %.1fs  持续 %d ms  峰值滞后 %d ms%s" % (
            s["from"] / 1000.0, s["to"] / 1000.0, dur, s["max"], jump))
        print("     开始前的活跃度：前 1 秒最小滞后 = %d ms" % s["active_before_ms"])
        print("     ⭐ 本线程最大抖动 = %.1f ms ← 这个值小 ⇒ **不是 CPU 被抢，是输入子系统被卡**" % s["jmax"])
        print("     该区间内 CPU 前三 = %s" % s["top"])
        if "e_change" in s:
            for a, b in s["e_change"]:
                print("     ⭐⭐ e.exe 进程列表在此区间变化：%s → %s" % (a, b))
        else:
            print("     e.exe 进程列表 = %s" % s["e"])
    with open(out.replace(".csv", ".segments.json"), "w", encoding="utf-8") as f:
        json.dump(segs, f, ensure_ascii=False, indent=2)
    print("\n原始数据：%s" % out)


if __name__ == "__main__":
    main()
