/* ============================================================================
 * mbkit_fwd.c —— MbKit 的「透明转发层」
 *
 * 目的：让一个原本直接声明 `node.dll` 的易语言模块，把库名改成 MbKit.dll
 *       之后**其余一字不改**即可运行；同时把内核（node.dll）的生命周期
 *       收归 MbKit 独占持有，这是热重载成立的前提。
 *
 * 三条设计约束（都很硬）：
 *   1) 转发必须对调用方完全透明 —— 调用约定(cdecl)、参数个数、返回值、
 *      栈平衡一个都不能动。所以汇编桩里只有 `jmp [slot]`，没有 push/pop。
 *   2) 不能在 DllMain 里加载依赖（loader lock）。改为首次调用时懒加载。
 *   3) 找不到内核时要**明确报错**，不能把 slot 留成 0 然后 jmp 0 ——
 *      那会把一个配置错误伪装成随机崩溃。
 * ==========================================================================*/
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "gen_fwd.h"
#include "mbkit_api.h"
#include "mbk_marshal.h"
#include "mbk_thread.h"

/* 内核导出地址表：与 gen_fwd.h 的 MBK_IDX_* 一一对应 */
void* mbk_slots[MBK_ALL_COUNT];

static HMODULE volatile g_hNode   = NULL;
static LONG             g_state   = 0;     /* 0=未初始化 1=已就绪 2=已失败 */
static char             g_nodePath[MAX_PATH];
static char             g_fwdErr[512];
static LONG             g_fatalShown = 0;

/* --------------------------------------------------------------------------
 * 内核路径解析
 *
 * 查找顺序（2026-09-24 重做：新增「自定义路径」，默认位置改为宿主程序根目录）：
 *   ① 自定义路径 —— 由易语言的 `WKE设置node路径` 下发（MbKit_SetNodePath）：
 *        · 以 .dll 结尾 → 当作 node.dll 的**完整文件名**
 *        · 其它         → 当作**目录**，取 <目录>\node.dll
 *      ★「显式设置即契约」：设了却没找到就**明确报错**，不再悄悄回退到别处。
 *        否则「我明明设了路径，跑的却是另一个内核」属于最难查的一类问题。
 *      ★ 空 / 未设置 = 走下面的默认链，默认链第一站就是**宿主程序根目录**。
 *   ② <宿主 exe 目录>\node.dll       —— 新的默认位置
 *   ③ <MbKit.dll 目录>\node.dll      —— 「两个文件放一起」的老布局
 *   ④ 从宿主 exe 目录逐级上溯（≤5 级）：每级试 \node.dll 与 \main\corn\node.dll
 *   ⑤ 当前工作目录：node.dll 与 \main\corn\node.dll
 *   ⑥ 裸 node.dll                    —— 交给系统搜索路径
 * ------------------------------------------------------------------------*/
static int path_file_exists(const char* p)
{
    DWORD a = GetFileAttributesA(p);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static void dir_of(const char* full, char* outDir)
{
    const char* s1 = strrchr(full, '\\');
    const char* s2 = strrchr(full, '/');
    const char* s  = (s1 > s2) ? s1 : s2;
    size_t n;
    if (!s) { outDir[0] = 0; return; }
    n = (size_t)(s - full);
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    memcpy(outDir, full, n);
    outDir[n] = 0;
}

/* 在 baseDir 下按 rel（形如 "main\\corn\\node.dll" 或 "node.dll"）找文件 */
static int try_rel(const char* baseDir, const char* rel, char* out)
{
    if (!baseDir || !baseDir[0]) return 0;
    _snprintf(out, MAX_PATH, "%s\\%s", baseDir, rel);
    out[MAX_PATH - 1] = 0;
    return path_file_exists(out);
}

/* --------------------------------------------------------------------------
 * 「自定义内核位置」的配置状态（由 MbKit_SetNodePath 写入）
 *
 * 只允许在内核**尚未加载**时设置：内核一旦加载，进程内就只能有这一份
 * （见 mbkit.c 顶部注释），再改路径没有任何意义，所以直接拒绝并返回 0。
 * ------------------------------------------------------------------------*/
static int  g_cfgSet  = 0;                 /* 是否设置了非空的自定义路径 */
static char g_cfgPath[MAX_PATH];           /* 自定义路径原文 */
static int  g_cfgMiss = 0;                 /* 本次解析：设了但没命中 */

static int ends_with_dll(const char* p)
{
    size_t n = p ? strlen(p) : 0;
    return (n >= 4) && (_stricmp(p + n - 4, ".dll") == 0);
}

static void copy_str(char* dst, size_t cap, const char* src)
{
    if (!cap) return;
    if (!src) src = "";
    _snprintf(dst, cap, "%s", src);
    dst[cap - 1] = 0;
}

/* 去掉首尾空白，以及用户从资源管理器「复制路径」时带上的成对引号 */
static void trim_inplace(char* s)
{
    size_t n, i = 0;
    if (!s) return;
    n = strlen(s);
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i) { memmove(s, s + i, n - i + 1); n -= i; }
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
    if (n >= 2 && ((s[0] == '"' && s[n - 1] == '"') || (s[0] == '\'' && s[n - 1] == '\''))) {
        memmove(s, s + 1, n - 2);
        s[n - 2] = 0;
        trim_inplace(s);
    }
}

