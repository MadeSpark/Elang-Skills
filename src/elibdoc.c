/*
 * elibdoc.c —— 易语言支持库文档导出器（单文件、免安装、无外部依赖）
 *
 * 做什么：
 *   1. 自动定位易语言安装目录（读注册表，多路回退 + 常见路径兜底）
 *   2. 扫描 <安装目录>\lib\*.fne，用 GetNewInf 读出每个支持库的命令表
 *   3. 把「命令名 / 参数名 / 参数类型 / 参数备注 / 命令备注」全部写成 AI 可读的文档
 *
 * 输出（**默认写到当前工作目录**）：
 *   索引.md              库清单、命令数统计、怎么用
 *   命令大全.md           全部命令（按库分节，一个文件看全）
 *   commands.jsonl       一行一条命令（机器可读）
 *   库/<Key>.md           按支持库分类的详细文档
 *
 * 编译（必须 32 位，支持库是 32 位 DLL）：
 *   export PATH="/c/msys64/mingw32/bin:$PATH"
 *   gcc -O2 -static -finput-charset=UTF-8 -fexec-charset=UTF-8 \
 *       -o 导出支持库文档.exe elibdoc.c -ladvapi32
 *
 * 用法：
 *   导出支持库文档.exe                    # 自动定位，导出到**当前目录**
 *   导出支持库文档.exe <输出目录>          # 位置参数：导出到指定目录
 *   导出支持库文档.exe -o <输出目录>       # 同上（等价写法）
 *   导出支持库文档.exe -l <lib目录>        # 手动指定支持库目录
 *   导出支持库文档.exe -q                 # 静默（不打印过程、不等待回车）
 *   导出支持库文档.exe --json             # 只把 commands.jsonl 打到标准输出，
 *                                         #   不写任何文件（供管道/检索）
 *
 * 设计取舍：不给输出目录时写「当前目录」而不是「exe 所在目录」，这样 AI/脚本
 * 可以先 cd 到自己的临时目录再调用，产物落在手边、用完即删，不必去猜 exe 在哪。
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

typedef unsigned int u32;

/* ==================================================================
 * 控制台输出（防乱码）
 *
 * 程序内部文本一律 UTF-8，但 Windows 控制台默认代码页是 936(GBK)：
 * 直接 printf 输出 UTF-8 字节，中文会显示成「瀹屾垚」这类乱码。
 * 所以：stdout/stderr 挂在控制台上时，转 UTF-16 走 WriteConsoleW
 *      （直接送 Unicode，与代码页无关）；被重定向到文件/管道时按
 *      UTF-8 原样写出（方便 AI/脚本读取）。
 * ================================================================== */
static void out_write(FILE *f, const char *s, size_t n)
{
    if (!n) return;
    HANDLE h = GetStdHandle(f == stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode;
    if (h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        int wn = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
        wchar_t *w = wn > 0 ? (wchar_t *)malloc((size_t)wn * sizeof(wchar_t)) : NULL;
        if (w) {
            DWORD off = 0, total;
            MultiByteToWideChar(CP_UTF8, 0, s, (int)n, w, wn);
            total = (DWORD)wn;
            while (off < total) {
                DWORD done = 0;
                if (!WriteConsoleW(h, w + off, total - off, &done, NULL) || !done) break;
                off += done;
            }
            free(w);
            if (off) return;          /* 已写出（哪怕部分），不再重复 */
        }
    }
    fwrite(s, 1, n, f);
}

/* 这个流是不是挂在控制台上？（被重定向到文件/管道时为假）
   用途：非交互调用（AI、脚本、管道）时不打印「按回车退出」也别 getchar。 */
static int is_console(FILE *f)
{
    HANDLE h = GetStdHandle(f == stderr ? STD_ERROR_HANDLE :
                            f == stdin  ? STD_INPUT_HANDLE : STD_OUTPUT_HANDLE);
    DWORD mode;
    return h && h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode) != 0;
}

/* 只有「人坐在终端前」才值得暂停等待；管道/重定向下 stdin 也多半不是控制台。
   注：stdin 被重定向时 getchar() 会立刻拿到 EOF，process 不会卡住，但会往管道里
   多吐一行提示 —— 对解析输出的调用方是噪声，所以这里直接不问。 */
static int interactive(void)
{
    return is_console(stdin) && is_console(stdout);
}

static void out_vprintf(FILE *f, const char *fmt, va_list ap)
{
    char sb[2048];
    char *b = sb;
    size_t cap = sizeof(sb);
    for (;;) {
        va_list a2;
        int r;
        va_copy(a2, ap);
        r = vsnprintf(b, cap, fmt, a2);
        va_end(a2);
        if (r >= 0 && (size_t)r < cap) break;          /* 装得下 */
        if (b != sb) free(b);
        cap = (r >= 0 ? (size_t)r : cap * 2) + 2;
        b = (char *)malloc(cap);
        if (!b) { b = sb; break; }                     /* 分配失败：用截断内容 */
    }
    out_write(f, b, strlen(b));
    if (b != sb) free(b);
}

static void op_printf(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    out_vprintf(stdout, fmt, ap);
    va_end(ap);
}

static void op_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    out_vprintf(f, fmt, ap);
    va_end(ap);
}

#undef  printf
#undef  fprintf
#define printf  op_printf
#define fprintf op_fprintf

/* ==================================================================
 * 动态缓冲
 * ================================================================== */
typedef struct {
    char  *p;
    size_t len, cap;
} Buf;

static void buf_reserve(Buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap : 4096;
    while (nc < b->len + extra + 1) nc *= 2;
    b->p = (char *)realloc(b->p, nc);
    b->cap = nc;
}

static void buf_add(Buf *b, const char *s, size_t n)
{
    if (!n) return;
    buf_reserve(b, n);
    memcpy(b->p + b->len, s, n);
    b->len += n;
    b->p[b->len] = 0;
}

static void buf_puts(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }

