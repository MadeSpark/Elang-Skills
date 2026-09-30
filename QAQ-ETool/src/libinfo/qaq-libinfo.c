/*
 * qaq-libinfo —— 易语言支持库（.fne）元数据导出器，输出 OpenEpl.ELibInfo v2 JSON。
 *
 * 用途：ELibInfo 自带的 ExportELibInfoV2.exe 在部分支持库（如 e2ee.fne）上会访问违例崩溃，
 *       本工具作为兜底：结构体布局严格按官方 SDK lib2.h，全程在向量化异常保护下读取，
 *       单个成员读取失败只会丢该成员，不会让整个库失败。
 *
 * 用法：qaq-libinfo.exe <xxx.fne 完整路径> <输出 json 路径>
 * 必须编译成 32 位（.fne 是 32 位 DLL）：
 *   gcc -O2 -s -static -o qaq-libinfo.exe qaq-libinfo.c
 *
 * 结构体（lib2.h，32 位，单位 u32）：
 *   LIB_INFO            [1]guid [2]major [3]minor [4]build [5,6]rqSys [7,8]rqKrnl [9]name [11]explain
 *                       [13]author [14]zip [15]addr [16]phone [17]fax [18]email [19]home [20]other
 *                       [21]dtCount [22]dt [23]catCount [24]szzCategory [25]cmdCount [26]cmd
 *                       [32]constCount [33]const          —— 注：以 SDK 字段顺序为准
 *   LIB_DATA_TYPE_INFO  14: name eg explain cmdCount cmdsIndex state bmp evCount ev propCount prop getItf elemCount elem
 *   CMD_INFO            9 : name eg explain (cat16|state16) ret (resv16|level16) (bmp16|bmpcnt16) argc args
 *   ARG_INFO            6 : name explain (bmp16|bmpcnt16) type default state
 *   UNIT_PROPERTY       5 : name eg explain (type16|state16) pick
 *   LIB_DATA_TYPE_ELEMENT 7: type arySpec name eg explain state default
 *   EVENT_INFO          5 : name explain state argc args      (EVENT_ARG_INFO  3: name explain state)
 *   EVENT_INFO2         6 : name explain state argc args ret  (EVENT_ARG_INFO2 4: name explain state type)
 *   LIB_CONST_INFO      8 : name eg explain (layout16|type16) text [pad] double
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

typedef unsigned int u32;

/* ------------------------------------------------------------ 安全读取 */
static jmp_buf g_jmp;
static volatile int g_guard = 0;