/* 宿主程序（exe）所在目录 —— 自定义路径为空时的默认内核位置 */
static int host_exe_dir(char* out)
{
    char exe[MAX_PATH];
    out[0] = 0;
    if (!GetModuleFileNameA(NULL, exe, MAX_PATH)) return 0;
    dir_of(exe, out);
    return out[0] != 0;
}

/* MbKit.dll 自身所在目录 */
static int self_dir(char* out)
{
    HMODULE hm = NULL;
    char self[MAX_PATH];
    out[0] = 0;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                          | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(UINT_PTR)&self_dir, &hm) || !hm) return 0;
    if (!GetModuleFileNameA(hm, self, MAX_PATH)) return 0;
    dir_of(self, out);
    return out[0] != 0;
}

static int resolve_node(char* out)
{
    char selfDir[MAX_PATH], exeDir[MAX_PATH], cur[MAX_PATH];
    int  i;

    g_cfgMiss = 0;
    out[0] = 0;

    /* ① 自定义路径（显式设置优先，且**只认它**） */
    if (g_cfgSet && g_cfgPath[0]) {
        if (ends_with_dll(g_cfgPath)) {
            if (path_file_exists(g_cfgPath)) {
                _snprintf(out, MAX_PATH, "%s", g_cfgPath);
                out[MAX_PATH - 1] = 0;
                return 1;
            }
        } else if (try_rel(g_cfgPath, "node.dll", out)) {
            return 1;
        }
        g_cfgMiss = 1;          /* 配置了却没命中：交由 bind_all 明确报错 */
        return 0;
    }

    /* ② 宿主程序（exe）目录 —— 新默认位置 */
    if (host_exe_dir(exeDir) && try_rel(exeDir, "node.dll", out)) return 1;

    /* ③ MbKit.dll 自身所在目录 —— 「两个文件放一起」的老布局 */
    if (self_dir(selfDir) && try_rel(selfDir, "node.dll", out)) return 1;

    /* ④ 宿主 exe 目录逐级上溯 */
    if (host_exe_dir(cur)) {
        for (i = 0; i < 5 && cur[0]; i++) {
            if (try_rel(cur, "node.dll", out)) return 1;
            if (try_rel(cur, "main\\corn\\node.dll", out)) return 1;
            dir_of(cur, cur);          /* 再上一层：就地覆盖 */
        }
    }

    /* ⑤ 当前工作目录（兼容以当前目录为基准的老用法） */
    {
        char cwd[MAX_PATH];
        if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
            if (try_rel(cwd, "node.dll", out)) return 1;
            if (try_rel(cwd, "main\\corn\\node.dll", out)) return 1;
        }
    }

    /* ⑥ 兜底：交系统搜索（等价于原模块相对路径的行为） */
    _snprintf(out, MAX_PATH, "node.dll");
    out[MAX_PATH - 1] = 0;
    return 0;
}

