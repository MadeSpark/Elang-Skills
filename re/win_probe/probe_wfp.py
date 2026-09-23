#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_wfp.py —— 判定「屏幕上是否真的显示了某进程的窗口」：不采像素，问窗口管理器。

## 为什么不用截屏逐像素（measure_visible.py）

全屏截屏会被**别的东西**污染 —— 用户自己在动鼠标、切窗口、弹通知，
"跑前/跑后双基线 + mask" 也挡不住。实测过：

    第 1 次：mask 吞掉 99.9% 网格 → 差异 0（**假通过**）
    第 2 次：mask 只 0.8%     → 差异 37688（真露脸）
    第 3 次：mask 只 0.7%     → 差异 24437（真露脸）

同一份代码三次结论互相矛盾 ⇒ **全屏像素法在有人用的桌面上不可用**。

## 本工具的做法

对屏幕上的一张网格，逐点问 `WindowFromPoint`：**这个点上是谁的窗口？**
再用 `GetWindowThreadProcessId` 判它属于哪个进程，与目标进程比对。

* 不采任何像素 ⇒ **别的程序怎么动都无所谓**（它们返回的是自己的 hwnd，归属不是目标 pid）；
* 语义恰好是我们想要的：`WindowFromPoint` 会**跳过**隐藏窗口和 `WS_EX_TRANSPARENT`/全透明分层窗口，
  所以"返回了目标窗口"≈"用户在那个点上真的看得见它"；
* 唯一的偏差是**保守的**：目标窗口被别的窗口盖住时会漏计（少数），不会多计。

## 用法

    python probe_wfp.py <exe名,如 e.exe> <观察秒数> [--step 20] [--out 日志]

并联跑：一边起本工具，一边跑 ecompile/elaunch，最后看 "峰值点数" 与时间序列。
    `--step` 是网格间距（像素）。默认 20 —— 窗口最小也有几百像素宽，绝不会漏。

## ⚠️ 只统计「本次新起的 pid」，绝不按进程名算