static LONG CALLBACK veh(EXCEPTION_POINTERS *ep)
{
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (g_guard && (code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
                    code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED || code == EXCEPTION_DATATYPE_MISALIGNMENT)) {
        g_guard = 0;
        longjmp(g_jmp, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static int readable(const void *p, size_t n)
{
    if (!p || (UINT_PTR)p < 0x10000) return 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    if ((const char *)p + n > (const char *)mbi.BaseAddress + mbi.RegionSize) return readable((const char *)p + 1, 0) && n < 4096;
    return 1;
}

/* ------------------------------------------------------------ 输出缓冲 */
typedef struct { char *p; size_t n, cap; } Buf;
static Buf out;

static void put(const char *s, size_t n)
{
    if (out.n + n + 1 > out.cap) {
        out.cap = (out.cap + n + 1) * 2;
        out.p = (char *)realloc(out.p, out.cap);
    }
    memcpy(out.p + out.n, s, n);
    out.n += n;
    out.p[out.n] = 0;
}
static void puts_(const char *s) { put(s, strlen(s)); }
static void putf(const char *fmt, ...)
{
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (k > 0) put(tmp, (size_t)k < sizeof(tmp) ? (size_t)k : sizeof(tmp) - 1);
}

/* GBK C 串 → JSON 字符串（UTF-8，带转义）；无效指针输出 null */
static void jstr(const char *s)
{
    if (!s || !readable(s, 1)) { puts_("null"); return; }
    size_t len = 0;
    while (len < 65536 && readable(s + len, 1) && s[len]) len++;
    int wn = MultiByteToWideChar(936, 0, s, (int)len, NULL, 0);
    wchar_t *w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t));
    MultiByteToWideChar(936, 0, s, (int)len, w, wn);
    int un = WideCharToMultiByte(CP_UTF8, 0, w, wn, NULL, 0, NULL, NULL);
    char *u = (char *)malloc(un + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, wn, u, un, NULL, NULL);
    free(w);
    put("\"", 1);
    for (int i = 0; i < un; i++) {
        unsigned char c = (unsigned char)u[i];
        switch (c) {
        case '"': puts_("\\\""); break;
        case '\\': puts_("\\\\"); break;
        case '\n': puts_("\\n"); break;
        case '\r': puts_("\\r"); break;
        case '\t': puts_("\\t"); break;
        default:
            if (c < 0x20) putf("\\u%04x", c);
            else put((char *)&c, 1);
        }
    }
    put("\"", 1);
    free(u);
}

static void key(const char *k) { put("\"", 1); puts_(k); puts_("\":"); }

/* ------------------------------------------------------------ 各结构 */
static void emit_params(const u32 *ap, int ac)
{
    puts_("[");
    for (int k = 0; k < ac; k++) {
        const u32 *g = ap + k * 6;
        if (!readable(g, 24)) break;
        if (k) puts_(",");
        u32 st = g[5];
        puts_("{"); key("Name"); jstr((const char *)g[0]);
        puts_(","); key("DataType"); putf("%d", (int)g[3]);
        puts_(","); key("Description"); jstr((const char *)g[1]);
        if (st & ((1u << 0) | (1u << 1))) { puts_(","); key("Optional"); puts_("true"); }
        puts_("}");
    }
    puts_("]");
}

static void emit_cmds(const u32 *cp, int nc)
{
    puts_("[");
    for (int i = 0; i < nc; i++) {
        if (i) puts_(",");
        const u32 *c = cp + i * 9;
        if (!readable(c, 36)) { puts_("{\"Name\":null,\"Parameters\":[]}"); continue; }
        int volatile ok = 0;
        g_guard = 1;
        if (setjmp(g_jmp) == 0) {
            size_t mark = out.n;
            u32 state = (c[3] >> 16) & 0xFFFF;
            puts_("{"); key("Name"); jstr((const char *)c[0]);
            puts_(","); key("EnglshName"); jstr((const char *)c[1]);
            puts_(","); key("Description"); jstr((const char *)c[2]);
            puts_(","); key("CategoryId"); putf("%d", (short)(c[3] & 0xFFFF));
            if (c[4]) { puts_(","); key("ReturnDataType"); putf("%d", (int)c[4]); }
            if (state & (1u << 2)) { puts_(","); key("Deprecated"); puts_("\"Hidden\""); }
            puts_(","); key("Parameters");
            int ac = (int)c[7];
            if (ac < 0 || ac > 256 || (ac && !readable((const void *)c[8], 24))) ac = 0;
            emit_params((const u32 *)c[8], ac);
            puts_("}");
            ok = 1;
            (void)mark;
        }
        g_guard = 0;
        if (!ok) puts_("{\"Name\":null,\"Parameters\":[]}");
    }
    puts_("]");
}

static void emit_members_props(const u32 *pp, int n)
{
    for (int i = 0; i < n; i++) {
        const u32 *p = pp + i * 5;
        if (i) puts_(",");
        if (!readable(p, 20)) { puts_("{\"Name\":null}"); continue; }
        u32 st = (p[3] >> 16) & 0xFFFF;
        puts_("{"); key("Name"); jstr((const char *)p[0]);
        puts_(","); key("EnglishName"); jstr((const char *)p[1]);
        puts_(","); key("Description"); jstr((const char *)p[2]);
        if (st & (1u << 4)) { puts_(","); key("Deprecated"); puts_("\"Hidden\""); }
        puts_("}");
    }
}

static void emit_members_elems(const u32 *ep, int n)
{
    for (int i = 0; i < n; i++) {
        const u32 *e = ep + i * 7;
        if (i) puts_(",");
        if (!readable(e, 28)) { puts_("{\"Name\":null}"); continue; }
        puts_("{"); key("Name"); jstr((const char *)e[2]);
        puts_(","); key("DataType"); putf("%d", (int)e[0]);
        puts_(","); key("EnglishName"); jstr((const char *)e[3]);
        puts_(","); key("Description"); jstr((const char *)e[4]);
        if (e[5] & (1u << 1)) { puts_(","); key("Deprecated"); puts_("\"Hidden\""); }
        puts_("}");
    }
}

static void emit_events(const u32 *ev, int n)
{
    if (n <= 0 || !readable(ev, 20)) return;
    int v2 = (ev[2] & 0x80000000u) != 0;
    int stride = v2 ? 6 : 5;
    for (int i = 0; i < n; i++) {
        const u32 *e = ev + i * stride;
        if (i) puts_(",");
        if (!readable(e, stride * 4)) { puts_("{\"Name\":null,\"Parameters\":[]}"); continue; }
        puts_("{"); key("Name"); jstr((const char *)e[0]);
        puts_(","); key("Description"); jstr((const char *)e[1]);
        if (v2 && e[5]) { puts_(","); key("ReturnDataType"); putf("%d", (int)e[5]); }
        puts_(","); key("Parameters"); puts_("[");
        int ac = (int)e[3];
        const u32 *ap = (const u32 *)e[4];
        int as = v2 ? 4 : 3;
        if (ac < 0 || ac > 64 || (ac && !readable(ap, as * 4))) ac = 0;
        for (int k = 0; k < ac; k++) {
            const u32 *a = ap + k * as;
            if (!readable(a, as * 4)) break;
            if (k) puts_(",");
            puts_("{"); key("Name"); jstr((const char *)a[0]);
            puts_(","); key("Description"); jstr((const char *)a[1]);
            if (v2) { puts_(","); key("DataType"); putf("%d", (int)a[3]); }
            puts_("}");
        }
        puts_("]}");
    }
}

static void emit_datatypes(const u32 *dp, int n)
{
    puts_("[");
    for (int i = 0; i < n; i++) {
        if (i) puts_(",");
        const u32 *d = dp + i * 14;
        if (!readable(d, 56)) { puts_("{\"Name\":null,\"Events\":[],\"Members\":[],\"Methods\":[]}"); continue; }
        int volatile ok = 0;
        size_t mark = out.n;
        g_guard = 1;
        if (setjmp(g_jmp) == 0) {
            u32 st = d[5];
            int win = (st & (1u << 6)) != 0;
            int isEnum = (st & (1u << 22)) != 0;
            puts_("{"); key("Name"); jstr((const char *)d[0]);
            puts_(","); key("EnglshName"); jstr((const char *)d[1]);
            puts_(","); key("Description"); jstr((const char *)d[2]);
            if (win) { puts_(","); key("Kind"); puts_("\"Component\""); }
            else if (isEnum) { puts_(","); key("Kind"); puts_("\"Enum\""); }
            if (st & 1u) { puts_(","); key("Deprecated"); puts_("\"Hidden\""); }
            if (win && (st & (1u << 7))) { puts_(","); key("IsContainer"); puts_("true"); }
            if (win && (st & (1u << 8))) { puts_(","); key("IsTabControl"); puts_("true"); }
            if (win && (st & (1u << 15))) { puts_(","); key("IsFunctional"); puts_("true"); }
            if (win) { puts_(","); key("CanFocus"); puts_((st & (1u << 16)) ? "false" : "true"); }
            if (win && (st & (1u << 17))) { puts_(","); key("NoTabStopByDefault"); puts_("true"); }
            /* 方法：库命令表中的索引 */
            puts_(","); key("Methods"); puts_("[");
            int mc = (int)d[3];
            const int *mi = (const int *)d[4];
            if (mc < 0 || mc > 100000 || (mc && !readable(mi, 4))) mc = 0;
            for (int k = 0; k < mc; k++) { if (k) puts_(","); putf("%d", mi[k]); }
            puts_("]");
            /* 事件 */
            puts_(","); key("Events"); puts_("[");
            /* 菜单类型没有 LDT_WIN_UNIT 位，但同样用事件/属性表：以计数为准 */
            int ec = (int)d[7];
            if (!isEnum && ec > 0 && ec < 10000) emit_events((const u32 *)d[8], ec);
            puts_("]");
            /* 成员：有属性表 = 属性（窗口/菜单组件）；否则 = 元素表 */
            puts_(","); key("Members"); puts_("[");
            int pc = (int)d[9];
            if (!isEnum && pc > 0 && pc < 10000 && readable((const void *)d[10], 20)) {
                emit_members_props((const u32 *)d[10], pc);
            } else {
                int ecn = (int)d[12];
                if (ecn > 0 && ecn < 100000 && readable((const void *)d[13], 28)) emit_members_elems((const u32 *)d[13], ecn);
            }
            puts_("]}");
            ok = 1;
        }
        g_guard = 0;
        if (!ok) {
            out.n = mark; out.p[out.n] = 0;
            puts_("{\"Name\":null,\"Events\":[],\"Members\":[],\"Methods\":[]}");
        }
    }
    puts_("]");
}

static void emit_consts(const u32 *kp, int n)
{
    puts_("[");
    for (int i = 0; i < n; i++) {
        if (i) puts_(",");
        const u32 *k = kp + i * 8;
        if (!readable(k, 32)) { puts_("{\"Name\":null}"); continue; }
        short type = (short)((k[3] >> 16) & 0xFFFF);
        puts_("{"); key("Name"); jstr((const char *)k[0]);
        puts_(","); key("EnglshName"); jstr((const char *)k[1]);
        puts_(","); key("Description"); jstr((const char *)k[2]);
        double dv;
        memcpy(&dv, k + 6, sizeof(dv));
        if (type == 3) {
            puts_(","); key("DataType"); putf("%d", (int)0x80000004);
            puts_(","); key("Value"); jstr((const char *)k[4]);
        } else if (type == 2) {
            puts_(","); key("DataType"); putf("%d", (int)0x80000002);
            puts_(","); key("Value"); puts_(dv != 0 ? "true" : "false");
        } else if (type == 1) {
            if (dv == (double)(int)dv) {
                puts_(","); key("DataType"); putf("%d", (int)0x80000301);
                puts_(","); key("Value"); putf("%d", (int)dv);
            } else {
                puts_(","); key("DataType"); putf("%d", (int)0x80000601);
                puts_(","); key("Value"); putf("%.17g", dv);
            }
        }
        puts_("}");
    }
    puts_("]");
}

static void emit_categories(const char *szz, int n)
{
    puts_("[");
    const char *p = szz;
    for (int i = 0; i < n && p && readable(p, 1); i++) {
        if (i) puts_(",");
        /* 每项：4 位数字图像索引 + 名称 + \0 */
        int img = 0;
        if (strlen(p) >= 4) img = atoi((char[5]){p[0], p[1], p[2], p[3], 0});
        puts_("{"); key("Name"); jstr(strlen(p) >= 4 ? p + 4 : p);
        if (img) { puts_(","); key("ImageId"); putf("%d", img); }
        puts_("}");
        p += strlen(p) + 1;
    }
    puts_("]");
}

/* GUID 文本 → 带连字符小写（ELibInfo 的 Guid 格式） */
static void emit_guid(const char *g)
{
    char hex[33]; int n = 0;
    for (const char *p = g; p && *p && n < 32; p++)
        if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F'))
            hex[n++] = (char)((*p >= 'A' && *p <= 'F') ? *p + 32 : *p);
    hex[n] = 0;
    if (n != 32) { jstr(g); return; }
    putf("\"%.8s-%.4s-%.4s-%.4s-%.12s\"", hex, hex + 8, hex + 12, hex + 16, hex + 20);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "用法: qaq-libinfo <xxx.fne> <out.json>\n");
        return 2;
    }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    AddVectoredExceptionHandler(1, veh);

    /* 让库能找到同目录依赖 */
    char dir[MAX_PATH];
    strncpy(dir, argv[1], MAX_PATH - 1);
    dir[MAX_PATH - 1] = 0;
    char *slash = strrchr(dir, '\\');
    if (slash) { *slash = 0; SetDllDirectoryA(dir); SetCurrentDirectoryA(dir); }

    HMODULE h = LoadLibraryExA(argv[1], NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!h) { fprintf(stderr, "LoadLibrary 失败 %lu\n", GetLastError()); return 3; }
    typedef const u32 *(WINAPI * GETNEWINF)(void);
    GETNEWINF f = (GETNEWINF)GetProcAddress(h, "GetNewInf");
    if (!f) { fprintf(stderr, "没有导出 GetNewInf\n"); return 4; }

    const u32 *li = NULL;
    g_guard = 1;
    if (setjmp(g_jmp) == 0) li = f();
    g_guard = 0;
    if (!li || !readable(li, 35 * 4)) { fprintf(stderr, "GetNewInf 失败\n"); return 5; }

    /* 文件名主干 */
    const char *base = strrchr(argv[1], '\\');
    base = base ? base + 1 : argv[1];
    char stem[MAX_PATH];
    strncpy(stem, base, MAX_PATH - 1);
    stem[MAX_PATH - 1] = 0;
    char *dot = strrchr(stem, '.');
    if (dot) *dot = 0;

    puts_("{"); key("Guid"); emit_guid((const char *)li[1]);
    puts_(","); key("Name"); jstr((const char *)li[9]);
    puts_(","); key("FileName"); jstr(stem);
    puts_(","); key("Version"); putf("\"%d.%d\"", (int)li[2], (int)li[3]);
    puts_(","); key("VersionCode"); putf("%d", (int)li[4]);
    puts_(","); key("MinRequiredEplVersion"); putf("\"%d.%d\"", (int)li[5], (int)li[6]);
    puts_(","); key("MinRequiredKrnlnVersion"); putf("\"%d.%d\"", (int)li[7], (int)li[8]);
    puts_(","); key("Description"); jstr((const char *)li[11]);

    /* 常量表在 [33]/[34]（与 src/elibdoc.c 的 LI_CONSTCOUNT/LI_CONST 一致，已在全部本机库上验证） */
    int ndt = (int)li[21], ncat = (int)li[23], ncmd = (int)li[25], nk = (int)li[33];
    puts_(","); key("DataTypes");
    if (ndt > 0 && ndt < 100000 && readable((const void *)li[22], 56)) emit_datatypes((const u32 *)li[22], ndt); else puts_("[]");
    puts_(","); key("Categories");
    if (ncat > 0 && ncat < 1000) emit_categories((const char *)li[24], ncat); else puts_("[]");
    puts_(","); key("Cmds");
    if (ncmd > 0 && ncmd < 1000000 && readable((const void *)li[26], 36)) emit_cmds((const u32 *)li[26], ncmd); else puts_("[]");
    puts_(","); key("Constants");
    if (nk > 0 && nk < 1000000 && readable((const void *)li[34], 32)) emit_consts((const u32 *)li[34], nk); else puts_("[]");
    puts_(","); key("Author"); puts_("{");
    key("Name"); jstr((const char *)li[13]);
    puts_(","); key("ZipCode"); jstr((const char *)li[14]);
    puts_(","); key("Address"); jstr((const char *)li[15]);
    puts_(","); key("Telephone"); jstr((const char *)li[16]);
    puts_(","); key("QQ"); jstr((const char *)li[17]);
    puts_(","); key("Email"); jstr((const char *)li[18]);
    puts_(","); key("HomePage"); jstr((const char *)li[19]);
    puts_(","); key("Additional"); jstr((const char *)li[20]);
    puts_("}}");

    FILE *fo = fopen(argv[2], "wb");
    if (!fo) { fprintf(stderr, "无法写 %s\n", argv[2]); return 6; }
    fwrite(out.p, 1, out.n, fo);
    fclose(fo);
    /* 不 FreeLibrary：部分库卸载时会崩 */
    ExitProcess(0);
}