/* --------------------------------------------------------------------------
 * 失败处理（2026-09-24 改）
 *
 * 旧行为：MessageBox + ExitProcess(1)。那套是给「独立压测宿主」写的；
 * 但现在 MbKit 是**寄生在别人的宿主进程里**（例如聊天机器人框架），
 * ExitProcess 会把宿主整个干掉 —— 用户只会看到「闪退」，无从排查。
 *
 * 新行为：
 *   · 记录错误（MbKit_FwdError 可查）+ OutputDebugString
 *   · 弹一次提示框告知原因（MBKIT_SILENT=1 可抑制，给自动化用）
 *   · **不退出**，改为把全部转发槽填成「安全桩」（返回 0、不碰栈），
 *     让调用方拿到 0 而不是踩空指针 —— 宿主活着，插件功能降级
 *   · 需要旧行为（CI/压测里"失败必须立刻可见"）时设 MBKIT_FATAL_EXIT=1
 * ------------------------------------------------------------------------*/
static int env_is_1(const char* name)
{
    char v[8];
    return (GetEnvironmentVariableA(name, v, sizeof(v)) > 0 && v[0] == '1');
}

static void fatal(const char* fmt, ...)
{
    char buf[768];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    buf[sizeof(buf) - 1] = 0;
    va_end(ap);

    OutputDebugStringA("[MbKit] FATAL: ");
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");

    /* 记进 g_fwdErr，供 MbKit_FwdError 诊断（保留最早的一条，信息量最大） */
    if (!g_fwdErr[0]) copy_str(g_fwdErr, sizeof(g_fwdErr), buf);

    /* 自动化场景（CI / 无人值守）可用 MBKIT_SILENT=1 抑制弹框，避免阻塞 */
    if (!env_is_1("MBKIT_SILENT") && InterlockedExchange(&g_fatalShown, 1) == 0) {
        char full[1280];
        _snprintf(full, sizeof(full) - 1,
                  "MbKit 无法进入 miniblink 内核。\n\n%s\n\n"
                  "内核（node.dll）的查找位置：\n"
                  "  1) 由「WKE设置node路径」指定的目录或文件\n"
                  "  2) 宿主程序所在目录（未设置时的默认位置）\n"
                  "  3) MbKit.dll 所在目录 / 宿主目录上溯若干级 / 当前目录\n\n"
                  "本次实际使用：%s", buf, g_nodePath[0] ? g_nodePath : "(未解析到)");
        full[sizeof(full) - 1] = 0;
        MessageBoxA(NULL, full, "MbKit", MB_ICONERROR | MB_OK);
    }

    if (env_is_1("MBKIT_FATAL_EXIT")) ExitProcess(1);   /* 旧行为，按需开启 */
}

/* --------------------------------------------------------------------------
 * 缺失导出的处理
 *
 * 现实情况：易语言模块的 .DLL命令 表里存在**永远取不到**的名字，例如
 *   JsBindGetter / JsBindSetter  <- 内核实际导出名是小写 jsBindGetter / jsBindSetter
 *   wkeNetSetURL / wkeGetWebView <- 内核根本没有（只有 jsGetWebView 等）
 * 原模块同样是运行时 GetProcAddress 取不到 —— 也就是说这些命令本来就没被用过。
 * 所以这里**不能因为缺几个就拒绝启动**，否则一个原本能跑的模块会因为几条
 * 死声明而完全打不开。策略：
 *   - 关键 API 缺失 -> 致命（这个模块不可能工作）
 *   - 其余缺失     -> 填「安全桩」并记账，诊断接口可查；调用它只返回 0，不崩
 * ------------------------------------------------------------------------*/
static LONG g_missingCount = 0;
static LONG g_missingHits  = 0;
static char g_missingList[1024];

static int __cdecl mbk_missing_stub(void)
{
    InterlockedIncrement(&g_missingHits);
    OutputDebugStringA("[MbKit] 调用了内核未导出的 API（该声明在原模块中同样是死的）\n");
    return 0;
}

static const char* const k_critical[] = {
    "wkeInitialize", "wkeCreateWebView", "wkeDestroyWebView", "wkeSetHandle",
};

/* 严格模式：MBKIT_STRICT=1 时，任何缺失都致命（用于排查） */
static int strict_mode(void)
{
    char v[8];
    DWORD n = GetEnvironmentVariableA("MBKIT_STRICT", v, sizeof(v));
    return (n > 0 && v[0] == '1');
}

