#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q1 实证：两个进程**同时**使用同一个 node.dll 文件，看会不会互相影响。

设计
  · 两个目录 A / B，各放一份 probe.exe；
  · 内核只有一份：kernel\\node.dll（md5 固定），两个进程都用 MBKIT_TEST_NODE 指过去；
  · 同时启动，各自写自己的 probe.log；
  · 运行期间采样进程表，看内核有没有派生**子进程**（多进程共用的潜在打架点）。

判据
  · A、B 都 KernelLoaded=1 且 wkeIsInitialize=1 ⇒ 共用同一份内核文件互不影响；
  · 两者的 KernelPath 必须指向同一个文件；
  · 记录是否出现新进程。
"""
import pathlib
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

HERE = pathlib.Path(__file__).resolve().parent
SHARE = HERE / "share"
KERNEL = SHARE / "kernel" / "node.dll"


def snapshot():
    cp = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True)
    pids = set()
    for line in (cp.stdout or b"").decode("gbk", "replace").splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[1].isdigit():
            pids.add((int(parts[1]), parts[0]))
    return pids


def main() -> int:
    if not KERNEL.exists():
        print("缺共享内核:", KERNEL)
        return 2
    env = {
        "MBKIT_SILENT": "1",
        "MBKIT_TEST_NODE": str(KERNEL),
        "SystemRoot": r"C:\Windows",
        "PATH": r"C:\Windows\System32;C:\Windows",
    }

    before = snapshot()
    print("共享内核:", KERNEL, KERNEL.stat().st_size, "字节")
    print("环境变量 MBKIT_TEST_NODE =", env["MBKIT_TEST_NODE"])
    print()

    procs = []
    for tag in ("A", "B"):
        d = SHARE / tag
        for f in ("probe.log", "MbKit.dll"):
            p = d / f
            if p.exists():
                p.unlink()
        # 子进程输出丢掉：probe 走的是 GBK，混进本脚本的 UTF-8 管道会把两边都搅乱
        procs.append((tag, subprocess.Popen([str(d / "probe.exe")], cwd=str(d), env=env,
                                            stdout=subprocess.DEVNULL,
                                            stderr=subprocess.DEVNULL)))

    # 运行期间采样，看有没有衍生进程（排掉我自己的采样器）
    seen = set()
    t0 = time.time()
    while any(p.poll() is None for _, p in procs) and time.time() - t0 < 120:
        now = snapshot()
        for pid, name in (now - before):
            if name.lower() == "tasklist.exe":
                continue
            if pid not in seen:
                seen.add(pid)
                print("  [+%.1fs] 新进程 pid=%d %s" % (time.time() - t0, pid, name))
        time.sleep(0.4)

    for tag, p in procs:
        print("  %s 退出码=%s" % (tag, p.wait()))

    print()
    ok = True
    for tag in ("A", "B"):
        log = SHARE / tag / "probe.log"
        print("=" * 60)
        print(tag, log)
        if not log.exists():
            print("  没有日志 —— 进程可能崩了")
            ok = False
            continue
        # 易语言「写到文件」写的是 ANSI(GBK) 字节，不是 UTF-8 —— 按 utf-8 读会把中文全弄坏
        txt = log.read_text(encoding="gbk", errors="replace")
        # 取**最后一次**出现的值（自检块在加载之后，才是真实状态）
        def last(key):
            vals = [ln.split(key + " = ", 1)[1].strip()
                    for ln in txt.splitlines() if key + " = " in ln]
            return vals[-1] if vals else None

        def last_block_field(key):
            vals = [ln.split(":", 1)[1].strip()
                    for ln in txt.splitlines() if key in ln and ":" in ln]
            return vals[-1] if vals else None

        for ln in txt.splitlines():
            if "释放内嵌宿主库" in ln or "落盘文件尺寸" in ln or "SetNodePath" in ln:
                print("   " + ln.strip())
        state = last_block_field("内核状态")
        base = last_block_field("内核模块")
        print("   wkeIsInitialize =", last("wkeIsInitialize"))
        print("   内核状态(自检)  =", state)
        print("   内核模块(自检)  =", base)
        print("   KernelPath      =", last("KernelPath"))
        mb = SHARE / tag / "MbKit.dll"
        print("   本目录 MbKit.dll =", (str(mb.stat().st_size) + " B") if mb.exists() else "（无）")
        if last("wkeIsInitialize") != "1" or state != "已加载" or not base or base == "0x00000000":
            ok = False
        if "kernel\\node.dll" not in (last("KernelPath") or ""):
            ok = False

    print()
    print("结论：", "A/B 同时共用同一份 node.dll 均正常工作 ✅" if ok else "存在失败项 ❌")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
