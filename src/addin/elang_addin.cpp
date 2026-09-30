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
#define OPEN_TARGET_ENV_VAR "ELANG_AI_OPEN_TARGET"
#define HIDE_ENV_VAR      "ELANG_AI_HIDE"
#define CMDLINE_TASK_OPT  "--ai-task="
#define INI_FILE_NAME     "elang_ai_task.ini"

/* ---- PoC-2：真正触发“调试运行”并捕获调试面板 ---- */
#define RUN_ENV_VAR       "ELANG_AI_RUN"         /* =1 → 允许 NES_RUN_FUNC(FN_COMPILE_AND_RUN) */
#define RUN_DELAY_ENV_VAR "ELANG_AI_RUN_DELAY"   /* 触发前等待毫秒（默认 1500） */
#define STOP_ENV_VAR      "ELANG_AI_STOP"        /* >0 → 运行 N 秒后发 FN_END_RUN（收尾2） */
#define COMPILE_ENV_VAR   "ELANG_AI_COMPILE"     /* static|static_exe|static_console|static_dll|exe|console|dll
                                                     → 触发对应编译功能号（PublicIDEFunctions.h FNST_RUN 家族） */
#define COMPILE_DELAY_ENV_VAR "ELANG_AI_COMPILE_DELAY" /* 触发前等待毫秒（默认 1500） */
#define COMPILE_OUT_ENV_VAR   "ELANG_AI_EXPECT_OUT"    /* 期望产物路径：出现即 CapWrite COMPILE-OUT-SEEN */

static char g_szCompileOutPath[MAX_PATH] = { 0 };      /* 期望产物路径（用于填编译保存对话框 + 出现检测） */
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

/* 无头档位解析（实现在第 6a 节）：0=关闭 1=只移出屏幕 2=隐藏+移出屏幕。
   前置声明 —— `DoProbes` 比它的定义更早。 */
static int HideModeFromEnv(void);
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
 * 3a-2. 事件通道 + 对话框监控（2026-09-27）
 * --------------------------------------------------------------------------
 * 目的：无头自动化里，e.exe 弹出的任何模态框（编译错误、找不到启动子程序…）
 *       以前没人应答，触发线程永不返回，表现为「AI 工具一路空等到超时」。
 * 现在：
 *   1) EventWrite —— 关键里程碑写进独立事件文件（ELANG_AI_EVENTS，缺省
 *      %TEMP%\elang_ai_events.log），Python 侧随时 tail，失败时把「最后现场」
 *      直接带回给 AI；
 *   2) DlgWatch   —— 独立线程 + 独立 WinEvent 钩子，盯本进程所有顶层 #32770
 *      对话框：完整记录（标题+子控件文本）进事件文件，并按 ELANG_AI_DLG_POLICY
 *      （observe=只记录（缺省）/ dismiss=替用户点掉）应答。
 *      编译保存框（DirectUIHWND / “请输入欲编译到”）由既有填充线程负责，
 *      本监控只记录、绝不去点。
 * ========================================================================== */
#define EVENTS_ENV_VAR     "ELANG_AI_EVENTS"
#define DLGPOLICY_ENV_VAR  "ELANG_AI_DLG_POLICY"

static char g_szEventsPath[MAX_PATH] = { 0 };

static void EventsInit(void)
{
    char szTemp[MAX_PATH] = { 0 };
    if (g_szEventsPath[0] != '\0') return;
    if (GetEnvStr(EVENTS_ENV_VAR, g_szEventsPath, MAX_PATH)) return;
    if (GetTempDir(szTemp, MAX_PATH))
        snprintf(g_szEventsPath, MAX_PATH, "%selang_ai_events.log", szTemp);
}

static void EventWrite(const char *pszKind, const char *pszFmt, ...)
{
    char    szLine[2048] = { 0 };
    char    szBody[1600] = { 0 };
    va_list ap;
    int     nLen, nBody;
    HANDLE  hFile;
    DWORD   dwWritten = 0;
    SYSTEMTIME st;

    if (!g_blAiMode) return;
    EventsInit();
    if (g_szEventsPath[0] == '\0') return;

    GetLocalTime(&st);
    va_start(ap, pszFmt);
    nBody = vsnprintf(szBody, sizeof(szBody) - 1, pszFmt, ap);
    va_end(ap);
    if (nBody < 0) return;

    nLen = snprintf(szLine, sizeof(szLine) - 3,
                    "[%02d:%02d:%02d.%03d] %s %s",
                    st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                    pszKind, szBody);
    if (nLen < 0) return;
    nLen = (int)strlen(szLine);
    szLine[nLen++] = '\r';
    szLine[nLen++] = '\n';
    szLine[nLen]   = '\0';

    if (g_blCsReady) EnterCriticalSection(&g_cs);
    hFile = CreateFileA(g_szEventsPath, FILE_APPEND_DATA,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        WriteFile(hFile, szLine, (DWORD)nLen, &dwWritten, NULL);
        CloseHandle(hFile);
    }
    if (g_blCsReady) LeaveCriticalSection(&g_cs);
}

/* ---- 模态框监控 -------------------------------------------------------- */

/* 0=observe 1=dismiss（缺省 observe；elaunch/ecompile 会显式传 dismiss） */
static volatile LONG g_nDlgPolicy = 0;

/* 安全应答规则：
 *   - 只有 1 个命令按钮                          → 点它（错误/提示框的「确定」）
 *   - 多个按钮且含 IDOK                          → 点 IDOK
 *   - 多个按钮、无 IDOK、含 IDCANCEL 且无 IDYES  → 点 IDCANCEL
 *   - 其余（是/否问句等语义不明）                → 只记录，绝不瞎猜 */
typedef struct _DLG_SCAN
{
    char  items[10][220];
    int   n;
    int   nBtn;                     /* 命令按钮个数 */
    HWND  hSingle;
    BOOL  blHas[8];                 /* 按 IDOK..IDCANCEL 序号表 */
} DLG_SCAN;

static BOOL CALLBACK DlgChildProc(HWND hChild, LPARAM lp)
{
    DLG_SCAN *p = (DLG_SCAN *)lp;
    char szCls[64] = { 0 };
    char szTxt[128] = { 0 };
    int  nId;

    if (p->n >= 10) return TRUE;
    GetClassNameA(hChild, szCls, sizeof(szCls));
    GetWindowTextA(hChild, szTxt, sizeof(szTxt));
    nId = (int)GetDlgCtrlID(hChild);
    snprintf(p->items[p->n], sizeof(p->items[0]), "cls='%s' id=%d text='%s'",
             szCls, nId, szTxt);
    p->n++;
    if (strcmp(szCls, "Button") == 0)
    {
        p->nBtn++;
        p->hSingle = hChild;
    }
    return TRUE;
}

typedef struct _DLG_SAVECHK { BOOL blSave; } DLG_SAVECHK;

static BOOL CALLBACK DlgSaveChkProc(HWND hChild, LPARAM lp)
{
    DLG_SAVECHK *p = (DLG_SAVECHK *)lp;
    char szCls[64] = { 0 };
    GetClassNameA(hChild, szCls, sizeof(szCls));
    if (strcmp(szCls, "DirectUIHWND") == 0) { p->blSave = TRUE; return FALSE; }
    return TRUE;
}

static void DlgHandle(HWND hDlg)
{
    char        szTitle[256] = { 0 };
    DLG_SCAN    scan;
    DLG_SAVECHK sv;
    int         i;
    HWND        hClick = NULL;
    int         nClickId = 0;
    BOOL        blHasOk, blHasYes, blHasNo, blHasCancel;

    GetWindowTextA(hDlg, szTitle, sizeof(szTitle));

    /* 编译保存框归既有填充线程管，这里只留一条事件绝不动它 */
    if (strstr(szTitle, "请输入欲编译到") != NULL)
    {
        EventWrite("DLG-SAVEBOX", "hwnd=0x%08X title='%s' (由填充线程接管)",
                   (unsigned)(size_t)hDlg, szTitle);
        return;
    }

    memset(&scan, 0, sizeof(scan));
    EnumChildWindows(hDlg, DlgChildProc, (LPARAM)&scan);
    memset(&sv, 0, sizeof(sv));
    EnumChildWindows(hDlg, DlgSaveChkProc, (LPARAM)&sv);

    {
        char szKids[1600] = { 0 };
        for (i = 0; i < scan.n; i++)
        {
            strncat(szKids, szKids[0] ? " | " : "",
                    sizeof(szKids) - strlen(szKids) - 1);
            strncat(szKids, scan.items[i],
                    sizeof(szKids) - strlen(szKids) - 1);
        }
        EventWrite("DLG-MODAL", "hwnd=0x%08X title='%s' btns=%d kids=[%s]",
                   (unsigned)(size_t)hDlg, szTitle, scan.nBtn, szKids);
    }

    if (g_nDlgPolicy < 1 || sv.blSave) return;
    if (strstr(szTitle, "另存") || strstr(szTitle, "Save As") ||
        strstr(szTitle, "覆盖") || strstr(szTitle, "覆蓋") ||
        strstr(szTitle, "已存在"))
        return;                          /* 覆盖确认框归 AnswerConfirmDialogs 管 */

    /* 按钮语义判定（见函数头注释的安全规则） */
    blHasOk = blHasYes = blHasNo = blHasCancel = FALSE;
    {
        HWND h;
        h = GetDlgItem(hDlg, IDOK);     if (h) { blHasOk = TRUE; }
        h = GetDlgItem(hDlg, IDYES);    if (h) { blHasYes = TRUE; }
        h = GetDlgItem(hDlg, IDNO);     if (h) { blHasNo = TRUE; }
        h = GetDlgItem(hDlg, IDCANCEL); if (h) { blHasCancel = TRUE; }
    }

    if (scan.nBtn == 1 && scan.hSingle != NULL)
    {
        hClick = scan.hSingle;          /* 单按钮：错误/提示框的「确定」 */
    }
    else if (blHasOk)
    {
        hClick = GetDlgItem(hDlg, IDOK);
        nClickId = IDOK;
    }
    else if (blHasCancel && !blHasYes && !blHasNo)
    {
        hClick = GetDlgItem(hDlg, IDCANCEL);
        nClickId = IDCANCEL;
    }

    if (hClick != NULL)
    {
        DWORD dwCmd = (DWORD)((WORD)(nClickId ? nClickId : 1) | ((DWORD)BN_CLICKED << 16));
        PostMessageA(hDlg, WM_COMMAND, (WPARAM)dwCmd, (LPARAM)hClick);
        PostMessageA(hClick, BM_CLICK, 0, 0);
        EventWrite("DLG-DISMISSED", "hwnd=0x%08X clicked=%d",
                   (unsigned)(size_t)hDlg, nClickId ? nClickId : 1);
    }
}

/* 每个 hwnd 只报一次（环形表；仅监控线程读写，无需加锁） */
#define DLG_SEEN_MAX 32
static HWND  g_hDlgSeen[DLG_SEEN_MAX];
static int   g_nDlgSeen = 0;

static void CALLBACK DlgWinEventProc(HWINEVENTHOOK hHook, DWORD dwEvent, HWND hWnd,
                                     LONG idObject, LONG idChild,
                                     DWORD dwThread, DWORD dwTime)
{
    char szCls[64] = { 0 };
    DWORD dwPid = 0;
    int   i;

    (void)hHook; (void)dwThread; (void)dwTime;
    if (hWnd == NULL || idObject != OBJID_WINDOW || idChild != 0) return;
    GetWindowThreadProcessId(hWnd, &dwPid);
    if (dwPid != GetCurrentProcessId()) return;

    GetClassNameA(hWnd, szCls, sizeof(szCls));

    if (dwEvent == EVENT_OBJECT_DESTROY)
    {
        if (strcmp(szCls, "ENewFrame") == 0)
            EventWrite("IDE-EXIT", "主窗口销毁 cls='ENewFrame' hwnd=0x%08X",
                       (unsigned)(size_t)hWnd);
        return;
    }

    if (strcmp(szCls, "#32770") != 0) return;
    if (dwEvent != EVENT_OBJECT_SHOW && dwEvent != EVENT_SYSTEM_DIALOGSTART &&
        dwEvent != EVENT_OBJECT_CREATE)
        return;
    if (!IsWindowVisible(hWnd)) return;

    for (i = 0; i < g_nDlgSeen && i < DLG_SEEN_MAX; i++)
        if (g_hDlgSeen[i] == hWnd) return;
    if (g_nDlgSeen < DLG_SEEN_MAX)
        g_hDlgSeen[g_nDlgSeen++] = hWnd;

    DlgHandle(hWnd);
}

