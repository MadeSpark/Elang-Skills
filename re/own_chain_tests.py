# -*- coding: utf-8 -*-
"""用自家链路（mkcage + elaunch --compile）实测三个靶子：
A 标准输出（static_console 产物直接跑，收 stdout）
B 置入代码 NOP
C 置入代码 INT3（负例）
"""
import io
import json
import os
import shutil
import subprocess

ROOT = r"C:\Users\MadeSpark\Desktop\测试"
UP = os.path.join(ROOT, r"re\up_test")
PY = r"C:\Users\MadeSpark\.workbuddy\binaries\python\versions\3.13.12\python.exe"
FNE = os.path.join(ROOT, r"re\compile_test\elang_addin.fne")
E2TXT = r"D:\Tools\e2txt\e2txt.exe"
DEMO_PRJ = os.path.join(ROOT, r"demos\02-嵌套控制流压测\项目")

CODE_A = u""".版本 2

.程序集 程序集1

.子程序 加法, 整数型, , 被测子程序
    .参数 a, 整数型
    .参数 b, 整数型

    返回 (a ＋ b)

.子程序 _启动子程序, 整数型

    标准输出 (1, “结果=” ＋ 到文本 (加法 (1, 2)))
    返回 (0)
"""

CODE_B = u""".版本 2

.程序集 程序集1

.子程序 _启动子程序, 整数型

    置入代码 ({ 144, 144 })    ' 90 90 = nop; nop
    标准输出 (1, “mc-nop-ok”)
    返回 (0)
"""

CODE_C = u""".版本 2

.程序集 程序集1

.子程序 _启动子程序, 整数型

    标准输出 (1, “before-int3”)
    置入代码 ({ 204 })    ' CC = int3
    标准输出 (1, “after-int3 不应出现”)
    返回 (0)
"""


def run_case(name, code, kind, run_capture=True):
    prj = os.path.join(UP, name)
    if os.path.exists(prj):
        shutil.rmtree(prj)
    shutil.copytree(DEMO_PRJ, prj)
    p = os.path.join(prj, r"代码\程序集1.static.e.txt")
    io.open(p, "w", encoding="utf-8-sig", newline="\r\n").write(code)
    base_e = os.path.join(UP, name + "_base.e")
    ai_e = os.path.join(UP, name + "_ai.e")
    for f in (base_e, ai_e):
        if os.path.exists(f):
            os.remove(f)
    r = subprocess.run([E2TXT, "-mode", "t2e", "-src", prj, "-dst", base_e,
                        "-enc", "UTF-8", "-level", "1", "-ns", "2"], capture_output=True)
    ok = "SUCC" in r.stdout.decode("gbk", "replace")
    print(name, "t2e:", "OK" if ok else "FAIL")
    if not ok:
        return
    r = subprocess.run([PY, os.path.join(ROOT, r"tools\mkcage.py"), base_e, ai_e,
                        "--fne", FNE, "-q"], capture_output=True)
    print(name, "cage: rc=%d" % r.returncode)
    out_exe = os.path.join(UP, name + "_out.exe")
    if os.path.exists(out_exe):
        os.remove(out_exe)
    r = subprocess.run([PY, os.path.join(ROOT, r"tools\elaunch.py"), ai_e,
                        "--compile", kind, "--expect-out", out_exe,
                        "--timeout", "180", "--fne", FNE, "--json"],
                       capture_output=True, cwd=ROOT)
    d = json.loads(r.stdout.decode("utf-8", "replace") or "{}")
    have = os.path.exists(out_exe)
    print(name, "compile:", d.get("status"), "| out:",
          os.path.getsize(out_exe) if have else "无")
    if not have or not run_capture:
        return
    r = subprocess.run([out_exe], capture_output=True, timeout=30, cwd=UP)
    so = r.stdout.decode("gbk", "replace").strip()
    print(name, "run: exit=%d(0x%08X) stdout=%r" % (
        r.returncode,
        r.returncode & 0xFFFFFFFF if r.returncode < 0 else r.returncode, so))


run_case("std_console", CODE_A, "static_console")
run_case("mc_nop2", CODE_B, "static_console")
run_case("mc_int32", CODE_C, "static_console", run_capture=True)
