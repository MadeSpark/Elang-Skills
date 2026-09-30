/*
 * selftest.c —— 隐藏自检：qaq-etool-gui.exe --selftest <cli路径> <日志文件> [源 .e] [输出目录]
 * 不显示窗口，用与 GUI 完全相同的转义/进程/管道/解码代码运行 CLI，
 * 结果以 UTF-8（无 BOM）写入日志。
 * 返回值：0 = 流程完成且全部检查通过；3 = 流程完成但有检查失败；1 = 无法进行（日志/CLI/启动失败）。
 */
#include "core.h"
#include "selftest.h"

#include <shlobj.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <strsafe.h>

/* ---------- 日志 ---------- */

static void LogW(HANDLE f, const wchar_t *s)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    char *b;
    DWORD wrote;

    if (n <= 0)
        return;
    b = (char *)malloc((size_t)n + 2);
    if (!b)
        return;
    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, b, n, NULL, NULL) != n) {
        free(b);
        return;
    }
    b[n - 1] = '\r';
    b[n] = '\n';
    WriteFile(f, b, (DWORD)n + 1, &wrote, NULL);
    free(b);
}

static void LogFmt(HANDLE f, const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    if (SUCCEEDED(StringCchVPrintfW(buf, 1024, fmt, ap)))
        LogW(f, buf);
    va_end(ap);
}

static void Log2(HANDLE f, const wchar_t *a, const wchar_t *b)
{
    wchar_t *s = WCat(a, b, NULL);
    if (s) {
        LogW(f, s);
        free(s);
    }
}

/* ---------- 转义单元断言 ---------- */

static int TestQuote(HANDLE f)
{
    static const struct { const wchar_t *in; const wchar_t *expect; } cases[] = {
        { L"",                          L"\"\"" },
        { L"abc",                       L"\"abc\"" },
        { L"a b",                       L"\"a b\"" },
        { L"a\"b",                      L"\"a\\\"b\"" },
        { L"C:\\dir\\",                 L"\"C:\\dir\\\\\"" },
        { L"C:\\dir\\\\",               L"\"C:\\dir\\\\\\\\\"" },
        { L"a\\\\\"b",                  L"\"a\\\\\\\\\\\"b\"" },
        { L"a\\b c",                    L"\"a\\b c\"" },
        { L"\"",                        L"\"\\\"\"" },
        { L"C:\\测试\\新增 文字文件.e", L"\"C:\\测试\\新增 文字文件.e\"" },
    };
    int fails = 0;
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        wchar_t *q = QuoteArg(cases[i].in);
        BOOL okStr = q && wcscmp(q, cases[i].expect) == 0;
        BOOL okRound = FALSE;

        /* 反向验证：用 CommandLineToArgvW 解析回来必须等于原串 */
        if (q) {
            wchar_t *cmd = WCat(L"x.exe ", q, NULL);
            if (cmd) {
                int argc = 0;
                LPWSTR *argv = CommandLineToArgvW(cmd, &argc);
                if (argv) {
                    okRound = (argc == 2 && wcscmp(argv[1], cases[i].in) == 0);
                    LocalFree(argv);
                }
                free(cmd);
            }
        }
        {
            WStr s;
            ZeroMemory(&s, sizeof(s));
            WS_Add(&s, (okStr && okRound) ? L"[转义] 通过 " : L"[转义] 失败 ");
            AppendQuotedArg(&s, cases[i].in); /* 仅用于显示 */
            WS_Add(&s, L" => ");
            WS_Add(&s, q ? q : L"(null)");
            WS_Add(&s, okRound ? L"  回解析=一致" : L"  回解析=不一致");
            {
                wchar_t *line = WS_Detach(&s);
                if (line) {
                    LogW(f, line);
                    free(line);
                }
            }
        }
        if (!(okStr && okRound))
            fails++;
        free(q);
    }
    /* 整条命令行 */
    {
        const wchar_t *args[] = { L"-src", L"C:\\a b\\", L"", L"x\"y" };
        wchar_t *cmd = BuildCommandLine(L"C:\\t\\QAQ-ETool.exe", args, 4);
        int argc = 0;
        LPWSTR *argv = cmd ? CommandLineToArgvW(cmd, &argc) : NULL;
        BOOL ok = argv && argc == 5 && wcscmp(argv[0], L"C:\\t\\QAQ-ETool.exe") == 0;
        int k;
        for (k = 0; ok && k < 4; k++)
            ok = wcscmp(argv[k + 1], args[k]) == 0;
        Log2(f, ok ? L"[转义] 通过 整条命令行: " : L"[转义] 失败 整条命令行: ", cmd ? cmd : L"(null)");
        if (!ok)
            fails++;
        if (argv)
            LocalFree(argv);
        free(cmd);
    }
    return fails;
}

/* ---------- UTF-8 切分单元断言 ---------- */

typedef struct {
    wchar_t *lines[16];
    int n;
    WStr all;     /* 所有段拼接 */
    int segs;
} CollectCtx;

