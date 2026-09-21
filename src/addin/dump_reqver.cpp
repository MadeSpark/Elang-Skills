/* dump_reqver.cpp —— 打印 .fne 的 LIB_INFO 关键字段（重点 RqSys 版本与 State） */
#include <windows.h>
#include <stdio.h>
#include "sdk_compat.h"

typedef PLIB_INFO (*PFN_GET_LIB_INFO)(void);

int main(int argc, char **argv)
{
    if (argc < 2) { printf("usage: dump_reqver.exe <fne>\n"); return 2; }
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary fail %lu\n", GetLastError()); return 3; }
    PFN_GET_LIB_INFO pfn = (PFN_GET_LIB_INFO)GetProcAddress(h, "GetNewInf");
    if (!pfn) { printf("no GetNewInf\n"); return 4; }
    PLIB_INFO p = pfn();
    if (!p) { printf("GetNewInf returned NULL\n"); return 5; }
    printf("lib            = %s\n", argv[1]);
    printf("m_szName       = %s\n", p->m_szName);
    printf("m_nMajorVersion= %d\n", (int)p->m_nMajorVersion);
    printf("m_nMinorVersion= %d\n", (int)p->m_nMinorVersion);
    printf("m_nBuildNumber = %d\n", (int)p->m_nBuildNumber);
    printf("RqSysMajorVer  = %d   <-- e.exe 版本门槛(低)\n", (int)p->m_nRqSysMajorVer);
    printf("RqSysMinorVer  = %d\n", (int)p->m_nRqSysMinorVer);
    printf("RqKrnlMajor    = %d   <-- 核心库版本门槛\n", (int)p->m_nRqSysKrnlLibMajorVer);
    printf("RqKrnlMinor    = %d\n", (int)p->m_nRqSysKrnlLibMinorVer);
    printf("m_dwState      = 0x%08lX\n", (unsigned long)p->m_dwState);
    printf("m_dwLibFormatVer = %lu\n", (unsigned long)p->m_dwLibFormatVer);
    if (p->m_dwState & (1 << 7))
    {
        PLIB_INFO2 p2 = (PLIB_INFO2)p;
        printf("-- LIB_INFO2 extension (LBS_LIB_INFO2 set) --\n");
        printf("m_szHardwareCode      = %s\n", p2->m_szHardwareCode ? p2->m_szHardwareCode : "(null)");
        printf("m_szBuyingTips        = %s\n", p2->m_szBuyingTips ? p2->m_szBuyingTips : "(null)");
        printf("m_szBuyingURL         = %s\n", p2->m_szBuyingURL ? p2->m_szBuyingURL : "(null)");
        printf("m_szLicenseToUserName = %s\n", p2->m_szLicenseToUserName ? p2->m_szLicenseToUserName : "(null)");
    }
    else
    {
        printf("-- no LBS_LIB_INFO2 --\n");
    }
    return 0;
}
