/*
 * core.c —— GUI 与自检共用：字符串/路径工具、参数转义、UTF-8 行切分、子进程管道。
 */
#include "core.h"

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ======================================================================
 * 动态宽字符串
 * ====================================================================== */

static BOOL WS_Reserve(WStr *s, size_t add)
{
    size_t need, nc;
    wchar_t *np;

    if (s->oom)
        return FALSE;
    need = s->len + add + 1;
    if (need <= s->cap)
        return TRUE;
    nc = s->cap ? s->cap : 64;
    while (nc < need)
        nc *= 2;
    np = (wchar_t *)realloc(s->p, nc * sizeof(wchar_t));
    if (!np) {
        s->oom = TRUE;
        return FALSE;
    }
    s->p = np;
    s->cap = nc;
    return TRUE;
}

void WS_AddN(WStr *s, const wchar_t *t, size_t n)
{
    if (!WS_Reserve(s, n))
        return;
    if (n)
        memcpy(s->p + s->len, t, n * sizeof(wchar_t));
    s->len += n;
    s->p[s->len] = 0;
}

void WS_Add(WStr *s, const wchar_t *t)
{
    WS_AddN(s, t, wcslen(t));
}

void WS_AddC(WStr *s, wchar_t c)
{
    WS_AddN(s, &c, 1);
}

static void WS_AddRep(WStr *s, wchar_t c, size_t n)
{
    while (n--)
        WS_AddC(s, c);
}

wchar_t *WS_Detach(WStr *s)
{
    wchar_t *r;

    if (!s->oom && !s->p)
        WS_Reserve(s, 0);
    if (s->oom) {
        WS_Free(s);
        return NULL;
    }
    s->p[s->len] = 0;
    r = s->p;
    s->p = NULL;
    s->len = s->cap = 0;
    return r;
}

void WS_Free(WStr *s)
{
    free(s->p);
    s->p = NULL;
    s->len = s->cap = 0;
    s->oom = FALSE;
}

/* ======================================================================
 * 通用工具
 * ====================================================================== */

wchar_t *WDup(const wchar_t *s)
{
    size_t n;
    wchar_t *r;

    if (!s)
        return NULL;
    n = wcslen(s);
    r = (wchar_t *)malloc((n + 1) * sizeof(wchar_t));
    if (r)
        memcpy(r, s, (n + 1) * sizeof(wchar_t));
    return r;
}

wchar_t *WCat(const wchar_t *a, const wchar_t *b, const wchar_t *c)
{
    WStr s;
    ZeroMemory(&s, sizeof(s));
    if (a) WS_Add(&s, a);
    if (b) WS_Add(&s, b);
    if (c) WS_Add(&s, c);
    return WS_Detach(&s);
}

BOOL StartsWith(const wchar_t *s, const wchar_t *prefix)
{
    size_t n = wcslen(prefix);
    return s && wcsncmp(s, prefix, n) == 0;
}

static BOOL IsSpaceW(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == 0x3000;
}

void TrimPath(wchar_t *s)
{
    size_t n, i;

    if (!s)
        return;
    for (int pass = 0; pass < 2; pass++) {
        n = wcslen(s);
        while (n > 0 && IsSpaceW(s[n - 1]))
            s[--n] = 0;
        i = 0;
        while (s[i] && IsSpaceW(s[i]))
            i++;
        if (i)
            memmove(s, s + i, (n - i + 1) * sizeof(wchar_t));
        n = wcslen(s);
        /* 去掉成对包裹的引号，例如从资源管理器“复制为路径”粘贴的内容 */
        if (pass == 0 && n >= 2 && s[0] == L'"' && s[n - 1] == L'"') {
            memmove(s, s + 1, (n - 2) * sizeof(wchar_t));
            s[n - 2] = 0;
        } else {
            break;
        }
    }
}

