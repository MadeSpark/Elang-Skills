#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""逐像素取证：跑一条命令的同时**尽可能快**地连续截屏，找出屏幕上真实画过的窗口。

    python measure_visible.py <标签> [--dump 目录] [--top N] -- <要跑的完整命令...>

## 为什么要「跑前 / 跑后双基线」

被测期间**桌面上别的东西也会变**（浏览器开窗、聊天软件弹提示、时钟刷新、光标闪烁）。
只跟「跑前基线」比，这些无关变化会被误判成「易语言窗口露脸」。

解法：跑完再采一帧 `base_post`，把 `base` 与 `base_post` **都不同**的网格单元标成
「环境漂移」，所有帧一律跳过这些单元。真正一闪而过的窗口在跑后帧里已经消失
（`base_post == base`），**不会被误屏蔽**。

## 为什么要丢掉 sleep

DWM 约 16ms 合成一帧；窗口只显示一帧的话，80ms 的采样间隔**大概率直接漏掉**。
所以这里不 sleep，能多快采多快（瓶颈是 `BitBlt`+`GetDIBits`，约 20-30ms/帧）。

判据：无头成功 ⇒ 屏蔽后的最大差异应落到「零星后台抖动」量级（几十~几百），
而不是上万；对照实验 `--headless off` 会稳定复现上万级差异。
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shot  # noqa: E402

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

u32 = ctypes.WinDLL("user32", use_last_error=True)
gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)

STEP = 4


def grab():
    """返回 (w, h, bytes RGB24 自上而下带 stride 填充, stride)。"""
    x = u32.GetSystemMetrics(76)
    y = u32.GetSystemMetrics(77)
    w = u32.GetSystemMetrics(78)
    h = u32.GetSystemMetrics(79)
    hdc = u32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    gdi32.BitBlt(mem, 0, 0, w, h, hdc, x, y, 0x00CC0020)
    bi = shot.BITMAPINFO()
    bi.bmiHeader.biSize = ctypes.sizeof(shot.BITMAPINFOHEADER)
    bi.bmiHeader.biWidth = w
    bi.bmiHeader.biHeight = -h
    bi.bmiHeader.biPlanes = 1
    bi.bmiHeader.biBitCount = 24
    stride = ((w * 3 + 3) // 4) * 4
    buf = ctypes.create_string_buffer(stride * h)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)
    gdi32.DeleteObject(bmp)
    gdi32.DeleteDC(mem)
    u32.ReleaseDC(0, hdc)
    return w, h, buf.raw, stride


def sample(buf, w, h, stride, step=STEP):
    """按 step 网格抽样成紧凑字节串（整块比较走 C，比逐字节 Python 循环快得多）。"""
    out = []
    for r in range(0, h, step):
        row = buf[r * stride: r * stride + w * 3]
        out.append(row[0::3 * step])
    return b"".join(out)


