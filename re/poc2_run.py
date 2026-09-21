#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
poc2_run.py —— PoC-2 实验台：加壳 demo .e → 投放 .fne → e.exe 打开 → 触发“调试运行”
→ 增量捕获调试面板 → 三个问题取证。

用法：
  python re/poc2_run.py [demo.e] --tag <TAG> [--stop N] [--delay MS] [--wait SEC]

★ 取证纪律（team-lead 2026-09-21 要求，已落实）★
  1) 证据文件**按运行唯一命名**：`re/addin/evidence/<TAG>_trace.txt` / `<TAG>_capture.txt`。
     运行开始**只删同名 tag 的旧文件**，绝不删别次运行的证据。
  2) `ELANG_AI_CAPTURE` **直接指向** `re/addin/evidence/<TAG>_capture.txt`
     （不再从 %TEMP% 拷贝 —— 拷贝那一步就是丢证据的源头）。
  3) 跑完把两个证据文件的路径/大小/关键行打印出来，供人工 grep 复核。

只读纪律：实验后移除 lib\elang_addin.fne；e.exe 用精确 PID 关闭；
         并对 D:\ides\e\ 顶层做 before/after 文件 diff，报告（并清理）新增文件。
"""
import hashlib
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.dirname(HERE)
E_EXE = r"D:\ides\e\e.exe"
E_DIR = r"D:\ides\e"
LIB = os.path.join(E_DIR, "lib")
DST_FNE = os.path.join(LIB, "elang_addin.fne")
FNE_CLEAN = os.path.join(HERE, "addin", "elang_addin.fne")
EVID = os.path.join(HERE, "addin", "evidence")


def md5f(p):
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(65536), b""):
            h.update(c)
    return h.hexdigest()


def toplist(d):
    try:
        return set(os.listdir(d))
    except OSError:
        return set()


def main():
    argv = sys.argv[1:]
    demo = None
    opts = {"--stop": 0, "--delay": 3000, "--wait": 150}
    tag = None
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("--tag",):
            tag = argv[i + 1]; i += 2; continue
        if a in opts:
            opts[a] = int(argv[i + 1]); i += 2; continue
        if a.startswith("--"):
            i += 2; continue
        demo = a; i += 1
    if demo is None:
        demo = os.path.join(WS, "demos", "01-C盘结构输出", "项目", "代码.e")
    if not tag:
        tag = os.path.splitext(os.path.basename(demo))[0] + "_" + time.strftime("%m%d_%H%M%S")
    stop = opts["--stop"]; delay = opts["--delay"]; wait = opts["--wait"]

    # ★ 证据文件：按 tag 唯一命名，直接落在 evidence/ 下
    os.makedirs(EVID, exist_ok=True)
    TRACE = os.path.join(EVID, "%s_trace.txt" % tag)
    CAP = os.path.join(EVID, "%s_capture.txt" % tag)

    work = os.path.join(HERE, "poc2_work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    caged = os.path.join(work, os.path.splitext(os.path.basename(demo))[0] + "_ai.e")

    print("== [0] mkcage ==")
    r = subprocess.run([sys.executable, os.path.join(HERE, "mkcage.py"), demo, caged],
                       capture_output=True, text=True)
    print(r.stdout.strip().splitlines()[-1] if r.stdout else r.stderr[-400:])
    if r.returncode != 0:
        print("mkcage 失败:", r.stdout[-800:], r.stderr[-800:])
        return 2
    print("   caged md5=%s  != demo md5=%s  : %s"
          % (md5f(caged)[:12], md5f(demo)[:12], md5f(caged) != md5f(demo)))

    # ★ 只清「同名 tag」的旧证据，绝不删别次运行
    if os.path.exists(DST_FNE):
        os.remove(DST_FNE)
    for f in (TRACE, CAP):
        if os.path.exists(f):
            os.remove(f)
            print("   （清掉同名 tag 旧证据: %s）" % os.path.basename(f))

    before_dir = toplist(E_DIR)
    pid = None
    proc = None
    try:
        shutil.copy2(FNE_CLEAN, DST_FNE)
        env = os.environ.copy()
        env.update({
            "ELANG_AI_TRACE": TRACE,
            "ELANG_AI_TASK": caged,
            "ELANG_AI_RUN": "1",
            "ELANG_AI_RUN_DELAY": str(delay),
            "ELANG_AI_STOP": str(stop),
            "ELANG_AI_CAPTURE": CAP,
            "ELANG_AI_HIDE": "0",
        })
        print("== [1] launch e.exe (RUN=1 delay=%dms stop=%ds) ==" % (delay, stop))
        print("   TAG=%s" % tag)
        print("   TRACE -> %s" % TRACE)
        print("   CAP   -> %s" % CAP)
        proc = subprocess.Popen([E_EXE, caged], cwd=E_DIR, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pid = proc.pid
        print("   e.exe pid=%d" % pid)
        t0 = time.time()
        rc = None
        while time.time() - t0 < wait:
            rc = proc.poll()
            if rc is not None:
                break
            tr = ""
            if os.path.exists(TRACE):
                tr = open(TRACE, encoding="utf-8", errors="replace").read()
            if "RUN capture done" in tr or "WORKER done" in tr:
                break
            time.sleep(1.0)
        print("   alive=%s exit=%s elapsed=%.1fs" %
              (rc is None, ("0x%08X" % (rc & 0xffffffff)) if rc is not None else "-",
               time.time() - t0))

        tr = open(TRACE, encoding="utf-8", errors="replace").read() if os.path.exists(TRACE) else ""
        print("== [2] trace 关键行 ==")
        for n, ln in enumerate(tr.splitlines(), 1):
            if any(k in ln for k in ("NL_IDE_READY", "STEP4", "RUN trigger", "RUN FN_COMPILE",
                                     "RUN-STATE", "RUN-END", "STEP4 PoC-2", "RUN capture done",
                                     "RUN 到点", "RUN no-poll", "WORKER done")):
                print("   L%-4d %s" % (n, ln[:190]))
        print("   (trace 总行数=%d)" % len(tr.strip().splitlines()))

        print("== [3] capture 文件 %s ==" % CAP)
        if os.path.exists(CAP):
            cap = open(CAP, encoding="utf-8", errors="replace").read()
            print("   大小=%d 字节, 行数=%d" % (len(cap), len(cap.splitlines())))
            print("   ---- 前 8 行 ----")
            for n, ln in enumerate(cap.splitlines()[:8], 1):
                print("   L%-4d | %s" % (n, ln[:180]))
            print("   ---- 末 12 行 ----")
            lines = cap.splitlines()
            base = len(lines) - 12
            for n, ln in enumerate(lines[-12:], base + 1):
                print("   L%-4d | %s" % (n, ln[:180]))
        else:
            print("   （无 capture 文件）")
    finally:
        if pid:
            subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True, text=True)
            if proc:
                try:
                    proc.wait(timeout=10)
                except Exception:
                    pass
        if os.path.exists(DST_FNE):
            os.remove(DST_FNE)
        after_dir = toplist(E_DIR)
        newf = sorted(after_dir - before_dir)
        print("== [4] D:\\ides\\e\\ 新文件: %s ==" % (newf or "（无）"))
        for n in newf:
            p = os.path.join(E_DIR, n)
            if os.path.isfile(p):
                try:
                    os.remove(p)
                    print("   已清理: %s" % n)
                except OSError as e:
                    print("   无法清理 %s: %s" % (n, e))

    print("== [5] evidence 归档（供追证） ==")
    # 原始文件保留**逐字节**（面板文本为 GBK）。另产出 UTF-8 转码伴生文件，
    # 便于在 UTF-8 终端里直接 grep 中文（原始文件仍是权威证据）。
    for f in (TRACE, CAP):
        if os.path.exists(f):
            print("   %s  (%d 字节)" % (f, os.path.getsize(f)))
            u8 = os.path.splitext(f)[0] + ".utf8.txt"
            raw = open(f, "rb").read()
            with open(u8, "wb") as w:
                w.write(raw.decode("gbk", errors="replace").encode("utf-8"))
            print("   %s  （UTF-8 转码视图，供 grep 中文）" % u8)
        else:
            print("   %s  （缺失！）" % f)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
