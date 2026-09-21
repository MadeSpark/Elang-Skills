/*
 * elang_addin.cpp —— 「AI调试宿主」易语言支持库（PoC-1）
 * ===========================================================================
 * 目标（路线④a「独立技能 + 官方宿主化」的第一个 PoC）：
 *   验证一个“全新的”、第三方编译的 .fne 丢进 D:\ides\e\lib\ 后，
 *   e.exe 启动时到底会不会把它当普通支持库自动加载，并打通官方通知链路：
 *
 *     第 0 步：新库能否被 e.exe 自动加载（本 PoC 第一断言）
 *     第 1 步：探测官方钩子 —— 至少确认收到 NL_SYS_NOTIFY_FUNCTION 与 NL_IDE_READY，
 *              并把全过程写入 trace 文件
 *     第 2 步：读取“AI 工具提交的参数”
 *              （环境变量 ELANG_AI_TASK 首选 / 命令行 --ai-task= / 兜底
 *                %TEMP%\elang_ai_task.ini）；三者都没有 → 立刻静默返回，零影响
 *     第 3 步：只读探测 —— NES_GET_MAIN_HWND 取主窗口（应为类名 ENewFrame）、
 *              FN_IS_FUNC_ENABLED 查询功能号、hide=1 隐藏窗口、
 *              EnumChildWindows 列子窗口、记录 GetCommandLineA/GetModuleFileNameA
 *
 * 线程纪律（硬要求）：
 *   通知回调运行在 e.exe 的线程上，**绝不能阻塞**。收到 NL_IDE_READY 后只做
 *   “置位 + CreateThread”，真正的第 2/3 步全部在工作线程里做。
 *
 * 零影响（硬要求）：
 *   非 AI 模式（既无 AI 环境变量、也无 --ai-task= 参数、也无 ini）时，
 *   本库在 NL_IDE_READY 处直接 return，不写任何文件、不改任何窗口、不起线程。
 *
 * 编译：见同目录 build.sh（MinGW-w64 32 位 g++，-D__GCC_）。
 * 说明：为与官方 SDK 的 VC6/__cdecl 约定完全一致，sdk_compat.h 把 WINAPI 还原成
 *       空宏；因此本文件不再自定义 DllMain（避免调用约定错配），
 *       所有初始化都在 GetNewInf() 里惰性完成。
 */

#include "sdk_compat.h"

/* ==========================================================================
 * 0. 编译期自证：结构体 packing / 字段偏移
 * ========================================================================== */

/* 全程以 32 位编译（指针 4 字节）。LIB_INFO 全部成员均为 4 字节，天然无填充。 */
static_assert(sizeof(void *) == 4, "elang_addin must be compiled as 32-bit (x86)");
static_assert(sizeof(LIB_INFO) == 144, "LIB_INFO must be 144 bytes (36 x 4)");
static_assert(offsetof(LIB_INFO, m_dwState)   == 48,  "LIB_INFO.m_dwState offset mismatch");
static_assert(offsetof(LIB_INFO, m_nCmdCount) == 100, "LIB_INFO.m_nCmdCount offset mismatch");
static_assert(offsetof(LIB_INFO, m_pCmdsFunc) == 108, "LIB_INFO.m_pCmdsFunc offset mismatch");
static_assert(offsetof(LIB_INFO, m_pfnNotify) == 120, "LIB_INFO.m_pfnNotify offset mismatch");
static_assert(offsetof(LIB_INFO, m_pLibConst) == 136, "LIB_INFO.m_pLibConst offset mismatch");

/* ==========================================================================
 * 1. 常量 / 全局状态
 * ========================================================================== */

#define LI_LIB_GUID_STR   "7A1E4F22C3B0499E8D6A0011223344FE"
#define LI_LIB_NAME       "AI调试宿主"
#define LI_LIB_EXPLAIN    "PoC：读取启动参数、探测易语言官方 IDE 接口（只读，不修改任何工程）。"

/* m_dwState 取值（可用 -DLI_LIB_STATE=... 覆盖做实验）。
   参考真实库：dp1/cncnv = 0xC0000000(OS_WIN|OS_LINUX)，etools(纯 IDE 插件,0 命令)
   = 0xE0000104 = OS_ALL | LBS_IDE_PLUGIN | LBS_FUNC_NO_RUN_CODE。 */
#ifndef LI_LIB_STATE
#define LI_LIB_STATE      ((DWORD)(OS_ALL | LBS_IDE_PLUGIN | LBS_FUNC_NO_RUN_CODE))
#endif

#define TRACE_ENV_VAR     "ELANG_AI_TRACE"
#define TASK_ENV_VAR      "ELANG_AI_TASK"
#define HIDE_ENV_VAR      "ELANG_AI_HIDE"
#define CMDLINE_TASK_OPT  "--ai-task="
#define INI_FILE_NAME     "elang_ai_task.ini"

/* ---- PoC-2：真正触发“调试运行”并捕获调试面板 ---- */
#define RUN_ENV_VAR       "ELANG_AI_RUN"         /* =1 → 允许 NES_RUN_FUNC(FN_COMPILE_AND_RUN) */
#define RUN_DELAY_ENV_VAR "ELANG_AI_RUN_DELAY"   /* 触发前等待毫秒（默认 1500） */
#define STOP_ENV_VAR      "ELANG_AI_STOP"        /* >0 → 运行 N 秒后发 FN_END_RUN（收尾2） */
#define CAP_ENV_VAR       "ELANG_AI_CAPTURE"     /* 面板增量落盘文件（默认 %TEMP%\elang_ai_capture.txt） */

/* 由 e.exe 提供、由“本库回调”的整条链路，实测全部是 __stdcall（callee 清栈）：
 *   - 本库的 m_pfnNotify      （e.exe 以 __stdcall 调用，见 e.exe+0x46087A 之后不调栈）
 *   - e.exe 的 NotifySys 0x467FF0（末尾 `ret $0xc`，见 e.exe+0x46800D）
 * 官方 SDK 的 WINAPI 为空(__cdecl)是误导；若按 cdecl 调用系统 NotifySys，callee 已清
 * 3 个参数、我们再加 esp 会双重清栈 → 工作线程栈整体高 0xC → 读/写错位（实测会把
 * 只读数据当结果、并让 e.exe 提前退出）。故此处显式使用 __stdcall 的函数指针类型。 */
typedef INT (__stdcall *PFN_NOTIFY_SYS_STD)(INT nMsg, DWORD dwParam1, DWORD dwParam2);

static LIB_INFO          s_LibInfo;          /* 惰性填充，见 GetNewInf() */
static PFN_NOTIFY_SYS_STD g_fnNotifySys = NULL;
static BOOL             g_blAiMode    = FALSE;
static volatile LONG    g_lInitDone   = 0;
static volatile LONG    g_lWorkerRun  = 0;
static CRITICAL_SECTION g_cs;
static BOOL             g_blCsReady  = FALSE;
static char             g_szTracePath[MAX_PATH] = { 0 };

/* ==========================================================================
 * 2. 小工具
 * ========================================================================== */

/* 读环境变量；不存在或为空则返回 FALSE。 */
static BOOL GetEnvStr(const char *pszName, char *pszBuf, DWORD dwCap)
{
    DWORD dwLen;
    if (pszBuf == NULL || dwCap == 0) return FALSE;
    pszBuf[0] = '\0';
    if (pszName == NULL) return FALSE;
    dwLen = GetEnvironmentVariableA(pszName, pszBuf, dwCap);
    if (dwLen == 0 || dwLen >= dwCap) { pszBuf[0] = '\0'; return FALSE; }
    return TRUE;
}

/* 取 %TEMP% / %TMP% 目录（结尾带反斜杠）。 */
static BOOL GetTempDir(char *pszBuf, DWORD dwCap)
{
    DWORD n = GetTempPathA(dwCap, pszBuf);
    return (n > 0 && n < dwCap);
}