/* --------------------------------------------------------------------------
 * 懒加载：把 node.dll 的全部导出填进 mbk_slots[]
 * 线程安全：用 InterlockedCompareExchange 做一次性发布。
 * ------------------------------------------------------------------------*/
/* 内核不可用时：把全部转发槽填成安全桩。
 * 这一步**必须有** —— 否则槽位是 0，`jmp 0` 会让宿主随机崩在别人的栈上，
 * 而我们现在刻意不再 ExitProcess（见 fatal 的注释）。 */
static void fill_safe_stubs(void)
{
    int i;
    for (i = 0; i < MBK_ALL_COUNT; i++) mbk_slots[i] = (void*)&mbk_missing_stub;
}

/* ==========================================================================
 * 导出边界编组（2026-09-25 重做 —— 取代已实测否定的「内核代际」机制）
 *
 * 为什么必须把编组做在 252 个 wke* / js* 导出上：
 *   内核线程亲和（属主不可迁移、finalize 空实现、换拷贝停摆，全部实测）。
 *   旧版导出是纯 jmp 直通 —— 调用线程直接闯内核；业务线程（.ec 模块的
 *   MB浏览框.绑定 路径）一旦换了线程（DLL 重载后），就是挂死或 0xC0000005。
 *   只有把**每一个**内核调用都收进编组，内核才能永久落在常驻线程 K 上，
 *   业务线程的生死与内核归属彻底解耦（marshal_probe r1 实测全链路成立；
 *   r2/r3/r4 证明不存在可以直通的子集 —— 任何直调都挂）。
 *
 * 结构（gen_fwd.py 生成 gen_thunk.c）：
 *   公开导出 = 调用线程侧 thunk：按**真实签名**收参 → 打包 MBKCALL
 *              → mbk_marshal_exec（本文件）→ 带回返回值；
 *   K 侧执行体 tr_xxx：按**真实签名**解包 → 调内核真函数 → 回填返回值。
 *   被接管的 22 个回调登记类（mbkit_cb.c）同样经 mbk_marshal_3 编组。
 * ========================================================================*/
static volatile LONG g_liveViews = 0;

void mbk_live_add(long d)
{
    LONG v = InterlockedExchangeAdd(&g_liveViews, d) + d;
    if (v < 0) InterlockedExchange(&g_liveViews, 0);
}
long mbk_live_get(void) { return g_liveViews; }

/* ---- 编组执行层（实现 mbk_marshal.h 的契约）--------------------------
 * 编组计数（MbKit_Diag 可见）：
 *   total = 经导出边界进来的内核调用总数
 *   cross = 其中从非 K 线程发起（真正走了 PostThreadMessage 编组）的次数
 *   fail  = 被拒绝的次数（兼容模式跨线程 / 内核初始化失败 / 非法载荷）
 * --------------------------------------------------------------------*/
static LONG g_marshalTotal = 0;
static LONG g_marshalCross = 0;
static LONG g_marshalFail  = 0;

/* mbkit.c：内核属主三态与「当前线程能否碰内核」判定（K 模式/兼容模式） */
extern int   mbk_can_kernel(void);
extern int   mbk_owner_mode(void);       /* 1=K 模式 0=兼容模式（属主=业务线程） */
extern DWORD mbk_owner_tid(void);

/* mbkit.c【导出】：确保内核已落在常驻线程 K 上（幂等）。MBKIT_OK == 0 */
extern int __stdcall MbKit_EnsureKernelInit(void);

/* K 侧统一跳板（mbk_kernel_run 的任务形态）：
 * 取内核真地址 → 交由 gen_thunk.c 生成的签名化执行体。
 * 真地址缺失 = 内核没导出这个 API（fill_slots 已填安全桩）→ 返回 0，
 * 与旧版「安全桩」语义逐位一致，绝不 jmp 0。 */
static void marshal_trampoline(void* p)
{
    MBKCALL* c = (MBKCALL*)p;
    c->real = mbk_slots[c->idx];
    if (!c->real || c->real == (void*)&mbk_missing_stub) {
        c->ret.u = 0;
        return;
    }
    c->tramp(c);
}

