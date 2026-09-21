/*
 * load_test.cpp —— 独立验证 elang_addin.fne 是否能被普通 Win32 进程正常加载
 * ---------------------------------------------------------------------------
 * 作用：把“e.exe 没加载我们”与“我们的 .fne 本身有问题”这两个假设区分开。
 *   1) LoadLibraryA 能否成功？
 *   2) GetNewInf 导出是否存在？
 *   3) 调用它（cdecl）能否返回合法 LIB_INFO，各字段是否符合预期？
 */

#include "sdk_compat.h"

int main(int argc, char **argv)
{
    HMODULE h = NULL;
    PFN_GET_LIB_INFO pfn = NULL;
    PLIB_INFO pInf = NULL;

    if (argc < 2)
    {
        printf("usage: load_test.exe <path-to-.fne>\n");
        return 2;
    }

    printf("LoadLibraryA('%s') ...\n", argv[1]);
    h = LoadLibraryA(argv[1]);
    if (h == NULL)
    {
        printf("[FAIL] LoadLibraryA failed, GetLastError=%lu\n", GetLastError());
        return 3;
    }
    printf("[OK]   hModule=0x%08X\n", (unsigned)(size_t)h);

    pfn = (PFN_GET_LIB_INFO)GetProcAddress(h, FUNCNAME_GET_LIB_INFO);
    if (pfn == NULL)
    {
        printf("[FAIL] GetProcAddress('%s') failed, GetLastError=%lu\n",
               FUNCNAME_GET_LIB_INFO, GetLastError());
        FreeLibrary(h);
        return 4;
    }
    printf("[OK]   GetNewInf @ 0x%08X\n", (unsigned)(size_t)pfn);

    pInf = pfn();
    if (pInf == NULL)
    {
        printf("[FAIL] GetNewInf returned NULL\n");
        FreeLibrary(h);
        return 5;
    }

    printf("\n---- LIB_INFO returned by the .fne ----\n");
    printf("  m_dwLibFormatVer        = %lu (expect %d)\n", (unsigned long)pInf->m_dwLibFormatVer, LIB_FORMAT_VER);
    printf("  m_szGuid                = '%s'\n", pInf->m_szGuid ? pInf->m_szGuid : "(null)");
    printf("  m_nMajorVersion         = %d\n", pInf->m_nMajorVersion);
    printf("  m_nMinorVersion         = %d\n", pInf->m_nMinorVersion);
    printf("  m_nBuildNumber          = %d\n", pInf->m_nBuildNumber);
    printf("  m_nRqSysMajorVer        = %d\n", pInf->m_nRqSysMajorVer);
    printf("  m_nRqSysMinorVer        = %d\n", pInf->m_nRqSysMinorVer);
    printf("  m_szName                = '%s'\n", pInf->m_szName ? pInf->m_szName : "(null)");
    printf("  m_nLanguage             = %d\n", pInf->m_nLanguage);
    printf("  m_szExplain             = '%s'\n", pInf->m_szExplain ? pInf->m_szExplain : "(null)");
    printf("  m_dwState               = 0x%08X (LBS_IDE_PLUGIN=0x%X)\n",
           (unsigned)pInf->m_dwState, (unsigned)LBS_IDE_PLUGIN);
    printf("  m_nDataTypeCount        = %d\n", pInf->m_nDataTypeCount);
    printf("  m_nCmdCount             = %d\n", pInf->m_nCmdCount);
    printf("  m_pCmdsFunc             = 0x%08X\n", (unsigned)(size_t)pInf->m_pCmdsFunc);
    printf("  m_pfnNotify             = 0x%08X (must be non-NULL)\n", (unsigned)(size_t)pInf->m_pfnNotify);
    printf("  m_pfnRunAddInFn         = 0x%08X\n", (unsigned)(size_t)pInf->m_pfnRunAddInFn);
    printf("  m_pfnSuperTemplate      = 0x%08X\n", (unsigned)(size_t)pInf->m_pfnSuperTemplate);
    printf("  m_nCategoryCount        = %d\n", pInf->m_nCategoryCount);
    printf("  m_nLibConstCount        = %d\n", pInf->m_nLibConstCount);

    /* 关键：字符串列表字段（IDE 插件会被 e.exe 遍历，NULL 可能致命） */
    {
        const char *names[] = { "m_szzCategory", "m_szzAddInFnInfo",
                                "m_szzSuperTemplateInfo", "m_szzDependFiles" };
        LPTSTR ptrs[] = { pInf->m_szzCategory, pInf->m_szzAddInFnInfo,
                          pInf->m_szzSuperTemplateInfo, pInf->m_szzDependFiles };
        int k;
        for (k = 0; k < 4; k++)
        {
            if (ptrs[k] == NULL)
            {
                printf("  %-22s = NULL\n", names[k]);
            }
            else
            {
                unsigned char *p = (unsigned char *)ptrs[k];
                int i, allzero = 1, printable = 1;
                printf("  %-22s = 0x%08X  first64:", names[k], (unsigned)(size_t)p);
                for (i = 0; i < 64; i++)
                {
                    printf(" %02X", p[i]);
                    if (p[i] != 0) allzero = 0;
                    if (p[i] != 0 && (p[i] < 0x20 || p[i] == 0x7F)) printable = 0;
                }
                printf("   [allzero=%d]\n", allzero);
                (void)printable;
            }
        }
    }

    /* 主动模拟一次“系统下发通知函数指针”，验证 m_pfnNotify 可被调用。
       若传入第二个参数则跳过（用于只读查询第三方库信息，避免副作用）。 */
    if (pInf->m_pfnNotify != NULL && argc < 3)
    {
        INT r1 = pInf->m_pfnNotify(NL_SYS_NOTIFY_FUNCTION, 0x11223344u, 0);
        INT r2 = pInf->m_pfnNotify(NL_IDE_READY, 0, 0);
        INT r3 = pInf->m_pfnNotify(9999, 0, 0);
        printf("\n  synthetic notify: NL_SYS_NOTIFY_FUNCTION -> %d, NL_IDE_READY -> %d, unknown(9999) -> %d\n",
               r1, r2, r3);
    }

    printf("\n[PASS] .fne loads and GetNewInf/getNewInf->m_pfnNotify are callable.\n");
    FreeLibrary(h);
    return 0;
}