/* 判断命令行里是否出现指定选项前缀；若 pszValue 非空则把其后的值拷出来。 */
static BOOL CommandLineHasOpt(const char *pszOpt, char *pszValue, DWORD dwValueCap)
{
    const char *pszCmd = GetCommandLineA();
    const char *pszHit;
    if (pszValue != NULL && dwValueCap > 0) pszValue[0] = '\0';
    if (pszCmd == NULL || pszOpt == NULL) return FALSE;
    pszHit = strstr(pszCmd, pszOpt);
    if (pszHit == NULL) return FALSE;
    if (pszValue != NULL && dwValueCap > 0)
    {
        const char *pszBegin = pszHit + strlen(pszOpt);
        DWORD       i = 0;
        /* 值以双引号包裹时去掉引号，遇空格/引号/结尾停止 */
        if (*pszBegin == '"') pszBegin++;
        while (*pszBegin && *pszBegin != '"' && *pszBegin != ' ' && i + 1 < dwValueCap)
            pszValue[i++] = *pszBegin++;
        pszValue[i] = '\0';
    }
    return TRUE;
}

/* 兜底 ini 路径：%TEMP%\elang_ai_task.ini */
static BOOL GetFallbackIniPath(char *pszBuf, DWORD dwCap)
{
    char szTemp[MAX_PATH] = { 0 };
    if (!GetTempDir(szTemp, MAX_PATH)) return FALSE;
    if (snprintf(pszBuf, dwCap, "%s%s", szTemp, INI_FILE_NAME) <= 0) return FALSE;
    return (GetFileAttributesA(pszBuf) != INVALID_FILE_ATTRIBUTES);
}

/* ==========================================================================
 * 3. trace（仅 AI 模式启用；写不进去也绝不抛错）
 * ========================================================================== */

static void TraceInit(void)
{
    char szEnv[MAX_PATH] = { 0 };
    if (!g_blCsReady)
    {
        InitializeCriticalSection(&g_cs);
        g_blCsReady = TRUE;
    }
    if (g_szTracePath[0] != '\0') return;
    if (GetEnvStr(TRACE_ENV_VAR, szEnv, MAX_PATH))
        snprintf(g_szTracePath, MAX_PATH, "%s", szEnv);
    else
    {
        char szTemp[MAX_PATH] = { 0 };
        if (GetTempDir(szTemp, MAX_PATH))
            snprintf(g_szTracePath, MAX_PATH, "%selang_ai_host_trace.log", szTemp);
    }
}

static void Trace(const char *pszFmt, ...)
{
    char    szLine[2048] = { 0 };
    va_list ap;
    int     nLen;
    HANDLE  hFile;
    DWORD   dwWritten = 0;
    SYSTEMTIME st;

    if (!g_blAiMode) return;
    if (g_szTracePath[0] == '\0') TraceInit();
    if (g_szTracePath[0] == '\0') return;

    GetLocalTime(&st);
    nLen = snprintf(szLine, sizeof(szLine), "[%02d:%02d:%02d.%03d][tid=%lu] ",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    (unsigned long)GetCurrentThreadId());
    if (nLen < 0) return;

    va_start(ap, pszFmt);
    vsnprintf(szLine + nLen, sizeof(szLine) - (size_t)nLen - 3, pszFmt, ap);
    va_end(ap);

    nLen = (int)strlen(szLine);
    if (nLen > (int)sizeof(szLine) - 3) nLen = (int)sizeof(szLine) - 3;
    szLine[nLen++] = '\r';
    szLine[nLen++] = '\n';
    szLine[nLen]   = '\0';

    if (g_blCsReady) EnterCriticalSection(&g_cs);
    hFile = CreateFileA(g_szTracePath, FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, szLine, (DWORD)nLen, &dwWritten, NULL);
        CloseHandle(hFile);
    }
    if (g_blCsReady) LeaveCriticalSection(&g_cs);
}

/* ==========================================================================
 * 3b. 崩溃陷阱（仅诊断构建 -DPOC_CRASHTRAP 启用）
 * --------------------------------------------------------------------------
 * 目的：当 e.exe 因加载本库而 AV 时，把「异常码 / 出错地址 / 出错指令 /
 *      寄存器现场 / 调用栈候选返回地址 + 所属模块」无条件下写
 *      %TEMP%\elang_addin_crashtrap.txt，从而定位到底是 e.exe 自己的代码
 *      崩、还是调用进了我们库里的地址崩。
 *
 * 手段：AddVectoredExceptionHandler(1, ...) —— VEH 在该进程任何异常发生时会
 *      最先被调用（包括 e.exe 自己 try/except 捕获之前的时刻），且不依赖任何
 *      环境变量（与 SelfCheck 一样无条件落盘）。
 * ========================================================================== */
#ifdef POC_CRASHTRAP
#define CRASH_MAX_MOD 384

typedef struct _CRASH_MODRANGE
{
    DWORD  base;
    DWORD  size;
    char   name[64];
} CRASH_MODRANGE;

static CRASH_MODRANGE  g_CrashMods[CRASH_MAX_MOD];
static int             g_nCrashMods = 0;
static volatile LONG   g_lCrashLogged = 0;

/* 取映像大小（读 PE 头 OptionalHeader.SizeOfImage）。失败返回 0。 */
static DWORD PeImageSize(HMODULE h)
{
    const unsigned char *p = (const unsigned char *)h;
    DWORD dwLfanew;
    const unsigned char *nt;
    if (p == NULL) return 0;
    if (p[0] != 'M' || p[1] != 'Z') return 0;
    dwLfanew = *(const DWORD *)(p + 0x3C);
    nt = p + dwLfanew;
    if (nt[0] != 'P' || nt[1] != 'E') return 0;
    return *(const DWORD *)(nt + 0x18 + 0x38);   /* NT(4)+FileHdr(20)=0x18; SizeOfImage=+0x38 */
}

/* 枚举本进程全部模块基址/大小。
   ⚠️ 不用 ToolHelp32（MinGW 下对本工程出现未解析符号），改用 PEB 手工遍历——
   零额外导入，且在本进程内总是可用。 */
static void CrashSnapshotModules(void)
{
    /* x86 PE 结构（仅取需要的字段，全部 4 字节对齐）。 */
    typedef struct _MY_UNICODE_STRING { USHORT Length; USHORT MaxLength; PWSTR Buffer; } MY_UNICODE_STRING;
    typedef struct _MY_LDR_ENTRY
    {
        LIST_ENTRY     InLoadOrderLinks;              /* +0x00 */
        LIST_ENTRY     InMemoryOrderLinks;            /* +0x08 */
        LIST_ENTRY     InInitializationOrderLinks;    /* +0x10 */
        PVOID          DllBase;                       /* +0x18 */
        PVOID          EntryPoint;                    /* +0x1C */
        ULONG          SizeOfImage;                   /* +0x20 */
        MY_UNICODE_STRING FullDllName;                /* +0x24 */
        MY_UNICODE_STRING BaseDllName;                /* +0x2C */
    } MY_LDR_ENTRY;
    typedef struct _MY_PEB_LDR
    {
        ULONG      Length;            /* +0x00 */
        BOOLEAN    Initialized;       /* +0x04 */
        PVOID      SsHandle;          /* +0x08 */
        LIST_ENTRY InLoadOrderModuleList;        /* +0x0C */
        LIST_ENTRY InMemoryOrderModuleList;      /* +0x14 */
    } MY_PEB_LDR;

    PVOID        pPeb;
    MY_PEB_LDR  *pLdr;
    LIST_ENTRY  *pHead;
    LIST_ENTRY  *pNode;
    int          nGuard;

    g_nCrashMods = 0;

    /* 先放进「主程序」项，PEB 遍历失败时也能映射。 */
    {
        HMODULE hMain = GetModuleHandleA(NULL);
        if (hMain != NULL && g_nCrashMods < CRASH_MAX_MOD)
        {
            g_CrashMods[g_nCrashMods].base = (DWORD)(size_t)hMain;
            g_CrashMods[g_nCrashMods].size = PeImageSize(hMain);
            lstrcpynA(g_CrashMods[g_nCrashMods].name, "<main-exe>", 63);
            g_nCrashMods++;
        }
    }

    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(pPeb));
    if (pPeb == NULL) return;
    pLdr = (MY_PEB_LDR *)(*(PVOID *)((unsigned char *)pPeb + 0x0C));
    if (pLdr == NULL) return;

    pHead = &pLdr->InMemoryOrderModuleList;
    pNode = pHead->Flink;
    for (nGuard = 0; nGuard < CRASH_MAX_MOD && pNode != NULL && pNode != pHead; nGuard++)
    {
        MY_LDR_ENTRY *pEnt = (MY_LDR_ENTRY *)((unsigned char *)pNode - 0x08);
        if (g_nCrashMods < CRASH_MAX_MOD && pEnt->DllBase != NULL)
        {
            int k = 0;
            g_CrashMods[g_nCrashMods].base = (DWORD)(size_t)pEnt->DllBase;
            g_CrashMods[g_nCrashMods].size = (DWORD)pEnt->SizeOfImage;
            g_CrashMods[g_nCrashMods].name[0] = '\0';
            if (pEnt->BaseDllName.Buffer != NULL)
            {
                /* UNICODE → 近似 ANSI：只取低字节（模块名基本是 ASCII）。 */
                for (k = 0; k < (int)(pEnt->BaseDllName.Length / sizeof(WCHAR)) && k < 63; k++)
                    g_CrashMods[g_nCrashMods].name[k] = (char)(pEnt->BaseDllName.Buffer[k] & 0xFF);
                g_CrashMods[g_nCrashMods].name[k] = '\0';
            }
            if (g_CrashMods[g_nCrashMods].name[0] == '\0')
                lstrcpynA(g_CrashMods[g_nCrashMods].name, "?", 63);
            g_nCrashMods++;
        }
        pNode = pNode->Flink;
    }
}

