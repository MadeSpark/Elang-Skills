/*
 * offset_probe.cpp —— 结构体 packing / 字段偏移「自证」小程序
 * ---------------------------------------------------------------------------
 * 用与 elang_addin.fne **完全相同**的头文件（sdk_compat.h）编译，运行后把
 * LIB_INFO 的每个字段偏移、sizeof，以及关键 FN_* / NL_* / NES_* 宏值打印出来，
 * 供人工核对（这正是 PoC-1 汇报第 ⑦⑧ 项要求的偏移表）。
 *
 * 它不是支持库，只是一个普通控制台程序，用来把“编译期事实”落地成可审计文本。
 */

#include "sdk_compat.h"

#define P(field) \
    printf("  offsetof(LIB_INFO, %-24s) = %3u\n", #field, (unsigned)offsetof(LIB_INFO, field))

/* ===========================================================================
 * --dump-libinfo <path-to-fne>：加载一个 .fne 并打印其 LIB_INFO 的关键字段
 * ---------------------------------------------------------------------------
 * 目的（team-lead 2026-09-21 要求）：让 mkcage **从 .fne 现取** Guid / 库名 /
 * 版本，避免 .fne 重建后 Guid 漂移导致“加壳产物声明旧 Guid → e.exe 加载失败”。
 * 输出为**机器可读**的 `KEY=... / GUID=... / NAME=... / MAJOR=... / MINOR=...`
 * （中文按 GBK 原样输出；调用方按 GBK 解码）。GUID 保持 .fne 声明的大小写，
 * 由调用方决定是否 lower()。
 * =========================================================================== */
static int dump_libinfo(const char *pszFne)
{
    HMODULE h;
    PFN_GET_LIB_INFO pfn;
    PLIB_INFO pInf;
    const char *szKey;      /* KEY 约定 = .fne 文件名（去目录、去扩展名） */
    char szKeyBuf[260];
    int  i;

    szKey = strrchr(pszFne, '\\');
    if (szKey == NULL) szKey = strrchr(pszFne, '/');
    szKey = (szKey == NULL) ? pszFne : szKey + 1;
    strncpy(szKeyBuf, szKey, sizeof(szKeyBuf) - 1);
    szKeyBuf[sizeof(szKeyBuf) - 1] = '\0';
    for (i = 0; szKeyBuf[i]; i++)
        if (szKeyBuf[i] == '.') { szKeyBuf[i] = '\0'; break; }

    h = LoadLibraryA(pszFne);
    if (h == NULL)
    {
        fprintf(stderr, "[FAIL] LoadLibraryA('%s') err=%lu\n", pszFne, GetLastError());
        return 3;
    }
    pfn = (PFN_GET_LIB_INFO)GetProcAddress(h, FUNCNAME_GET_LIB_INFO);
    if (pfn == NULL)
    {
        fprintf(stderr, "[FAIL] GetProcAddress('%s') err=%lu\n",
                FUNCNAME_GET_LIB_INFO, GetLastError());
        FreeLibrary(h);
        return 4;
    }
    pInf = pfn();
    if (pInf == NULL)
    {
        fprintf(stderr, "[FAIL] GetNewInf() returned NULL\n");
        FreeLibrary(h);
        return 5;
    }

    printf("KEY=%s\n", szKeyBuf);
    printf("GUID=%s\n", pInf->m_szGuid ? pInf->m_szGuid : "");
    printf("NAME=%s\n", pInf->m_szName ? pInf->m_szName : "");
    printf("MAJOR=%d\n", pInf->m_nMajorVersion);
    printf("MINOR=%d\n", pInf->m_nMinorVersion);
    printf("LIBFORMATVER=%lu\n", (unsigned long)pInf->m_dwLibFormatVer);
    printf("CMDCOUNT=%d\n", pInf->m_nCmdCount);

    FreeLibrary(h);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && strcmp(argv[1], "--dump-libinfo") == 0)
        return dump_libinfo(argv[2]);

    printf("== elang_addin struct layout self-test ==\n");
    printf("sizeof(void*)   = %u\n", (unsigned)sizeof(void *));
    printf("sizeof(DWORD)   = %u\n", (unsigned)sizeof(DWORD));
    printf("sizeof(INT)     = %u\n", (unsigned)sizeof(INT));
    printf("sizeof(LPTSTR)  = %u\n", (unsigned)sizeof(LPTSTR));
    printf("sizeof(LIB_INFO)= %u\n", (unsigned)sizeof(LIB_INFO));
    printf("sizeof(LIB_INFO2)=%u\n", (unsigned)sizeof(LIB_INFO2));
    printf("sizeof(MDATA_INF)=%u\n", (unsigned)sizeof(MDATA_INF));
    printf("sizeof(CMD_INFO) =%u\n", (unsigned)sizeof(CMD_INFO));
    printf("sizeof(LIB_DATA_TYPE_INFO)=%u\n", (unsigned)sizeof(LIB_DATA_TYPE_INFO));
    printf("sizeof(LIB_CONST_INFO)=%u\n", (unsigned)sizeof(LIB_CONST_INFO));
    printf("\n-- LIB_INFO field offsets --\n");
    P(m_dwLibFormatVer);
    P(m_szGuid);
    P(m_nMajorVersion);
    P(m_nMinorVersion);
    P(m_nBuildNumber);
    P(m_nRqSysMajorVer);
    P(m_nRqSysMinorVer);
    P(m_nRqSysKrnlLibMajorVer);
    P(m_nRqSysKrnlLibMinorVer);
    P(m_szName);
    P(m_nLanguage);
    P(m_szExplain);
    P(m_dwState);
    P(m_szAuthor);
    P(m_szZipCode);
    P(m_szAddress);
    P(m_szPhoto);
    P(m_szFax);
    P(m_szEmail);
    P(m_szHomePage);
    P(m_szOther);
    P(m_nDataTypeCount);
    P(m_pDataType);
    P(m_nCategoryCount);
    P(m_szzCategory);
    P(m_nCmdCount);
    P(m_pBeginCmdInfo);
    P(m_pCmdsFunc);
    P(m_pfnRunAddInFn);
    P(m_szzAddInFnInfo);
    P(m_pfnNotify);
    P(m_pfnSuperTemplate);
    P(m_szzSuperTemplateInfo);
    P(m_nLibConstCount);
    P(m_pLibConst);
    P(m_szzDependFiles);

    printf("\n-- critical offsets (expected) --\n");
    printf("  m_dwState   expect 48  got %u\n", (unsigned)offsetof(LIB_INFO, m_dwState));
    printf("  m_pCmdsFunc expect 108 got %u\n", (unsigned)offsetof(LIB_INFO, m_pCmdsFunc));
    printf("  m_pfnNotify expect 120 got %u\n", (unsigned)offsetof(LIB_INFO, m_pfnNotify));
    printf("  sizeof      expect 144 got %u\n", (unsigned)sizeof(LIB_INFO));

    printf("\n-- key macros --\n");
    printf("  LIB_FORMAT_VER            = %d\n", LIB_FORMAT_VER);
    printf("  LBS_IDE_PLUGIN            = 0x%X\n", (unsigned)LBS_IDE_PLUGIN);
    printf("  NL_SYS_NOTIFY_FUNCTION    = %d\n", NL_SYS_NOTIFY_FUNCTION);
    printf("  NL_IDE_READY              = %d\n", NL_IDE_READY);
    printf("  NL_RIGHT_POPUP_MENU_SHOW  = %d\n", NL_RIGHT_POPUP_MENU_SHOW);
    printf("  NL_GET_CMD_FUNC_NAMES     = %d\n", NL_GET_CMD_FUNC_NAMES);
    printf("  NES_GET_MAIN_HWND         = %d\n", NES_GET_MAIN_HWND);
    printf("  NES_RUN_FUNC              = %d\n", NES_RUN_FUNC);
    printf("  FN_COMPILE_AND_RUN        = 0x%08X\n", (unsigned)FN_COMPILE_AND_RUN);
    printf("  FN_END_RUN                = 0x%08X\n", (unsigned)FN_END_RUN);
    printf("  FN_OPEN_FILE2             = 0x%08X\n", (unsigned)FN_OPEN_FILE2);
    printf("  FN_ADD_TAB                = 0x%08X\n", (unsigned)FN_ADD_TAB);
    printf("  FN_SWITCH_OUTPUT_BAR      = 0x%08X\n", (unsigned)FN_SWITCH_OUTPUT_BAR);
    printf("  FN_IS_FUNC_ENABLED        = 0x%08X\n", (unsigned)FN_IS_FUNC_ENABLED);
    printf("  FN_STEP_INTO              = 0x%08X\n", (unsigned)FN_STEP_INTO);
    printf("  FN_SET_BREAK_POINTER      = 0x%08X\n", (unsigned)FN_SET_BREAK_POINTER);

    printf("\n[PASS] all compile-time static_asserts already verified at build time.\n");
    return 0;
}
