# -*- coding: utf-8 -*-
"""生成 V3/V4 变体：分别单独抬高 RqSys 或 RqKrnl 门槛。"""
import io

SRC = r"C:\Users\MadeSpark\Desktop\测试\src\addin\elang_addin.cpp"
DST = r"C:\Users\MadeSpark\Desktop\测试\src\addin"

s = io.open(SRC, encoding="utf-8").read()

v3 = s.replace("s_LibInfo.m_nRqSysMinorVer         = 0;", "s_LibInfo.m_nRqSysMinorVer         = 7;")
assert v3 != s
io.open(DST + r"\v3_rqsys_only.cpp", "w", encoding="utf-8").write(v3)

v4 = s.replace("s_LibInfo.m_nRqSysKrnlLibMajorVer  = 3;", "s_LibInfo.m_nRqSysKrnlLibMajorVer  = 5;")
v4 = v4.replace("s_LibInfo.m_nRqSysKrnlLibMinorVer  = 0;", "s_LibInfo.m_nRqSysKrnlLibMinorVer  = 3;")
assert v4 != s
io.open(DST + r"\v4_rqkrnl_only.cpp", "w", encoding="utf-8").write(v4)
print("v3/v4 written")