void mbk_marshal_exec(MBKCALL* c)
{
    InterlockedIncrement(&g_marshalTotal);

    if (!c || c->idx < 0 || c->idx >= MBK_ALL_COUNT || !c->tramp) {
        InterlockedIncrement(&g_marshalFail);
        if (c) c->ret.u = 0;
        return;
    }

    mbk_ensure();        /* 槽表懒加载（首次调用在此 LoadLibrary node.dll） */

    /* 兼容模式（内核被某业务线程裸初始化绑死、无法迁移）且当前是别的线程：
     * 明确失败 —— 返回 0，绝不跨线程硬闯（硬约束：宁可降级不可崩宿主）。 */
    if (!mbk_can_kernel()) {
        InterlockedIncrement(&g_marshalFail);
        c->ret.u = 0;
        return;
    }

    /* 内核尚未初始化：首个内核调用即把内核落到常驻线程 K。
     * （wkeInitialize 导出本身也已改为同一语义 —— 见 gen_thunk.c 特例。） */
    if (!mbk_owner_tid() && MbKit_EnsureKernelInit() != 0 /* MBKIT_OK */) {
        InterlockedIncrement(&g_marshalFail);
        c->ret.u = 0;
        return;
    }

    if (GetCurrentThreadId() != mbk_kernel_tid())
        InterlockedIncrement(&g_marshalCross);

    /* 属主线程上调用 → 就地执行（回调重入安全）；否则投递到 K 同步等待 */
    mbk_kernel_run(marshal_trampoline, c);
}

/* 三参便捷版：签名 (void*,void*,void*) -> 整数 —— 回调登记类（mbkit_cb.c）
 * 与大量内核 API 的公共形态。执行体从 mbk_tramps[]（gen_thunk.c）按 idx 取。 */
unsigned mbk_marshal_3(int idx, unsigned a0, unsigned a1, unsigned a2)
{
    MBKCALL c;
    ZeroMemory(&c, sizeof(c));
    c.idx   = idx;
    c.tramp = mbk_tramps[idx];
    c.kind  = 1;
    c.a[0]  = (long long)a0;
    c.a[1]  = (long long)a1;
    c.a[2]  = (long long)a2;
    mbk_marshal_exec(&c);
    return c.ret.u;
}

/* 当前内核实际路径（供诊断 / MbKit_KernelPath 用） */
const char* mbk_current_kernel_path(void)
{
    return g_nodePath;
}

/* 把内核 h 的全部导出填进 mbk_slots[]（不含 LoadLibrary 与路径解析） */
static void fill_slots(HMODULE h)
{
    int i, miss = 0, critMiss = 0;

    g_missingList[0] = 0;
    for (i = 0; i < MBK_ALL_COUNT; i++) {
        void* p = (void*)GetProcAddress(h, mbk_fwd_names[i]);
        if (p) { mbk_slots[i] = p; continue; }

        miss++;
        mbk_slots[i] = (void*)&mbk_missing_stub;
        {   /* 记账（供 MbKit_MissingList 诊断） */
            size_t used = strlen(g_missingList);
            if (used + strlen(mbk_fwd_names[i]) + 2 < sizeof(g_missingList))
                _snprintf(g_missingList + used, sizeof(g_missingList) - used,
                          "%s ", mbk_fwd_names[i]);
        }
        {
            int k;
            for (k = 0; k < (int)(sizeof(k_critical) / sizeof(k_critical[0])); k++)
                if (strcmp(mbk_fwd_names[i], k_critical[k]) == 0) critMiss++;
        }
    }
    InterlockedExchange(&g_missingCount, miss);

    if (miss) {
        OutputDebugStringA("[MbKit] 内核未导出（已填安全桩）：");
        OutputDebugStringA(g_missingList);
        OutputDebugStringA("\n");
    }
    (void)critMiss;
}

