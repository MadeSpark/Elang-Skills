#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""probe_e.py —— 直接启动 e.exe 打开指定 .e，看它是否自崩（不经 elaunch/加壳）。

用法: probe_e.py <某个.e> [等待秒数]
输出: 退出码（0xC0000005 = 访问违例）；到点仍在跑则打印 still-running 后结束进程。
"""
import subprocess
import sys
import time

E = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"

tgt = sys.argv[1]
wait = float(sys.argv[2]) if len(sys.argv) > 2 else 12.0

p = subprocess.Popen([E, tgt], cwd=E_DIR,
                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
t0 = time.time()
while time.time() - t0 < wait:
    rc = p.poll()
    if rc is not None:
        print("退出码 = %d (0x%08X)，存活 %.1f s" % (rc, rc & 0xFFFFFFFF, time.time() - t0))
        break
    time.sleep(0.2)
else:
    print("仍存活 %.1f s → 未自崩；现在结束它" % wait)
    p.terminate()
    try:
        p.wait(timeout=5)
    except Exception:
        p.kill()
    print("已结束")

out = p.stdout.read() if p.stdout else b""
if out:
    print("---- 子进程输出 ----")
    print(out.decode("gbk", errors="replace")[:2000])