/* 把地址映射到「模块名+偏移」。返回静态缓冲里的描述串（非线程安全，崩溃时够用）。 */
static const char *CrashMapAddr(DWORD a, char *pszOut, int nOutCap)
{
    int i;
    for (i = 0; i < g_nCrashMods; i++)
    {
        DWORD b = g_CrashMods[i].base;
        DWORD s = g_CrashMods[i].size;
        if (s != 0 && a >= b && a < b + s)
        {
            snprintf(pszOut, (size_t)nOutCap, "%s+0x%X", g_CrashMods[i].name, (unsigned)(a - b));
            return pszOut;
        }
    }
    snprintf(pszOut, (size_t)nOutCap, "?%08X", (unsigned)a);
    return pszOut;
}

/* 无条件追写一行到 %TEMP%\elang_addin_crashtrap.txt。 */
static void CrashWriteLine(const char *pszLine)
{
    char   szTemp[MAX_PATH] = { 0 };
    char   szPath[MAX_PATH] = { 0 };
    HANDLE hFile;
    DWORD  dwWritten = 0;

    if (GetTempPathA(MAX_PATH, szTemp) == 0) return;
    snprintf(szPath, MAX_PATH, "%selang_addin_crashtrap.txt", szTemp);
    hFile = CreateFileA(szPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, pszLine, (DWORD)strlen(pszLine), &dwWritten, NULL);
        CloseHandle(hFile);
    }
}

/* ⚠️ VEH 回调由系统以 __stdcall 调用；sdk_compat.h 已把 WINAPI 还原成空，故此处
   必须显式写 __stdcall（同 WorkerProc）。 */
