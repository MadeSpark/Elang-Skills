/*
 * core.h —— QAQ-ETool GUI 与自检共用的核心代码
 * 包含：宽字符串工具、路径工具、Windows 命令行参数转义、
 *       UTF-8 按行切分（处理跨缓冲区截断）、子进程启动与管道收集。
 */
#ifndef QAQ_GUI_CORE_H
#define QAQ_GUI_CORE_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#define STRSAFE_NO_DEPRECATE

#include <windows.h>
#include <stddef.h>

/* 路径缓冲上限（宽字符数），与 Windows 长路径上限一致 */
#define PATH_BUF 32768

/* 单行无换行时的强制切分阈值（字节） */
#define SPLIT_MAX_LINE 65536

/* ---------- 动态宽字符串 ---------- */
typedef struct {
    wchar_t *p;
    size_t len;
    size_t cap;
    BOOL oom; /* 发生过内存不足 */
} WStr;

void WS_AddN(WStr *s, const wchar_t *t, size_t n);
void WS_Add(WStr *s, const wchar_t *t);
void WS_AddC(WStr *s, wchar_t c);
/* 取出结果（调用者 free）；内存不足时返回 NULL 并释放内部缓冲 */
wchar_t *WS_Detach(WStr *s);
void WS_Free(WStr *s);

/* ---------- 通用工具 ---------- */
wchar_t *WDup(const wchar_t *s);
/* 拼接 a+b+c（c 可为 NULL），调用者 free */
wchar_t *WCat(const wchar_t *a, const wchar_t *b, const wchar_t *c);
BOOL StartsWith(const wchar_t *s, const wchar_t *prefix);
/* 去掉首尾空白以及成对包裹的双引号（原地） */
void TrimPath(wchar_t *s);
BOOL IsFile(const wchar_t *p);
BOOL IsDir(const wchar_t *p);
/* 绝对路径（调用者 free），失败 NULL */
wchar_t *FullPath(const wchar_t *p);
/* 本 exe 所在目录（无结尾反斜杠，调用者 free） */
wchar_t *ExeDir(void);
/* 父目录（去掉最后一段；无分隔符返回 NULL，调用者 free） */
wchar_t *ParentDir(const wchar_t *p);
/* 错误行判定：以 "ERROR:" 或 "[错误]" 开头 */
BOOL IsErrorLine(const wchar_t *line);

/* ---------- 命令行转义 ---------- */
/* 按 CommandLineToArgvW / MSVCRT 规则把单个参数加引号并转义，追加到 out */
void AppendQuotedArg(WStr *out, const wchar_t *arg);
/* 单个参数转义后的新字符串（调用者 free） */
wchar_t *QuoteArg(const wchar_t *arg);
/* 生成完整命令行："app" "arg1" "arg2" ...（调用者 free） */
wchar_t *BuildCommandLine(const wchar_t *app, const wchar_t *const *args, int nargs);

/* ---------- UTF-8 按行切分 ---------- */
/* 行回调：line 为新分配的以 0 结尾的宽字符串，回调获得所有权，须 free() */
typedef void (*LineFn)(void *ctx, wchar_t *line);

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    LineFn fn;
    void *ctx;
    BOOL atStart; /* 尚未输出第一段（用于去 BOM） */
} LineSplitter;

void Splitter_Init(LineSplitter *s, LineFn fn, void *ctx);
/* 追加字节流；遇到 '\n' 输出一行（去掉结尾 '\r'） */
BOOL Splitter_Feed(LineSplitter *s, const char *data, size_t n);
/* 流结束：输出剩余内容并释放缓冲 */
void Splitter_Finish(LineSplitter *s);
/* 返回 b[0..len) 中以完整 UTF-8 字符结尾的最长前缀长度 */
size_t Utf8CompletePrefix(const unsigned char *b, size_t len);

/* ---------- 子进程 ---------- */
typedef struct {
    BOOL launched;   /* 是否成功启动 */
    DWORD exitCode;  /* 启动成功时的退出码 */
    DWORD winErr;    /* 启动失败时的 Win32 错误码 */
} RunResult;

void Runner_Init(void);
void Runner_Shutdown(void);
/* 请求终止当前子进程（只针对本程序启动的进程句柄） */
void Runner_RequestCancel(void);
void Runner_ResetCancel(void);

/*
 * 同步运行 app（参数 args），stdout+stderr 合并到同一管道，
 * 按行回调 fn。返回 TRUE 表示进程已启动并结束（结果见 rr）。
 */
BOOL RunCli(const wchar_t *app, const wchar_t *const *args, int nargs,
            LineFn fn, void *ctx, RunResult *rr);

#endif