static void buf_printf(Buf *b, const char *fmt, ...)
{
    char t[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(t, sizeof(t) - 1, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)sizeof(t) - 1;
    buf_add(b, t, (size_t)n);
}

static void buf_free(Buf *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

/* ==================================================================
 * 指针校验 / 字符串转换
 * ================================================================== */
static int ptr_ok(const void *p)
{
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR a = (UINT_PTR)p;
    if (!p || a < 0x10000) return 0;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & PAGE_GUARD) return 0;
    if ((mbi.Protect & 0xFF) == PAGE_NOACCESS) return 0;
    return 1;
}
#define PTR(p) (ptr_ok((const void *)(p)) ? (const void *)(p) : NULL)

/* 支持库字符串可能是 GBK 或 UTF-16LE；统一转成 UTF-8（返回 malloc 的串） */
static char *to_utf8(const char *p)
{
    if (!ptr_ok(p)) return NULL;
    char *out = NULL;
    if (p[0] != 0 && p[1] == 0) {
        int n = (int)wcslen((const wchar_t *)p);
        int need = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t *)p, n, NULL, 0, NULL, NULL);
        out = (char *)malloc(need + 1);
        if (need > 0) WideCharToMultiByte(CP_UTF8, 0, (const wchar_t *)p, n, out, need, NULL, NULL);
        out[need > 0 ? need : 0] = 0;
    } else {
        int n = (int)strlen(p);
        int wneed = MultiByteToWideChar(CP_ACP, 0, p, n, NULL, 0);
        wchar_t *w = (wchar_t *)malloc((wneed + 1) * sizeof(wchar_t));
        if (wneed > 0) MultiByteToWideChar(CP_ACP, 0, p, n, w, wneed);
        w[wneed > 0 ? wneed : 0] = 0;
        int uneed = WideCharToMultiByte(CP_UTF8, 0, w, wneed, NULL, 0, NULL, NULL);
        out = (char *)malloc(uneed + 1);
        if (uneed > 0) WideCharToMultiByte(CP_UTF8, 0, w, wneed, out, uneed, NULL, NULL);
        out[uneed > 0 ? uneed : 0] = 0;
        free(w);
    }
    return out;
}

/* UTF-8 → ANSI(ACP)：供只接受 ANSI 的 Win32 文件 API（fopen/CreateDirectoryA/…）使用。
   本程序内部一律用 UTF-8 表示路径与文本；只有调用这些 API 的瞬间才转成 ACP。 */
static void u8_to_acp(const char *u8, char *out, size_t n)
{
    int wn = MultiByteToWideChar(CP_UTF8, 0, u8, -1, NULL, 0);
    if (wn <= 0) { snprintf(out, n, "%s", u8); return; }
    wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    if (!w) { snprintf(out, n, "%s", u8); return; }
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, w, wn);
    WideCharToMultiByte(CP_ACP, 0, w, -1, out, (int)n, NULL, NULL);
    out[n - 1] = 0;
    free(w);
}

/* ANSI(ACP) → UTF-8（返回 malloc 的串） */
static char *acp_to_u8(const char *s)
{
    int wn = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
    if (wn <= 0) return _strdup(s);
    wchar_t *w = (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, s, -1, w, wn);
    int un = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *out = (char *)malloc((size_t)(un > 0 ? un : 1));
    if (un > 0) WideCharToMultiByte(CP_UTF8, 0, w, -1, out, un, NULL, NULL);
    else out[0] = 0;
    free(w);
    return out;
}

static const char *file_stem(const char *path)
{
    const char *b = path, *s = path;
    static char stem[512];
    for (; *s; s++) if (*s == '\\' || *s == '/') b = s + 1;
    snprintf(stem, sizeof(stem), "%s", b);
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    return stem;
}

/* ==================================================================
 * 文件系统
 * ================================================================== */
static int ensure_dir(const char *path)
{
    char a[MAX_PATH * 2];
    u8_to_acp(path, a, sizeof(a));
    char t[MAX_PATH * 2];
    snprintf(t, sizeof(t), "%s", a);
    size_t n = strlen(t);
    for (size_t i = 0; i < n; i++) {
        if ((t[i] == '\\' || t[i] == '/') && i > 2) {
            char c = t[i];
            t[i] = 0;
            CreateDirectoryA(t, NULL);
            t[i] = c;
        }
    }
    CreateDirectoryA(t, NULL);
    return GetFileAttributesA(a) != INVALID_FILE_ATTRIBUTES;
}

static int write_file(const char *path, const void *data, size_t len)
{
    char a[MAX_PATH * 2];
    u8_to_acp(path, a, sizeof(a));
    FILE *f = fopen(a, "wb");
    if (!f) return -1;
    if (len) fwrite(data, 1, len, f);
    fclose(f);
    return 0;
}

/* ==================================================================
 * 注册表定位易语言
 * ================================================================== */
static int reg_str(HKEY root, const char *sub, const char *name, char *out, DWORD outlen)
{
    HKEY k;
    if (RegOpenKeyExA(root, sub, 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    DWORD type = 0, n = outlen;
    LONG r = RegQueryValueExA(k, name, NULL, &type, (LPBYTE)out, &n);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS) return 0;
    out[outlen - 1] = 0;
    return 1;
}

/* 从 `"C:\path\e.exe" "%1"` 里取出 exe 路径（调用方 free） */
static char *exe_from_command(const char *cmd)
{
    if (!cmd) return NULL;
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    char *out = (char *)malloc(strlen(cmd) + 1);
    if (*cmd == '"') {
        cmd++;
        const char *e = strchr(cmd, '"');
        size_t n = e ? (size_t)(e - cmd) : strlen(cmd);
        memcpy(out, cmd, n);
        out[n] = 0;
    } else {
        const char *e = cmd;
        while (*e && *e != ' ' && *e != '\t') e++;
        size_t n = (size_t)(e - cmd);
        memcpy(out, cmd, n);
        out[n] = 0;
    }
    return out;
}

/* 由 ProgID 找可执行文件 */
static char *exe_from_progid(const char *progid)
{
    if (!progid || !*progid) return NULL;
    char pid[512];
    snprintf(pid, sizeof(pid), "%s", progid);
    /* 去掉首尾引号/空白 */
    char *s = pid;
    while (*s == ' ' || *s == '"') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '"')) s[--n] = 0;

    HKEY roots[2] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};

    if (_strnicmp(s, "Applications\\", 13) == 0) {
        const char *exe = s + 13;
        for (int i = 0; i < 2; i++) {
            char sub[768], cmd[1024];
            snprintf(sub, sizeof(sub),
                     "Software\\Classes\\Applications\\%s\\shell\\open\\command", exe);
            if (reg_str(roots[i], sub, NULL, cmd, sizeof(cmd))) {
                char *p = exe_from_command(cmd);
                if (p) return p;
            }
        }
        return NULL;
    }
    for (int i = 0; i < 2; i++) {
        char sub[768], cmd[1024];
        snprintf(sub, sizeof(sub), "Software\\Classes\\%s\\shell\\open\\command", s);
        if (reg_str(roots[i], sub, NULL, cmd, sizeof(cmd))) {
            char *p = exe_from_command(cmd);
            if (p) return p;
        }
    }
    return NULL;
}