def diff_mask(a, b, mask):
    """只在 mask 为 0 的网格位置统计差异「像素」数（3 通道任一不同算 1）。"""
    n = 0
    for i, (p, q) in enumerate(zip(a, b)):
        if p != q and not mask[i // 3]:
            n += 1
    return n // 3


def diff(a, b):
    if a == b:
        return 0
    n = 0
    for p, q in zip(a, b):
        if p != q:
            n += 1
    return n // 3


def diff_boxes(a, b, w, h, mask=None, step=STEP):
    """差异区域包围盒（真实像素坐标），可选按 mask 过滤。"""
    cols = len(range(0, w, step))
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for i, (p, q) in enumerate(zip(a, b)):
        if p == q:
            continue
        gp = i // 3
        if mask is not None and mask[gp]:
            continue
        r, c = divmod(gp, cols)
        x0 = min(x0, c)
        x1 = max(x1, c)
        y0 = min(y0, r)
        y1 = max(y1, r)
    if x1 < 0:
        return None
    return (x0 * step, y0 * step, (x1 + 1) * step, (y1 + 1) * step)


def dump_png(path, buf, w, h, stride):
    rows = []
    for r in range(h):
        s = buf[r * stride: r * stride + w * 3]
        rows.append(bytes(b for i in range(0, len(s), 3) for b in (s[i + 2], s[i + 1], s[i])))
    shot._png(path, w, h, rows)


def parse_argv(argv):
    k = argv.index("--")
    head = argv[1:k]
    label = head[0] if head else "run"
    dump = None
    topn = 5
    i = 1
    while i < len(head):
        if head[i] == "--dump":
            dump = head[i + 1]
            i += 2
        elif head[i] == "--top":
            topn = int(head[i + 1])
            i += 2
        else:
            i += 1
    return label, dump, topn, argv[k + 1:]


def main():
    if "--" not in sys.argv:
        print("用法: measure_visible.py <标签> [--dump 目录] [--top N] -- <命令...>")
        return 2
    label, dump, topn, cmd = parse_argv(sys.argv)

    time.sleep(0.4)
    w, h, base_raw, stride = grab()
    base = sample(base_raw, w, h, stride)
    time.sleep(0.5)
    _, _, r2, s2 = grab()
    grid = len(base) // 3
    print("== measure_visible label=%s screen=%dx%d 抽样网格=%d 像素 基线自身抖动=%d =="
          % (label, w, h, grid, diff(base, sample(r2, w, h, s2))))

    t0 = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    samples = []            # (t, 抽样字节串)  —— 小，全留
    keep = []               # (t, 未掩码差异, 原始位图, stride) —— 大，只留高分帧（有界）
    thr_raw = grid // 200
    while proc.poll() is None and time.time() - t0 < 300:
        _, _, cur, st = grab()
        t = round(time.time() - t0, 3)
        smp = sample(cur, w, h, st)
        samples.append((t, smp))
        # 用「未掩码」差异先筛一遍（掩码只会让数值更小，所以这一步不会漏掉真候选）
        d_raw = diff(base, smp)
        if d_raw > thr_raw:
            keep.append((t, d_raw, cur, st))
            keep.sort(key=lambda x: -x[1])
            del keep[24:]                     # 只留差异最大的 24 帧原始位图
    proc.wait()
    t_end = time.time() - t0

    # 跑后基线：整段跑完后的稳定画面
    time.sleep(0.4)
    _, _, post_raw, pstride = grab()
    post = sample(post_raw, w, h, pstride)

    # 环境漂移 mask：跑前基线 vs 跑后基线 都不同的网格单元
    mask = bytearray(grid)
    for i, (p, q) in enumerate(zip(base, post)):
        if p != q:
            mask[i // 3] = 1
    n_mask = sum(mask)

    scored = [(t, diff_mask(base, smp, mask), smp) for t, smp in samples]
    order = sorted(range(len(scored)), key=lambda i: -scored[i][1])
    mx = scored[order[0]][:2] if scored else (0, 0)
    thr = max(150, grid // 200)
    big = [s for s in scored if s[1] > thr]

    print("   跑时 %.1fs   帧数=%d   平均采样间隔=%.1fms"
          % (t_end, len(samples), (t_end / max(1, len(samples))) * 1000))
    print("   环境漂移 mask：%d / %d 网格单元被屏蔽（%.1f%%）"
          % (n_mask, grid, 100.0 * n_mask / max(1, grid)))
    print("   >>> 最大差异=%d 抽样像素 @%.3fs   超过阈值(%d)的帧数=%d"
          % (mx[1], mx[0], thr, len(big)))
    print("   差异 top%d: %s" % (topn, [(t, d) for t, d, _s in
                                       [scored[i] for i in order[:topn]]]))

    if dump:
        os.makedirs(dump, exist_ok=True)
        dump_png(os.path.join(dump, "%s_BASE.png" % label), base_raw, w, h, stride)
        smp_by_t = dict((t, smp) for t, smp in samples)
        seen = []
        for t, d_raw, cur, st in keep:
            if any(abs(t - s) < 0.4 for s in seen):
                continue
            seen.append(t)
            if len(seen) > 6:
                break
            box = diff_boxes(base, smp_by_t.get(t, sample(cur, w, h, st)), w, h, mask)
            name = os.path.join(dump, "%s_%06.3fs_%dpix.png" % (label, t, d_raw))
            dump_png(name, cur, w, h, st)
            print("   落盘 %s  差异包围盒=%s" % (name, box))
    return 0


if __name__ == "__main__":
    sys.exit(main())
