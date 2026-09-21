# -*- coding: utf-8 -*-
"""机器码实测 v2：用写文件代替标准输出做观测。
D mc_nop2：置入代码 {144,144} → 写文件 mc-nop-ok
E mc_int32：写 before → 置入代码 {204}(int3) → （after 不应出现）
"""
import io
import os
import shutil
import subprocess

ROOT = os.getcwd()
WORK = os.path.join(ROOT, r"re\up_test")
E2TXT = r"D:\Tools\e2txt\e2txt.exe"
SRC_PRJ = os.path.join(ROOT, r"demos\02-嵌套控制流压测\项目")

CODE_D = u"""\u7248\u672c 2

\u7a0b\u5e8f\u96c6 \u7a0b\u5e8f\u96c61

.\u5b50\u7a0b\u5e8f _\u542f\u52a8\u5b50\u7a0b\u5e8f, \u6574\u6570\u578b

    .\u5c40\u90e8\u53d8\u91cf \u53f7, \u6574\u6570\u578b

    \u7f6e\u5165\u4ee3\u7801 ({ 144, 144 })    ' 90 90 = nop; nop
    \u53f7 \uff1d \u6253\u5f00\u6587\u4ef6 ("mc.txt", #\u91cd\u5199, )
    \u5199\u6587\u672c\u884c (\u53f7, "mc-nop-ok")
    \u5173\u95ed\u6587\u4ef6 (\u53f7)
    \u8fd4\u56de (0)
"""

CODE_E = u"""\u7248\u672c 2

\u7a0b\u5e8f\u96c6 \u7a0b\u5e8f\u96c61

.\u5b50\u7a0b\u5e8f _\u542f\u52a8\u5b50\u7a0b\u5e8f, \u6574\u6570\u578b

    .\u5c40\u90e8\u53d8\u91cf \u53f7, \u6574\u6570\u578b

    \u53f7 \uff1d \u6253\u5f00\u6587\u4ef6 ("mc.txt", #\u91cd\u5199, )
    \u5199\u6587\u672c\u884c (\u53f7, "before-int3")
    \u5173\u95ed\u6587\u4ef6 (\u53f7)
    \u7f6e\u5165\u4ee3\u7801 ({ 204 })    ' CC = int3
    \u53f7 \uff1d \u6253\u5f00\u6587\u4ef6 ("mc.txt", #\u6539\u5199, )
    \u79fb\u5230\u6587\u4ef6\u5c3e (\u53f7)
    \u5199\u6587\u672c\u884c (\u53f7, "after-int3")
    \u5173\u95ed\u6587\u4ef6 (\u53f7)
    \u8fd4\u56de (0)
"""

for name, code in [("mc_nop2_proj", CODE_D), ("mc_int32_proj", CODE_E)]:
    prj = os.path.join(WORK, name)
    if os.path.exists(prj):
        shutil.rmtree(prj)
    shutil.copytree(SRC_PRJ, prj)
    p = os.path.join(prj, r"代码\程序集1.static.e.txt")
    io.open(p, "w", encoding="utf-8-sig", newline="\r\n").write(code)
    dst = os.path.join(WORK, name + ".e")
    if os.path.exists(dst):
        os.remove(dst)
    r = subprocess.run([E2TXT, "-mode", "t2e", "-src", prj, "-dst", dst,
                        "-enc", "UTF-8", "-level", "1", "-ns", "2"], capture_output=True)
    print(name, "t2e:", "SUCC" in r.stdout.decode("gbk", "replace"))

for name in ["mc_nop2_proj", "mc_int32_proj"]:
    box = os.path.join(WORK, name)
    lib = os.path.join(box, "lib")
    os.makedirs(lib, exist_ok=True)
    src_k = r"D:\ides\e\lib\krnln.fne"
    dst_k = os.path.join(lib, "krnln.fne")
    if not os.path.exists(dst_k):
        shutil.copy(src_k, dst_k)
    src_e = os.path.join(WORK, name + ".e")
    dst_e = os.path.join(box, name + ".e")
    shutil.copy(src_e, dst_e)
print("sandboxes ready")