/* 返回 1 表示定位到（libdir 写出） */
static int find_libdir(char *libdir, size_t ln, char *note, size_t nn)
{
    HKEY roots[2] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
    const char *rname[2] = {"HKCU", "HKLM"};
    char progid[512];
    char *exe = NULL;
    char src[256] = "";

    /* 1) UserChoice：用户实际选定的 .e 打开方式 */
    if (!exe &&
        reg_str(HKEY_CURRENT_USER,
                "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.e\\UserChoice",
                "ProgId", progid, sizeof(progid))) {
        exe = exe_from_progid(progid);
        if (exe) snprintf(src, sizeof(src), "HKCU\\...\\FileExts\\.e\\UserChoice = %s", progid);
    }
    /* 2) .e 的默认 ProgID */
    if (!exe) {
        for (int i = 0; i < 2 && !exe; i++) {
            if (reg_str(roots[i], "Software\\Classes\\.e", NULL, progid, sizeof(progid))) {
                exe = exe_from_progid(progid);
                if (exe) snprintf(src, sizeof(src), "%s\\Software\\Classes\\.e = %s", rname[i], progid);
            }
        }
    }
    /* 3) e_auto_file */
    if (!exe) {
        for (int i = 0; i < 2 && !exe; i++) {
            char cmd[1024];
            if (reg_str(roots[i], "Software\\Classes\\e_auto_file\\shell\\open\\command", NULL,
                        cmd, sizeof(cmd))) {
                exe = exe_from_command(cmd);
                if (exe) snprintf(src, sizeof(src), "%s\\Software\\Classes\\e_auto_file", rname[i]);
            }
        }
    }
    /* 4) App Paths */
    if (!exe) {
        for (int i = 0; i < 2 && !exe; i++) {
            char cmd[1024];
            if (reg_str(roots[i],
                        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\e.exe",
                        NULL, cmd, sizeof(cmd))) {
                exe = exe_from_command(cmd);
                if (exe) snprintf(src, sizeof(src), "%s\\...\\App Paths\\e.exe", rname[i]);
            }
        }
    }

    if (exe) {
        char *exe_u8 = acp_to_u8(exe);
        char inst[MAX_PATH];
        snprintf(inst, sizeof(inst), "%s", exe_u8);
        char *slash = strrchr(inst, '\\');
        if (slash) *slash = 0;
        char inst_a[MAX_PATH], lib_a[MAX_PATH];
        u8_to_acp(inst, inst_a, sizeof(inst_a));
        if (GetFileAttributesA(inst_a) != INVALID_FILE_ATTRIBUTES) {
            snprintf(libdir, ln, "%s\\lib", inst);
            u8_to_acp(libdir, lib_a, sizeof(lib_a));
            if (GetFileAttributesA(lib_a) != INVALID_FILE_ATTRIBUTES) {
                snprintf(note, nn, ".e 关联 → %s（来源: %s）", exe_u8, src);
                free(exe_u8);
                free(exe);
                return 1;
            }
            snprintf(note, nn, ".e 关联 → %s，但旁边没有 lib 目录", exe_u8);
        } else {
            snprintf(note, nn, ".e 关联指向的 %s 不存在（来源: %s）", exe_u8, src);
        }
        free(exe_u8);
        free(exe);
    }

    /* 5) 易语言自己的注册表项 */
    {
        char p[MAX_PATH];
        for (int i = 0; i < 2; i++) {
            if (reg_str(roots[i], "Software\\FlySky\\E\\Install", "Path", p, sizeof(p))) {
                char *p_u8 = acp_to_u8(p);
                char p_a[MAX_PATH], lib_a[MAX_PATH];
                u8_to_acp(p_u8, p_a, sizeof(p_a));
                if (GetFileAttributesA(p_a) != INVALID_FILE_ATTRIBUTES) {
                    snprintf(libdir, ln, "%s\\lib", p_u8);
                    u8_to_acp(libdir, lib_a, sizeof(lib_a));
                    if (GetFileAttributesA(lib_a) != INVALID_FILE_ATTRIBUTES) {
                        snprintf(note, nn, "注册表 %s\\Software\\FlySky\\E\\Install\\Path → %s",
                                 rname[i], p_u8);
                        free(p_u8);
                        return 1;
                    }
                }
                free(p_u8);
            }
        }
    }

    /* 6) 常见路径兜底 */
    {
        const char *drives[] = {"C:", "D:", "E:", "F:", "G:"};
        const char *subs[] = {"\\ides\\e", "\\易语言", "\\E语言", "\\e",
                              "\\Program Files\\易语言", "\\Program Files (x86)\\易语言"};
        for (int d = 0; d < 5; d++) {
            for (int s = 0; s < 6; s++) {
                char inst[MAX_PATH], c[MAX_PATH], inst_a[MAX_PATH], c_a[MAX_PATH];
                snprintf(inst, sizeof(inst), "%s%s", drives[d], subs[s]);
                snprintf(c, sizeof(c), "%s\\e.exe", inst);
                u8_to_acp(c, c_a, sizeof(c_a));
                if (GetFileAttributesA(c_a) != INVALID_FILE_ATTRIBUTES) {
                    snprintf(libdir, ln, "%s\\lib", inst);
                    snprintf(note, nn, "兜底扫描命中: %s", inst);
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* ==================================================================
 * 支持库结构
 * ================================================================== */
typedef u32 LIBINFO_W[36];

#define LI_GUID        1
#define LI_MAJOR       2
#define LI_MINOR       3
#define LI_NAME        9
#define LI_EXPLAIN     11
#define LI_AUTHOR      13
#define LI_HOMEPAGE    19
#define LI_DTCOUNT     21
#define LI_DT          22
#define LI_CMDCOUNT    25
#define LI_CMD         26
#define LI_CONSTCOUNT  33
#define LI_CONST       34

#define CI_NAME        0
#define CI_EGNAME      1
#define CI_EXPLAIN     2
#define CI_CATSTATE    3
#define CI_RETTYPE     4
#define CI_RESLEVEL    5
#define CI_ARGCOUNT    7
#define CI_ARGS        8

#define AI_NAME        0
#define AI_EXPLAIN     1
#define AI_TYPE        3
#define AI_DEFAULT     4
#define AI_STATE       5

#define DT_NAME        0
#define DT_EXPLAIN     2

#define LC_NAME        0
#define LC_EXPLAIN     2
#define LC_TEXT        4

typedef struct {
    char *name, *explain, *type;
    u32   state;
    int   def, typeid;
} Arg;

typedef struct {
    char *name, *eg, *explain, *ret;
    int   cat, userlevel;
    u32   state, rettype;
    int   argc;
    Arg  *args;
} Cmd;

typedef struct {
    char *name, *explain, *text;
} Const;

typedef struct {
    char  *file, *key, *name, *explain, *author, *homepage, *guid, *version;
    int    major, minor;
    int    ncmd, ntype, nconst;
    Cmd   *cmds;
    char **typenames;
    char **typeexplains;
    Const *consts;
    int    ok;
    char   err[256];
} Lib;

/* ---------- 类型名 / 标志解码 ---------- */
static const char *sys_type_name(int low)
{
    int b = low & 0xFF, c = (low >> 8) & 0xFF;
    if (b == 0 && c == 0) return "通用型";
    if (b == 1) {
        switch (c) {
        case 1: return "字节型";
        case 2: return "短整数型";
        case 3: return "整数型";
        case 4: return "长整数型";
        case 5: return "小数型";
        case 6: return "双精度小数型";
        }
        return "数值型";
    }
    if (b == 2) return "逻辑型";
    if (b == 3) return "日期时间型";
    if (b == 4) return "文本型";
    if (b == 5) return "字节集型";
    if (b == 6) return "子程序指针型";
    if (b == 8) return "子语句型";
    return NULL;
}

/* 把 DATA_TYPE 解成易语言类型名。返回静态或库内字符串 */
static const char *type_name(u32 dt, const Lib *L)
{
    if (dt == 0) return "空";
    int ary = (dt & 0x20000000u) != 0;
    int low = (int)(dt & 0xFFFF);
    int hi = (int)((dt >> 16) & 0xFFFF);
    static char buf[128];
    const char *base = NULL;

    if (hi == 0x8000) {
        base = sys_type_name(low);
        if (!base) {
            snprintf(buf, sizeof(buf), "系统类型(0x%04X)", low);
            base = buf;
        }
    } else if (dt & 0x40000000u) {
        base = "自定义类型";
    } else {
        int idx = low;
        if (idx >= 0 && idx < L->ntype && L->typenames && L->typenames[idx] && L->typenames[idx][0])
            base = L->typenames[idx];
        else {
            snprintf(buf, sizeof(buf), "库类型#%d", idx);
            base = buf;
        }
    }
    if (ary) {
        static char abuf[160];
        snprintf(abuf, sizeof(abuf), "数组 %s", base);
        return abuf;
    }
    return base;
}

/* 参数标志 → 中文后缀 */
static void arg_flags_text(u32 st, char *out, size_t n)
{
    out[0] = 0;
    const char *parts[6];
    int k = 0;
    if (st & (1 << 0)) parts[k++] = "有默认值";
    if (st & (1 << 1)) parts[k++] = "可空";
    if (st & (1 << 2)) parts[k++] = "参考";
    if (st & (1 << 5)) parts[k++] = "数组";
    if (st & (1 << 6)) parts[k++] = "任意类型";
    if (st & (1 << 9)) parts[k++] = "变量或立即数";
    size_t pos = 0;
    for (int i = 0; i < k; i++) {
        int w = snprintf(out + pos, n - pos, "%s%s", i ? "、" : "", parts[i]);
        if (w < 0 || (size_t)w >= n - pos) break;
        pos += (size_t)w;
    }
}

/* 是否可作为普通命令列出（排除「隐含」「已失效」） */
static int cmd_usable(u32 state) { return !(state & (1u << 2) || state & (1u << 3)); }

/* ==================================================================
 * 读取单个支持库
 * ================================================================== */
static jmp_buf g_jmp;
static volatile int g_running = 0;

static LONG CALLBACK veh_handler(EXCEPTION_POINTERS *ep)
{
    (void)ep;
    if (g_running) {
        g_running = 0;
        longjmp(g_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static const u32 *g_li = NULL;
static Lib *g_L = NULL;

static void parse_lib_body(const u32 *li, Lib *L)
{
    L->guid = to_utf8((const char *)li[LI_GUID]);
    L->major = (int)li[LI_MAJOR];
    L->minor = (int)li[LI_MINOR];
    L->name = to_utf8((const char *)li[LI_NAME]);
    L->explain = to_utf8((const char *)li[LI_EXPLAIN]);
    L->author = to_utf8((const char *)li[LI_AUTHOR]);
    L->homepage = to_utf8((const char *)li[LI_HOMEPAGE]);

    /* --- 库自定义数据类型（参数类型解析要用它） --- */
    u32 ndt = li[LI_DTCOUNT];
    const u32 *dtp = (const u32 *)PTR(li[LI_DT]);
    if ((int)ndt < 0 || ndt > 100000) ndt = 0;
    L->ntype = (int)ndt;
    if (ndt) {
        L->typenames = (char **)calloc(ndt, sizeof(char *));
        L->typeexplains = (char **)calloc(ndt, sizeof(char *));
        for (u32 j = 0; j < ndt && dtp; j++) {
            const u32 *d = dtp + j * 14;
            if (!ptr_ok(d)) break;
            L->typenames[j] = to_utf8((const char *)d[DT_NAME]);
            L->typeexplains[j] = to_utf8((const char *)d[DT_EXPLAIN]);
        }
    }

    /* --- 命令表 --- */
    u32 nc = li[LI_CMDCOUNT];
    const u32 *cp = (const u32 *)PTR(li[LI_CMD]);
    if ((int)nc < 0 || nc > 1000000) nc = 0;
    L->ncmd = (int)nc;
    if (nc) {
        L->cmds = (Cmd *)calloc(nc, sizeof(Cmd));
        for (u32 i = 0; i < nc; i++) {
            const u32 *c = cp + i * 9;
            Cmd *C = &L->cmds[i];
            if (!ptr_ok(c)) continue;
            C->name = to_utf8((const char *)c[CI_NAME]);
            C->eg = to_utf8((const char *)c[CI_EGNAME]);
            C->explain = to_utf8((const char *)c[CI_EXPLAIN]);
            C->cat = (short)(c[CI_CATSTATE] & 0xFFFF);
            C->state = (c[CI_CATSTATE] >> 16) & 0xFFFF;
            C->rettype = c[CI_RETTYPE];
            C->userlevel = (short)((c[CI_RESLEVEL] >> 16) & 0xFFFF);
            int ac = (int)c[CI_ARGCOUNT];
            if (ac < 0 || ac > 128) ac = 0;
            C->argc = ac;
            const u32 *ap = (const u32 *)PTR(c[CI_ARGS]);
            if (ac) {
                C->args = (Arg *)calloc(ac, sizeof(Arg));
                for (int k = 0; k < ac; k++) {
                    const u32 *g = ap + k * 6;
                    Arg *A = &C->args[k];
                    if (!ptr_ok(g)) continue;
                    A->name = to_utf8((const char *)g[AI_NAME]);
                    A->explain = to_utf8((const char *)g[AI_EXPLAIN]);
                    A->typeid = (int)g[AI_TYPE];
                    A->def = (int)g[AI_DEFAULT];
                    A->state = g[AI_STATE];
                }
            }
        }
    }

    /* --- 常量表 --- */
    u32 nk = li[LI_CONSTCOUNT];
    const u32 *ks = (const u32 *)PTR(li[LI_CONST]);
    if ((int)nk < 0 || nk > 1000000) nk = 0;
    L->nconst = (int)nk;
    if (nk) {
        L->consts = (Const *)calloc(nk, sizeof(Const));
        for (u32 i = 0; i < nk && ks; i++) {
            const u32 *k = ks + i * 8;
            if (!ptr_ok(k)) continue;
            L->consts[i].name = to_utf8((const char *)k[LC_NAME]);
            L->consts[i].explain = to_utf8((const char *)k[LC_EXPLAIN]);
            L->consts[i].text = to_utf8((const char *)k[LC_TEXT]);
        }
    }
}

static int load_lib(const char *path, Lib *L)
{
    memset(L, 0, sizeof(*L));
    L->file = _strdup(path);
    L->key = _strdup(file_stem(path));

    char path_a[MAX_PATH * 2];
    u8_to_acp(path, path_a, sizeof(path_a));
    HMODULE h = LoadLibraryExA(path_a, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) {
        snprintf(L->err, sizeof(L->err), "LoadLibrary 失败 (GetLastError=%lu)",
                 (unsigned long)GetLastError());
        return -1;
    }
    typedef const u32 *(WINAPI *GETNEWINF)(void);
    GETNEWINF f = (GETNEWINF)GetProcAddress(h, "GetNewInf");
    if (!f) {
        snprintf(L->err, sizeof(L->err), "没有导出 GetNewInf（不是易语言支持库？）");
        return -1;
    }

    g_li = NULL;
    g_L = L;
    if (setjmp(g_jmp) == 0) {
        g_running = 1;
        const u32 *li = f();
        if (ptr_ok(li)) {
            parse_lib_body(li, L);
            L->ok = 1;
        } else {
            snprintf(L->err, sizeof(L->err), "GetNewInf 返回了非法指针");
        }
        g_running = 0;
    } else {
        g_running = 0;
        L->ok = 0;
        snprintf(L->err, sizeof(L->err), "读取库信息时发生异常(访问违例)");
        return -1;
    }
    /* 注意：不 FreeLibrary，部分库在卸载时会崩 */
    return L->ok ? 0 : -1;
}

/* ==================================================================
 * 文档生成
 * ================================================================== */

/* 命令是否要写进文档 */
static int keep_cmd(const Cmd *c) { return c->name && c->name[0] && cmd_usable(c->state); }

/* 写进 Markdown 表格单元格：换行 / 竖线必须转义，否则表格会被撑破 */
static void md_cell(Buf *b, const char *s)
{
    if (!s) return;
    for (const char *p = s; *p; p++) {
        if (*p == '\r') continue;
        if (*p == '\n') { buf_puts(b, "<br>"); continue; }
        if (*p == '|') { buf_puts(b, "\\|"); continue; }
        buf_add(b, p, 1);
    }
}

static void md_cmd(Buf *b, const Cmd *c, const Lib *L, int level)
{
    const char *h = level == 3 ? "###" : "####";
    buf_printf(b, "%s %s\n\n", h, c->name);
    if (c->eg && c->eg[0]) buf_printf(b, "> 英文名 `%s`", c->eg);
    else buf_puts(b, ">");
    if (c->rettype) {
        const char *rt = type_name(c->rettype, L);
        if (rt && strcmp(rt, "空") != 0) buf_printf(b, " ｜ 返回值 **%s**", rt);
    }
    buf_puts(b, "\n\n");
    if (c->explain && c->explain[0]) {
        buf_puts(b, c->explain);
        buf_puts(b, "\n\n");
    }
    if (c->argc > 0) {
        buf_puts(b, "| # | 参数名 | 类型 | 修饰 | 说明 |\n|---|---|---|---|---|\n");
        for (int i = 0; i < c->argc; i++) {
            const Arg *a = &c->args[i];
            char fl[128];
            arg_flags_text(a->state, fl, sizeof(fl));
            buf_printf(b, "| %d | `", i + 1);
            md_cell(b, a->name ? a->name : "(未命名)");
            buf_puts(b, "` | ");
            md_cell(b, type_name((u32)a->typeid, L));
            buf_printf(b, " | %s | ", fl[0] ? fl : "-");
            md_cell(b, a->explain);
            buf_puts(b, " |\n");
        }
        buf_puts(b, "\n");
    }
}

static void gen_lib_md(const Lib *L, const char *outdir)
{
    Buf b; memset(&b, 0, sizeof(b));
    buf_printf(&b, "# %s\n\n", L->name ? L->name : L->key);
    buf_printf(&b, "- **库标识**: `%s`\n", L->key);
    if (L->version) buf_printf(&b, "- **版本**: %s\n", L->version);
    if (L->guid && L->guid[0]) buf_printf(&b, "- **GUID**: `%s`\n", L->guid);
    if (L->author && L->author[0]) buf_printf(&b, "- **作者**: %s\n", L->author);
    if (L->homepage && L->homepage[0]) buf_printf(&b, "- **主页**: %s\n", L->homepage);
    buf_printf(&b, "- **文件**: `%s`\n", file_stem(L->file));
    buf_printf(&b, "- **可用命令数**: %d\n", L->ncmd);
    if (L->explain && L->explain[0]) buf_printf(&b, "\n%s\n", L->explain);

    /* 类型表 */
    if (L->ntype > 0) {
        int shown = 0;
        for (int i = 0; i < L->ntype; i++)
            if (L->typenames && L->typenames[i] && L->typenames[i][0]) shown++;
        if (shown) {
            buf_puts(&b, "\n## 数据类型\n\n| 名称 | 说明 |\n|---|---|\n");
            for (int i = 0; i < L->ntype; i++) {
                if (!L->typenames || !L->typenames[i] || !L->typenames[i][0]) continue;
                buf_puts(&b, "| `");
                md_cell(&b, L->typenames[i]);
                buf_puts(&b, "` | ");
                md_cell(&b, L->typeexplains && L->typeexplains[i] ? L->typeexplains[i] : "");
                buf_puts(&b, " |\n");
            }
        }
    }

    buf_puts(&b, "\n## 命令\n\n");
    for (int i = 0; i < L->ncmd; i++) {
        if (!keep_cmd(&L->cmds[i])) continue;
        md_cmd(&b, &L->cmds[i], L, 3);
    }

    if (L->nconst > 0) {
        int shown = 0;
        for (int i = 0; i < L->nconst; i++)
            if (L->consts[i].name && L->consts[i].name[0]) shown++;
        if (shown) {
            buf_puts(&b, "\n## 常量\n\n| 名称 | 值 | 说明 |\n|---|---|---|\n");
            for (int i = 0; i < L->nconst; i++) {
                const Const *k = &L->consts[i];
                if (!k->name || !k->name[0]) continue;
                buf_puts(&b, "| `");
                md_cell(&b, k->name);
                buf_puts(&b, "` | ");
                md_cell(&b, k->text);
                buf_puts(&b, " | ");
                md_cell(&b, k->explain);
                buf_puts(&b, " |\n");
            }
        }
    }

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\库", outdir);
    ensure_dir(path);
    snprintf(path, sizeof(path), "%s\\库\\%s.md", outdir, L->key);
    write_file(path, b.p ? b.p : "", b.len);
    buf_free(&b);
}

static void gen_all_md(const Lib *libs, int n, const char *outdir)
{
    Buf b; memset(&b, 0, sizeof(b));
    int total = 0;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < libs[i].ncmd; j++)
            if (keep_cmd(&libs[i].cmds[j])) total++;

    buf_printf(&b, "# 易语言支持库命令大全\n\n");
    buf_printf(&b, "共 **%d** 个支持库、**%d** 条可用命令。\n\n", n, total);
    buf_puts(&b, "## 目录\n\n");
    for (int i = 0; i < n; i++) {
        if (!libs[i].ok) continue;
        buf_printf(&b, "- [%s](#%s)\n", libs[i].name ? libs[i].name : libs[i].key, libs[i].key);
    }
    buf_puts(&b, "\n---\n\n");

    for (int i = 0; i < n; i++) {
        const Lib *L = &libs[i];
        if (!L->ok) continue;
        buf_printf(&b, "<a id=\"%s\"></a>\n## %s\n\n", L->key,
                   L->name ? L->name : L->key);
        if (L->version) buf_printf(&b, "`%s` ｜ 版本 %s ｜ 命令 %d 条\n\n", L->key, L->version, L->ncmd);
        if (L->explain && L->explain[0]) buf_printf(&b, "%s\n\n", L->explain);
        for (int j = 0; j < L->ncmd; j++) {
            if (!keep_cmd(&L->cmds[j])) continue;
            md_cmd(&b, &L->cmds[j], L, 4);
        }
        buf_puts(&b, "---\n\n");
    }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\命令大全.md", outdir);
    write_file(path, b.p ? b.p : "", b.len);
    buf_free(&b);
}

static void json_escape(Buf *b, const char *s)
{
    if (!s) { buf_puts(b, "null"); return; }
    buf_puts(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\n': buf_puts(b, "\\n"); break;
        case '\r': buf_puts(b, "\\r"); break;
        case '\t': buf_puts(b, "\\t"); break;
        default:
            if (*p < 0x20) buf_printf(b, "\\u%04x", *p);
            else buf_add(b, (const char *)p, 1);
        }
    }
    buf_puts(b, "\"");
}

static void build_jsonl(Buf *b, const Lib *libs, int n)
{
    for (int i = 0; i < n; i++) {
        const Lib *L = &libs[i];
        if (!L->ok) continue;
        for (int j = 0; j < L->ncmd; j++) {
            const Cmd *c = &L->cmds[j];
            if (!keep_cmd(c)) continue;
            buf_puts(b, "{\"lib\":");
            json_escape(b, L->key);
            buf_puts(b, ",\"libName\":");
            json_escape(b, L->name);
            buf_puts(b, ",\"name\":");
            json_escape(b, c->name);
            buf_puts(b, ",\"egName\":");
            json_escape(b, c->eg);
            buf_puts(b, ",\"explain\":");
            json_escape(b, c->explain);
            buf_puts(b, ",\"retType\":");
            json_escape(b, c->rettype ? type_name(c->rettype, L) : "");
            buf_puts(b, ",\"args\":[");
            for (int k = 0; k < c->argc; k++) {
                const Arg *a = &c->args[k];
                char fl[128];
                arg_flags_text(a->state, fl, sizeof(fl));
                if (k) buf_puts(b, ",");
                buf_puts(b, "{\"name\":");
                json_escape(b, a->name);
                buf_puts(b, ",\"type\":");
                json_escape(b, type_name((u32)a->typeid, L));
                buf_puts(b, ",\"flags\":");
                json_escape(b, fl);
                buf_puts(b, ",\"explain\":");
                json_escape(b, a->explain);
                buf_puts(b, "}");
            }
            buf_puts(b, "]}\n");
        }
    }
}

static void gen_jsonl(const Lib *libs, int n, const char *outdir)
{
    Buf b; memset(&b, 0, sizeof(b));
    build_jsonl(&b, libs, n);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\commands.jsonl", outdir);
    write_file(path, b.p ? b.p : "", b.len);
    buf_free(&b);
}

static void gen_index(const Lib *libs, int n, const char *outdir,
                      const char *libdir, const char *how)
{
    Buf b; memset(&b, 0, sizeof(b));
    int total = 0, okn = 0, badn = 0;
    for (int i = 0; i < n; i++) {
        if (!libs[i].ok) { badn++; continue; }
        okn++;
        for (int j = 0; j < libs[i].ncmd; j++)
            if (keep_cmd(&libs[i].cmds[j])) total++;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    buf_puts(&b, "# 易语言支持库命令索引\n\n");
    buf_printf(&b, "- 支持库目录：`%s`\n", libdir);
    buf_printf(&b, "- 定位方式：%s\n", how);
    buf_printf(&b, "- 导出时间：%04d-%02d-%02d %02d:%02d\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    buf_printf(&b, "- 统计：%d 个支持库（成功 %d，失败 %d），可用命令 **%d** 条\n\n",
               n, okn, badn, total);

    buf_puts(&b, "## 怎么用\n\n");
    buf_puts(&b, "| 想找什么 | 去哪看 |\n|---|---|\n");
    buf_puts(&b, "| 某个库的全部命令 | `库/<库标识>.md` |\n");
    buf_puts(&b, "| 所有命令（一个文件看全） | `命令大全.md` |\n");
    buf_puts(&b, "| 程序化检索 | `commands.jsonl`（一行一条命令） |\n\n");

    buf_puts(&b, "## 支持库列表\n\n");
    buf_puts(&b, "| 库标识 | 名称 | 版本 | 命令数 | 说明 |\n|---|---|---|---|---|\n");
    for (int i = 0; i < n; i++) {
        const Lib *L = &libs[i];
        if (!L->ok) {
            buf_printf(&b, "| `%s` | *(读取失败)* | - | - | %s |\n", L->key, L->err);
            continue;
        }
        int cnt = 0;
        for (int j = 0; j < L->ncmd; j++) if (keep_cmd(&L->cmds[j])) cnt++;
        buf_printf(&b, "| [`%s`](库/%s.md) | ", L->key, L->key);
        md_cell(&b, L->name);
        buf_printf(&b, " | %s | %d | ", L->version ? L->version : "", cnt);
        md_cell(&b, L->explain);
        buf_puts(&b, " |\n");
    }

    if (badn) {
        buf_puts(&b, "\n## 读取失败的支持库\n\n");
        for (int i = 0; i < n; i++)
            if (!libs[i].ok)
                buf_printf(&b, "- `%s` — %s\n", libs[i].key, libs[i].err);
    }

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\索引.md", outdir);
    write_file(path, b.p ? b.p : "", b.len);
    buf_free(&b);
}

/* ==================================================================
 * main
 * ================================================================== */
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    out_vprintf(stderr, fmt, ap);
    va_end(ap);
    out_write(stderr, "\n", 1);
}

int main(int argc, char **argv)
{
    char outdir[MAX_PATH] = "";
    char libdir_arg[MAX_PATH] = "";
    int quiet = 0, pause = 1, json_only = 0, outdir_set = 0;

    for (int i = 1; i < argc; i++) {
        char *a = acp_to_u8(argv[i]);          /* 命令行参数是 ANSI，统一转 UTF-8 */
        if (!strcmp(a, "-o") || !strcmp(a, "--out")) {
            if (i + 1 >= argc) { die("选项 %s 后面缺少目录参数", a); free(a); return 2; }
            char *v = acp_to_u8(argv[++i]);
            snprintf(outdir, sizeof(outdir), "%s", v);
            outdir_set = 1;
            free(v);
        } else if (!strcmp(a, "-l") || !strcmp(a, "--lib")) {
            if (i + 1 >= argc) { die("选项 %s 后面缺少目录参数", a); free(a); return 2; }
            char *v = acp_to_u8(argv[++i]);
            snprintf(libdir_arg, sizeof(libdir_arg), "%s", v);
            free(v);
        } else if (!strcmp(a, "-q") || !strcmp(a, "--quiet")) { quiet = 1; pause = 0; }
        else if (!strcmp(a, "--json")) { json_only = 1; quiet = 1; pause = 0; }
        else if (!strcmp(a, "--no-pause")) pause = 0;
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            char *a0 = acp_to_u8(argv[0]);          /* argv 是 ANSI，转 UTF-8 再打印 */
            printf("用法: %s [输出目录] [选项]\n\n", a0);
            printf("  导出支持库文档.exe                    文档写到【当前目录】\n");
            printf("  导出支持库文档.exe D:\\out            文档写到 D:\\out\n");
            printf("  导出支持库文档.exe -o D:\\out         同上（等价写法）\n\n");
            printf("  -l, --lib <目录>    手动指定支持库目录（默认自动定位易语言安装目录）\n");
            printf("  -q, --quiet         不打印过程，也不等待回车\n");
            printf("      --json          只把 commands.jsonl 写到标准输出，不落盘\n");
            printf("      --no-pause      打印过程，但跑完不等待回车\n");
            printf("  -h, --help          显示本帮助\n\n");
            printf("产物: 索引.md / 命令大全.md / commands.jsonl / 库/*.md\n\n");
            printf("例:\n");
            printf("  %s -q\n", a0);
            printf("  %s D:\\out\n", a0);
            printf("  %s --json | findstr /i 取文本\n", a0);
            free(a0);
            return 0;
        }
        else if (a[0] == '-') { die("未知选项: %s（用 -h 看用法）", a); free(a); return 2; }
        else {                                   /* 位置参数 = 输出目录 */
            if (outdir_set) { die("只能指定一个输出目录（已经是 %s）", outdir); free(a); return 2; }
            snprintf(outdir, sizeof(outdir), "%s", a);
            outdir_set = 1;
        }
        free(a);
    }

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOOPENFILEERRORBOX);
    AddVectoredExceptionHandler(1, veh_handler);

    /* 只有坐在终端前才等待回车；管道/重定向（AI、脚本）直接跑完退出 */
    if (!interactive()) pause = 0;

    /* 默认输出目录 = **当前工作目录**（不是 exe 所在目录：AI 先 cd 到自己的
       临时目录再调用，产物就落在手边，也不用去猜 exe 装在哪）。 */
    if (!outdir[0]) {
        wchar_t wcwd[MAX_PATH];
        DWORD n = GetCurrentDirectoryW(MAX_PATH, wcwd);
        if (n > 0 && n < MAX_PATH) {
            int need = WideCharToMultiByte(CP_UTF8, 0, wcwd, (int)n, NULL, 0, NULL, NULL);
            char *u8 = need > 0 ? (char *)malloc((size_t)need + 1) : NULL;
            if (u8) {
                WideCharToMultiByte(CP_UTF8, 0, wcwd, (int)n, u8, need, NULL, NULL);
                u8[need] = 0;
                snprintf(outdir, sizeof(outdir), "%s", u8);
                free(u8);
            }
        }
        if (!outdir[0]) snprintf(outdir, sizeof(outdir), ".");
    }

    char libdir[MAX_PATH], note[512] = "";
    if (libdir_arg[0]) {
        /* 允许直接给 lib 目录，或给安装目录 */
        char t[MAX_PATH];
        snprintf(t, sizeof(t), "%s", libdir_arg);
        size_t n = strlen(t);
        while (n && (t[n - 1] == '\\' || t[n - 1] == '/')) t[--n] = 0;
        const char *base = strrchr(t, '\\');
        if (base && _stricmp(base + 1, "lib") == 0) snprintf(libdir, sizeof(libdir), "%s", t);
        else snprintf(libdir, sizeof(libdir), "%s\\lib", t);
        snprintf(note, sizeof(note), "命令行 -l 指定: %s", libdir);
    } else if (!find_libdir(libdir, sizeof(libdir), note, sizeof(note))) {
        die("找不到易语言安装目录。");
        if (note[0]) die("已尝试: %s", note);
        die("请用 -l <支持库目录> 手动指定，例如 -l \"D:\\ides\\e\\lib\"");
        if (pause) { printf("\n按回车退出..."); getchar(); }
        return 1;
    }

    if (!quiet) {
        printf("易语言支持库文档导出\n");
        printf("  支持库目录: %s\n", libdir);
        printf("  定位方式  : %s\n", note);
        printf("  输出目录  : %s\n\n", outdir);
        fflush(stdout);
    }

    /* 枚举 .fne */
    char pattern[MAX_PATH], pattern_a[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.fne", libdir);
    u8_to_acp(pattern, pattern_a, sizeof(pattern_a));
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern_a, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        die("目录里没有 .fne 文件: %s", libdir);
        if (pause) { printf("\n按回车退出..."); getchar(); }
        return 1;
    }
    Lib *libs = NULL;
    int n = 0, cap = 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            libs = (Lib *)realloc(libs, (size_t)cap * sizeof(Lib));
        }
        char *name_u8 = acp_to_u8(fd.cFileName);
        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", libdir, name_u8);
        int rc = load_lib(full, &libs[n]);
        if (rc == 0) {
            libs[n].version = (char *)malloc(32);
            snprintf(libs[n].version, 32, "%d.%d", libs[n].major, libs[n].minor);
        }
        if (!quiet) {
            printf("  [%2d] %-24s %s\n", n + 1, name_u8,
                   rc == 0 ? "ok" : libs[n].err);
            fflush(stdout);
        }
        free(name_u8);
        n++;
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);

    /* --json：只往标准输出吐 commands.jsonl，不建目录、不写文件
       （stdout 被重定向时按 UTF-8 原样写出，方便直接落成 .jsonl 或喂给检索工具） */
    if (json_only) {
        Buf b; memset(&b, 0, sizeof(b));
        build_jsonl(&b, libs, n);
        out_write(stdout, b.p ? b.p : "", b.len);
        buf_free(&b);
        return 0;
    }

    ensure_dir(outdir);
    gen_index(libs, n, outdir, libdir, note);
    gen_all_md(libs, n, outdir);
    gen_jsonl(libs, n, outdir);
    for (int i = 0; i < n; i++)
        if (libs[i].ok) gen_lib_md(&libs[i], outdir);

    if (!quiet) {
        int okn = 0, total = 0;
        for (int i = 0; i < n; i++) {
            if (!libs[i].ok) continue;
            okn++;
            for (int j = 0; j < libs[i].ncmd; j++)
                if (keep_cmd(&libs[i].cmds[j])) total++;
        }
        printf("\n完成: %d/%d 个支持库，可用命令 %d 条\n", okn, n, total);
        printf("文档已写入: %s\n", outdir);
        printf("  索引.md / 命令大全.md / commands.jsonl / 库/*.md\n");
    }
    if (pause) { printf("\n按回车退出..."); getchar(); }
    return 0;
}