static DWORD __stdcall DlgWatchProc(LPVOID lp)
{
    HWINEVENTHOOK hkDlg, hkObj;
    (void)lp;

    {
        char sz[32] = { 0 };
        if (GetEnvStr(DLGPOLICY_ENV_VAR, sz, sizeof(sz)))
            g_nDlgPolicy = (_stricmp(sz, "dismiss") == 0) ? 1 : 0;
    }

    hkDlg = SetWinEventHook(EVENT_SYSTEM_DIALOGSTART, EVENT_SYSTEM_DIALOGEND, NULL,
                            DlgWinEventProc, GetCurrentProcessId(), 0,
                            WINEVENT_OUTOFCONTEXT);
    hkObj = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL,
                            DlgWinEventProc, GetCurrentProcessId(), 0,
                            WINEVENT_OUTOFCONTEXT);
    EventWrite("DLGW-START", "policy=%s hookDlg=0x%08X hookObj=0x%08X",
               g_nDlgPolicy ? "dismiss" : "observe",
               (unsigned)(size_t)hkDlg, (unsigned)(size_t)hkObj);
    Trace("DLGWATCH start policy=%s hookDlg=0x%08X hookObj=0x%08X",
          g_nDlgPolicy ? "dismiss" : "observe",
          (unsigned)(size_t)hkDlg, (unsigned)(size_t)hkObj);

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    for (;;)
    {
        /* WINEVENT_OUTOFCONTEXT 的回调只在本线程「取消息状态」时投递 ⇒ 纯消息泵，
           绝不做 EnumWindows（那是 HidePollProc 的教训）。 */
        DWORD dwR = MsgWaitForMultipleObjectsEx(0, NULL, 50, QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE);
        if (dwR == WAIT_OBJECT_0)
        {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
    }
    /* not reached */
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

/* 编译功能号会打开 IDE 的模态保存框，必须由 IDE 主消息线程执行。
   早期实现在 WorkerProc 里直接调 NES_RUN_FUNC：小工程偶尔可用，但大型 DLL 工程会在
   COMPILE-TRIGGER 后触发 e.exe 访问冲突。WH_GETMESSAGE 钩子让请求在主线程取到
   私有消息时执行，Worker 只负责等待和保存框填充。 */
#define WM_ELANG_AI_INVOKE (WM_APP + 0x04A1)

typedef struct _IDE_UI_CALL
{
    DWORD         dwFn;
    DWORD         dwArg1;
    DWORD         dwArg2;
    BOOL          blHandled;
    HANDLE        hDone;
    volatile LONG lPending;
} IDE_UI_CALL;

static IDE_UI_CALL g_IdeUiCall = { 0 };

static LRESULT __stdcall IdeMainGetMsgHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == PM_REMOVE && lParam != 0)
    {
        MSG *pMsg = (MSG *)lParam;
        if (pMsg->message == WM_ELANG_AI_INVOKE &&
            InterlockedCompareExchange(&g_IdeUiCall.lPending, 0, 1) == 1)
        {
            pMsg->message = WM_NULL;  /* 不让私有消息进入 IDE 的正常分发。 */
            Trace("COMPILE main-thread invoke 0x%08X ...", (unsigned)g_IdeUiCall.dwFn);
            g_IdeUiCall.blHandled = IdeRunFunc(g_IdeUiCall.dwFn,
                                                g_IdeUiCall.dwArg1,
                                                g_IdeUiCall.dwArg2);
            Trace("COMPILE main-thread invoke return handled=%d",
                  g_IdeUiCall.blHandled ? 1 : 0);
            if (g_IdeUiCall.hDone != NULL)
                SetEvent(g_IdeUiCall.hDone);
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static BOOL IdeRunFuncOnMainThread(DWORD dwFn, DWORD dwArg1, DWORD dwArg2,
                                   BOOL *pblHandled)
{
    HWND       hMain;
    DWORD      dwThreadId = 0;
    HMODULE    hSelf = NULL;
    HHOOK      hHook = NULL;
    DWORD      dwWait;
    BOOL       blOk = FALSE;

    if (pblHandled != NULL)
        *pblHandled = FALSE;
    hMain = IdeGetMainHwnd();
    if (hMain == NULL || !IsWindow(hMain))
    {
        Trace("COMPILE main-thread invoke aborted: main window unavailable");
        return FALSE;
    }
    dwThreadId = GetWindowThreadProcessId(hMain, NULL);
    if (dwThreadId == 0 ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(size_t)&IdeMainGetMsgHook, &hSelf))
    {
        Trace("COMPILE main-thread invoke aborted: module/thread lookup failed err=%lu",
              (unsigned long)GetLastError());
        return FALSE;
    }
    if (InterlockedCompareExchange(&g_IdeUiCall.lPending, 1, 0) != 0)
    {
        Trace("COMPILE main-thread invoke aborted: request already pending");
        return FALSE;
    }

    g_IdeUiCall.dwFn = dwFn;
    g_IdeUiCall.dwArg1 = dwArg1;
    g_IdeUiCall.dwArg2 = dwArg2;
    g_IdeUiCall.blHandled = FALSE;
    g_IdeUiCall.hDone = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (g_IdeUiCall.hDone == NULL)
    {
        InterlockedExchange(&g_IdeUiCall.lPending, 0);
        Trace("COMPILE main-thread invoke aborted: CreateEvent failed err=%lu",
              (unsigned long)GetLastError());
        return FALSE;
    }

    hHook = SetWindowsHookExA(WH_GETMESSAGE, IdeMainGetMsgHook, hSelf, dwThreadId);
    if (hHook == NULL || !PostThreadMessageA(dwThreadId, WM_ELANG_AI_INVOKE, 0, 0))
    {
        Trace("COMPILE main-thread invoke setup failed hook=0x%08X err=%lu",
              (unsigned)(size_t)hHook, (unsigned long)GetLastError());
        if (hHook != NULL)
            UnhookWindowsHookEx(hHook);
        CloseHandle(g_IdeUiCall.hDone);
        g_IdeUiCall.hDone = NULL;
        InterlockedExchange(&g_IdeUiCall.lPending, 0);
        return FALSE;
    }

    /* 真编译会在主线程的模态保存框+实际代码生成中等待；大模块（>500 子程序）
       可能需要 6~15 分钟（如 miniblink.ec 的 ecom 编译）。上限给到 30 分钟，
       外层 elaunch.py 仍有独立 timeout 做最后兜底。 */
    dwWait = WaitForSingleObject(g_IdeUiCall.hDone, 30 * 60 * 1000);
    if (dwWait == WAIT_OBJECT_0)
    {
        blOk = TRUE;
        if (pblHandled != NULL)
            *pblHandled = g_IdeUiCall.blHandled;
    }
    else
    {
        Trace("COMPILE main-thread invoke wait failed/timeout code=%lu",
              (unsigned long)dwWait);
        InterlockedExchange(&g_IdeUiCall.lPending, 0);
    }
    UnhookWindowsHookEx(hHook);
    CloseHandle(g_IdeUiCall.hDone);
    g_IdeUiCall.hDone = NULL;
    return blOk;
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
    { FN_PUBLISH,               "FN_PUBLISH" },
    { FN_COMPILE_STANDARD,      "FN_COMPILE_STANDARD" },
    { FN_COMPILE_STATIC,        "FN_COMPILE_STATIC" },
    { FN_COMPILE_EPACKAGE,      "FN_COMPILE_EPACKAGE" },
    { FN_COMPILE_WINDOWS_EXE,               "FN_COMPILE_WINDOWS_EXE" },
    { FN_COMPILE_WINDOWS_CONOLE_EXE,        "FN_COMPILE_WINDOWS_CONOLE_EXE" },
    { FN_COMPILE_WINDOWS_DLL,               "FN_COMPILE_WINDOWS_DLL" },
    { FN_COMPILE_WINDOWS_ECOM,              "FN_COMPILE_WINDOWS_ECOM" },   /* 编译为易模块(.ec) */
    { FN_STATIC_COMPILE_WINDOWS_EXE,        "FN_STATIC_COMPILE_WINDOWS_EXE" },
    { FN_STATIC_COMPILE_WINDOWS_CONOLE_EXE, "FN_STATIC_COMPILE_WINDOWS_CONOLE_EXE" },
    { FN_STATIC_COMPILE_WINDOWS_DLL,        "FN_STATIC_COMPILE_WINDOWS_DLL" },

    /* ===================== 窗体设计器 / 组件操作族（2026-09-23 新增探测） =====================
     * 目的：判定「让 IDE 自己改窗口」这条路线是否有官方通道。
     * 若这些功能号 enabled=1，则可在 .fne 内驱动窗体设计器（不碰 form.json 天书字段）。 */
    { FN_INSERT_NEW_WIN_FORM,   "FN_INSERT_NEW_WIN_FORM"   },  /* 插入一个新窗体 */
    { FN_MENU_MODI,             "FN_MENU_MODI"             },  /* 启动当前窗体的菜单设计器 */
    { FN_REMOVE_SPEC_UNIT,      "FN_REMOVE_SPEC_UNIT"      },  /* 删除窗体上被选择的组件 */
    { FN_MOVE_LEFT_ONE,         "FN_MOVE_LEFT_ONE"         },  /* 移动被选组件 */
    { FN_MOVE_RIGHT_ONE,        "FN_MOVE_RIGHT_ONE"        },
    { FN_MOVE_UP_ONE,           "FN_MOVE_UP_ONE"           },
    { FN_MOVE_DOWN_ONE,         "FN_MOVE_DOWN_ONE"         },
    { FN_MOVE_LEFT_ONE_GRID,    "FN_MOVE_LEFT_ONE_GRID"    },
    { FN_MOVE_RIGHT_ONE_GRID,   "FN_MOVE_RIGHT_ONE_GRID"   },
    { FN_MOVE_UP_ONE_GRID,      "FN_MOVE_UP_ONE_GRID"      },
    { FN_MOVE_DOWN_ONE_GRID,    "FN_MOVE_DOWN_ONE_GRID"    },
    { FN_RESIZE_LEFT_ONE,       "FN_RESIZE_LEFT_ONE"       },  /* 缩放被选组件 */
    { FN_RESIZE_RIGHT_ONE,      "FN_RESIZE_RIGHT_ONE"      },
    { FN_RESIZE_UP_ONE,         "FN_RESIZE_UP_ONE"         },
    { FN_RESIZE_DOWN_ONE,       "FN_RESIZE_DOWN_ONE"       },
    { FN_RESIZE_LEFT_ONE_GRID,  "FN_RESIZE_LEFT_ONE_GRID"  },
    { FN_RESIZE_RIGHT_ONE_GRID, "FN_RESIZE_RIGHT_ONE_GRID" },
    { FN_RESIZE_UP_ONE_GRID,    "FN_RESIZE_UP_ONE_GRID"    },
    { FN_RESIZE_DOWN_ONE_GRID,  "FN_RESIZE_DOWN_ONE_GRID"  },
    { FN_HORZ_CENTER,           "FN_HORZ_CENTER"           },  /* 居中 / 置顶底 */
    { FN_VERT_CENTER,           "FN_VERT_CENTER"           },
    { FN_UNIT_GO_TOP,           "FN_UNIT_GO_TOP"           },
    { FN_UNIT_GO_BOTTOM,        "FN_UNIT_GO_BOTTOM"        },
    { FN_HORZ_UNIT_MIDDLE,      "FN_HORZ_UNIT_MIDDLE"      },
    { FN_VERT_UNIT_MIDDLE,      "FN_VERT_UNIT_MIDDLE"      },
    { FN_LEFT_ALIGN,            "FN_LEFT_ALIGN"            },  /* 对齐 / 分布 / 等宽高 */
    { FN_TOP_ALIGN,             "FN_TOP_ALIGN"             },
    { FN_RIGHT_ALIGN,           "FN_RIGHT_ALIGN"           },
    { FN_BOTTOM_ALIGN,          "FN_BOTTOM_ALIGN"          },
    { FN_HORZ_MIDDLE,           "FN_HORZ_MIDDLE"           },
    { FN_VERT_MIDDLE,           "FN_VERT_MIDDLE"           },
    { FN_HORZ_AVG,              "FN_HORZ_AVG"              },
    { FN_VERT_AVG,              "FN_VERT_AVG"              },
    { FN_EQU_WIDTH,             "FN_EQU_WIDTH"             },
    { FN_EQU_HEIGHT,            "FN_EQU_HEIGHT"            },
    { FN_EQU_WIDTHHEIGHT,       "FN_EQU_WIDTHHEIGHT"       },
    { FN_LOCK_UNIT,             "FN_LOCK_UNIT"             },  /* 加锁/解锁组件 */
    { FN_UNLOCK_UNIT,           "FN_UNLOCK_UNIT"           },
    { FN_INSERT_NEW_CLASS_MOD,  "FN_INSERT_NEW_CLASS_MOD"  },  /* 插入新类程序集 */
    { FN_INSERT_NEW_LOCAL_VAR,  "FN_INSERT_NEW_LOCAL_VAR"  },  /* 插入新局部变量 */
    { FN_INSERT_NEW_CONST_RES,  "FN_INSERT_NEW_CONST_RES"  },  /* 插入新常量/图片/声音资源 */
    { FN_INSERT_NEW_PIC_RES,    "FN_INSERT_NEW_PIC_RES"    },
    { FN_INSERT_NEW_SOUND_RES,  "FN_INSERT_NEW_SOUND_RES"  },
    { FN_INSERT_NEW_LONGTEXT,   "FN_INSERT_NEW_LONGTEXT"   },
    { FN_GOTO_SPEC_UNIT_DATA_TYPE, "FN_GOTO_SPEC_UNIT_DATA_TYPE" }, /* 跳到设计器所选组件的类型 */
    { FN_RELINK,                "FN_RELINK"                },  /* 重新名称关联 */
    { FN_EVA_DESIGNER,          "FN_EVA_DESIGNER"          },  /* EVA 设计器（语义待考） */

    /* ===================== 组件/易程序数据 剪贴板族（关键：复制/粘贴组件的官方通道） ===================== */
    { FN_GET_ACTIVE_WND_TYPE,      "FN_GET_ACTIVE_WND_TYPE"      }, /* 当前活动文档类型(1程序集/5窗体设计…) */
    { FN_INPUT_ECOM,               "FN_INPUT_ECOM"               }, /* 输入易模块 */
    { FN_CLIP_GET_EPRG_DATA_SIZE,  "FN_CLIP_GET_EPRG_DATA_SIZE"  }, /* 读剪贴板"易程序数据"尺寸 */
    { FN_CLIP_GET_EPRG_DATA,       "FN_CLIP_GET_EPRG_DATA"       }, /* 读剪贴板"易程序数据"内容 */
    { FN_CLIP_SET_EPRG_DATA,       "FN_CLIP_SET_EPRG_DATA"       }, /* 写剪贴板"易程序数据"内容 */
    { FN_GET_CARET_ROW_INDEX,      "FN_GET_CARET_ROW_INDEX"      },
    { FN_GET_CARET_COL_INDEX,      "FN_GET_CARET_COL_INDEX"      },
    { FN_GET_PRG_HELP,             "FN_GET_PRG_HELP"             },
    { FN_GET_NUM_ECOM,             "FN_GET_NUM_ECOM"             },
    { FN_GET_ECOM_FILE_NAME,       "FN_GET_ECOM_FILE_NAME"       },
    { FN_GET_NUM_LIB,              "FN_GET_NUM_LIB"              },
    { FN_GET_LIB_INFO_TEXT,        "FN_GET_LIB_INFO_TEXT"        },
    { FN_ADD_NEW_ECOM2,            "FN_ADD_NEW_ECOM2"            }, /* 加入指定文件名的易模块 */
    { FN_INPUT_PRG2,               "FN_INPUT_PRG2"               }, /* 合并指定程序文件 */
    { FN_SET_AND_COMPILE_PRG_ITEM_TEXT, "FN_SET_AND_COMPILE_PRG_ITEM_TEXT" },
    { FN_REPLACE_ALL2,             "FN_REPLACE_ALL2"             },
    { FN_FORCE_PROCESS,            "FN_FORCE_PROCESS"            },
    { FN_GOTO_LAST_MODI_PLACE,     "FN_GOTO_LAST_MODI_PLACE"     },
    { FN_UNDO,                     "FN_UNDO"                     },
    { FN_REDO,                     "FN_REDO"                     },
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

    /* --- 3.e hide=1 隐藏主窗口（`HideModeFromEnv()` 语义见无头守护一节） --- */
    {
        int nHide = HideModeFromEnv();
        if (nHide >= 2)
        {
            if (hMain != NULL && IsWindow(hMain))
            {
                Trace("PROBE hide=%d -> ShowWindow(hwnd=0x%08X, SW_HIDE) (visible before=%d)",
                      nHide, (unsigned)(size_t)hMain, IsWindowVisible(hMain) ? 1 : 0);
                ShowWindow(hMain, SW_HIDE);
                Trace("PROBE hide=%d -> visible after=%d", nHide, IsWindowVisible(hMain) ? 1 : 0);
            }
            else
            {
                Trace("PROBE hide=%d requested but main window is NULL", nHide);
            }
        }
        else
        {
            Trace("PROBE hide not requested (ELANG_AI_HIDE=%d, 需要 2)", nHide);
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

/* ---- 编译任务：ELANG_AI_COMPILE=<种类> → 触发 FNST_RUN / FNST_MODIFY 家族对应功能号 ----
 * 两种形态：
 *   ① 真编译（static* / exe / console / dll）：无“运行态”标志可轮询，完成与否由**外部
 *      包装脚本**监视 ELANG_AI_EXPECT_OUT 指向的产物文件（本库发现文件出现时只做记录）。
 *      产物落盘后 IDE 仍存活，由包装脚本按精确 PID 收尾。
 *   ② 预编译（precompile → FN_PRE_COMPILE）：**只做语法/语义静态检查，不生成任何产物**。
 *      它的功能参数1是个 BOOL*，调用返回后即为「是否预编译成功」——这是官方原生的
 *      「检查代码」入口，比走真编译快得多，也不会弹保存框。
 */
static DWORD CompileFuncNoFromEnv(void)
{
    char szKind[32] = { 0 };
    if (GetEnvStr(COMPILE_ENV_VAR, szKind, sizeof(szKind)) == FALSE || !szKind[0])
        return 0;
    if (strcmp(szKind, "1") == 0 || strcmp(szKind, "static") == 0)
        return FN_COMPILE_STATIC;                        /* 静态编译（IDE 当前工程类型的静态形态） */
    if (strcmp(szKind, "precompile") == 0)
        return FN_PRE_COMPILE;                           /* 预编译：只检查，不出产物 */
    if (strcmp(szKind, "static_exe") == 0)
        return FN_STATIC_COMPILE_WINDOWS_EXE;            /* 静态编译为 Windows 程序 */
    if (strcmp(szKind, "static_console") == 0)
        return FN_STATIC_COMPILE_WINDOWS_CONOLE_EXE;     /* 静态编译为 Windows 控制台程序 */
    if (strcmp(szKind, "static_dll") == 0)
        return FN_STATIC_COMPILE_WINDOWS_DLL;            /* 静态编译为 Windows 动态链接库 */
    if (strcmp(szKind, "exe") == 0)
        return FN_COMPILE_WINDOWS_EXE;                   /* 编译为 Windows 程序 */
    if (strcmp(szKind, "console") == 0)
        return FN_COMPILE_WINDOWS_CONOLE_EXE;            /* 编译为 Windows 控制台程序 */
    if (strcmp(szKind, "dll") == 0)
        return FN_COMPILE_WINDOWS_DLL;                   /* 编译为 Windows 动态链接库 */
    if (strcmp(szKind, "ecom") == 0)
        return FN_COMPILE_WINDOWS_ECOM;                  /* 编译为 Windows 易模块（产物 .ec）
                                                          * 2026-09-23 补：此前只有 exe/dll，
                                                          * 易模块（.ec）工程编译不出东西。 */
    Trace("COMPILE unknown kind '%s'", szKind);
    return 0;
}

/* 触发静态编译后 IDE 会弹「请输入欲编译到的可执行文件名：」保存对话框（实测）。
   我们就在 IDE 进程内：找到它 → 把期望产物路径填进文件名框 → 点确认。

   ⚠️⚠️ 这里**绝对不能要求窗口可见**（2026-09-22 踩坑）：无头模式下宿主/外部会把 IDE 的
   窗口 SW_HIDE 或移出屏幕，加了 `IsWindowVisible` 过滤就永远找不到这个对话框 ——
   模态框不会被消掉，触发线程的 `IdeRunFunc(FN_COMPILE_STATIC…)` **永远不返回**，
   表现为编译静默卡死到超时（曾误判成「IDE 依赖窗口可见」）。判据只用
   「同进程 + 类名 #32770 + 标题含『请输入欲编译到』」，标题本身已经足够特异。 */
static BOOL CALLBACK FindCompileSaveDlgProc(HWND hWnd, LPARAM lp)
{
    DWORD dwPid = 0;
    char  szClass[64] = { 0 };
    char  szTitle[256] = { 0 };

    GetWindowThreadProcessId(hWnd, &dwPid);
    if (dwPid != GetCurrentProcessId())
        return TRUE;
    GetClassNameA(hWnd, szClass, sizeof(szClass));
    if (strcmp(szClass, "#32770") != 0)
        return TRUE;
    GetWindowTextA(hWnd, szTitle, sizeof(szTitle));
    if (strstr(szTitle, "请输入欲编译到") == NULL)     /* GBK 源码 → GBK 窗口文本，可直接比 */
        return TRUE;
    *(HWND *)lp = hWnd;
    return FALSE;
}

/* --------------------------------------------------------------------------
 * 保存框填充（2026-09-24 重写）
 * --------------------------------------------------------------------------
 * 旧实现 = 「`WM_SETTEXT` 到枚举到的**第一个** `Edit`」+「只 `BM_CLICK` 一次」，
 * 在真实工程上**静默失效**：实测 `re/opt/e2ework2` 的 trace 里填充器连续
 * **1155 次**报 `filled path='...\probe.exe' (visible=1)`，但产物从未出现、
 * 保存框从未消失 —— 也就是说「填」和「点」至少有一个是假动作，而旧代码
 * **没有任何回读校验**，所以它自己一直以为成功了。
 *
 * 四个必须同时解决的坑：
 *
 * 1) **文件名框不是「第一个 Edit」**。保存框里同时有「查找范围（目录）」
 *    「文件名」「文件类型」三套输入控件，且每套都是一个 `ComboBox` 套着一个
 *    内层 `Edit`（`EnumChildWindows` 会把它们**全部**枚举出来）。写错框 =
 *    白写，而且毫无提示。→ 改为**收集全部候选 + 回读自证**；文件名框优先按
 *    comdlg32 的固定控件号 `edt1(0x0480)` 取，取不到再退到「最后一个 Edit」。
 *
 * 2) **只写内层 `Edit` 不够**。`ComboBox` 型文件名框取的是 **ComboBox 自身**
 *    的文本/选中项，光改内层 `Edit` 的文本，对话框读到的还是空 —— 这与
 *    `re/opt/dump_dialogs.py` 观测到的「点击后 Edit 文本仍为空」完全吻合。
 *    → 同时给 `Edit` 和它的父 `ComboBox` 各写一次。
 *
 * 3) **覆盖确认框没人回答**。目标文件已存在（或文件名没填进去、易语言退回按
 *    工程名另存）时，会再弹一个 `#32770`（标题「另存新檔 / 另存为 /
 *    Confirm Save As」）。模态栈不消 ⇒ 触发线程的 `IdeRunFunc` **永不返回**，
 *    编译一路卡到超时。→ 每轮先把它点掉。
 *
 * 4) **`BM_CLICK` 可能不被处理**。对模态框只投一个 `BM_CLICK` 不够稳。
 *    → 「`WM_COMMAND(IDOK)` + `BM_CLICK` + 向输入框投 `VK_RETURN`」三连，
 *    任一条被处理即可。
 *
 * 诊断：`ELANG_AI_DLG_ONLY=1` ⇒ **只扫描落盘、不填不点**，把整个控件树
 * （类名/控件号/父类/使能/可见/文本）写进 trace。用于 90 秒级的快速取证，
 * 不必等编译超时（旧跑法一次要 9 分钟才拿到结论）。
 * -------------------------------------------------------------------------- */
#define DLG_DUMP_MAX 8192

typedef struct
{
    HWND   hDlg;
    HWND   hEditFirst;                   /* 第一个 Edit（兜底） */
    HWND   hEditLast;                    /* 最后一个 Edit（= 文件名框的常见位置） */
    int    nEdit;
    int    nCombo;
    int    nBtn;
    size_t cbDump;
    char   szDump[DLG_DUMP_MAX];
} SAVEDLG_SCAN;

static void DlgDumpAppend(SAVEDLG_SCAN *p, const char *pszFmt, ...)
{
    va_list ap;
    int     n;

    if (p->cbDump + 2 >= sizeof(p->szDump))
        return;
    va_start(ap, pszFmt);
    n = _vsnprintf(p->szDump + p->cbDump, sizeof(p->szDump) - p->cbDump - 1,
                   pszFmt, ap);
    va_end(ap);
    if (n > 0)
        p->cbDump += (size_t)n;
    if (p->cbDump + 2 < sizeof(p->szDump))
    {
        p->szDump[p->cbDump++] = '\n';
        p->szDump[p->cbDump]   = '\0';
    }
}

/* 把多行 dump 按行喂给 Trace（Trace 单行上限 2048）。 */
static void DlgDumpTrace(const char *pszTag, const char *pszDump)
{
    const char *p = pszDump;
    char        szLine[1600];

    while (*p != '\0')
    {
        const char *pEol = strchr(p, '\n');
        size_t      cb   = (pEol != NULL) ? (size_t)(pEol - p) : strlen(p);
        if (cb >= sizeof(szLine))
            cb = sizeof(szLine) - 1;
        memcpy(szLine, p, cb);
        szLine[cb] = '\0';
        if (cb > 0)
            Trace("%s %s", pszTag, szLine);
        if (pEol == NULL)
            break;
        p = pEol + 1;
    }
}

static BOOL CALLBACK ScanSaveDlgChildProc(HWND hChild, LPARAM lp)
{
    SAVEDLG_SCAN *p = (SAVEDLG_SCAN *)lp;
    char          szCls[64]  = { 0 };
    char          szPar[64]  = { 0 };
    char          szTxt[400] = { 0 };
    HWND          hPar = GetParent(hChild);

    GetClassNameA(hChild, szCls, sizeof(szCls));
    if (hPar != NULL)
        GetClassNameA(hPar, szPar, sizeof(szPar));
    GetWindowTextA(hChild, szTxt, sizeof(szTxt));

    DlgDumpAppend(p, "  id=%-5d cls=%-16s par=%-14s en=%d vis=%d txt='%s'",
                  GetDlgCtrlID(hChild), szCls, szPar,
                  IsWindowEnabled(hChild) ? 1 : 0,
                  IsWindowVisible(hChild) ? 1 : 0, szTxt);

    if (_stricmp(szCls, "Edit") == 0)
    {
        if (p->hEditFirst == NULL)
            p->hEditFirst = hChild;
        p->hEditLast = hChild;
        ++p->nEdit;
    }
    else if (_stricmp(szCls, "ComboBox") == 0 || _stricmp(szCls, "ComboBoxEx32") == 0)
    {
        ++p->nCombo;
    }
    else if (_stricmp(szCls, "Button") == 0)
    {
        ++p->nBtn;
    }
    return TRUE;
}

/* 写文本到控件并**回读自证**；pszBack 回填实际读到的内容（诊断用）。 */
static BOOL SetCtlTextVerified(HWND hCtl, const char *pszText,
                              char *pszBack, size_t cbBack)
{
    int nTry;

    if (pszBack != NULL && cbBack > 0)
        pszBack[0] = '\0';
    for (nTry = 0; nTry < 2; ++nTry)
    {
        SendMessageA(hCtl, WM_SETTEXT, 0, (LPARAM)pszText);
        if (pszBack != NULL && cbBack > 0)
        {
            pszBack[0] = '\0';
            SendMessageA(hCtl, WM_GETTEXT, (WPARAM)cbBack, (LPARAM)pszBack);
            if (pszBack[0] == '\0')
                GetWindowTextA(hCtl, pszBack, (int)cbBack);
            if (strcmp(pszBack, pszText) == 0)
                return TRUE;
        }
        if (nTry == 0)
            SetFocus(hCtl);                 /* 部分控件要求先聚焦才吃 WM_SETTEXT */
    }
    return FALSE;
}

/* --------------------------------------------------------------------------
 * 把路径写进保存框 —— 判据必须是**对话框自己的答案**（2026-09-24）
 * --------------------------------------------------------------------------
 * 踩过的坑：易语言的保存框是 **Vista 风格（Explorer 式）公共对话框**，
 * 控件树长这样（`re/opt/dlg_watch.py` 实测）：
 *
 *     DirectUIHWND
 *       └ FloatNotifySink
 *           ├ ComboBox            ← 「保存位置」目录框
 *           │   └ Edit id=1001
 *           └ ComboBox            ← 「文件名」框
 *               └ Edit id=1001    ← 两个 Edit 的类名/控件号**完全一样**
 *
 * 于是「取第 N 个编辑框」这种启发式必然摇摆：命中文件名框就成功，命中目录框就
 * 把完整路径写进了目录框、文件名框留空 ⇒ 易语言退回「上次目录 + 默认文件名」
 * （实测产物落到了上一次编译的目录，脚本判成 timeout，其实编译是成功的）。
 *
 * 更坏的是：**控件回读会骗人** —— 写进目录框后 `WM_GETTEXT` 一样回读成功。
 *
 * 最终判据（两条，按对话框是否支持 `CDM_GETFILEPATH` 分流）：
 *   ① 对话框支持 CDM（老式 comdlg32）→ 直接问它「你现在要存到哪个完整路径」，
 *      逐个候选试到**对话框自己的答案**匹配为止（最严格）；
 *   ② 不支持（易模块这个 Vista 风格框就是这样，恒返回空）→ 用**几何位置**：
 *      同一个框里「保存位置」在上、「文件名」在下，取**最靠下的宽编辑框**
 *      （实测坐标见 CollectEditProc 上方注释），再用控件回读自证。
 *   → 2026-09-24 之前用的是「第 N 个编辑框」，两种布局下都会摇摆，正是
 *     产物偶发落到上一次目录的根因。
 * -------------------------------------------------------------------------- */
#ifndef CDM_FIRST
#define CDM_FIRST (WM_USER + 0x0064)
#endif
#define KIT_CDM_GETFILEPATH    (CDM_FIRST + 0x0001)
#define KIT_CDM_SETCONTROLTEXT (CDM_FIRST + 0x0004)

/* comdlg32 的「文件名」编辑框控件号。Vista 风格下未必真是它，但 CDM_* 由对话框
   自己解释，先走这条官方路线成本最低。 */
#define KIT_EDT1 0x0480

/* 问保存框：你现在的完整目标路径是什么？返回 TRUE = 拿到了非空答案。 */
static BOOL QueryDlgFilePath(HWND hDlg, char *pszOut, DWORD dwCap)
{
    DWORD dwLen;

    if (pszOut == NULL || dwCap == 0)
        return FALSE;
    pszOut[0] = '\0';
    dwLen = (DWORD)SendMessageA(hDlg, KIT_CDM_GETFILEPATH, (WPARAM)dwCap,
                                (LPARAM)pszOut);
    pszOut[dwCap - 1] = '\0';
    return (dwLen > 0 && pszOut[0] != '\0');
}

/* 路径比较：忽略大小写、`/` 与 `\` 等价（用户传的路径两种都有）。 */
static BOOL PathEquals(const char *pszA, const char *pszB)
{
    while (*pszA != '\0' && *pszB != '\0')
    {
        char ca = *pszA++;
        char cb = *pszB++;
        if (ca == '/') ca = '\\';
        if (cb == '/') cb = '\\';
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
            return FALSE;
    }
    return (*pszA == '\0' && *pszB == '\0');
}

#define DLG_MAX_EDIT 12

typedef struct
{
    HWND hEdit;
    HWND hCombo;            /* 父 ComboBox（ComboBox 型文件名框要连它一起写） */
    int  nTop;              /* 屏幕 y：越大越靠下。对话框被挪到 -32000 也保持相对次序 */
    int  nWidth;
    char szOrig[600];       /* 原文本，试错后要还原 */
} DLG_CAND;

typedef struct
{
    DLG_CAND a[DLG_MAX_EDIT];
    int      n;
} DLG_CANDS;

/* 收集候选编辑框。判据的关键是**位置**：同一个保存框里，「保存位置（目录）」框
   在上、「文件名」框在下，两者类名与父类完全一样（实测见 dlg_watch）。
   实测坐标（对话框被挪到 -32000 后仍保持相对次序）：

       地址栏 Edit                y=-31956  宽 52    ← 最上，排除
       目录框 ComboBox → Edit     y=-31641  宽 518
       文件名框 ComboBox → Edit   y=-31619  宽 518   ← 最下 ⇒ 就是它
       存檔 / 取消 按钮           y=-31541

   太小的框（地址栏、搜索框）直接跳过，避免被 y 排序选中。 */
static BOOL CALLBACK CollectEditProc(HWND hChild, LPARAM lp)
{
    DLG_CANDS *p = (DLG_CANDS *)lp;
    char       szCls[64] = { 0 };
    char       szPar[64] = { 0 };
    HWND       hPar;
    RECT       rc;

    if (p->n >= DLG_MAX_EDIT)
        return FALSE;
    GetClassNameA(hChild, szCls, sizeof(szCls));
    if (_stricmp(szCls, "Edit") != 0)
        return TRUE;
    if (!GetWindowRect(hChild, &rc))
        return TRUE;
    if ((rc.right - rc.left) < 40 || (rc.bottom - rc.top) < 8)
        return TRUE;

    hPar = GetParent(hChild);
    if (hPar != NULL)
        GetClassNameA(hPar, szPar, sizeof(szPar));

    p->a[p->n].hEdit  = hChild;
    p->a[p->n].hCombo = (_stricmp(szPar, "ComboBox") == 0 ||
                         _stricmp(szPar, "ComboBoxEx32") == 0) ? hPar : NULL;
    p->a[p->n].nTop   = (int)rc.top;
    p->a[p->n].nWidth = (int)(rc.right - rc.left);
    GetWindowTextA(hChild, p->a[p->n].szOrig, sizeof(p->a[p->n].szOrig));
    ++p->n;
    return TRUE;
}

/* 把一个候选控件（及其父 ComboBox）写掉并还原。 */
static void WriteCand(const DLG_CAND *p, const char *pszPath)
{
    SendMessageA(p->hEdit, WM_SETTEXT, 0, (LPARAM)pszPath);
    if (p->hCombo != NULL)
        SendMessageA(p->hCombo, WM_SETTEXT, 0, (LPARAM)pszPath);
}

static void RestoreCand(const DLG_CAND *p)
{
    SendMessageA(p->hEdit, WM_SETTEXT, 0, (LPARAM)p->szOrig);
    if (p->hCombo != NULL)
        SendMessageA(p->hCombo, WM_SETTEXT, 0, (LPARAM)p->szOrig);
}

/* 成功返回 TRUE，把最终写进去的文本回填 pszVerify，并把生效控件回填 *phTarget。 */
static BOOL TrySetSavePath(HWND hDlg, const char *pszPath,
                          char *pszVerify, size_t cbVerify, HWND *phTarget)
{
    DLG_CANDS cd;
    char      szFile[MAX_PATH * 2] = { 0 };
    char      szBack[600] = { 0 };
    BOOL      blCdm = FALSE;
    int       i, j;

    memset(&cd, 0, sizeof(cd));
    EnumChildWindows(hDlg, CollectEditProc, (LPARAM)&cd);
    if (cd.n == 0)
    {
        Trace("COMPILE dlg 没有任何候选编辑框");
        return FALSE;
    }

    /* 按屏幕 y 从下往上排（插入排序，n ≤ 12） */
    for (i = 1; i < cd.n; ++i)
    {
        DLG_CAND t = cd.a[i];
        j = i - 1;
        while (j >= 0 && cd.a[j].nTop < t.nTop)
        {
            cd.a[j + 1] = cd.a[j];
            --j;
        }
        cd.a[j + 1] = t;
    }

    Trace("COMPILE dlg 候选 %d 个（从下往上）:", cd.n);
    for (i = 0; i < cd.n; ++i)
        Trace("COMPILE   #%d y=%-7d w=%-5d combo=%d txt='%s'", i, cd.a[i].nTop,
              cd.a[i].nWidth, cd.a[i].hCombo != NULL ? 1 : 0, cd.a[i].szOrig);

    /* 有的保存框不实现 CDM_GETFILEPATH（易模块这个就不实现，恒返回空），
       就退回几何启发式：**最靠下的宽编辑框**就是文件名框。 */
    blCdm = QueryDlgFilePath(hDlg, szFile, sizeof(szFile));

    for (i = 0; i < cd.n; ++i)
    {
        WriteCand(&cd.a[i], pszPath);

        if (blCdm)
        {
            szFile[0] = '\0';
            if (QueryDlgFilePath(hDlg, szFile, sizeof(szFile)) &&
                PathEquals(szFile, pszPath))
            {
                snprintf(pszVerify, cbVerify, "%s", szFile);
                if (phTarget != NULL) *phTarget = cd.a[i].hEdit;
                Trace("COMPILE dlg 命中 #%d（GETFILEPATH='%s'）", i, szFile);
                return TRUE;
            }
            RestoreCand(&cd.a[i]);
            continue;
        }

        /* 无 CDM：写进去 + 回读自证。第一个成功的就是文件名框（已按 y 从下往上排） */
        if (SetCtlTextVerified(cd.a[i].hEdit, pszPath, szBack, sizeof(szBack)) ||
            (cd.a[i].hCombo != NULL &&
             SetCtlTextVerified(cd.a[i].hCombo, pszPath, szBack, sizeof(szBack))))
        {
            snprintf(pszVerify, cbVerify, "%s", szBack);
            if (phTarget != NULL) *phTarget = cd.a[i].hEdit;
            Trace("COMPILE dlg 几何选中 #%d（y=%d w=%d，回读成功）CDM=%d",
                  i, cd.a[i].nTop, cd.a[i].nWidth, blCdm ? 1 : 0);
            return TRUE;
        }

        /* 候选全试完仍失败：不作标记，交给上层决定是否点确定 */
    }

    Trace("COMPILE dlg CDM=%d，所有候选都写不进去", blCdm ? 1 : 0);
    return FALSE;
}

/* 「另存新檔 / 另存为 / Confirm Save As」覆盖确认框。
   ⚠️ 必须排除真正的编译保存框（标题含「请输入欲编译到」），否则会点错。 */
static BOOL CALLBACK FindConfirmDlgProc(HWND hWnd, LPARAM lp)
{
    DWORD dwPid = 0;
    char  szCls[64]   = { 0 };
    char  szTitle[256] = { 0 };

    GetWindowThreadProcessId(hWnd, &dwPid);
    if (dwPid != GetCurrentProcessId())
        return TRUE;
    GetClassNameA(hWnd, szCls, sizeof(szCls));
    if (strcmp(szCls, "#32770") != 0)
        return TRUE;
    GetWindowTextA(hWnd, szTitle, sizeof(szTitle));
    if (strstr(szTitle, "请输入欲编译到") != NULL)
        return TRUE;
    if (strstr(szTitle, "另存") != NULL || strstr(szTitle, "Save As") != NULL ||
        strstr(szTitle, "覆盖") != NULL || strstr(szTitle, "覆蓋") != NULL ||
        strstr(szTitle, "已存在") != NULL)
    {
        *(HWND *)lp = hWnd;
        return FALSE;
    }
    return TRUE;
}

/* 对一个模态按钮「三连点」：WM_COMMAND(IDOK) + BM_CLICK。 */
static void PressDlgButton(HWND hDlg, int nId)
{
    HWND hBtn = GetDlgItem(hDlg, nId);
    DWORD dwCmd;

    if (hBtn == NULL)
        return;
    dwCmd = (DWORD)((WORD)nId | ((DWORD)BN_CLICKED << 16));
    SendMessageA(hDlg, WM_COMMAND, (WPARAM)dwCmd, (LPARAM)hBtn);
    PostMessageA(hBtn, BM_CLICK, 0, 0);
}

/* 点掉所有挡路的覆盖确认框。返回 TRUE=至少处理过一个。 */
static BOOL AnswerConfirmDialogs(void)
{
    BOOL blAny = FALSE;
    int  n;

    for (n = 0; n < 4; ++n)                 /* 实测一次能连弹 2 个 */
    {
        HWND hDlg = NULL;
        EnumWindows(FindConfirmDlgProc, (LPARAM)&hDlg);
        if (hDlg == NULL)
            break;
        {
            char szTitle[256] = { 0 };
            GetWindowTextA(hDlg, szTitle, sizeof(szTitle));
            Trace("COMPILE confirm dialog '%s' -> press YES", szTitle);
            CapWrite("COMPILE-CONFIRM", szTitle);
        }
        PressDlgButton(hDlg, IDYES);
        PressDlgButton(hDlg, IDOK);
        blAny = TRUE;
        Sleep(80);
    }
    return blAny;
}

/* 返回 TRUE=找到并确认了保存对话框 */
static BOOL TryFillCompileSaveDialog(void)
{
    HWND hDlg = NULL;

    if (!g_szCompileOutPath[0])
        return FALSE;
    EnumWindows(FindCompileSaveDlgProc, (LPARAM)&hDlg);
    if (hDlg == NULL)
        return FALSE;
    /* ⚠️ 无头模式下这个模态框可能已被 SW_HIDE / 挪到 -32000。需要把它恢复成
       「可见但仍在屏幕外」再填再点 —— 实测（2026-09-22）对话框处于隐藏状态时
       `BM_CLICK` 不会被正常处理，模态框不消，触发线程的 IdeRunFunc 永远不返回，
       真编译表现为「保存框填了、然后什么都不发生、超时」。

       ⚠️⚠️ 必须**一步完成「移到屏幕外 + 显示」**：先 `ShowWindow` 再 `SetWindowPos`
       会让对话框在原位置先显示一帧。

       另注（同日晚些时候的实测，`re/win_probe/probe_wfp.py`）：真正造成露脸的
       一帧**不是这里**，而是**易语言自己把模态框建在 (0,0) 并显示**，
       我们的填充器还没跑到。现在由 `HideWinEventProc`（事件驱动）在
       `EVENT_OBJECT_SHOW` 触发的**同一毫秒内**处理掉，这一帧才消失。 */
    SetWindowPos(hDlg, NULL, -32000, -32000, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    {
        static int  s_nScan       = 0;
        static int  s_nScanEdit   = 0;   /* 其中「含编辑框」的次数 */
        static BOOL s_blDlgOnly   = FALSE;
        static BOOL s_blDlgOnlySet = FALSE;

        SAVEDLG_SCAN sc;
        HWND   hTarget = NULL;
        char   szBack[600] = { 0 };
        BOOL   blOk;

        /* 先把挡路的覆盖确认框点掉 —— 它不消，保存框就永远不会真正生效 */
        AnswerConfirmDialogs();

        if (!s_blDlgOnlySet)
        {
            char szEnv[8] = { 0 };
            s_blDlgOnlySet = TRUE;
            if (GetEnvStr("ELANG_AI_DLG_ONLY", szEnv, sizeof(szEnv)))
                s_blDlgOnly = (szEnv[0] == '1');
        }

        memset(&sc, 0, sizeof(sc));
        sc.hDlg = hDlg;
        EnumChildWindows(hDlg, ScanSaveDlgChildProc, (LPARAM)&sc);

        /* 落盘策略：**含编辑框的**前 3 次 + 之后每 50 次。
           （只看「前 3 次扫描」是上一版的做法，结果 3 次都发生在编辑框还没创建的
           时刻，dump 里 edits=0，等于什么都没抓到 —— 见 `re/opt/dlg_watch.py` 的实证。） */
        if ((sc.nEdit > 0 && s_nScanEdit < 3) || (s_nScan > 0 && (s_nScan % 50) == 0))
        {
            Trace("DLG-DUMP#%d hwnd=%p edits=%d combos=%d btns=%d",
                  s_nScan + 1, (void *)hDlg, sc.nEdit, sc.nCombo, sc.nBtn);
            DlgDumpTrace("DLG-DUMP", sc.szDump);
            if (sc.nEdit > 0)
                ++s_nScanEdit;
        }
        ++s_nScan;

        if (s_blDlgOnly)
            return FALSE;                   /* 取证模式：只看不填 */

        if (sc.nEdit == 0)
        {
            /* Vista 风格保存框刚弹出时文件名编辑框还没创建 —— 此时**绝不能**
               点「存檔」，否则等于拿空文件名去存（见本函数上方长注释）。 */
            Trace("COMPILE save dialog has NO Edit control");
            CapWrite("COMPILE-DIALOG-FAIL", "no-edit-control");
            return FALSE;
        }

        /* 选目标 + 写入 + 自证，全在 TrySetSavePath 里；它同时把真正生效的那个
           编辑框回填到 hTarget，供下面投 VK_RETURN 用。 */
        blOk = TrySetSavePath(hDlg, g_szCompileOutPath, szBack, sizeof(szBack),
                              &hTarget);
        if (!blOk)
        {
            CapWrite("COMPILE-DIALOG-FAIL", "set-path-failed");
            return FALSE;
        }

        Trace("COMPILE fill ok: 控件文本='%s'（期望 '%s'）", szBack, g_szCompileOutPath);
        if (hTarget == NULL)
            hTarget = GetDlgItem(hDlg, KIT_EDT1);
        if (hTarget == NULL)
            hTarget = sc.hEditLast;

        CapWrite("COMPILE-DIALOG", "fill+confirm");
        {
            HWND hOk = GetDlgItem(hDlg, IDOK);
            if (hOk != NULL && IsWindowEnabled(hOk))
            {
                /* 三连：WM_COMMAND(IDOK) + BM_CLICK + 向输入框投回车
                   （回车由模态循环的 IsDialogMessage 转成「默认按钮」= 保存） */
                PressDlgButton(hDlg, IDOK);
                PostMessageA(hTarget, WM_KEYDOWN, VK_RETURN, 1);
                PostMessageA(hTarget, WM_KEYUP,   VK_RETURN, 1);
            }
            else
                Trace("COMPILE IDOK missing/disabled");
        }
    }
    return TRUE;
}

/* ⚠️ 关键：触发线程调 NES_RUN_FUNC(FN_COMPILE_STATIC…) 时，IDE 在**同一线程**里弹
 * 模态保存对话框，调用不返回 → 触发线程无法自己填框。必须另起线程专职填框。
 *
 * 重试策略：**只要那个对话框还在，就再填再点一次**（限速 300ms）。
 * 只点一次的版本踩过坑 —— 无头模式下第一次 `BM_CLICK` 可能没被处理，对话框留在那里，
 * 而 `blFilled` 已经置位，于是再也没人管它，编译静默卡到超时。 */
static DWORD __stdcall DialogFillerProc(LPVOID lp)
{
    DWORD t0 = GetTickCount();
    DWORD tLastFill = 0;
    int   nFill = 0;
    BOOL  blOutSeen = FALSE;
    (void)lp;

    while ((GetTickCount() - t0) < 600000)
    {
        if (!blOutSeen && (GetTickCount() - tLastFill) >= 300)
        {
            if (TryFillCompileSaveDialog())
            {
                tLastFill = GetTickCount();
                ++nFill;
                if (nFill > 1 && (nFill % 10) == 0)
                    Trace("COMPILE save dialog still present, re-filled %d times", nFill);
            }
        }
        if (nFill > 0 && !blOutSeen)
        {
            /* 填完之后盯产物：出现即记录，再陪 3 秒让面板刷完，然后退出 */
            DWORD dwAttr = GetFileAttributesA(g_szCompileOutPath);
            if (dwAttr != INVALID_FILE_ATTRIBUTES && !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
            {
                CapWrite("COMPILE-OUT-SEEN", g_szCompileOutPath);
                Trace("COMPILE output seen: %s", g_szCompileOutPath);
                EventWrite("COMPILE-OUT-SEEN", "%s", g_szCompileOutPath);
                blOutSeen = TRUE;
                Sleep(3000);
                break;
            }
        }
        Sleep(50);
    }
    Trace("DIALOG-FILLER exit (fills=%d)", nFill);
    return 0;
}

static void DoCompileTask(HWND hMain)
{
    char  szTmp[MAX_PATH] = { 0 };
    char  szEnv[MAX_PATH] = { 0 };
    char  szOpenTarget[MAX_PATH] = { 0 };
    int   nDelay = 1500;
    DWORD dwFn;
    DWORD t0;
    int   nIter = 0;
    BOOL  blOutSeen = FALSE;
    BOOL  blPre;                            /* TRUE = 预编译（只检查，无产物无保存框） */

    dwFn = CompileFuncNoFromEnv();
    if (dwFn == 0)
        return;
    blPre = (dwFn == FN_PRE_COMPILE);
    EventWrite("COMPILE-START", "fn=0x%08X pre=%d", (unsigned)dwFn, blPre ? 1 : 0);

    /* 捕获文件（与 RUN 同一路径约定） */
    if (GetEnvStr(CAP_ENV_VAR, g_szCapturePath, MAX_PATH) == FALSE)
    {
        if (GetTempDir(szTmp, MAX_PATH))
            snprintf(g_szCapturePath, MAX_PATH, "%selang_ai_capture.txt", szTmp);
    }
    g_hCapture = CreateFileA(g_szCapturePath, FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CapWrite("=== COMPILE BEGIN ===", "");

    if (GetEnvStr(COMPILE_DELAY_ENV_VAR, szEnv, sizeof(szEnv)) && szEnv[0])
        nDelay = atoi(szEnv);
    GetEnvStr(COMPILE_OUT_ENV_VAR, g_szCompileOutPath, MAX_PATH);

    /* 引导工程仅负责按需加载 FNE；真实源码由官方打开文件功能载入后再编译。
       这样不会让 mkcage 的 e2t→t2e 有损链碰到目标 .e。 */
    if (GetEnvStr(OPEN_TARGET_ENV_VAR, szOpenTarget, sizeof(szOpenTarget)) &&
        szOpenTarget[0] != '\0')
    {
        BOOL blOpened = FALSE;
        BOOL blHandled = FALSE;
        Trace("COMPILE open target '%s' ...", szOpenTarget);
        blOpened = IdeRunFuncOnMainThread(FN_OPEN_FILE2,
                                          (DWORD)(size_t)szOpenTarget, 0,
                                          &blHandled);
        Trace("COMPILE open target invoked=%d handled=%d",
              blOpened ? 1 : 0, blHandled ? 1 : 0);
        CapWrite("COMPILE-OPEN-TARGET",
                 (blOpened && blHandled) ? "opened=1" : "opened=0");
        Sleep(1000);
    }
    {
        BOOL blEnabled = FALSE, blHandled = FALSE;
        blEnabled = IdeIsFuncEnabled(dwFn, &blHandled);
        Trace("COMPILE func=0x%08X handled=%d enabled=%d delay=%dms",
              (unsigned)dwFn, blHandled ? 1 : 0, blEnabled ? 1 : 0, nDelay);
        CapWrite("COMPILE-CHECK", blEnabled ? "enabled=1" : "enabled=0");
    }

    PanelSnapshot(hMain);
    PanelDumpTails("BEFORE");
    Sleep((DWORD)(nDelay > 0 ? nDelay : 0));

    Trace("COMPILE trigger 0x%08X ...", (unsigned)dwFn);
    CapWrite("COMPILE-TRIGGER", blPre ? "pre-compile" : "send");
    {
        HANDLE hFiller = NULL;
        BOOL   blHandled;
        if (!blPre && g_szCompileOutPath[0])
            hFiller = CreateThread(NULL, 0, DialogFillerProc, NULL, 0, NULL);
        if (blPre)
        {
            /* FN_PRE_COMPILE 的功能参数1 = BOOL*，返回「是否预编译成功」。
               我们在 IDE 进程内，直接用本栈上的 BOOL 变量接结果即可。 */
            BOOL blPreOk = FALSE;
            BOOL blInvoked = IdeRunFuncOnMainThread(FN_PRE_COMPILE,
                                                    (DWORD)(size_t)&blPreOk, 0,
                                                    &blHandled);
            Trace("COMPILE pre-compile invoked=%d handled=%d ok=%d",
                  blInvoked ? 1 : 0, blHandled ? 1 : 0, blPreOk ? 1 : 0);
            {
                char szB[80];
                snprintf(szB, sizeof(szB), "invoked=%d handled=%d ok=%d",
                         blInvoked ? 1 : 0, blHandled ? 1 : 0, blPreOk ? 1 : 0);
                CapWrite("COMPILE-PRE-RET", szB);
            }
        }
        else
        {
            /* 模态保存对话框在主线程阻塞，由 filler 解掉；Worker 不得直调 IDE。 */
            IdeRunFuncOnMainThread(dwFn, 0, 0, &blHandled);
            Trace("COMPILE handled=%d", blHandled ? 1 : 0);
            CapWrite("COMPILE-TRIGGER-RET", blHandled ? "handled=1" : "handled=0");
        }
        if (hFiller != NULL)
        {
            WaitForSingleObject(hFiller, 30000);
            CloseHandle(hFiller);
        }
    }

    if (blPre)
    {
        /* 预编译：无产物、无保存框。调用返回时结果已在手，剩下的只是把**报错原文**
           从输出面板落下来 —— 轮询到面板静默 1.5 秒即收（编译输出是一次性刷完的，
           不排空就会把错误丢了）。 */
        int nQuiet = 0;
        t0 = GetTickCount();
        while ((GetTickCount() - t0) < 30000)
        {
            if (PanelPollOnce() > 0)
            {
                PanelDumpTails("DELTA");
                nQuiet = 0;
            }
            else if (++nQuiet >= 15)
            {
                break;
            }
            Sleep(100);
        }
        PanelDumpTails("FINAL");
        CapWrite("=== COMPILE END ===", "pre-compile");
        EventWrite("COMPILE-END", "pre-compile");
        Trace("COMPILE pre-compile capture done (file=%s)", g_szCapturePath);
        if (g_hCapture != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_hCapture);
            g_hCapture = INVALID_HANDLE_VALUE;
        }
        return;
    }

    /* 最多观察 600 秒；每 100ms 轮询面板增量；发现期望产物出现即记录
       （真正的收尾由包装脚本做：杀自己这个会话的 e.exe PID） */
    t0 = GetTickCount();
    while ((GetTickCount() - t0) < 600000)
    {
        int nChanged = PanelPollOnce();
        nIter++;

        if (nChanged > 0)
            PanelDumpTails("DELTA");      /* 编译错误/进度都写在输出面板，落下来给包装脚本判读 */

        /* 保存对话框出现后立即填充确认（标题「请输入欲编译到…」） */
        if (g_szCompileOutPath[0] && !blOutSeen)
            TryFillCompileSaveDialog();

        if (!blOutSeen && g_szCompileOutPath[0])
        {
            DWORD dwAttr = GetFileAttributesA(g_szCompileOutPath);
            if (dwAttr != INVALID_FILE_ATTRIBUTES && !(dwAttr & FILE_ATTRIBUTE_DIRECTORY))
            {
                CapWrite("COMPILE-OUT-SEEN", g_szCompileOutPath);
                Trace("COMPILE output seen: %s", g_szCompileOutPath);
                EventWrite("COMPILE-OUT-SEEN", "%s", g_szCompileOutPath);
                blOutSeen = TRUE;
            }
        }
        if (blOutSeen && nChanged == 0 && nIter % 30 == 0)
        {
            /* 产物已出现且面板安静 → 再陪跑一小段就收（等面板刷完编译日志） */
            char szB[64];
            snprintf(szB, sizeof(szB), "quiesce t=%.1fs", (double)(GetTickCount() - t0) / 1000.0);
            CapWrite("COMPILE-QUIESCE", szB);
            break;
        }
        if (nIter % 100 == 0)
            Trace("COMPILE poll iter=%d changed=%d outSeen=%d", nIter, nChanged, blOutSeen ? 1 : 0);
        Sleep(100);
    }

    PanelDumpTails("FINAL");
    CapWrite("=== COMPILE END ===", blOutSeen ? "out-seen" : "timeout-or-not-seen");
    EventWrite("COMPILE-END", blOutSeen ? "out-seen" : "timeout-or-not-seen");
    Trace("COMPILE capture done (iters=%d, outSeen=%d, file=%s)", nIter, blOutSeen ? 1 : 0, g_szCapturePath);
    if (g_hCapture != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hCapture);
        g_hCapture = INVALID_HANDLE_VALUE;
    }
}

/* ==========================================================================
 * 6c. 无头：把本进程（e.exe）的顶层窗口挪到屏幕外
 * --------------------------------------------------------------------------
 * 外部的 elaunch.py 已经有一层「启动即 SW_HIDE + 2ms 轮询」；这里再放一层，
 * 好处是**不依赖任何外部脚本**也能做到不开窗口，并且能在窗口被 IDE 自己重新
 * 显示之后继续把它推回屏幕外。
 *
 * 为什么是「移出屏幕」而不是「SW_HIDE」（2026-09-22 实测）：
 *   把窗口 SW_HIDE 会让 `IsWindowVisible` 全变 FALSE；本库的保存框填充器原本
 *   带这个过滤，于是找不到那个模态框 ⇒ 编译静默卡死到超时。填充器已改为不看
 *   可见性，但「移出屏幕但保持可见」对 IDE 侧更接近原生状态，风险更低，
 *   用户侧的效果完全一样（看不见）。轮询间隔取 20ms —— 快路径由 elaunch.py 的
 *   2ms 轮询负责，这里只做兜底。
 *
 * 只动**本进程**（GetCurrentProcessId）且**没有 owner** 的顶层窗口：
 *   - 调试运行时，被调试程序是另一个进程 → 它的窗口不受影响（用户可以正常看见）；
 *   - 本库自己的隐藏工具窗口（owner 非空）不动，免得节外生枝。
 * ========================================================================== */
/* --------------------------------------------------------------------------
 * 6a. 无头守护（HideGuard）
 * --------------------------------------------------------------------------
 * 2026-09-22 重写。三条硬教训（都由 `re/win_probe/` 的逐像素取证逼出来）：
 *
 * 1) **绝不能过滤「有 owner 的窗口」**。保存框 `#32770`、启动对话框
 *    `Afx:400000:0:10003:0:0`、启动闪屏阴影 `shadow_window` **全都是 own=1**。
 *    老实现 `if (GetWindow(hWnd, GW_OWNER) != NULL) return TRUE;` 把它们全放过去了。
 * 2) **必须尽早起线程**。旧实现挂在 `WorkerProc`（`NL_IDE_READY`）里，比宿主库
 *    自身加载还晚约 5 秒；而 `Afx:…` 启动对话框恰好在 `NL_IDE_READY` 前一瞬显示。
 *    现在改在 `EnsureInit()`（`GetNewInf` 里）就起。
 * 3) **必须 1ms 级**。DWM 约 16ms 合成一帧，只要在窗口显示后 ~1ms 内挪走，
 *    屏幕上就不会出现任何一帧。默认 `Sleep()` 精度 15.6ms ⇒ 必须 `timeBeginPeriod(1)`。
 *
 * 动作 = 「可见 且 与虚拟屏幕相交」的顶层窗口 → `SW_HIDE`（仅 hide 档）+ 移到 -32000。
 * 因为 hide 后窗口不可见、park 后不再与屏幕相交，下一轮自然跳过 ⇒ 单次动作即可，
 * 不产生反复调用。
 * -------------------------------------------------------------------------- */

/* `ELANG_AI_HIDE` 取值语义（Python 侧传真实 mode 字符串；`1` 为历史兼容）：
 *   "0" / "off"          → 关闭守护
 *   "park" / "shide"     → 只移出屏幕（保持可见）
 *   其它非空（"1"/"hide"） → 额外 SW_HIDE
 * 返回：0=关闭 1=park 2=hide */
static int HideModeFromEnv(void)
{
    char sz[32] = { 0 };
    if (!GetEnvStr(HIDE_ENV_VAR, sz, sizeof(sz)))
        return 0;
    if (sz[0] == '\0' || strcmp(sz, "0") == 0 || strcmp(sz, "off") == 0)
        return 0;
    if (strcmp(sz, "park") == 0 || strcmp(sz, "shide") == 0)
        return 1;
    return 2;
}

static volatile LONG g_nHideActions = 0;    /* 累计动作次数（诊断） */

/* 把系统定时器精度提到 0.5ms。**不做这一步，`Sleep(1)` 实际会睡 15.6ms**
 * （Windows 默认精度），守护就跟不上 DWM 的 16ms 合成节拍。
 * 走 ntdll 动态解析，避免为 `timeBeginPeriod` 引入 winmm 依赖 / 改链接参数。
 *
 * ⚠️⚠️ **`typedef` 里绝不能写 `WINAPI`**（2026-09-22 血案）：
 *   `sdk_compat.h` 已把 `WINAPI` `#define` 成**空宏**（为兼容官方 SDK 的 VC6 __cdecl 约定），
 *   所以 `(WINAPI *)` 在这里等于 `(__cdecl *)`；而 `NtSetTimerResolution` 实际是
 *   `__stdcall`。callee 清栈 + caller 再清一次 ⇒ **ESP 偏移 12 字节**。
 *   又因为 `-O2` 隐含 `-fomit-frame-pointer`，本函数及**调用者**的局部变量全靠 ESP 寻址
 *   ⇒ `HideGuardProc` 里的 `nMode` 直接读成一段系统 DLL 地址（实测 `0x77E5D81B`），
 *   而日志照样打印、功能"看着还行"，极难发现。
 *   ⇒ 凡指向 Win32 / ntdll 导出函数的函数指针，**一律显式写 `__stdcall`**。 */
static void RaiseTimerResolution(void)
{
    typedef LONG (__stdcall *PFN_NtSetTimerResolution)(ULONG, BOOLEAN, PULONG);
    HMODULE h = GetModuleHandleA("ntdll.dll");
    PFN_NtSetTimerResolution pfn;
    ULONG ulCur = 0;

    if (h == NULL) h = LoadLibraryA("ntdll.dll");
    if (h == NULL) return;
    pfn = (PFN_NtSetTimerResolution)GetProcAddress(h, "NtSetTimerResolution");
    if (pfn != NULL)
    {
        if (pfn(5000, TRUE, &ulCur) >= 0)     /* 5000 × 100ns = 0.5ms */
            Trace("HIDEGUARD timer resolution -> %lu (x100ns)", (unsigned long)ulCur);
    }
}

/* 无头守护的当前档位（1=park 2=hide）。事件回调与轮询回调共用。 */
static volatile LONG g_nHideMode = 0;

static LONG g_nCloak = 0;         /* 成功贴上的斗篷数（诊断） */
static LONG g_nCloakFail = 0;     /* 贴失败的次数（诊断） */
static LONG g_nClkNewWin = 0;     /* 对 cls='NewWindow' 的贴斗篷尝试次数（诊断） */

/* 提前给窗口"披隐形斗篷"：`WS_EX_LAYERED` + 透明度 0 + `WS_EX_TRANSPARENT`。
 *
 * ⚠️⚠️ **这一步必须发生在窗口"第一次被显示"之前**（2026-09-22 实测）：
 *   只在 `EVENT_OBJECT_SHOW` / 轮询里处理，总会漏掉"显示→我们动手"之间的那一帧 ——
 *   `probe_wfp.py` 稳定抓到 `NewWindow` 有 30~80ms 露脸。
 *   挂在 `EVENT_OBJECT_CREATE` 上（窗口已建好、尚未显示）就能把**第一帧**也消灭。
 *
 * 为什么加 `WS_EX_LAYERED` 而不是只 `SW_HIDE`：
 *   "隐藏"和"位置"都是易语言自己的状态，它随时能写回去（实测它确实会）；
 *   而 `WS_EX_LAYERED` + alpha 0 是**窗口属性**，易语言只改可见性/位置动不了它，
 *   于是无论它怎么重新显示都画不出一个像素。顺带 `WindowFromPoint` 会跳过它，
 *   使「能不能被采样到」恰好等价于「用户能不能看到」。
 *
 * 只处理**顶层窗口**：子窗口随父窗口一起不绘制，逐个贴反而可能干扰控件行为。 */
static void CloakWindow(HWND hWnd)
{
    DWORD dwPid = 0;
    LONG  ex;
    LONG  st;
    int   bWatch;

    if (hWnd == NULL || !IsWindow(hWnd))
        return;
    GetWindowThreadProcessId(hWnd, &dwPid);
    if (dwPid != GetCurrentProcessId())
        return;
    st = GetWindowLongA(hWnd, GWL_STYLE);
    ex = GetWindowLongA(hWnd, GWL_EXSTYLE);
    {
        char szC[64] = { 0 };
        GetClassNameA(hWnd, szC, sizeof(szC));
        bWatch = (strcmp(szC, "NewWindow") == 0);
        if (bWatch && InterlockedIncrement(&g_nClkNewWin) <= 40)
            Trace("HIDEGUARD cloak NewWindow try#%ld style=0x%08X ex=0x%08X vis=%d",
                  (long)g_nClkNewWin, (unsigned)st, (unsigned)ex,
                  IsWindowVisible(hWnd) ? 1 : 0);
    }
    if (st & WS_CHILD)
    {
        if (bWatch && g_nClkNewWin <= 40)
            Trace("HIDEGUARD cloak NewWindow SKIPPED: 是子窗口(WS_CHILD)");
        return;
    }
    if ((ex & WS_EX_LAYERED) == 0)
    {
        /* ⚠️ `SetWindowLong` 光改 ex-style 还不够：窗口**已可见**时不会立刻生效，
           文档要求随后 `SetWindowPos` 带 `SWP_FRAMECHANGED`。
           但窗口**还没显示**时（也就是我们在 `EVENT_OBJECT_CREATE` 里最常遇到的情形）
           完全不需要它 —— 分层属性会在它第一次被显示时自动生效。跳过这一次调用
           可以少一次可能阻塞的窗口操作。
           ⚠️ 另注：`SetWindowLong` 会**同步**向窗口线程发 `WM_STYLECHANGING`，
           主线程忙时会把我们卡住 —— 所以只在"还没披过"时才调（这里已经是）。 */
        SetWindowLongA(hWnd, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT);
        if (IsWindowVisible(hWnd))
            SetWindowPos(hWnd, NULL, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED | SWP_ASYNCWINDOWPOS);
    }
    if (SetLayeredWindowAttributes(hWnd, 0, 0, LWA_ALPHA))
    {
        if (bWatch && g_nClkNewWin <= 40)
            Trace("HIDEGUARD cloak NewWindow OK (ex=0x%08X)", (unsigned)ex);
        else if (InterlockedIncrement(&g_nCloak) <= 40)
        {
            char szCls[64] = { 0 };
            GetClassNameA(hWnd, szCls, sizeof(szCls));
            Trace("HIDEGUARD cloak#%ld cls='%s' ex=0x%08X", (long)g_nCloak, szCls, (unsigned)ex);
        }
    }
    else if (InterlockedIncrement(&g_nCloakFail) <= 12)
    {
        char szCls[64] = { 0 };
        GetClassNameA(hWnd, szCls, sizeof(szCls));
        Trace("HIDEGUARD cloak FAILED cls='%s' err=%lu", szCls, (unsigned long)GetLastError());
    }
}

/* 单个窗口的隐藏动作。返回 TRUE 表示真的动了它。
 * 判据：「属于本进程」且「可见」且「与虚拟屏幕相交」——三个都满足才动，
 * 所以 hide 之后窗口不可见、park 之后不再相交，下一轮自然跳过，不会反复调用。 */
static BOOL ActOnWindow(HWND hWnd, int nMode)
{
    DWORD dwPid = 0;
    RECT  rc;

    if (hWnd == NULL || !IsWindow(hWnd))
        return FALSE;
    GetWindowThreadProcessId(hWnd, &dwPid);
    if (dwPid != GetCurrentProcessId())
        return FALSE;                       /* 只动自己的窗口 */
    if (!IsWindowVisible(hWnd))
        return FALSE;                       /* 不可见的既看不到、也不用挪（热路径，先判它） */
    if (!GetWindowRect(hWnd, &rc))
        return FALSE;

    {   /* 与虚拟屏幕求交，完全在屏幕外就放过。数值即 SM_X/Y/CXVIRTUALSCREEN/CYVIRTUALSCREEN。 */
        int vx = GetSystemMetrics(76);
        int vy = GetSystemMetrics(77);
        int vw = GetSystemMetrics(78);
        int vh = GetSystemMetrics(79);
        if (rc.right <= vx || rc.left >= vx + vw || rc.bottom <= vy || rc.top >= vy + vh)
            return FALSE;
    }

    if (InterlockedIncrement(&g_nHideActions) <= 32)
    {
        char szCls[64] = { 0 };
        GetClassNameA(hWnd, szCls, sizeof(szCls));
        Trace("HIDEGUARD act#%ld mode=%d cls='%s' rect=(%ld,%ld,%ld,%ld)",
              (long)g_nHideActions, nMode, szCls,
              (long)rc.left, (long)rc.top, (long)rc.right, (long)rc.bottom);
    }

    if (nMode >= 2)
        CloakWindow(hWnd);                   /* 持久隐身（详见其注释） */

    /* ⚠️ `SWP_ASYNCWINDOWPOS` + `SWP_HIDEWINDOW`：把「移出屏幕 + 隐藏」合成**一个
       post 出去**的请求，既不阻塞我们，也不用再调同步的 `ShowWindow`。
       少了 `SWP_HIDEWINDOW` 就得靠 `ShowWindow`，而同步版 `ShowWindow` 一样会
       等窗口线程 —— 易语言主线程忙时把我们的钩子线程卡死 3 秒（实测
       `poll iter=1 enumMs=2953`），钩子事件随之积压丢弃。 */
    SetWindowPos(hWnd, NULL, -32000, -32000, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING |
                 SWP_ASYNCWINDOWPOS | SWP_HIDEWINDOW);
    return TRUE;
}

/* --- 事件驱动：系统「窗口显示 / 位置变化」时主动叫我们，零轮询延迟 ----------------
 * 轮询兜底在实测里抓不住 16ms 的一帧（`EnumWindows` 走完全系统窗口要几十毫秒，
 * 再加上 `Sleep` 粒度），所以必须让系统在窗口**刚显示的那一刻**就通知我们。
 *
 * `WINEVENT_OUTOFCONTEXT` 的回调**运行在调用 `SetWinEventHook` 的那个线程上**
 * （即守护线程，且它必须处于取消息状态）。所以 `g_hq` 只有守护线程会读写，
 * **不需要加锁**。回调里只入队、不做窗口操作 —— 避免在钩子回调里做可能引发
 * 重入/阻塞的动作。 */
#define HQ_MAX 64
static HWND g_hq[HQ_MAX];
static int  g_nHq = 0;
static volatile LONG g_nEvtAll = 0;     /* 回调被系统调用的总次数（诊断：钩子通不通） */
static volatile LONG g_nEvtQueued = 0;  /* 通过过滤、真正入队的次数（诊断） */
static volatile LONG g_nEvtAct = 0;     /* 在回调里当场处理掉的次数（诊断） */
static LONG g_nEvtKind[4] = { 0, 0, 0, 0 };  /* 0=CREATE 1=DESTROY 2=SHOW 3=LOCATIONCHANGE */
static LONG g_nEvtNewWin = 0;           /* 回调里见到 cls='NewWindow' 的次数（诊断） */

/* ⚠️ 在回调里**当场处理**，而不是只入队等主循环（2026-09-22 实测）：
 *   入队→主循环 还有几毫秒延迟，实测 `_EL_PicWnd` 那次就因此晚了 88ms 才按住。
 *   回调运行在本守护线程上（`WINEVENT_OUTOFCONTEXT`），直接操作窗口是安全的；
 *   我们自己 `SetWindowPos` 引发的后续事件会被投进消息队列，**在本回调返回后**才派发，
 *   所以不会重入。 */
static void CALLBACK HideWinEventProc(HWINEVENTHOOK hHook, DWORD dwEvent, HWND hWnd,
                                      LONG idObject, LONG idChild,
                                      DWORD dwThread, DWORD dwTime)
{
    (void)hHook; (void)dwThread; (void)dwTime;
    InterlockedIncrement(&g_nEvtAll);
    if (dwEvent >= EVENT_OBJECT_CREATE && dwEvent <= EVENT_OBJECT_SHOW)
        InterlockedIncrement(&g_nEvtKind[dwEvent - EVENT_OBJECT_CREATE]);
    else if (dwEvent == EVENT_OBJECT_LOCATIONCHANGE)
        InterlockedIncrement(&g_nEvtKind[3]);
    if (hWnd == NULL)
        return;
    if (idObject != OBJID_WINDOW || idChild != 0)
        return;                             /* 只要"窗口本身"，不要里面的子控件 */
    InterlockedIncrement(&g_nEvtQueued);
    {
        char szC[64] = { 0 };
        GetClassNameA(hWnd, szC, sizeof(szC));
        if (strcmp(szC, "NewWindow") == 0)
        {
            LONG n = InterlockedIncrement(&g_nEvtNewWin);
            if (n <= 24)
                Trace("HIDEGUARD evt NewWindow#%ld event=0x%04X child=%d vis=%d",
                      (long)n, (unsigned)dwEvent, (int)idChild,
                      IsWindowVisible(hWnd) ? 1 : 0);
        }
    }

    /* ⭐ 先披斗篷**再**判可见性：`EVENT_OBJECT_CREATE` 时窗口还没显示，
       `ActOnWindow` 会因"不可见"而放行，但斗篷必须此刻就贴上 —— 否则第一次
       显示的那一帧照样看得见。这是消灭"第一帧"的关键顺序。 */
    if (g_nHideMode >= 2)
        CloakWindow(hWnd);

    if (ActOnWindow(hWnd, (int)g_nHideMode))
        InterlockedIncrement(&g_nEvtAct);
    /* 兜底：万一当场没按住（比如窗口此刻还没"可见"），交给主循环再过一遍 */
    else if (g_nHq < HQ_MAX)
        g_hq[g_nHq++] = hWnd;
}

static BOOL CALLBACK ParkTopWindowsProc(HWND hWnd, LPARAM lp)
{
    int nMode = (int)lp;
    /* 无条件披斗篷（**不管可见与否**）：钩子事件有投递延迟，总有窗口在我们收到
       `EVENT_OBJECT_CREATE` 之前就已经被显示；靠这一路"每轮补披"把它们兜住。
       对已披过的窗口，这里只剩 `GetWindowLong` + `SetLayeredWindowAttributes` 两次调用。 */
    if (nMode >= 2)
        CloakWindow(hWnd);
    ActOnWindow(hWnd, nMode);
    return TRUE;
}

static DWORD __stdcall HideGuardProc(LPVOID lp)
{
    int nMode = (int)(size_t)lp;
    int nIter = 0;
    HWINEVENTHOOK hkShow = NULL;
    HWINEVENTHOOK hkMove = NULL;

    /* ⚠️ 栈自检：`RaiseTimerResolution` 若把调用约定写错（见其注释），
       `nMode` 会在它返回后变成垃圾 —— 这两行一目了然。 */
    Trace("HIDEGUARD start (raw lp=0x%08X mode=%d; 1=park 2=hide)",
          (unsigned)(size_t)lp, nMode);

    RaiseTimerResolution();
    Trace("HIDEGUARD after timer-resolve: mode=%d raw lp=0x%08X (若与上面不同 = 栈已被破坏!)",
          nMode, (unsigned)(size_t)lp);

    g_nHideMode = nMode;

    /* 事件驱动：只订阅**本进程**窗口，避免全系统事件洪流（`EVENT_OBJECT_LOCATIONCHANGE`
       在别的进程上是每秒上千次）。`idProcess` 由系统侧过滤。
       三个事件各司其职：
         CREATE  —— 窗口"创建时就是可见"(WS_VISIBLE) 的情况，不一定会再发 SHOW；
         SHOW    —— 常规的"由隐转显"；
         LOCATIONCHANGE —— IDE 把已经藏好的窗口**又摆回屏幕**（实测会发生）。 */
    hkShow = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_SHOW, NULL,
                             HideWinEventProc, GetCurrentProcessId(), 0,
                             WINEVENT_OUTOFCONTEXT);
    hkMove = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, NULL,
                             HideWinEventProc, GetCurrentProcessId(), 0,
                             WINEVENT_OUTOFCONTEXT);
    Trace("HIDEGUARD hook create+show=0x%08X move=0x%08X",
          (unsigned)(size_t)hkShow, (unsigned)(size_t)hkMove);

    /* ⚠️ 必须把优先级抬到 TIME_CRITICAL（2026-09-22 实测）：e.exe 主线程一旦忙起来，
       普通优先级的守护线程会被饿到**每轮 140ms**（`iter=10` 花了 1.4 秒），
       等于完全失去守护作用。本线程绝大多数时间在 MsgWait 上睡觉，不会抢 CPU。 */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    for (;;)
    {
        /* ⚠️⚠️ 本循环里**绝不允许出现 `EnumWindows`**（2026-09-22 血的教训）：
           `WINEVENT_OUTOFCONTEXT` 的事件**只有在本线程处于"取消息状态"时**才会被投递，
           而 `EnumWindows` 是同步枚举、可能一卡就是**2.4 秒**（实测 `iter=1 enumMs=2375`）。
           把兜底轮询放在这里 ⇒ 每轮都把自己的钩子堵死 ⇒ 事件永远迟到几十毫秒，
           表现为"斗篷贴了也没用、`NewWindow` 稳定露 30~80ms"。
           现在兜底轮询挪到独立线程 `HidePollProc`，本循环只做消息泵，延迟 ~1ms。 */
        DWORD dwR = MsgWaitForMultipleObjectsEx(0, NULL, 1, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (dwR == WAIT_OBJECT_0)
        {
            MSG msg;
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        /* 事件队列里是"回调当场没按住"的窗口，再过一遍 */
        if (g_nHq > 0)
        {
            int n = g_nHq;
            int i;
            g_nHq = 0;                       /* 只有本线程写，无需加锁 */
            for (i = 0; i < n; i++)
                ActOnWindow(g_hq[i], nMode);
        }

        if ((++nIter % 2000) == 0)
            Trace("HIDEGUARD iter=%d evtAll=%ld(C%d D%d S%d L%d) newWinEvt=%ld evtAct=%ld cloak=%ld/%ld acts=%ld",
                  nIter, (long)g_nEvtAll,
                  (int)g_nEvtKind[0], (int)g_nEvtKind[1],
                  (int)g_nEvtKind[2], (int)g_nEvtKind[3],
                  (long)g_nEvtNewWin,
                  (long)g_nEvtAct, (long)g_nCloak, (long)g_nCloakFail,
                  (long)g_nHideActions);
    }
    /* not reached */
}

/* 兜底轮询线程：定期全量扫一遍本进程窗口，补披斗篷 + 挪出屏幕。
 * ⚠️ 它**必须独立于钩子线程**（见 `HideGuardProc` 里那段注释）——
 *    `EnumWindows` 会长时间阻塞"取消息状态"，和钩子共线程等于自废武功。
 * 频率刻意取 50ms：钩子才是主力，这里只兜"易语言偷偷把 ex-style 改回来"这类漏网。 */
static DWORD __stdcall HidePollProc(LPVOID lp)
{
    int nMode = (int)(size_t)lp;
    int nIter = 0;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;)
    {
        DWORD dwT0 = GetTickCount();
        EnumWindows(ParkTopWindowsProc, (LPARAM)(size_t)nMode);
        if (++nIter == 1 || (nIter % 400) == 0)
            Trace("HIDEGUARD poll iter=%d enumMs=%lu", nIter,
                  (unsigned long)(GetTickCount() - dwT0));
        Sleep(3);
    }
    /* not reached */
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

    /* 无头守护**不在这里起** —— 已提前到 `EnsureInit()`（宿主库加载那一刻）。
       这里只留一行说明，避免以后有人又把它挪回来（那会晚 ~5 秒、漏掉启动对话框）。 */
    Trace("WORKER start (hide-guard 已在 EnsureInit 里起，早于本线程约 5s)");

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

    /* 第 5 步：编译任务（ELANG_AI_COMPILE）——与 RUN 互斥使用，一个会话只做一个 */
    {
        HWND hMain = IdeGetMainHwnd();
        if (hMain != NULL && IsWindow(hMain))
            DoCompileTask(hMain);
        else
            Trace("STEP5 skipped: main window is NULL or ELANG_AI_COMPILE unset");
    }

    Trace("WORKER done");
    EventWrite("WORKER-DONE", "");
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
        EventWrite("IDE-READY", "ai_mode=%d", g_blAiMode ? 1 : 0);
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
        EventWrite("IDE-UNLOAD", "");
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
        EventWrite("ADDIN-LOADED", "pid=%lu", (unsigned long)GetCurrentProcessId());

        /* 事件监控线程：与无头档位无关，AI 会话一律起（弹窗监控不依赖隐身）。 */
        {
            HANDLE hD = CreateThread(NULL, 0, DlgWatchProc, NULL, 0, NULL);
            if (hD != NULL) CloseHandle(hD);
            else Trace("DLGWATCH CreateThread FAILED err=%lu", GetLastError());
        }

        /* 无头守护：**这里是本库最早能跑代码的地方**，必须在这儿起线程。
           曾经挂在 `WorkerProc`（NL_IDE_READY）里 —— 那比宿主库加载还晚约 5 秒，
           而 `Afx:…` 启动对话框正好卡在 NL_IDE_READY 前显示，于是漏了一帧。
           详见 HideGuardProc 上方注释。 */
        {
            int nMode = HideModeFromEnv();
            if (nMode > 0)
            {
                /* 两条线程，各司其职（见两个 Proc 的注释）：
                     HideGuardProc —— 装 `SetWinEventHook` + 纯消息泵，**绝不阻塞**，
                                      事件驱动、延迟 ~1ms；
                     HidePollProc  —— 独立兜底轮询（50ms），
                                      `EnumWindows` 可能卡几秒，绝不能和钩子共线程。 */
                HANDLE hG = CreateThread(NULL, 0, HideGuardProc, (LPVOID)(size_t)nMode, 0, NULL);
                HANDLE hP = CreateThread(NULL, 0, HidePollProc, (LPVOID)(size_t)nMode, 0, NULL);
                if (hG != NULL) CloseHandle(hG);
                else Trace("HIDEGUARD CreateThread FAILED err=%lu", GetLastError());
                if (hP != NULL) CloseHandle(hP);
                else Trace("HIDEGUARD poll CreateThread FAILED err=%lu", GetLastError());
            }
            else
            {
                Trace("HIDEGUARD disabled (ELANG_AI_HIDE=0/off)");
            }
        }
    }
}

extern "C" PLIB_INFO WINAPI GetNewInf(void)
{
    EnsureInit();
    return &s_LibInfo;
}