static void bind_all(void)
{
    int critMiss = 0;
    int i;

    if (!resolve_node(g_nodePath)) {
        if (g_cfgMiss) {
            /* 显式设置了路径却没找到：**不静默回退**，明确报错。
             * 静默回退会让「明明设了路径、跑的却是别处内核」变成无解之谜。 */
            fatal("「WKE设置node路径」指定的位置里没有 node.dll：\n  %s\n"
                  "（不需要自定义时请把路径留空，默认即宿主程序根目录）", g_cfgPath);
            fill_safe_stubs();
            return;
        }
        /* 未按预期找到，但 LoadLibrary 仍可能通过系统搜索成功 */
        g_nodePath[MAX_PATH - 1] = 0;
    }

    g_hNode = LoadLibraryA(g_nodePath);
    if (!g_hNode) {
        char  tried[MAX_PATH];
        DWORD e1 = GetLastError();
        DWORD e2;
        copy_str(tried, sizeof(tried), g_nodePath);
        if (strcmp(tried, "node.dll") != 0)
            g_hNode = LoadLibraryA("node.dll");          /* 换裸名再试一次 */
        e2 = g_hNode ? 0 : GetLastError();
        if (!g_hNode) {
            fatal("加载内核失败：LoadLibrary(\"%s\") error=%lu；"
                  "按裸名 node.dll 再试 error=%lu。",
                  tried, (unsigned long)e1, (unsigned long)e2);
            fill_safe_stubs();
            return;
        }
    }

    fill_slots(g_hNode);

    /* 关键 API 缺失 -> 致命（这个模块不可能工作） */
    for (i = 0; i < MBK_ALL_COUNT; i++) {
        int k;
        if (mbk_slots[i] != (void*)&mbk_missing_stub) continue;
        for (k = 0; k < (int)(sizeof(k_critical) / sizeof(k_critical[0])); k++)
            if (strcmp(mbk_fwd_names[i], k_critical[k]) == 0) critMiss++;
    }
    if (critMiss) {
        fatal("内核 %s 缺少 %d 个**关键** API —— 该模块无法工作。缺失清单：%s",
              g_nodePath, critMiss, g_missingList);
        fill_safe_stubs();
        return;
    }
    if (g_missingCount && strict_mode()) {
        fatal("严格模式：内核缺少 %d 个导出：%s", (int)g_missingCount, g_missingList);
        fill_safe_stubs();
        return;
    }

    /* （旧版此处安装 create/destroy/initialize 拦截桩 —— 已随「内核代际」机制
     *  废弃：252 个导出现在全部是 gen_thunk.c 的编组 thunk，无需再改槽表。） */
}

int mbk_ensure(void)
{
    LONG s = g_state;
    if (s == 1) return 1;
    if (s == 2) return 0;

    if (InterlockedCompareExchange(&g_state, 0x7FFFFFFF, 0) == 0) {
        bind_all();                                   /* 只有一个线程进来 */
        InterlockedExchange(&g_state, g_hNode ? 1 : 2);
    } else {
        while ((s = g_state) == 0x7FFFFFFF) Sleep(1); /* 等价于等待发布 */
    }
    return g_state == 1;
}

/* --------------------------------------------------------------------------
 * 供宿主/诊断使用的少量接口
 * ------------------------------------------------------------------------*/
int __stdcall MbKit_FwdIndex(const char* name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < MBK_ALL_COUNT; i++)
        if (strcmp(name, mbk_fwd_names[i]) == 0) return i;
    return -1;
}

/* 内核模块句柄（供 mbkit.c 复用，确保进程内只有一份 node.dll） */
HMODULE mbk_node(void)
{
    mbk_ensure();
    return g_hNode;
}

const char* __stdcall MbKit_KernelPath(void)
{
    mbk_ensure();
    return mbk_current_kernel_path();
}

unsigned int __stdcall MbKit_KernelModule(void)
{
    mbk_ensure();
    return (unsigned int)(UINT_PTR)g_hNode;
}

const char* __stdcall MbKit_FwdError(void)
{
    return g_fwdErr[0] ? g_fwdErr : "";
}

/* 缺失导出的诊断（缺失项已被填成安全桩，不会崩） */
int __stdcall MbKit_MissingCount(void) { mbk_ensure(); return (int)g_missingCount; }
int __stdcall MbKit_MissingHits(void)  { return (int)g_missingHits; }
const char* __stdcall MbKit_MissingList(void)
{
    mbk_ensure();
    return g_missingList;
}

/* ==========================================================================
 * 「自定义内核位置」接口（2026-09-24 新增）
 *
 * 供给易语言侧的 `WKE设置node路径` 使用。设计要点：
 *   · **只在内核尚未加载时接受设置** —— 内核一旦加载，进程内就只能有这一份
 *     （见 mbkit.c 顶部注释），再改路径没有意义，所以直接返回 0 让调用方知道。
 *   · 本函数**不触发**内核加载（刻意不调 mbk_ensure）：设置路径通常是宿主
 *     启动早期做的第一件事，此刻不该把 33 MB 的内核拖起来。
 * ========================================================================*/
