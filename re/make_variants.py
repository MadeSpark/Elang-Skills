# -*- coding: utf-8 -*-
"""生成 elang_addin 的两个实验变体源码。
V1 = GetNewInf 以 LIB_INFO2 形态返回（模拟 AutoLinker 的 LBS_LIB_INFO2 + 授权字段）
V2 = 只抬高 RqSys/RqKrnl 版本门槛（3.7 / 5.3）
"""
import io

SRC = r"C:\Users\MadeSpark\Desktop\测试\src\addin\elang_addin.cpp"
DST = r"C:\Users\MadeSpark\Desktop\测试\src\addin"

RET_OLD = "    return &s_LibInfo;"
RET_NEW = """    {
        /* V1 实验变体：以 LIB_INFO2 形态返回（模拟 AutoLinker 的 LBS_LIB_INFO2） */
        static LIB_INFO2 s_LibInfo2;
        s_LibInfo2.m_infLib = s_LibInfo;
        s_LibInfo2.m_szHardwareCode      = NULL;
        s_LibInfo2.m_szBuyingTips        = NULL;
        s_LibInfo2.m_szBuyingURL         = NULL;
        s_LibInfo2.m_szLicenseToUserName = (LPTSTR)"You";
        return (PLIB_INFO)&s_LibInfo2;
    }"""

s = io.open(SRC, encoding="utf-8").read()
assert RET_OLD in s, "return line not found"
v1 = s.replace(RET_OLD, RET_NEW)
io.open(DST + r"\v1_libinfo2.cpp", "w", encoding="utf-8").write(v1)

v2 = s.replace("s_LibInfo.m_nRqSysMinorVer         = 0;", "s_LibInfo.m_nRqSysMinorVer         = 7;")
v2 = v2.replace("s_LibInfo.m_nRqSysKrnlLibMajorVer  = 3;", "s_LibInfo.m_nRqSysKrnlLibMajorVer  = 5;")
v2 = v2.replace("s_LibInfo.m_nRqSysKrnlLibMinorVer  = 0;", "s_LibInfo.m_nRqSysKrnlLibMinorVer  = 3;")
assert v2 != s, "v2 replace failed"
io.open(DST + r"\v2_rqsys.cpp", "w", encoding="utf-8").write(v2)
print("variants written OK")