2026-09-22 血的教训：一开始按**进程名** `e.exe` 匹配归属，结果**用户自己开着的那个
易语言**被一起算进来，屏幕上冒出 `ENewFrame 'AILI·LIFE - D:\\DevelopmentFolder\\插件开发目录\\'`
连续 13.7 秒 —— 看着像"无头彻底失效"，其实是**别人的窗口**。
现在固定：开场先记下已有的 `e.exe` pid 当基线，只有**运行期间新出现的**那些才计入。
"""
import argparse
import ctypes
import io
import os
import sys
import time
from ctypes import wintypes as wt

u32 = ctypes.WinDLL("user32", use_last_error=True)
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)

PROCESS_QUERY_LIMITED_INFORMATION = 0x1000

u32.WindowFromPoint.argtypes = [wt.POINT]
u32.WindowFromPoint.restype = wt.HWND
u32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
u32.GetWindowThreadProcessId.restype = wt.DWORD
u32.GetAncestor.argtypes = [wt.HWND, wt.UINT]
u32.GetAncestor.restype = wt.HWND
u32.GetClassNameA.argtypes = [wt.HWND, ctypes.c_char_p, ctypes.c_int]
u32.GetClassNameA.restype = ctypes.c_int
u32.GetWindowTextA.argtypes = [wt.HWND, ctypes.c_char_p, ctypes.c_int]
u32.GetWindowTextA.restype = ctypes.c_int
u32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
u32.GetWindowRect.restype = wt.BOOL

k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenProcess.restype = wt.HANDLE
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD,
                                           ctypes.c_wchar_p, ctypes.POINTER(wt.DWORD)]
k32.QueryFullProcessImageNameW.restype = wt.BOOL

psapi.EnumProcesses.argtypes = [ctypes.POINTER(wt.DWORD), wt.DWORD,
                                ctypes.POINTER(wt.DWORD)]
psapi.EnumProcesses.restype = wt.BOOL

GA_ROOT = 2


# --- 整屏抓取（GDI，零依赖）------------------------------------------------- #
class _BIH(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG),
                ("biYPelsPerMeter", wt.LONG), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


class _BI(ctypes.Structure):
    _fields_ = [("bmiHeader", _BIH), ("bmiColors", wt.DWORD * 3)]


g32 = ctypes.WinDLL("gdi32", use_last_error=True)
g32.CreateCompatibleDC.argtypes = [wt.HDC]
g32.CreateCompatibleDC.restype = wt.HDC
g32.CreateCompatibleBitmap.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int]
g32.CreateCompatibleBitmap.restype = wt.HBITMAP
g32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
g32.SelectObject.restype = wt.HGDIOBJ
g32.BitBlt.argtypes = [wt.HDC, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                       wt.HDC, ctypes.c_int, ctypes.c_int, wt.DWORD]
g32.GetDIBits.argtypes = [wt.HDC, wt.HBITMAP, wt.UINT, wt.UINT, ctypes.c_void_p,
                          ctypes.POINTER(_BI), wt.UINT]
g32.DeleteObject.argtypes = [wt.HGDIOBJ]
g32.DeleteDC.argtypes = [wt.HDC]
u32.GetDC.argtypes = [wt.HWND]
u32.GetDC.restype = wt.HDC
u32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]

SRCCOPY = 0x00CC0020


def _dump_png(path, data, w, h, stride):
    """把 grab_screen 的 BGR 原始缓冲写成 PNG（纯 stdlib）。"""
    import struct
    import zlib

    def chunk(tag, body):
        return (struct.pack(">I", len(body)) + tag + body
                + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF))

    rows = []
    for r in range(h):
        s = data[r * stride: r * stride + w * 3]
        rows.append(b"".join(bytes((s[i + 2], s[i + 1], s[i])) for i in range(0, len(s), 3)))
    raw = b"".join(b"\x00" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def grab_screen(vx, vy, w, h):
    """抓一次整屏，返回 (bytes, stride)。坐标为**虚拟屏幕**原点对齐。"""
    hdc = u32.GetDC(0)
    mem = g32.CreateCompatibleDC(hdc)
    bmp = g32.CreateCompatibleBitmap(hdc, w, h)
    g32.SelectObject(mem, bmp)
    g32.BitBlt(mem, 0, 0, w, h, hdc, vx, vy, SRCCOPY)
    bi = _BI()
    bi.bmiHeader.biSize = ctypes.sizeof(_BIH)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h                 # top-down
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 24
    bi.bmiHeader.biCompression = 0
    stride = ((w * 3 + 3) // 4) * 4
    buf = ctypes.create_string_buffer(stride * h)
    g32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    out = buf.raw
    g32.DeleteObject(bmp)
    g32.DeleteDC(mem)
    u32.ReleaseDC(0, hdc)
    return out, stride


def setup_stdout():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except BaseException:                                          # noqa: BLE001
        pass


def proc_name(pid):
    """pid -> 小写 exe 文件名（拿不到就返回 None）。"""
    h = k32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not h:
        return None
    try:
        buf = ctypes.create_unicode_buffer(1024)
        n = wt.DWORD(len(buf))
        if not k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n)):
            return None
        return os.path.basename(buf.value).lower()
    finally:
        k32.CloseHandle(h)


def find_pids(target):
    """按 exe 名找所有 pid。"""
    arr = (wt.DWORD * 4096)()
    need = wt.DWORD()
    if not psapi.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(need)):
        return set()
    n = need.value // ctypes.sizeof(wt.DWORD)
    out = set()
    for i in range(n):
        pid = arr[i]
        if pid == 0:
            continue
        nm = proc_name(pid)
        if nm == target:
            out.add(pid)
    return out


def win_info(hwnd):
    cls = ctypes.create_string_buffer(128)
    u32.GetClassNameA(hwnd, cls, 128)
    ttl = ctypes.create_string_buffer(256)
    u32.GetWindowTextA(hwnd, ttl, 256)
    r = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(r))
    return (cls.value.decode("gbk", "replace"),
            ttl.value.decode("gbk", "replace"),
            (r.left, r.top, r.right, r.bottom))


def sweep(xs, ys, targets, hit_boxes, pts):
    """扫一遍网格。返回**命中点数**（即"我们窗口位于最上层"的采样点数）。

    `targets` 是**本次新起进程的 pid 集合**（不是"进程名"！）。
    ⚠️ 这个区分是血的教训（2026-09-22）：一开始按**进程名** `e.exe` 匹配，
    结果**用户自己开着的易语言**被一起算进来 —— 屏幕上出现 13.7 秒的
    `ENewFrame 'AILI·LIFE - …'`，看着像我们的无头彻底失效，实际是别人的窗口。

    ⚠️⚠️ 本函数的结果**不等于"用户看得见"**：`WindowFromPoint` **并不会跳过**
    `WS_EX_TRANSPARENT` 窗口（真正会跳过的是 `ChildWindowFromPointEx` 的
    `CWP_SKIPTRANSPARENT` 标志，反证默认是包含的）。所以被我们"披了透明斗篷"的
    窗口照样会被采到。要判断"是否真的画出了像素"，必须再做**屏幕像素比对**
    （见 `main()` 里的 `grab_screen` 部分）。

    优化：**缓存上一个点的 hwnd 归属**。窗口动辄几百像素宽，一行里连续几十个点
    常常落在同一个 hwnd 上；命中缓存就省掉系统调用。实测把单次扫描从 ~34ms
    压到 ~9ms（≈110 次/秒），足以分辨 16ms 一帧。
    """
    cnt = 0
    last_h = 0
    last_ok = False
    for y in ys:
        for x in xs:
            h = u32.WindowFromPoint(wt.POINT(x, y))
            if h == last_h:
                if last_ok:
                    cnt += 1
                    pts.append((x, y))
                    if len(hit_boxes) < 12:
                        hit_boxes.append((test_now(), x, y) + win_info(h))
                continue
            last_h = h
            last_ok = False
            if not h:
                continue
            pid = wt.DWORD()
            u32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            if pid.value not in targets:
                continue
            last_ok = True
            cnt += 1
            pts.append((x, y))
            if len(hit_boxes) < 12:
                hit_boxes.append((test_now(), x, y) + win_info(h))
    return cnt


_t0 = time.time()
_WALL0 = time.time()


def test_now():
    return round(time.time() - _t0, 3)


def wall(rel=None):
    """把相对秒数换算成墙上时钟字符串，方便与宿主库 trace 对齐。"""
    t = time.time() if rel is None else (_WALL0 + rel)
    return time.strftime("%H:%M:%S", time.localtime(t)) + (".%03d" % int((t % 1) * 1000))


def main():
    setup_stdout()
    ap = argparse.ArgumentParser()
    ap.add_argument("exe", help="目标进程名，如 e.exe")
    ap.add_argument("secs", type=float, help="观察秒数")
    ap.add_argument("--step", type=int, default=20, help="网格间距（像素），默认 20")
    ap.add_argument("--out", default=None, help="日志文件路径")
    ap.add_argument("--dump", default=None, help="像素有差异时把整屏 PNG 落盘到该目录")
    ap.add_argument("--baseline-gap", type=float, default=0.8,
                    help="双基线间隔秒（默认 0.8s；≥时钟秒针跳动周期的一半）")
    a = ap.parse_args()

    want = a.exe.lower()

    vx = u32.GetSystemMetrics(76)
    vy = u32.GetSystemMetrics(77)
    vw = u32.GetSystemMetrics(78)
    vh = u32.GetSystemMetrics(79)
    xs = list(range(vx + a.step // 2, vx + vw, a.step))
    ys = list(range(vy + a.step // 2, vy + vh, a.step))
    grid = len(xs) * len(ys)

    lines = []

    def log(s):
        lines.append(s)
        print(s, flush=True)

    log("== probe_wfp 目标=%s 虚拟屏幕=(%d,%d,%dx%d) 网格=%dx%d=%d 点 间距=%dpx =="
        % (want, vx, vy, vw, vh, len(xs), len(ys), grid, a.step))

    base = find_pids(want)
    log("   启动前已有的 %s（**不计入**）: %s" % (want, sorted(base) or "无"))

    targets = set()                 # 只统计「本次新起」的 pid —— 见 sweep() 的注释
    hist = []
    runs = []                       # 每段"我们的窗口在最上层"：{start,end,max,notes,pix}
    t_end = time.time() + a.secs
    n_sweep = 0
    peak = 0
    peak_t = 0.0
    peak_pix = 0
    peak_pix_t = 0.0
    peak_note = []
    total_ms = 0.0

    base_img, _bstride = grab_screen(vx, vy, vw, vh)
    # ⭐ 双基线 mask：两帧基线之间「本来就自己动」的点（时钟、动画图标、光标残留、
    #    通知气泡）属于环境噪声，运行期在这些点上比出的像素差异**不算数**。
    #    2026-09-23 交接文档 P0-1：残留定性曾被 164 个差异点误导 —— 那批点未屏蔽漂移。
    #    取舍：mask 只会遮掉「基线期就在变」的点，代价是这些点上真出现残留会漏报；
    #    对「一闪而过的小窗口」判漏报远好于被桌面噪声骗出假警报。
    time.sleep(a.baseline_gap)
    base2_img, _ = grab_screen(vx, vy, vw, vh)
    mask = set()
    if base2_img is not None:
        for py in ys:
            for px in xs:
                o = (py - vy) * _bstride + (px - vx) * 3
                if base_img[o:o + 3] != base2_img[o:o + 3]:
                    mask.add((px, py))
        log("   已采集双基线（间隔 %.2fs）：动态点 %d / %d 进 mask（比对时跳过）"
            % (a.baseline_gap, len(mask), grid))
    log("   已采集运行前基线画面（用于像素比对，共 %d 字节）" % len(base_img))
    dump_saved = [0]
    if a.dump:
        os.makedirs(a.dump, exist_ok=True)
        _dump_png(os.path.join(a.dump, "BASE.png"), base_img, vw, vh, _bstride)

    while time.time() < t_end:
        t = test_now()
        # 每 30 次扫刷新一次 pid 集合（进程是陆续出现的）
        if n_sweep % 30 == 0:
            new = find_pids(want) - base - targets
            if new:
                targets |= new
                log("   [%7.3fs] 新出现 %s 目标 pid: %s" % (t, want, sorted(new)))
        t1 = time.time()
        boxes = []
        pts = []
        cnt = sweep(xs, ys, targets, boxes, pts)
        # ⭐ 像素判据：只有"我们的窗口位于最上层"的那些点才值得比对；
        #    再和**运行前基线**逐点比像素 —— 两者都满足才算"用户真的看见了"。
        pix = 0 if cnt > 0 else None
        if cnt > 0 and base_img is not None:
            cur_img, stride = grab_screen(vx, vy, vw, vh)
            d = 0
            box = [vw, vh, 0, 0]
            for (px, py) in pts:
                if (px, py) in mask:
                    continue                      # 环境噪声点：双基线已证明它自己会动
                o = (py - vy) * stride + (px - vx) * 3
                if cur_img[o:o + 3] != base_img[o:o + 3]:
                    d += 1
                    if px < box[0]: box[0] = px
                    if py < box[1]: box[1] = py
                    if px > box[2]: box[2] = px
                    if py > box[3]: box[3] = py
            pix = d
            if d > 0 and a.dump:
                os.makedirs(a.dump, exist_ok=True)
                if dump_saved[0] < 8:
                    dump_saved[0] += 1
                    nm = os.path.join(a.dump, "hit_%06.3fs_%dpix.png" % (t, d))
                    _dump_png(nm, cur_img, vw, vh, stride)
                    log("   💾 落盘 %s  像素差异包围盒=%s" % (nm, box))
        dt = (time.time() - t1) * 1000
        total_ms += dt
        n_sweep += 1
        hist.append((t, cnt, pix))
        if pix is not None and pix > peak_pix:
            peak_pix = pix
            peak_pix_t = t
        if cnt > 0:
            note = sorted({(b[3], b[4], b[5]) for b in boxes})
            if runs and (t - runs[-1]["end"]) < 0.5:
                runs[-1]["end"] = t
                runs[-1]["max"] = max(runs[-1]["max"], cnt)
                runs[-1]["pix"] = max(runs[-1]["pix"], pix or 0)
                if not runs[-1]["notes"]:
                    runs[-1]["notes"] = note
            else:
                runs.append({"start": t, "end": t, "max": cnt, "notes": note, "pix": pix or 0})
        if cnt > peak:
            peak = cnt
            peak_t = t
            peak_note = list(boxes)
            log("   >>> [%s] %7.3fs 新峰值 %d 点（%.1f%% 屏幕）  扫描耗时 %.1fms"
                % (wall(t), t, cnt, 100.0 * cnt / grid, dt))

    # 收尾：报告
    big = [h for h in hist if h[1] > 0]
    log("")
    log("== 结果 ==")
    log("   扫描 %d 次，平均每次 %.1fms（%.1f 次/秒）"
        % (n_sweep, total_ms / max(1, n_sweep), 1000.0 * n_sweep / max(0.001, a.secs)))
    log("   [A] 命中 =「目标窗口位于该点最上层」= %d / %d 点（%.2f%% 屏幕）@%.3fs"
        % (peak, grid, 100.0 * peak / grid, peak_t))
    log("   [B] **像素 =「该点真的画出了非基线画面」** = %d 点 @%.3fs"
        % (peak_pix, peak_pix_t))
    log("   —— [A] 会被「已披透明斗篷但依旧可命中的窗口」抬高，**判据以 [B] 为准**")
    log("   命中过（>0 点）的采样数 = %d / %d（%.2f%% 的时间），其中像素真的有差异的 = %d 次"
        % (len(big), len(hist), 100.0 * len(big) / max(1, len(hist)),
           sum(1 for h in hist if (h[2] or 0) > 0)))
    if peak_note:
        log("   峰值当时的窗口明细：")
        for t, x, y, cls, ttl, rect in peak_note[:6]:
            log("      [%7.3fs] (%d,%d) cls='%s' title='%s' rect=%s"
                % (t, x, y, cls, ttl[:40], rect))
    # 连续命中段（把「闪一下」和「一直露着」区分开）
    iv = (total_ms / max(1, n_sweep)) / 1000.0        # 单次扫描间隔（秒）
    vruns = [r for r in runs if r["pix"] > 0]
    total_ms_visible = sum((r["end"] - r["start"]) * 1000 for r in vruns)
    if runs:
        log("   区间（起→止 / 时长 / [A]点数 / **[B]像素** / 窗口）：")
        for r in runs[:20]:
            lo = (r["end"] - r["start"]) * 1000
            hi = lo + 2 * iv * 1000
            log("      [%s] %7.3fs → %7.3fs   %.0f~%.0fms   A=%5d  B=%5d%s"
                % (wall(r["start"]), r["start"], r["end"], lo, hi,
                   r["max"], r["pix"], "" if r["pix"] > 0 else "   ← 像素无差异（不可见）"))
            for cls, ttl, rect in r["notes"][:4]:
                log("            cls='%s' title='%s' rect=%s" % (cls, ttl[:40], rect))
        log("   ⏱ **像素可见**时长（采样下限）= %.0fms（A 口径 %.0fms）"
            % (total_ms_visible,
               sum((r["end"] - r["start"]) * 1000 for r in runs)))
    if not vruns:
        log("   ✅ **全程 0 像素差异**：屏幕上从未真的画出过目标进程（pid=%s）的窗口"
            % (sorted(targets) or "未起"))

    if a.out:
        with io.open(a.out, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print("   日志 -> %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