int mbk_set_node_path(const char* p)
{
    if (g_state != 0) return 0;          /* 已加载 / 正在加载 / 已失败 */
    if (!p) p = "";
    copy_str(g_cfgPath, sizeof(g_cfgPath), p);
    trim_inplace(g_cfgPath);
    g_cfgSet = (g_cfgPath[0] != 0);
    return 1;
}

int __stdcall MbKit_SetNodePath(const char* path)
{
    return mbk_set_node_path(path);
}

/* 当前配置的自定义路径（未设置时返回空串）。不触发内核加载。 */
const char* __stdcall MbKit_ConfigNodePath(void)
{
    return g_cfgPath;
}

/* 内核是否已经加载完成。不触发内核加载（所以「查询」不会顺带把内核拖起来）。 */
int __stdcall MbKit_KernelLoaded(void)
{
    return (g_state == 1) ? 1 : 0;
}

/* 一行式自检：把「为什么内核没进来 / 从哪儿进来的」一次说清。
 * 返回静态缓冲，调用方不要释放、不要长期持有。 */
const char* __stdcall MbKit_Diag(void)
{
    static char buf[1600];
    char exeDir[MAX_PATH], cwd[MAX_PATH];
    const char* stateTxt =
        (g_state == 1) ? "已加载" :
        (g_state == 2) ? "加载失败" :
        (g_state == 0x7FFFFFFF) ? "正在加载" : "尚未加载";
    /* 内核属主模式：1=K 模式（编组，重载安全） 0=兼容模式（内核绑在业务线程） */
    DWORD ownerTid = mbk_owner_tid();
    const char* modeTxt = ownerTid
        ? (mbk_owner_mode() ? "K 模式（编组，重载安全）" : "兼容模式（内核绑业务线程）")
        : "未初始化";

    exeDir[0] = cwd[0] = 0;
    host_exe_dir(exeDir);
    GetCurrentDirectoryA(MAX_PATH, cwd);

    _snprintf(buf, sizeof(buf) - 1,
        "MbKit 自检\n"
        "  内核状态     : %s\n"
        "  内核属主     : 线程 %lu（%s）\n"
        "  内核路径     : %s\n"
        "  内核模块     : 0x%08X\n"
        "  自定义路径   : %s\n"
        "  宿主程序目录 : %s\n"
        "  当前工作目录 : %s\n"
        "  内核缺失导出 : %d（其中被真正调用过 %d 次）\n"
        "  缺失清单     : %s\n"
        "  加载期错误   : %s\n"
        "  编组调用     : 共 %ld 次（跨线程 %ld，失败 %ld）\n"
        /* ★ 2026-09-25：把回调层的编号并进自检 —— 这是「事件为什么没反应 / 缩放为什么
         *   不重绘（撕裂）」唯一能在**用户真机**里读到的直接指标。
         *   被挡 > 0 说明仍有回调被存活校验拦下；正常应为 0。 */
        "  回调登记     : %d 个（存活 %d）  命中 %d  被挡 %d\n"
        "  说明         : 被挡 > 0 表示有回调因「业务身份判不出/已失效」被拦；\n"
        "                 正常应为 0。若持续增长，请把本行反馈给维护者。\n",
        stateTxt,
        (unsigned long)ownerTid, modeTxt,
        g_nodePath[0] ? g_nodePath : "(未解析)",
        (unsigned)(UINT_PTR)g_hNode,
        g_cfgSet ? g_cfgPath : "(未设置 → 默认: 宿主程序根目录)",
        exeDir[0] ? exeDir : "(?)",
        cwd[0] ? cwd : "(?)",
        (int)g_missingCount, (int)g_missingHits,
        g_missingList[0] ? g_missingList : "(无)",
        g_fwdErr[0] ? g_fwdErr : "(无)",
        (long)g_marshalTotal, (long)g_marshalCross, (long)g_marshalFail,
        MbKit_CbCount(), MbKit_CbLive(), MbKit_CbHits(), MbKit_CbBlocked());
    buf[sizeof(buf) - 1] = 0;
    return buf;
}