BOOL IsFile(const wchar_t *p)
{
    DWORD a;
    if (!p || !*p)
        return FALSE;
    a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

BOOL IsDir(const wchar_t *p)
{
    DWORD a;
    if (!p || !*p)
        return FALSE;
    a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

wchar_t *FullPath(const wchar_t *p)
{
    DWORD n, r;
    wchar_t *b;

    if (!p || !*p)
        return NULL;
    n = GetFullPathNameW(p, 0, NULL, NULL);
    if (n == 0)
        return NULL;
    b = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!b)
        return NULL;
    r = GetFullPathNameW(p, n, b, NULL);
    if (r == 0 || r >= n) {
        free(b);
        return NULL;
    }
    return b;
}

static wchar_t *LastSep(wchar_t *p)
{
    wchar_t *a = wcsrchr(p, L'\\');
    wchar_t *b = wcsrchr(p, L'/');
    return (a > b) ? a : b;
}

wchar_t *ExeDir(void)
{
    wchar_t *b = (wchar_t *)malloc(PATH_BUF * sizeof(wchar_t));
    DWORD n;
    wchar_t *sep;

    if (!b)
        return NULL;
    n = GetModuleFileNameW(NULL, b, PATH_BUF);
    if (n == 0 || n >= PATH_BUF) {
        free(b);
        return NULL;
    }
    sep = LastSep(b);
    if (!sep) {
        free(b);
        return NULL;
    }
    *sep = 0;
    return b;
}

wchar_t *ParentDir(const wchar_t *p)
{
    wchar_t *c = WDup(p);
    wchar_t *sep;
    size_t n;

    if (!c)
        return NULL;
    /* 去掉结尾分隔符（保留 "C:\" 这类根） */
    n = wcslen(c);
    while (n > 3 && (c[n - 1] == L'\\' || c[n - 1] == L'/'))
        c[--n] = 0;
    sep = LastSep(c);
    if (!sep) {
        free(c);
        return NULL;
    }
    /* "C:\x" 的父目录保留为 "C:\" */
    if (sep == c + 2 && c[1] == L':')
        sep[1] = 0;
    else
        *sep = 0;
    return c;
}

BOOL IsErrorLine(const wchar_t *line)
{
    return StartsWith(line, L"ERROR:") || StartsWith(line, L"[错误]");
}

/* ======================================================================
 * Windows 命令行转义
 * 规则（CommandLineToArgvW / MSVCRT）：
 *   - 整体用双引号包裹（始终加引号，空串变为 ""）；
 *   - 位于双引号之前的 n 个反斜杠 → 2n+1 个反斜杠 + \"；
 *   - 位于结尾（即包裹引号之前）的 n 个反斜杠 → 2n 个；
 *   - 其余位置的反斜杠原样保留。
 * ====================================================================== */

void AppendQuotedArg(WStr *out, const wchar_t *arg)
{
    const wchar_t *p = arg ? arg : L"";

    WS_AddC(out, L'"');
    for (;;) {
        size_t bs = 0;
        while (*p == L'\\') {
            bs++;
            p++;
        }
        if (*p == 0) {
            WS_AddRep(out, L'\\', bs * 2);
            break;
        }
        if (*p == L'"') {
            WS_AddRep(out, L'\\', bs * 2 + 1);
            WS_AddC(out, L'"');
        } else {
            WS_AddRep(out, L'\\', bs);
            WS_AddC(out, *p);
        }
        p++;
    }
    WS_AddC(out, L'"');
}

wchar_t *QuoteArg(const wchar_t *arg)
{
    WStr s;
    ZeroMemory(&s, sizeof(s));
    AppendQuotedArg(&s, arg);
    return WS_Detach(&s);
}

wchar_t *BuildCommandLine(const wchar_t *app, const wchar_t *const *args, int nargs)
{
    WStr s;
    int i;

    ZeroMemory(&s, sizeof(s));
    /* argv[0] 不做反斜杠转义解析，但 exe 路径不可能含引号或以反斜杠结尾，统一处理即可 */
    AppendQuotedArg(&s, app);
    for (i = 0; i < nargs; i++) {
        WS_AddC(&s, L' ');
        AppendQuotedArg(&s, args[i]);
    }
    return WS_Detach(&s);
}

/* ======================================================================
 * UTF-8 按行切分
 * 按字节缓存直到遇到 '\n'（UTF-8 多字节序列中不会出现 0x0A），
 * 因此跨 ReadFile 缓冲区被截断的多字节字符会自然拼回完整行后再解码。
 * 超长无换行时在完整字符边界强制切分，避免拆开多字节字符。
 * ====================================================================== */

size_t Utf8CompletePrefix(const unsigned char *b, size_t len)
{
    size_t i = len;
    int back = 0;

    while (i > 0 && back < 4) {
        unsigned char c = b[i - 1];
        if ((c & 0xC0) != 0x80) {
            size_t need;
            if (c < 0x80)       need = 1;
            else if (c >= 0xF8) need = 1; /* 非法首字节，按单字节处理 */
            else if (c >= 0xF0) need = 4;
            else if (c >= 0xE0) need = 3;
            else if (c >= 0xC0) need = 2;
            else                need = 1;
            return (i - 1 + need <= len) ? len : i - 1;
        }
        i--;
        back++;
    }
    return len; /* 末尾全是续字节（非法序列），原样交给解码器替换 */
}

void Splitter_Init(LineSplitter *s, LineFn fn, void *ctx)
{
    ZeroMemory(s, sizeof(*s));
    s->fn = fn;
    s->ctx = ctx;
    s->atStart = TRUE;
}

static void Splitter_Emit(LineSplitter *s, const char *p, size_t n, BOOL stripCR)
{
    int wn = 0;
    wchar_t *w;

    if (s->atStart) {
        s->atStart = FALSE;
        if (n >= 3 && memcmp(p, "\xEF\xBB\xBF", 3) == 0) { /* 去 UTF-8 BOM */
            p += 3;
            n -= 3;
        }
    }
    if (stripCR && n > 0 && p[n - 1] == '\r')
        n--;
    if (n > 0) {
        wn = MultiByteToWideChar(CP_UTF8, 0, p, (int)n, NULL, 0);
        if (wn <= 0)
            wn = 0;
    }
    w = (wchar_t *)malloc(((size_t)wn + 1) * sizeof(wchar_t));
    if (!w)
        return;
    if (wn > 0 && MultiByteToWideChar(CP_UTF8, 0, p, (int)n, w, wn) != wn)
        wn = 0;
    w[wn] = 0;
    s->fn(s->ctx, w); /* 所有权交给回调 */
}

BOOL Splitter_Feed(LineSplitter *s, const char *data, size_t n)
{
    size_t scanFrom, start = 0, i;

    if (s->len + n > s->cap) {
        size_t nc = s->cap ? s->cap : 8192;
        char *nb;
        while (nc < s->len + n)
            nc *= 2;
        nb = (char *)realloc(s->buf, nc);
        if (!nb)
            return FALSE;
        s->buf = nb;
        s->cap = nc;
    }
    memcpy(s->buf + s->len, data, n);
    scanFrom = s->len; /* 旧数据中必然没有 '\n' */
    s->len += n;

    for (i = scanFrom; i < s->len; i++) {
        if (s->buf[i] == '\n') {
            Splitter_Emit(s, s->buf + start, i - start, TRUE);
            start = i + 1;
        }
    }
    while (s->len - start >= SPLIT_MAX_LINE) {
        size_t cut = Utf8CompletePrefix((const unsigned char *)s->buf + start, SPLIT_MAX_LINE);
        if (cut == 0)
            cut = SPLIT_MAX_LINE;
        Splitter_Emit(s, s->buf + start, cut, FALSE);
        start += cut;
    }
    if (start) {
        memmove(s->buf, s->buf + start, s->len - start);
        s->len -= start;
    }
    return TRUE;
}

void Splitter_Finish(LineSplitter *s)
{
    if (s->len > 0)
        Splitter_Emit(s, s->buf, s->len, TRUE);
    free(s->buf);
    s->buf = NULL;
    s->len = s->cap = 0;
}

/* ======================================================================
 * 子进程
 * ====================================================================== */

static CRITICAL_SECTION g_procLock;
static HANDLE g_hChild;          /* 当前子进程句柄，受 g_procLock 保护 */
static volatile LONG g_cancel;   /* 已请求终止 */

void Runner_Init(void)
{
    InitializeCriticalSection(&g_procLock);
}

void Runner_Shutdown(void)
{
    DeleteCriticalSection(&g_procLock);
}

void Runner_ResetCancel(void)
{
    InterlockedExchange(&g_cancel, 0);
}

void Runner_RequestCancel(void)
{
    InterlockedExchange(&g_cancel, 1);
    EnterCriticalSection(&g_procLock);
    if (g_hChild)
        TerminateProcess(g_hChild, 1); /* 只终止自己启动的进程 */
    LeaveCriticalSection(&g_procLock);
}

BOOL RunCli(const wchar_t *app, const wchar_t *const *args, int nargs,
            LineFn fn, void *ctx, RunResult *rr)
{
    SECURITY_ATTRIBUTES sa;
    HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    wchar_t *cmd;
    BOOL ok;
    DWORD err, code = 0;
    LineSplitter sp;
    char buf[4096];

    ZeroMemory(rr, sizeof(*rr));
    cmd = BuildCommandLine(app, args, nargs);
    if (!cmd) {
        rr->winErr = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    if (wcslen(cmd) >= 32767) { /* CreateProcessW 命令行上限 */
        free(cmd);
        rr->winErr = ERROR_FILENAME_EXCED_RANGE;
        return FALSE;
    }

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        rr->winErr = GetLastError();
        free(cmd);
        return FALSE;
    }
    /* 读端留在本进程，不可继承 */
    if (!SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0)) {
        rr->winErr = GetLastError();
        CloseHandle(rd);
        CloseHandle(wr);
        free(cmd);
        return FALSE;
    }
    /* 标准输入给 NUL，避免子进程等待输入 */
    nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                      OPEN_EXISTING, 0, NULL);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = (nul != INVALID_HANDLE_VALUE) ? nul : NULL;
    si.hStdOutput = wr;
    si.hStdError = wr; /* stdout 与 stderr 合并到同一管道 */
    ZeroMemory(&pi, sizeof(pi));

    ok = CreateProcessW(app, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    err = ok ? 0 : GetLastError();

    /* 父进程必须关闭写端，否则子进程退出后 ReadFile 不会返回 */
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    free(cmd);

    if (!ok) {
        CloseHandle(rd);
        rr->winErr = err;
        return FALSE;
    }
    CloseHandle(pi.hThread);

    EnterCriticalSection(&g_procLock);
    g_hChild = pi.hProcess;
    if (g_cancel) /* 启动前已请求终止 */
        TerminateProcess(pi.hProcess, 1);
    LeaveCriticalSection(&g_procLock);
    rr->launched = TRUE;

    Splitter_Init(&sp, fn, ctx);
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(rd, buf, sizeof(buf), &got, NULL))
            break; /* ERROR_BROKEN_PIPE：子进程已关闭写端 */
        if (got == 0)
            break;
        if (!Splitter_Feed(&sp, buf, got))
            break;
    }
    Splitter_Finish(&sp);
    CloseHandle(rd);

    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &code))
        code = (DWORD)-1;

    EnterCriticalSection(&g_procLock);
    g_hChild = NULL;
    CloseHandle(pi.hProcess);
    LeaveCriticalSection(&g_procLock);

    rr->exitCode = code;
    return TRUE;
}