static void CollectLine(void *ctx, wchar_t *line)
{
    CollectCtx *c = (CollectCtx *)ctx;
    WS_Add(&c->all, line);
    c->segs++;
    if (c->n < 16)
        c->lines[c->n++] = line;
    else
        free(line);
}

static void CollectFree(CollectCtx *c)
{
    int i;
    for (i = 0; i < c->n; i++)
        free(c->lines[i]);
    WS_Free(&c->all);
    ZeroMemory(c, sizeof(*c));
}

static int TestSplitter(HANDLE f)
{
    /* 源文件为 UTF-8，窄字符串字面量即 UTF-8 字节 */
    static const char input[] = "\xEF\xBB\xBF[信息] 中文行1\r\nSUCC:C:\\测试\\输出\n末行无换行";
    static const wchar_t *expect[] = { L"[信息] 中文行1", L"SUCC:C:\\测试\\输出", L"末行无换行" };
    static const size_t chunks[] = { 1, 2, 3, 5, 7, 4096 };
    size_t total = sizeof(input) - 1;
    int fails = 0;
    size_t ci;

    for (ci = 0; ci < sizeof(chunks) / sizeof(chunks[0]); ci++) {
        CollectCtx c;
        LineSplitter sp;
        size_t off = 0;
        BOOL ok;
        int k;

        ZeroMemory(&c, sizeof(c));
        Splitter_Init(&sp, CollectLine, &c);
        while (off < total) {
            size_t n = chunks[ci];
            if (n > total - off)
                n = total - off;
            Splitter_Feed(&sp, input + off, n);
            off += n;
        }
        Splitter_Finish(&sp);
        ok = (c.n == 3);
        for (k = 0; ok && k < 3; k++)
            ok = wcscmp(c.lines[k], expect[k]) == 0;
        LogFmt(f, L"[切分] %s 每块 %u 字节 → %d 行", ok ? L"通过" : L"失败", (unsigned)chunks[ci], c.n);
        if (!ok)
            fails++;
        CollectFree(&c);
    }

    /* 超长无换行：70002 字节的“中”，强制切分不得拆开多字节字符 */
    {
        const int count = 23334;
        size_t blen = (size_t)count * 3, off = 0;
        char *big = (char *)malloc(blen);
        CollectCtx c;
        LineSplitter sp;
        BOOL ok = FALSE;

        ZeroMemory(&c, sizeof(c));
        if (big) {
            int k;
            for (k = 0; k < count; k++)
                memcpy(big + (size_t)k * 3, "\xE4\xB8\xAD", 3); /* 中 */
            Splitter_Init(&sp, CollectLine, &c);
            while (off < blen) {
                size_t n = 4093;
                if (n > blen - off)
                    n = blen - off;
                Splitter_Feed(&sp, big + off, n);
                off += n;
            }
            Splitter_Finish(&sp);
            ok = !c.all.oom && c.all.len == (size_t)count && c.segs >= 2;
            for (k = 0; ok && k < count; k++)
                ok = c.all.p[k] == L'中';
            free(big);
        }
        LogFmt(f, L"[切分] %s 超长行强制切分：%d 段，共 %u 字符，无替换字符",
               ok ? L"通过" : L"失败", c.segs, (unsigned)c.all.len);
        if (!ok)
            fails++;
        CollectFree(&c);
    }
    return fails;
}

/* ---------- 真实调用 CLI ---------- */

typedef struct {
    HANDLE f;
    const wchar_t *tag;
    int nLines, nSucc, nSuccZh, nError, nBad;
} StCtx;

static void StLine(void *ctx, wchar_t *line)
{
    StCtx *c = (StCtx *)ctx;
    WStr s;
    wchar_t *out;

    c->nLines++;
    if (StartsWith(line, L"SUCC:")) {
        c->nSucc++;
        if (wcsstr(line, L"自测"))
            c->nSuccZh++;
    }
    if (IsErrorLine(line))
        c->nError++;
    if (wcschr(line, 0xFFFD))
        c->nBad++;

    ZeroMemory(&s, sizeof(s));
    WS_Add(&s, L"[");
    WS_Add(&s, c->tag);
    WS_Add(&s, L"] ");
    WS_Add(&s, line);
    out = WS_Detach(&s);
    if (out) {
        LogW(c->f, out);
        free(out);
    }
    free(line);
}

static BOOL RunCase(HANDLE f, const wchar_t *tag, const wchar_t *cli,
                    const wchar_t *const *args, int nargs, StCtx *c, RunResult *rr)
{
    wchar_t *cmd = BuildCommandLine(cli, args, nargs);
    wchar_t head[64];

    StringCchPrintfW(head, 64, L"[%s] 命令: ", tag);
    Log2(f, head, cmd ? cmd : L"(null)");
    free(cmd);

    ZeroMemory(c, sizeof(*c));
    c->f = f;
    c->tag = tag;
    if (!RunCli(cli, args, nargs, StLine, c, rr)) {
        LogFmt(f, L"[%s] 无法启动进程，Win32 错误 %lu", tag, rr->winErr);
        return FALSE;
    }
    LogFmt(f, L"[%s] 退出码=%lu 行数=%d", tag, rr->exitCode, c->nLines);
    return TRUE;
}

