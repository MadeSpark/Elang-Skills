# -*- coding: utf-8 -*-
"""构造三个测试工程（复制 demo02 项目形状 → 换代码 → t2e 出 .e）。
A console_test：标准输出 控制台验证模式
B mc_nop     ：置入代码 {144,144} 最小机器码
C mc_int3    ：置入代码 {204} = int3 崩溃负例
"""
import io
import os
import shutil
import subprocess
import sys

ROOT = r"C:\Users\MadeSpark\Desktop\测试"
SRC_PRJ = os.path.join(ROOT, r"demos\02-嵌套控制流压测\项目")
WORK = os.path.join(ROOT, r"re\up_test")
E2TXT = r"D:\Tools\e2txt\e2txt.exe"

CODE_A = """.版本 2

.程序集 程序集1

.子程序 加法, 整数型, , 被测子程序
    .参数 a, 整数型
    .参数 b, 整数型

    返回 (a ＋ b)

.子程序 _启动子程序, 整数型

    标准输出 (, “结果=” ＋ 到文本 (加法 (1, 2)))
    标准输出 (, “中文输出OK”)
    返回 (0)
"""

CODE_B = """.版本 2

.程序集 程序集1

.子程序 _启动子程序, 整数型

    置入代码 ({ 144, 144 })    ' 90 90 = nop; nop
    标准输出 (, “mc-nop-ok”)
    返回 (0)
"""

CODE_C = """.版本 2

.程序集 程序集1

.子程序 _启动子程序, 整数型

    标准输出 (, “before-int3”)
    置入代码 ({ 204 })    ' CC = int3
    标准输出 (, “after-int3-不应出现”)
    返回 (0)
"""


def write_text(path, s):
    with io.open(path, "w", encoding="utf-8-sig", newline="\r\n") as f:
        f.write(s)


def make(name, code):
    prj = os.path.join(WORK, name)
    if os.path.exists(prj):
        shutil.rmtree(prj)
    shutil.copytree(SRC_PRJ, prj)
    write_text(os.path.join(prj, r"代码\程序集1.static.e.txt"), code)
    dst = os.path.join(WORK, name + ".e")
    if os.path.exists(dst):
        os.remove(dst)
    r = subprocess.run([E2TXT, "-mode", "t2e", "-src", prj, "-dst", dst,
                        "-enc", "UTF-8", "-level", "1", "-ns", "2"],
                       capture_output=True)
    out = r.stdout.decode("gbk", "replace")
    ok = "SUCC" in out and os.path.exists(dst)
    print(name, "t2e:", "OK" if ok else "FAIL", "|", out.strip()[:80])
    return dst


make("console_proj", CODE_A)
make("mc_nop_proj", CODE_B)
make("mc_int3_proj", CODE_C)
