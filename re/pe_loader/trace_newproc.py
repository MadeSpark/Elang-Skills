#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""追查探针运行时冒出来的新进程：抓它的加载模块与路径，判断是不是 node.dll 派生的。"""
import pathlib
import subprocess
import sys
import time

sys.stdout.reconfigure(encoding="utf-8")

HERE = pathlib.Path(__file__).resolve().parent
SHARE = HERE / "share"
KERNEL = SHARE / "kernel" / "node.dll"


def snap():
    cp = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True)
    out = {}
    for line in (cp.stdout or b"").decode("gbk", "replace").splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[1].isdigit():
            out[int(parts[1])] = parts[0]
    return out


def detail(pid):
    cp = subprocess.run(["tasklist", "/M", "/FI", "PID eq %d" % pid, "/FO", "CSV", "/NH"],
                        capture_output=True)
    return (cp.stdout or b"").decode("gbk", "replace")


def main():
    env = {"MBKIT_SILENT": "1", "MBKIT_TEST_NODE": str(KERNEL),
           "SystemRoot": r"C:\Windows", "PATH": r"C:\Windows\System32;C:\Windows"}
    d = SHARE / "A"
    before = snap()
    print("启动探针前进程数 =", len(before))
    p = subprocess.Popen([str(d / "probe.exe")], cwd=str(d), env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t0 = time.time()
    found = {}
    while p.poll() is None and time.time() - t0 < 60:
        now = snap()
        for pid in sorted(set(now) - set(before)):
            name = now[pid]
            if name.lower() == "tasklist.exe" or pid in found:
                continue
            found[pid] = name
            print("\n[+%.1fs] 新进程 pid=%d %s" % (time.time() - t0, pid, name))
            print("  模块列表:")
            for ln in detail(pid).splitlines()[:12]:
                print("   ", ln[:300])
        time.sleep(0.2)
    print("\n探针退出码 =", p.wait())
    # 退出后再看它们是否还在
    time.sleep(1.0)
    after = snap()
    for pid, name in found.items():
        print("  退出后 pid=%d %s 仍在? %s" % (pid, name, "是" if pid in after else "否"))


if __name__ == "__main__":
    main()