static LONG __stdcall CrashTrapVeh(PEXCEPTION_POINTERS pep)
{
    const EXCEPTION_RECORD *er;
    const CONTEXT          *ctx;
    char   szBuf[512] = { 0 };
    char   szMap[96]  = { 0 };
    DWORD  dwCode;
    DWORD  dwAccess = 0;
    DWORD  dwAccessAddr = 0;

    if (pep == NULL || pep->ExceptionRecord == NULL || pep->ContextRecord == NULL)
        return EXCEPTION_CONTINUE_SEARCH;

    er  = pep->ExceptionRecord;
    ctx = pep->ContextRecord;
    dwCode = er->ExceptionCode;

    /* 只关心致命异常，避免把程序里正常的 SEH 流控也当崩溃记。 */
    if (dwCode != EXCEPTION_ACCESS_VIOLATION &&
        dwCode != EXCEPTION_ILLEGAL_INSTRUCTION &&
        dwCode != EXCEPTION_PRIV_INSTRUCTION &&
        dwCode != EXCEPTION_STACK_OVERFLOW &&
        dwCode != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        dwCode != 0xC0000409)   /* STATUS_STACK_BUFFER_OVERRUN (/GS) */
        return EXCEPTION_CONTINUE_SEARCH;

    if (InterlockedCompareExchange(&g_lCrashLogged, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    if (dwCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
    {
        dwAccess     = er->ExceptionInformation[0];   /* 0=read 1=write 8=exec */
        dwAccessAddr = (DWORD)er->ExceptionInformation[1];
    }

    CrashSnapshotModules();

    snprintf(szBuf, sizeof(szBuf),
             "================ CRASHTRAP (pid=%lu) ================\r\n",
             (unsigned long)GetCurrentProcessId());
    CrashWriteLine(szBuf);

    CrashMapAddr((DWORD)(size_t)er->ExceptionAddress, szMap, sizeof(szMap));
    snprintf(szBuf, sizeof(szBuf),
             "ExceptionCode   = 0x%08X\r\n"
             "ExceptionAddr   = 0x%08X  (%s)\r\n",
             (unsigned)dwCode, (unsigned)(size_t)er->ExceptionAddress, szMap);
    CrashWriteLine(szBuf);

    if (dwAccess || dwAccessAddr)
    {
        CrashMapAddr(dwAccessAddr, szMap, sizeof(szMap));
        snprintf(szBuf, sizeof(szBuf),
                 "AccessType      = %s\r\n"
                 "AccessAddr      = 0x%08X  (%s)\r\n",
                 dwAccess == 0 ? "READ" : (dwAccess == 1 ? "WRITE" : (dwAccess == 8 ? "EXEC" : "?")),
                 (unsigned)dwAccessAddr, szMap);
        CrashWriteLine(szBuf);
    }

    CrashMapAddr((DWORD)ctx->Eip, szMap, sizeof(szMap));
    snprintf(szBuf, sizeof(szBuf),
             "EIP=0x%08X (%s)  ESP=0x%08X  EBP=0x%08X  EAX=0x%08X  EBX=0x%08X  ECX=0x%08X  EDX=0x%08X  ESI=0x%08X  EDI=0x%08X\r\n",
             (unsigned)ctx->Eip, szMap,
             (unsigned)ctx->Esp, (unsigned)ctx->Ebp,
             (unsigned)ctx->Eax, (unsigned)ctx->Ebx, (unsigned)ctx->Ecx,
             (unsigned)ctx->Edx, (unsigned)ctx->Esi, (unsigned)ctx->Edi);
    CrashWriteLine(szBuf);

    /* --- 调用栈候选：扫 [ESP, ESP+1024) 里落在已知模块内的 DWORD --- */
    CrashWriteLine("-- stack scan (candidate return addresses inside known modules) --\r\n");
    {
        DWORD *pStack;
        int    i;
        DWORD  lo = ctx->Esp;
        DWORD  hi = ctx->Esp + 1024;
        /* 防止越界读：确保栈区可读 */
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((LPCVOID)lo, &mbi, sizeof(mbi)) != 0)
        {
            DWORD dwRegionEnd = (DWORD)(size_t)mbi.BaseAddress + mbi.RegionSize;
            if (hi > dwRegionEnd) hi = dwRegionEnd;
        }
        pStack = (DWORD *)(size_t)lo;
        for (i = 0; i < (int)((hi - lo) / 4); i++)
        {
            DWORD v = pStack[i];
            int   k;
            for (k = 0; k < g_nCrashMods; k++)
            {
                DWORD b = g_CrashMods[k].base;
                DWORD s = g_CrashMods[k].size;
                if (s != 0 && v >= b && v < b + s)
                {
                    snprintf(szBuf, sizeof(szBuf),
                             "  stack[+0x%03X] = 0x%08X  ->  %s+0x%X\r\n",
                             (unsigned)(i * 4), (unsigned)v,
                             g_CrashMods[k].name, (unsigned)(v - b));
                    CrashWriteLine(szBuf);
                    break;
                }
            }
        }
    }
    CrashWriteLine("================ end CRASHTRAP ================\r\n\r\n");

    /* 不吞掉异常：交回给系统 / e.exe 自己的处理（保持原有崩溃行为，便于观察退出码）。 */
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif /* POC_CRASHTRAP */

/* ==========================================================================
 * 4. 与 IDE 交互的小封装
 * ========================================================================== */

/* NES_GET_MAIN_HWND：取易语言主窗口句柄（预期类名 ENewFrame）。 */
static HWND IdeGetMainHwnd(void)
{
    if (g_fnNotifySys == NULL) return NULL;
    return (HWND)g_fnNotifySys(NES_GET_MAIN_HWND, 0, 0);
}

/* NES_RUN_FUNC：请 IDE 执行一个功能。
   dwParam1 = 功能号；dwParam2 = 双 DWORD 数组指针 {功能参数1, 功能参数2}。
   返回真表示该功能被 IDE 处理。 */
static BOOL IdeRunFunc(DWORD dwFuncNo, DWORD dwArg1, DWORD dwArg2)
{
    DWORD adwArgs[2];
    INT   nRet;
    if (g_fnNotifySys == NULL) return FALSE;
    adwArgs[0] = dwArg1;
    adwArgs[1] = dwArg2;
    nRet = g_fnNotifySys(NES_RUN_FUNC, dwFuncNo, (DWORD)(size_t)adwArgs);
    return nRet ? TRUE : FALSE;
}

/* FN_IS_FUNC_ENABLED：查询指定功能当前是否可用。 */
static BOOL IdeIsFuncEnabled(DWORD dwFuncNo, BOOL *pblHandled)
{
    BOOL blEnabled = FALSE;
    BOOL blHandled = IdeRunFunc(FN_IS_FUNC_ENABLED, dwFuncNo, (DWORD)(size_t)&blEnabled);
    if (pblHandled != NULL) *pblHandled = blHandled;
    return blEnabled;
}

/* ==========================================================================
 * 5. 第 2 步：读取 AI 工具提交的参数
 * ========================================================================== */

/* 返回 TRUE 并写出任务参数；否则返回 FALSE。 */
static BOOL ReadAiTask(char *pszOut, DWORD dwCap, char *pszSrc, DWORD dwSrcCap)
{
    char szIni[MAX_PATH] = { 0 };
    if (pszOut != NULL && dwCap > 0) pszOut[0] = '\0';
    if (pszSrc != NULL && dwSrcCap > 0) pszSrc[0] = '\0';

    /* 首选：环境变量 ELANG_AI_TASK */
    if (GetEnvStr(TASK_ENV_VAR, pszOut, dwCap))
    {
        if (pszSrc != NULL) snprintf(pszSrc, dwSrcCap, "env:%s", TASK_ENV_VAR);
        return TRUE;
    }
    /* 次选：命令行 --ai-task=xxx */
    if (CommandLineHasOpt(CMDLINE_TASK_OPT, pszOut, dwCap) && pszOut[0] != '\0')
    {
        if (pszSrc != NULL) snprintf(pszSrc, dwSrcCap, "cmdline:%s", CMDLINE_TASK_OPT);
        return TRUE;
    }
    /* 兜底：%TEMP%\elang_ai_task.ini 的 [ai] task= */
    if (GetFallbackIniPath(szIni, MAX_PATH))
    {
        GetPrivateProfileStringA("ai", "task", "", pszOut, dwCap, szIni);
        if (pszOut[0] != '\0')
        {
            if (pszSrc != NULL) snprintf(pszSrc, dwSrcCap, "ini:%s", szIni);
            return TRUE;
        }
    }
    return FALSE;
}

/* ==========================================================================
 * 6. 第 3 步：只读探测
 * ========================================================================== */

typedef struct _ENUM_CTX
{
    int nCount;
    HWND hMain;
} ENUM_CTX;

static BOOL CALLBACK EnumChildProc(HWND hWnd, LPARAM lParam)
{
    ENUM_CTX  *pCtx = (ENUM_CTX *)lParam;
    char       szClass[256] = { 0 };
    char       szText[256]  = { 0 };
    RECT       rc = { 0, 0, 0, 0 };
    if (pCtx == NULL) return FALSE;
    pCtx->nCount++;
    GetClassNameA(hWnd, szClass, 255);
    GetWindowTextA(hWnd, szText, 255);
    GetWindowRect(hWnd, &rc);
    Trace("  child#%d hwnd=0x%08X class='%s' text='%s' rect=(%ld,%ld,%ld,%ld) visible=%d",
          pCtx->nCount, (unsigned)(size_t)hWnd, szClass, szText,
          (long)rc.left, (long)rc.top, (long)rc.right, (long)rc.bottom,
          IsWindowVisible(hWnd) ? 1 : 0);
    return TRUE;   /* 继续枚举 */
}

/* 编译期宏值自证表 */
static void TraceMacroValues(void)
{
    Trace("MACRO FN_COMPILE_AND_RUN = 0x%08X (expect 0x05020002)", (unsigned)FN_COMPILE_AND_RUN);
    Trace("MACRO FN_END_RUN         = 0x%08X", (unsigned)FN_END_RUN);
    Trace("MACRO FN_OPEN_FILE2      = 0x%08X (expect 0x03010008)", (unsigned)FN_OPEN_FILE2);
    Trace("MACRO FN_ADD_TAB         = 0x%08X (expect 0x05030001)", (unsigned)FN_ADD_TAB);
    Trace("MACRO FN_SWITCH_OUTPUT_BAR = 0x%08X (expect 0x04020003)", (unsigned)FN_SWITCH_OUTPUT_BAR);
    Trace("MACRO FN_IS_FUNC_ENABLED = 0x%08X (expect 0x05030004)", (unsigned)FN_IS_FUNC_ENABLED);
    Trace("MACRO FN_STEP_INTO       = 0x%08X (expect 0x05010001)", (unsigned)FN_STEP_INTO);
    Trace("MACRO FN_SET_BREAK_POINTER = 0x%08X (expect 0x05010006)", (unsigned)FN_SET_BREAK_POINTER);
    Trace("MACRO NL_SYS_NOTIFY_FUNCTION = %d / NL_IDE_READY = %d / NL_RIGHT_POPUP_MENU_SHOW = %d",
          NL_SYS_NOTIFY_FUNCTION, NL_IDE_READY, NL_RIGHT_POPUP_MENU_SHOW);
    Trace("MACRO LBS_IDE_PLUGIN = 0x%X / NES_GET_MAIN_HWND = %d / NES_RUN_FUNC = %d",
          (unsigned)LBS_IDE_PLUGIN, NES_GET_MAIN_HWND, NES_RUN_FUNC);
    Trace("MACRO LIB_FORMAT_VER = %d", LIB_FORMAT_VER);
}

static void TraceStructOffsets(void)
{
    Trace("STruct sizeof(LIB_INFO)=%u  offsetof m_dwState=%u m_nCmdCount=%u m_pCmdsFunc=%u m_pfnNotify=%u m_pLibConst=%u",
          (unsigned)sizeof(LIB_INFO),
          (unsigned)offsetof(LIB_INFO, m_dwState),
          (unsigned)offsetof(LIB_INFO, m_nCmdCount),
          (unsigned)offsetof(LIB_INFO, m_pCmdsFunc),
          (unsigned)offsetof(LIB_INFO, m_pfnNotify),
          (unsigned)offsetof(LIB_INFO, m_pLibConst));
    Trace("STruct sizeof(LIB_INFO2)=%u (LIB_INFO=%u + 4 ptrs)",
          (unsigned)sizeof(LIB_INFO2), (unsigned)sizeof(LIB_INFO));
}

/* 功能号可用性查询表 */
typedef struct _FUNC_PROBE { DWORD dwFn; const char *pszName; } FUNC_PROBE;

static const FUNC_PROBE g_ProbeFns[] =
{
    { FN_COMPILE_AND_RUN,       "FN_COMPILE_AND_RUN" },
    { FN_END_RUN,               "FN_END_RUN" },
    { FN_COMPILE,               "FN_COMPILE" },
    { FN_OPEN_FILE2,            "FN_OPEN_FILE2" },
    { FN_ADD_TAB,               "FN_ADD_TAB" },
    { FN_SWITCH_OUTPUT_BAR,     "FN_SWITCH_OUTPUT_BAR" },
    { FN_IS_FUNC_ENABLED,       "FN_IS_FUNC_ENABLED" },
    { FN_STEP_INTO,             "FN_STEP_INTO" },
    { FN_STEP,                  "FN_STEP" },
    { FN_STEP_OUT,              "FN_STEP_OUT" },
    { FN_RUN_TO_CURSOR,         "FN_RUN_TO_CURSOR" },
    { FN_SET_BREAK_POINTER,     "FN_SET_BREAK_POINTER" },
    { FN_CLEAR_ALL_BREAK_POINTER, "FN_CLEAR_ALL_BREAK_POINTER" },
    { FN_SHOW_NEXT_STATMENT,    "FN_SHOW_NEXT_STATMENT" },
    { FN_GET_PRG_TEXT,          "FN_GET_PRG_TEXT" },
    { FN_GET_ACTIVE_WND_TYPE,   "FN_GET_ACTIVE_WND_TYPE" },
    { FN_MOVE_CARET,            "FN_MOVE_CARET" },
    { FN_INSERT_TEXT,           "FN_INSERT_TEXT" },
    { FN_PRE_COMPILE,           "FN_PRE_COMPILE" },
};

static void DoProbes(void)
{
    HWND     hMain = NULL;
    char     szClass[256] = { 0 };
    char     szCmd[4096] = { 0 };
    char     szExe[MAX_PATH] = { 0 };
    int      i;
    int      nTry;
    ENUM_CTX ctx;

    TraceMacroValues();
    TraceStructOffsets();

    /* --- 3.a 主窗口 --- */
    for (nTry = 0; nTry < 30; nTry++)
    {
        hMain = IdeGetMainHwnd();
        if (hMain != NULL && IsWindow(hMain)) break;
        Sleep(200);
    }
    if (hMain != NULL && IsWindow(hMain))
    {
        GetClassNameA(hMain, szClass, 255);
        Trace("PROBE NES_GET_MAIN_HWND -> hwnd=0x%08X class='%s' visible=%d (expect class ENewFrame)",
              (unsigned)(size_t)hMain, szClass, IsWindowVisible(hMain) ? 1 : 0);
    }
    else
    {
        Trace("PROBE NES_GET_MAIN_HWND -> NULL (main window not found after retries)");
    }

    /* --- 3.b 记录命令行 / 模块名 --- */
    lstrcpynA(szCmd, GetCommandLineA() ? GetCommandLineA() : "", (int)sizeof(szCmd) - 1);
    GetModuleFileNameA(NULL, szExe, MAX_PATH - 1);
    Trace("PROBE GetCommandLineA    = '%s'", szCmd);
    Trace("PROBE GetModuleFileNameA(nullptr) = '%s'", szExe);

    /* --- 3.c FN_IS_FUNC_ENABLED 结果表 --- */
    Trace("PROBE FN_IS_FUNC_ENABLED table (nMsg=%d func=0x%08X):", NES_RUN_FUNC, (unsigned)FN_IS_FUNC_ENABLED);
    for (i = 0; i < (int)(sizeof(g_ProbeFns) / sizeof(g_ProbeFns[0])); i++)
    {
        BOOL blHandled = FALSE;
        BOOL blEnabled = IdeIsFuncEnabled(g_ProbeFns[i].dwFn, &blHandled);
        Trace("   func 0x%08X %-28s -> handled=%d enabled=%d",
              (unsigned)g_ProbeFns[i].dwFn, g_ProbeFns[i].pszName,
              blHandled ? 1 : 0, blEnabled ? 1 : 0);
    }

    /* --- 3.d 子窗口清单 --- */
    if (hMain != NULL && IsWindow(hMain))
    {
        ctx.nCount = 0;
        ctx.hMain  = hMain;
        Trace("PROBE EnumChildWindows of main window begin:");
        EnumChildWindows(hMain, EnumChildProc, (LPARAM)&ctx);
        Trace("PROBE EnumChildWindows end: total=%d", ctx.nCount);
    }

    /* --- 3.e hide=1 隐藏主窗口 --- */
    {
        char szHide[16] = { 0 };
        if (GetEnvStr(HIDE_ENV_VAR, szHide, sizeof(szHide)) && strcmp(szHide, "1") == 0)
        {
            if (hMain != NULL && IsWindow(hMain))
            {
                Trace("PROBE hide=1 -> ShowWindow(hwnd=0x%08X, SW_HIDE) (visible before=%d)",
                      (unsigned)(size_t)hMain, IsWindowVisible(hMain) ? 1 : 0);
                ShowWindow(hMain, SW_HIDE);
                Trace("PROBE hide=1 -> visible after=%d", IsWindowVisible(hMain) ? 1 : 0);
            }
            else
            {
                Trace("PROBE hide=1 requested but main window is NULL");
            }
        }
        else
        {
            Trace("PROBE hide not requested (ELANG_AI_HIDE != 1)");
        }
    }
}

/* ==========================================================================
 * 6b. PoC-2：真正触发“调试运行” + 增量捕获调试面板
 * --------------------------------------------------------------------------
 * 回答三个问题：
 *   Q1 日志里是否出现“程序自己输出的业务文本”？（对面板文本做变更检测，看是否含业务串）
 *   Q2 面板能否**增量流式落盘**？（只在新增后缀时追加写文件）
 *   Q3 三种收尾（正常结束 / 主动 FN_END_RUN / 异常）的**原始尾行**是什么？
 *
 * 全部在本 DLL（= e.exe 进程内）执行，所以可对 IDE 子控件用 GetWindowTextA 直接取文本。
 * ========================================================================== */

#define PANEL_MAX       96
#define PANEL_TEXT_MAX  16384

typedef struct _PANEL_ITEM
{
    HWND   hwnd;
    char   szCls[64];
    int    nLen;                     /* 上次文本长度 */
    char   szText[PANEL_TEXT_MAX];
} PANEL_ITEM;

static PANEL_ITEM  g_aPanel[PANEL_MAX];
static int         g_nPanel = 0;
static char        g_szCapturePath[MAX_PATH] = { 0 };
static HANDLE      g_hCapture = INVALID_HANDLE_VALUE;
static BOOL        g_blNoPoll = FALSE;

/* 捕获文件：追加一行（带毫秒时间戳）。 */
static void CapWrite(const char *pszKind, const char *pszBody)
{
    char   szLine[PANEL_TEXT_MAX + 256];
    SYSTEMTIME st;
    DWORD  dwWritten = 0;
    int    n;
    if (g_hCapture == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&st);
    n = snprintf(szLine, sizeof(szLine), "[%02d:%02d:%02d.%03d][%s] %s\r\n",
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, pszKind,
                 pszBody ? pszBody : "");
    if (n > 0) WriteFile(g_hCapture, szLine, (DWORD)n, &dwWritten, NULL);
}

/* 该窗口类是否是“文本承载类”，值得轮询其文本。 */
static BOOL PanelIsTextClass(const char *pszCls)
{
    if (pszCls == NULL || pszCls[0] == '\0') return FALSE;
    if (_stricmp(pszCls, "Edit") == 0) return TRUE;
    if (_strnicmp(pszCls, "RichEdit", 8) == 0) return TRUE;
    if (_strnicmp(pszCls, "Scintilla", 9) == 0) return TRUE;
    return FALSE;
}

static BOOL CALLBACK PanelEnumProc(HWND hWnd, LPARAM lParam)
{
    char szCls[64] = { 0 };
    (void)lParam;
    if (g_nPanel >= PANEL_MAX) return FALSE;
    GetClassNameA(hWnd, szCls, 63);
    if (!PanelIsTextClass(szCls)) return TRUE;
    g_aPanel[g_nPanel].hwnd = hWnd;
    lstrcpynA(g_aPanel[g_nPanel].szCls, szCls, 63);
    g_aPanel[g_nPanel].szText[0] = '\0';
    g_aPanel[g_nPanel].nLen = GetWindowTextA(hWnd, g_aPanel[g_nPanel].szText,
                                             PANEL_TEXT_MAX - 1);
    g_nPanel++;
    return TRUE;
}

static void PanelSnapshot(HWND hMain)
{
    g_nPanel = 0;
    EnumChildWindows(hMain, PanelEnumProc, 0);
    Trace("RUN panel snapshot: %d 个文本类子窗口", g_nPanel);
    {
        int i;
        for (i = 0; i < g_nPanel; i++)
            Trace("RUN   panel[%d] hwnd=0x%08X class='%s' len=%d",
                  i, (unsigned)(size_t)g_aPanel[i].hwnd, g_aPanel[i].szCls, g_aPanel[i].nLen);
    }
}

/* 轮询一次：对每个面板窗口取全文，若变化则把**新增后缀**增量写盘。 */
static int PanelPollOnce(void)
{
    int i, nChanged = 0;
    for (i = 0; i < g_nPanel; i++)
    {
        char szNow[PANEL_TEXT_MAX] = { 0 };
        int  nNow = GetWindowTextA(g_aPanel[i].hwnd, szNow, PANEL_TEXT_MAX - 1);
        if (nNow != g_aPanel[i].nLen || strcmp(szNow, g_aPanel[i].szText) != 0)
        {
            /* 计算公共前缀长度，只落“新增后缀” */
            int nCommon = 0;
            int nOld = g_aPanel[i].nLen;
            while (nCommon < nOld && nCommon < nNow &&
                   g_aPanel[i].szText[nCommon] == szNow[nCommon])
                nCommon++;
            {
                char szHead[160];
                snprintf(szHead, sizeof(szHead), "hwnd=0x%08X class='%s' oldlen=%d newlen=%d",
                         (unsigned)(size_t)g_aPanel[i].hwnd, g_aPanel[i].szCls, nOld, nNow);
                CapWrite("PANEL-CHANGED", szHead);
                if (nNow > nCommon)
                {
                    char szDelta[PANEL_TEXT_MAX + 32];
                    snprintf(szDelta, sizeof(szDelta), "DELTA: %s", szNow + nCommon);
                    CapWrite("PANEL-DELTA", szDelta);
                }
                else
                {
                    CapWrite("PANEL-RESET", "(文本被清空/回退)");
                }
            }
            /* 更新缓存 */
            memcpy(g_aPanel[i].szText, szNow, (size_t)nNow + 1);
            g_aPanel[i].nLen = nNow;
            nChanged++;
        }
    }
    return nChanged;
}

/* 把每个面板窗口当前的“原始尾行”写入捕获文件与 trace（用于 Q3）。 */
static void PanelDumpTails(const char *pszTag)
{
    int i;
    for (i = 0; i < g_nPanel; i++)
    {
        char *p, *pLast, *pPrev;
        char  szTail[1024];
        if (g_aPanel[i].nLen <= 0) continue;
        p = g_aPanel[i].szText;
        /* 找最后两行 */
        pLast = strrchr(p, '\n');
        pLast = pLast ? pLast + 1 : p;
        pPrev = NULL;
        if (pLast != p)
        {
            char *q = pLast - 2;              /* 跳过 \n 前的 \r */
            while (q > p && *q != '\n') q--;
            pPrev = (q > p) ? q + 1 : p;
        }
        snprintf(szTail, sizeof(szTail), "%s hwnd=0x%08X class='%s' | prev='%.300s' | last='%.300s'",
                 pszTag, (unsigned)(size_t)g_aPanel[i].hwnd, g_aPanel[i].szCls,
                 pPrev ? pPrev : "", pLast);
        /* 去掉行内换行 */
        {
            char *c;
            for (c = szTail; *c; c++) if (*c == '\r' || *c == '\n') *c = ' ';
        }
        CapWrite("PANEL-TAIL", szTail);
        Trace("RUN %s", szTail);
    }
}

/* 触发“调试运行”并轮询；全程在 worker 线程。 */
static void DoRunAndCapture(HWND hMain)
{
    char  szTmp[MAX_PATH] = { 0 };
    char  szEnv[64] = { 0 };
    int   nDelay = 1500;
    int   nStop  = 0;
    DWORD t0;
    BOOL  blStopped = FALSE;
    BOOL  blWasRunning = FALSE;
    int   nIter = 0;

    /* 捕获文件 */
    if (GetEnvStr(CAP_ENV_VAR, g_szCapturePath, MAX_PATH) == FALSE)
    {
        if (GetTempDir(szTmp, MAX_PATH))
            snprintf(g_szCapturePath, MAX_PATH, "%selang_ai_capture.txt", szTmp);
    }
    g_hCapture = CreateFileA(g_szCapturePath, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CapWrite("=== RUN BEGIN ===", "");

    if (GetEnvStr(RUN_DELAY_ENV_VAR, szEnv, sizeof(szEnv)) && szEnv[0])
        nDelay = atoi(szEnv);
    if (GetEnvStr(STOP_ENV_VAR, szEnv, sizeof(szEnv)) && szEnv[0])
        nStop = atoi(szEnv);

    Trace("RUN capture file = %s (delay=%dms stop=%ds)", g_szCapturePath, nDelay, nStop);

    PanelSnapshot(hMain);
    PanelDumpTails("BEFORE");

    /* 让 IDE 静一会，避免刚 ready 就编译 */
    Sleep((DWORD)(nDelay > 0 ? nDelay : 0));

    /* 触发 FN_COMPILE_AND_RUN（等价 F5“调试运行”）
       约定：NES_RUN_FUNC, dwParam1=功能号, dwParam2=双DWORD数组。此处无参数传 0。 */
    Trace("RUN trigger FN_COMPILE_AND_RUN(0x%08X) ...", (unsigned)FN_COMPILE_AND_RUN);
    CapWrite("RUN-TRIGGER", "FN_COMPILE_AND_RUN");
    {
        BOOL blHandled = IdeRunFunc(FN_COMPILE_AND_RUN, 0, 0);
        Trace("RUN FN_COMPILE_AND_RUN handled=%d", blHandled ? 1 : 0);
        CapWrite("RUN-TRIGGER-RET", blHandled ? "handled=1" : "handled=0");
    }

    t0 = GetTickCount();
    /* 对照用：ELANG_AI_NOPOLL=1 时不做面板轮询（用来判定“崩溃是否由本库轮询诱发”） */
    {
        char szNP[8] = { 0 };
        if (GetEnvStr("ELANG_AI_NOPOLL", szNP, sizeof(szNP)) && strcmp(szNP, "1") == 0)
            g_blNoPoll = TRUE;
        Trace("RUN no-poll=%d", g_blNoPoll ? 1 : 0);
    }
    /* 最多观察 180 秒；每 200ms 轮询面板 + 判定运行状态跃迁 */
    while ((GetTickCount() - t0) < 180000)
    {
        BOOL blHandled = FALSE;
        BOOL blEndEnabled = IdeIsFuncEnabled(FN_END_RUN, &blHandled);   /* 运行中=1 */
        int  nChanged = g_blNoPoll ? 0 : PanelPollOnce();
        nIter++;

        if (blEndEnabled != blWasRunning)
        {
            char szB[128];
            snprintf(szB, sizeof(szB), "FN_END_RUN enabled: %d -> %d (t=%.1fs)",
                     blWasRunning ? 1 : 0, blEndEnabled ? 1 : 0,
                     (double)(GetTickCount() - t0) / 1000.0);
            Trace("RUN %s", szB);
            CapWrite("RUN-STATE", szB);
            if (blWasRunning && !blEndEnabled)
            {
                /* 运行 → 非运行：运行结束（正常或异常） */
                CapWrite("RUN-END-DETECTED", "FN_END_RUN enabled 1->0");
                PanelDumpTails("AFTER-END");
                break;
            }
            if (!blWasRunning && blEndEnabled && nStop < 0 && !blStopped)
            {
                /* 收尾2-immediate：运行刚一开始就主动要求结束（用于“用户中断”取证） */
                Trace("RUN 运行开始即中止（收尾2-immediate），发送 FN_END_RUN");
                CapWrite("RUN-STOP", "immediate on run-start -> FN_END_RUN");
                IdeRunFunc(FN_END_RUN, 0, 0);
                blStopped = TRUE;
            }
            blWasRunning = blEndEnabled;
        }

        if (nStop > 0 && !blStopped && (int)((GetTickCount() - t0) / 1000) >= nStop)
        {
            Trace("RUN 到点(%ds)，发送 FN_END_RUN（收尾2）", nStop);
            CapWrite("RUN-STOP", "send FN_END_RUN");
            IdeRunFunc(FN_END_RUN, 0, 0);
            blStopped = TRUE;
        }

        if (nIter % 40 == 0)
            Trace("RUN poll iter=%d changed=%d endEnabled=%d", nIter, nChanged, blEndEnabled ? 1 : 0);

        Sleep(50);
    }

    PanelDumpTails("FINAL");
    CapWrite("=== RUN END ===", "");
    Trace("RUN capture done (iters=%d, file=%s)", nIter, g_szCapturePath);
    if (g_hCapture != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hCapture);
        g_hCapture = INVALID_HANDLE_VALUE;
    }
}

/* ==========================================================================
 * 7. 工作线程：第 2/3 步
 * ========================================================================== */

/* ⚠️ 线程入口必须用真实的 __stdcall（windows.h 的 LPTHREAD_START_ROUTINE 约定），
   不能用 sdk_compat.h 里被还原成空的 WINAPI。 */
static DWORD __stdcall WorkerProc(LPVOID lParam)
{
    char szTask[MAX_PATH * 2] = { 0 };
    char szSrc[64] = { 0 };
    (void)lParam;

    Trace("WORKER start (thread created on NL_IDE_READY)");

    /* 第 2 步：读取 AI 工具提交的参数 */
    if (!ReadAiTask(szTask, sizeof(szTask), szSrc, sizeof(szSrc)))
    {
        Trace("STEP2 no AI task param found (env/%s, cmdline/%s, ini) -> silent return, zero impact",
              TASK_ENV_VAR, CMDLINE_TASK_OPT);
        return 0;
    }

    Trace("STEP2 AI task param -> '%s' (source=%s)", szTask, szSrc);

    /* 第 3 步：只读探测 */
    Trace("STEP3 read-only probe begin");
    DoProbes();
    Trace("STEP3 read-only probe end");

    /* 第 4 步（PoC-2）：真正触发“调试运行” + 增量捕获调试面板 */
    {
        char szRun[16] = { 0 };
        if (GetEnvStr(RUN_ENV_VAR, szRun, sizeof(szRun)) && strcmp(szRun, "1") == 0)
        {
            HWND hMain = IdeGetMainHwnd();
            Trace("STEP4 PoC-2 run+capture begin (hMain=0x%08X)", (unsigned)(size_t)hMain);
            if (hMain != NULL && IsWindow(hMain))
                DoRunAndCapture(hMain);
            else
                Trace("STEP4 abort: main window is NULL");
            Trace("STEP4 PoC-2 run+capture end");
        }
        else
        {
            Trace("STEP4 skipped (ELANG_AI_RUN != 1)");
        }
    }

    Trace("WORKER done");
    return 0;
}

/* ==========================================================================
 * 8. 通知回调
 * ==========================================================================
 * ★★★ 关键结论（实测，已定为根因）★★★
 *   官方 SDK 头 mtypes.h 把 WINAPI 定义成“空”（= __cdecl），这是 16 位时代残留，
 *   具有误导性。实测真实支持库的 notify 回调：
 *     cncnv.fne@0x100010F0 / dp1.fne@0x10003550 / console.fne@0x10001500 /
 *     iext.fne@0x10001B80 —— 全部以 `ret $0xc`（= __stdcall，callee 清 3 个参数）返回；
 *   而 e.exe 调用 m_pfnNotify 后 **没有** `add $0xc,%esp`
 *     （见 e.exe+0x460872..0x46087A：push 1 / push 0x467FF0 / push 0 → call *0x78(%eax)，
 *       紧接着 0x46087D 直接读 [esp+0x14] 而不调栈）。
 *   ⇒ e.exe 是按 __stdcall 调用 m_pfnNotify 的。
 *   若本库按 SDK 的“空 WINAPI”(__cdecl) 实现，则不弹出参数 → e.exe 栈整体低 0xC →
 *   后续读取到垃圾指针(实测=0x00000001) → 在 e.exe+0x46088C 调用的“LIB_INFO 转储”
 *   函数里 `mov 0x4(%edi),%ecx`(edi=1) 触发 0xC0000005 读地址 0x00000005 崩溃。
 *   （已由 -DPOC_CRASHTRAP 的 VEH 抓现场证实。）
 *   因此：本库所有“由 e.exe 回调”的导出/指针一律显式 __stdcall。
 * ========================================================================== */

#ifndef LI_NOTIFY_CONV
#define LI_NOTIFY_CONV   __stdcall
#endif

INT LI_NOTIFY_CONV ProcessNotifyLib(INT nMsg, DWORD dwParam1, DWORD dwParam2)
{
    (void)dwParam2;
    switch (nMsg)
    {
    case NL_SYS_NOTIFY_FUNCTION:
        /* 可能被通知多次，后值覆盖前值。系统提供的 NotifySys 是 __stdcall。 */
        g_fnNotifySys = (PFN_NOTIFY_SYS_STD)(size_t)dwParam1;
        Trace("HOOK NL_SYS_NOTIFY_FUNCTION -> PFN_NOTIFY_SYS=0x%08X (overwritten)",
              (unsigned)dwParam1);
        return NR_OK;

    case NL_IDE_READY:
        Trace("HOOK NL_IDE_READY (ai_mode=%d)", g_blAiMode ? 1 : 0);
        if (g_blAiMode)
        {
            /* 只置位 + 起线程，绝不在通知回调里做耗时工作。 */
            if (InterlockedCompareExchange(&g_lWorkerRun, 1, 0) == 0)
            {
                HANDLE hThread = CreateThread(NULL, 0, WorkerProc, NULL, 0, NULL);
                if (hThread != NULL) CloseHandle(hThread);
                else Trace("HOOK NL_IDE_READY -> CreateThread FAILED err=%lu", GetLastError());
            }
            else
            {
                Trace("HOOK NL_IDE_READY -> worker already started, skip");
            }
        }
        return NR_OK;

    case NL_RIGHT_POPUP_MENU_SHOW:
        Trace("HOOK NL_RIGHT_POPUP_MENU_SHOW hmenu=0x%08X resid=%u",
              (unsigned)dwParam1, (unsigned)dwParam2);
        return NR_OK;

    case NL_UNLOAD_FROM_IDE:
        Trace("HOOK NL_UNLOAD_FROM_IDE");
        return NR_OK;

    case NL_FREE_LIB_DATA:
        Trace("HOOK NL_FREE_LIB_DATA");
        return NR_OK;

    default:
        Trace("NOTIFY-UNHANDLED nMsg=%d -> NR_ERR", nMsg);
        return NR_ERR;
    }
}

/* 静态编译场景用到的三个查询（动态 .fne 无所谓，返回 NULL 即可）。
   同样必须 __stdcall —— e.exe 会按 __stdcall 调用。 */
extern "C" INT LI_NOTIFY_CONV ElangAi_ProcessNotifyLib(INT nMsg, DWORD dwParam1, DWORD dwParam2);

INT LI_NOTIFY_CONV ElangAi_ProcessNotifyLib(INT nMsg, DWORD dwParam1, DWORD dwParam2)
{
    /* 记录“收到的每一个通知”，便于定位 e.exe 在崩溃前最后发了什么 */
    Trace("NOTIFY nMsg=%d (0x%X) p1=0x%08X p2=0x%08X",
          nMsg, (unsigned)nMsg, (unsigned)dwParam1, (unsigned)dwParam2);
    if (nMsg == NL_GET_CMD_FUNC_NAMES)        return 0;      /* 本库无任何命令 */
    if (nMsg == NL_GET_NOTIFY_LIB_FUNC_NAME)  return (INT)(size_t)"ElangAi_ProcessNotifyLib";
    if (nMsg == NL_GET_DEPENDENT_LIBS)        return 0;
    return ProcessNotifyLib(nMsg, dwParam1, dwParam2);
}

/* ==========================================================================
 * 9. 惰性初始化 + 库信息 + 导出
 * ========================================================================== */

#ifdef POC_SELFCHECK
/* PoC 诊断专用：无条件（不依赖任何环境变量）在 %TEMP% 落一个标记文件，
   用来把“库根本没被加载”与“加载了但环境变量没传进去”这两种情况区分开。
   仅诊断构建启用（-DPOC_SELFCHECK），正式库不编译本块。 */
static void SelfCheck(void)
{
    char   szTemp[MAX_PATH] = { 0 };
    char   szPath[MAX_PATH] = { 0 };
    char   szTrace[MAX_PATH] = { 0 };
    char   szTask[MAX_PATH] = { 0 };
    char   szBuf[768] = { 0 };
    HANDLE hFile;
    DWORD  dwWritten = 0;

    GetTempPathA(MAX_PATH, szTemp);
    snprintf(szPath, MAX_PATH, "%selang_addin_selfcheck.txt", szTemp);
    GetEnvStr(TRACE_ENV_VAR, szTrace, MAX_PATH);
    GetEnvStr(TASK_ENV_VAR, szTask, MAX_PATH);
    snprintf(szBuf, sizeof(szBuf),
             "SELFCHECK GetNewInf() called\r\n"
             "pid=%lu\r\n"
             "env ELANG_AI_TRACE='%s'\r\n"
             "env ELANG_AI_TASK='%s'\r\n"
             "ai_mode=%d\r\n\r\n",
             (unsigned long)GetCurrentProcessId(), szTrace, szTask, g_blAiMode);
    hFile = CreateFileA(szPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, szBuf, (DWORD)strlen(szBuf), &dwWritten, NULL);
        CloseHandle(hFile);
    }
}
#endif

/* 判断是否处于 AI 模式：任一 AI 触发条件成立即视为 AI 模式。
   非 AI 模式时本库完全不产生任何副作用（零影响）。 */
static BOOL DetectAiMode(void)
{
    char szTmp[MAX_PATH] = { 0 };
    if (GetEnvStr(TRACE_ENV_VAR, szTmp, MAX_PATH)) return TRUE;
    if (GetEnvStr(TASK_ENV_VAR, szTmp, MAX_PATH))  return TRUE;
    if (CommandLineHasOpt(CMDLINE_TASK_OPT, NULL, 0)) return TRUE;
    if (GetFallbackIniPath(szTmp, MAX_PATH))       return TRUE;
    return FALSE;
}

/* 填库信息。注意：无论是否 AI 模式，都必须返回一个“合法且惰性”的 LIB_INFO，
   否则 e.exe 会把本库视为坏库并弹框报错（那才是真的“有影响”）。
   AI 模式只额外决定：是否写 trace / 是否起工作线程 / 是否隐藏窗口。 */
static void FillLibInfo(void)
{
    s_LibInfo.m_dwLibFormatVer         = LIB_FORMAT_VER;
    s_LibInfo.m_szGuid                 = (LPTSTR)LI_LIB_GUID_STR;
    s_LibInfo.m_nMajorVersion          = 1;
    s_LibInfo.m_nMinorVersion          = 0;
    s_LibInfo.m_nBuildNumber           = 1;
    s_LibInfo.m_nRqSysMajorVer         = 3;   /* < 3.6 → 官方默认按 Windows 处理，回避 OS 位检查 */
    s_LibInfo.m_nRqSysMinorVer         = 0;
    s_LibInfo.m_nRqSysKrnlLibMajorVer  = 3;
    s_LibInfo.m_nRqSysKrnlLibMinorVer  = 0;
    s_LibInfo.m_szName                 = (LPTSTR)LI_LIB_NAME;
    s_LibInfo.m_nLanguage              = __GBK_LANG_VER;
    s_LibInfo.m_szExplain              = (LPTSTR)LI_LIB_EXPLAIN;
    s_LibInfo.m_dwState                = (DWORD)LI_LIB_STATE;   /* 注意：真实库都会带 OS 位 */
    s_LibInfo.m_szAuthor               = (LPTSTR)"PoC";
    s_LibInfo.m_szZipCode              = (LPTSTR)"";
    s_LibInfo.m_szAddress              = (LPTSTR)"";
    s_LibInfo.m_szPhoto                = (LPTSTR)"";
    s_LibInfo.m_szFax                  = (LPTSTR)"";
    s_LibInfo.m_szEmail                = (LPTSTR)"";
    s_LibInfo.m_szHomePage             = (LPTSTR)"";
    s_LibInfo.m_szOther                = (LPTSTR)"";
    s_LibInfo.m_nDataTypeCount         = 0;
    s_LibInfo.m_pDataType              = NULL;
    s_LibInfo.m_nCategoryCount         = 0;
    s_LibInfo.m_szzCategory            = NULL;   /* 与 etools.fne 对齐：0 类别时此指针为 NULL */
    s_LibInfo.m_nCmdCount              = 0;
    s_LibInfo.m_pBeginCmdInfo          = NULL;
    s_LibInfo.m_pCmdsFunc              = NULL;
    s_LibInfo.m_pfnRunAddInFn          = NULL;
    s_LibInfo.m_szzAddInFnInfo         = NULL;
    s_LibInfo.m_pfnNotify              = (PFN_NOTIFY_LIB)ElangAi_ProcessNotifyLib;  /* 不能为 NULL；__stdcall */
    s_LibInfo.m_pfnSuperTemplate       = NULL;
    s_LibInfo.m_szzSuperTemplateInfo   = NULL;
    s_LibInfo.m_nLibConstCount         = 0;
    s_LibInfo.m_pLibConst              = NULL;
    s_LibInfo.m_szzDependFiles         = (LPTSTR)NULL;
}

static void EnsureInit(void)
{
    /* GetNewInf 只会被 e.exe 单线程调用一次；用一个简单标志即可。 */
    if (g_lInitDone) return;
    g_lInitDone = 1;

    FillLibInfo();
    g_blAiMode = DetectAiMode();
#ifdef POC_SELFCHECK
    SelfCheck();
#endif
#ifdef POC_CRASHTRAP
    /* 崩溃陷阱：无论是否 AI 模式一律武装（诊断构建专用）。 */
    CrashSnapshotModules();
    AddVectoredExceptionHandler(1, (PVECTORED_EXCEPTION_HANDLER)CrashTrapVeh);
    {
        char szArmed[96] = { 0 };
        snprintf(szArmed, sizeof(szArmed),
                 "CRASHTRAP armed: GetNewInf entered, pid=%lu, modules=%d\r\n",
                 (unsigned long)GetCurrentProcessId(), g_nCrashMods);
        CrashWriteLine(szArmed);
    }
#endif
    if (g_blAiMode)
    {
        TraceInit();
        Trace("===== elang_addin loaded into e.exe (pid=%lu) =====", (unsigned long)GetCurrentProcessId());
        Trace("LIBINFO state=0x%08X (LBS_IDE_PLUGIN=%d) name='%s' guid=%s",
              (unsigned)s_LibInfo.m_dwState, LBS_IDE_PLUGIN, LI_LIB_NAME, LI_LIB_GUID_STR);
        Trace("LIBINFO pfnNotify=0x%08X", (unsigned)(size_t)s_LibInfo.m_pfnNotify);
    }
}

extern "C" PLIB_INFO WINAPI GetNewInf(void)
{
    EnsureInit();
    return &s_LibInfo;
}