/* 默认源文件：exe 目录\..\..\测试源码\新增 文字文件.e */
static wchar_t *DefaultSrc(void)
{
    wchar_t *dir = ExeDir(), *rel, *full;
    if (!dir)
        return NULL;
    rel = WCat(dir, L"\\..\\..\\测试源码\\新增 文字文件.e", NULL);
    free(dir);
    full = FullPath(rel);
    free(rel);
    return full;
}

/* 默认输出：%TEMP%\opencode\gui 自测\输出 1（长文件名形式） */
static wchar_t *DefaultOut(void)
{
    wchar_t *tmp = (wchar_t *)malloc(PATH_BUF * sizeof(wchar_t));
    wchar_t *lng = (wchar_t *)malloc(PATH_BUF * sizeof(wchar_t));
    wchar_t *r = NULL;
    DWORD n;

    if (tmp && lng) {
        n = GetTempPathW(PATH_BUF, tmp);
        if (n > 0 && n < PATH_BUF) {
            DWORD m = GetLongPathNameW(tmp, lng, PATH_BUF);
            r = WCat((m > 0 && m < PATH_BUF) ? lng : tmp, L"opencode\\gui 自测\\输出 1", NULL);
        }
    }
    free(tmp);
    free(lng);
    return r;
}

int SelfTest(const wchar_t *cliArg, const wchar_t *logPath,
             const wchar_t *srcArg, const wchar_t *outArg)
{
    HANDLE f;
    int fails = 0, rc = 1;
    wchar_t *cli = NULL, *src = NULL, *out = NULL, *parent = NULL;
    wchar_t *srcB = NULL, *outB = NULL;
    StCtx ca, cb;
    RunResult ra, rb;

    f = CreateFileW(logPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 1;

    LogW(f, L"QAQ-ETool GUI 自检开始");
    fails += TestQuote(f);
    fails += TestSplitter(f);

    cli = FullPath(cliArg);
    if (!IsFile(cli)) {
        Log2(f, L"[致命] CLI 不存在: ", cliArg);
        goto done;
    }
    src = srcArg ? FullPath(srcArg) : DefaultSrc();
    out = outArg ? FullPath(outArg) : DefaultOut();
    parent = out ? ParentDir(out) : NULL;
    if (!src || !out || !parent) {
        LogW(f, L"[致命] 无法确定源文件/输出路径");
        goto done;
    }
    {
        int e = SHCreateDirectoryExW(NULL, parent, NULL);
        if (e != ERROR_SUCCESS && e != ERROR_ALREADY_EXISTS && e != ERROR_FILE_EXISTS)
            LogFmt(f, L"[警告] 创建输出父目录失败，错误 %d", e);
    }

    /* (a) 正常解包 */
    {
        const wchar_t *args[] = { L"-mode", L"e2t", L"-src", src, L"-dst", out };
        BOOL ok;
        if (!RunCase(f, L"a", cli, args, 6, &ca, &ra))
            goto done;
        ok = ca.nSucc > 0 && (ra.exitCode == 0 || ra.exitCode == 2) && ca.nBad == 0 && ca.nSuccZh > 0;
        LogFmt(f, L"[检查a] %s：SUCC 行=%d（含中文路径 %d），退出码=%lu，替换字符行=%d",
               ok ? L"通过" : L"失败", ca.nSucc, ca.nSuccZh, ra.exitCode, ca.nBad);
        if (!ok)
            fails++;
    }

    /* (b) 来源不存在 */
    srcB = WCat(parent, L"\\不存在的 文件.e", NULL);
    outB = WCat(parent, L"\\输出 2", NULL);
    if (!srcB || !outB) {
        LogW(f, L"[致命] 内存不足");
        goto done;
    }
    {
        const wchar_t *args[] = { L"-mode", L"e2t", L"-src", srcB, L"-dst", outB };
        BOOL ok;
        if (!RunCase(f, L"b", cli, args, 6, &cb, &rb))
            goto done;
        ok = cb.nError > 0 && rb.exitCode == 1 && cb.nBad == 0;
        LogFmt(f, L"[检查b] %s：ERROR 行=%d，退出码=%lu，替换字符行=%d",
               ok ? L"通过" : L"失败", cb.nError, rb.exitCode, cb.nBad);
        if (!ok)
            fails++;
    }

    rc = fails ? 3 : 0;
    LogFmt(f, L"RESULT: %s（失败项 %d）", fails ? L"FAIL" : L"PASS", fails);

done:
    if (rc == 1)
        LogW(f, L"RESULT: ABORT");
    CloseHandle(f);
    free(cli);
    free(src);
    free(out);
    free(parent);
    free(srcB);
    free(outB);
    return rc;
}
