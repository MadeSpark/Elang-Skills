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
#include <stdlib.h>

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
 * 新行为（2026-09-24 起；2026-10-01 再收紧一处）：
 *   · 记录错误（MbKit_FwdError 可查）+ 写进 MbKit_自检 + OutputDebugString
 *   · **默认不弹框**：MbKit 寄生在别人宿主的进程里（聊天机器人框架 / 易语言程序），
 *     在宿主启动期突然弹一个模态框，用户看到的就是「框架卡死」（框还在等点击）。
 *     需要人工看框时设 MBKIT_SHOW_ERRORBOX=1（MBKIT_SILENT=1 仍然优先=永不弹）。
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

    /* 弹框改为**显式开启**（MBKIT_SHOW_ERRORBOX=1），MBKIT_SILENT=1 依旧压过一切。
     * 理由：在宿主启动期弹模态框 = 用户眼里的「框架卡死」，且框要人来点。 */
    if (!env_is_1("MBKIT_SILENT") && env_is_1("MBKIT_SHOW_ERRORBOX") &&
        InterlockedExchange(&g_fatalShown, 1) == 0) {
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
extern int   mbk_owner_mode(void);       /* 1=K 模式 0=属主直通 */
extern DWORD mbk_owner_tid(void);
extern int   mbk_k_mode_wanted(void);    /* 1=MBKIT_K=1（旧 K 模式） */

/* mbkit.c【导出】：确保内核已落在常驻线程 K 上（幂等）。MBKIT_OK == 0 */
extern int __stdcall MbKit_EnsureKernelInit(void);
/* mbkit.c【导出】v1.4.1：真正把内核建在**当前调用线程**上（重活；DllMain 里不许调） */
extern int __stdcall MbKit_EnsureOwner(void);

/* mbkit.c：view 销毁「延迟回收」队列（任务5，2026-10-10）—— 把 wkeDestroyWebView 移出
 * 插件卸载窗口；属主直通下由属主线程在编组入口回收（见 mbkit.c 7b 节）。 */
extern int  mbk_defer_destroy(void* wv);
extern int  mbk_cancel_destroy(void* wv);
extern int  mbk_destroy_pending(void);
extern int  mbk_flush_deferred_destroy(void);
extern void mbk_destroy_stats(long* deferred, long* cancelled, long* reclaimed, int* qlen);

/* 初始化类导出：只登记启动、立即返回，**不碰内核**。
 * 这批 API 的特性就是「调完不代表内核已经起来」（wkeIsInitialize 本来就是查询），
 * 所以推迟完全符合语义；而把它们做成重活正是 DllMain 死锁的来源。 */
static int is_init_class(const char* n)
{
    return !strcmp(n, "wkeInitialize") ||
           !strcmp(n, "wkeInitializeEx") ||
           !strcmp(n, "wkeIsInitialize") ||
           !strcmp(n, "wkeFinalize");        /* 内核都没起来时「卸载」= 无事可做 */
}

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

/* ==========================================================================
 * ★ 2026-10-10 新增：内核「未就绪 / 不可用」时的语义修复（白屏根因）
 *
 * 背景（实测）：兼容模式（内核被某业务线程裸初始化、属主不可迁移）下，非属主
 *   线程发起的内核调用以前直接 `c->ret.u = 0; return;`。对 wkeLoadFile / LoadURL /
 *   LoadHTML 这类**发起导航**的 void 调用，这就是「静默丢弃」——日志停在
 *   「开始载入…」、此后既无 [Console] 也无 page_ready，画面永远单一底色（白屏）。
 *   它不是崩溃、不是卡死（183 条线程栈里没有一条含 MbKit）。修复分两处：
 *     A) 属主未就绪：不再直接丢弃，退化为有界等待（复用既有等待原语 MbKit_WaitKernel）；
 *     B) 兼容模式跨线程：导航类调用压进静态环形队列，待内核可用时按序重放；
 *        非导航类保持原样返回 0（不能推迟的语义）。
 *   内核已就绪的正常路径语义与性能逐字不变 —— 新逻辑只挂在这两个「未就绪/不可用」
 *   分支里（入口处仅多一次 volatile 读的 O(1) 快检）。
 * ========================================================================*/

/* 有限等待里的「抽消息小睡」：借既有 mbk_wait_pump 抽取并派发本线程消息 ms 毫秒
 * （而非新造 Sleep 轮询 —— 保证等待属主就绪期间 UI/内核消息不被饿死）。
 * 手法：一个临时**永不置位**事件；mbk_wait_pump 对未置位事件按 30ms 分片
 *   MsgWaitForMultipleObjects + PeekMessage/DispatchMessage，到点返回 0。 */
static void mbk_pump_pause(DWORD ms)
{
    HANDLE evt = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (evt) {
        mbk_wait_pump(evt, ms);        /* 未置位 → 分片抽消息，到点返回 0 */
        CloseHandle(evt);
    } else {
        mbk_wait_pump(NULL, ms);       /* 退化：等 ms 毫秒 */
    }
}

/* 「发起导航」类导出：都是 void 返回的导航动作，推迟到内核可用时再执行不破坏语义。
 * 名字以 gen_fwd.h 的 mbk_fwd_names[] 实际字符串为准（内核无裸 wkeLoad / wkeLoadFile）。 */
static int is_navigation_class(const char* n)
{
    return !strcmp(n, "wkeLoadFileW")           ||
           !strcmp(n, "wkeLoadHTML")            ||
           !strcmp(n, "wkeLoadHTMLW")           ||
           !strcmp(n, "wkeLoadHtmlWithBaseUrl") ||
           !strcmp(n, "wkeLoadURL")             ||
           !strcmp(n, "wkeLoadURLW")            ||
           !strcmp(n, "wkeLoadW");
}

/* 取一个「单参 → u32」的内核 getter（如 wkeIsLoading(wv)），返回值为其 ret.u。
 *   ★ 硬约束：只在属主线程上调用（绝不对非属主线程碰内核）；调用方负责判定。 */
static unsigned mbk_kernel_get_u32(int idx, unsigned a0)
{
    MBKCALL q;
    if (idx < 0 || idx >= MBK_ALL_COUNT) return 0;
    ZeroMemory(&q, sizeof(q));
    q.idx   = idx;
    q.tramp = mbk_tramps[idx];
    q.kind  = 1;                 /* u32 返回（gen_thunk.c 里同 kind） */
    q.a[0]  = (long long)a0;
    if (!q.tramp) return 0;
    mbk_kernel_run(marshal_trampoline, &q);
    return q.ret.u;
}

/* 导航类导出就地执行完成后的「后验」诊断：wv = c->a[0]（导航 API 首参即 webview）。
 * 判据：若失败轮三条 getter 全 0（且 initCount/bootState 与成功轮一致），
 *       即证实「内核没接住这次 load」——主假设成立。 */
static void nav_after_trace(const MBKCALL* c)
{
    unsigned wv        = (unsigned)c->a[0];
    unsigned loading   = 0;
    unsigned completed = 0;
    unsigned failed    = 0;
    int      onOwner   = (mbk_owner_tid() != 0 && mbk_owner_tid() == GetCurrentThreadId());

    if (wv && onOwner && mbk_can_kernel()) {
        loading   = mbk_kernel_get_u32(MBK_IDX_wkeIsLoading,          wv);
        completed = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingCompleted, wv);
        failed    = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingFailed,    wv);
    }
    (void)loading; (void)completed; (void)failed;   /* 原诊断日志已删：仅保留 getter 读序 */
}

/* ==========================================================================
 * ★ 2026-10-11：首次导航的「延迟复查」+ 可选「自愈重发」
 *
 * 目的（定性白屏）：原「导航返回瞬间的一次采样」只看瞬时状态；白屏轮读到的
 *   loading=1/completed=0/failed=0 只说明「当时还没完」，不能区分——
 *     (A) completed 永远 0  ⇒ 导航真的永不完成（内核任务丢了）；
 *     (B) 几秒后 completed=1 ⇒ 载入其实完成，白是渲染/呈现层。
 *   故补 t≈1000/3000/6000ms 三次复查。
 *
 * ★ 定时机制（关键约束）：**属主直通模式下没有常驻线程 K**（mbk_kernel_tid()==0）。
 *   因此**绝不能**用 CreateTimerQueueTimer/独立定时线程去 marshal —— mbk_kernel_run
 *   在非属主线程上会把任务投到**不存在的 K** 上，等满 60s 超时且**回调根本不执行**
 *   （见 mbk_thread.c）。所以采样改由**后续 mbk_marshal_exec 入口**驱动：这些入口
 *   天然发生在属主线程（插件会自己轮询 wkeIsLoading），既保证「属主线程执行」，
 *   又零新增线程/定时器。属主直通无 K ⇒ 这是唯一安全且最省的定时方式。
 *
 * 自愈重发：环境变量 MBKIT_NAV_RETRY_MS>0 时启用；到点仍 completed==0&&failed==0
 *   就用**原样参数**重发同一条导航，同一 view 最多 3 次；完成即停。默认关闭（A/B）。
 * ========================================================================*/
#define MBK_NAV_MAXV    4                       /* 最多跟踪 4 个 view 的首次导航 */
#define MBK_NAV_SAMPLES 3                       /* 每个 view 最多复查 3 次 */
#define MBK_NAV_KICKS   40                      /* 每个 view 最多踢动 40 次 */
#define MBK_NAV_PATHW   300                     /* 留存的宽路径长度（含 0） */
static const DWORD kNvSampleMs[MBK_NAV_SAMPLES] = { 1000, 3000, 6000 };

typedef struct {
    volatile LONG active;                       /* 1=仍需复查（仅属主线程读写） */
    volatile LONG kickOn;                       /* 1=仍需踢动（仅属主线程读写） */
    int           used;                         /* 该槽是否被占用（粘性，保「首次」语义） */
    unsigned      wv;                           /* webview 指针（低 32 位） */
    int           idx;                          /* 原导航导出 idx */
    long long     a[12];                        /* 原样参数（供复查定位 + 自愈重发） */
    DWORD         tick;                         /* 原导航返回时刻（GetTickCount） */
    int           shots;                        /* 已复查次数 0..3 */
    int           retry;                        /* 已重发次数 0..3 */
    int           kickNext;                     /* 下一次踢动序号 0..40 */
    int           hasPath;                      /* 1 = pathW 有效（wkeLoadFileW 才有） */
    int           delivered;                    /* 1 = 已做过一次内容投递 */
    wchar_t       pathW[MBK_NAV_PATHW];         /* 原始宽路径（导航返回瞬间留存） */
} MBKNAV;

static MBKNAV        g_nv[MBK_NAV_MAXV];
static volatile LONG g_nvAny     = 0;           /* 非 0 = 表里尚有「复查」项（快检） */
static volatile LONG g_nvKickAny = 0;           /* 非 0 = 表里尚有「踢动」项（快检） */

/* MBKIT_NAV_PREREAD（默认 "1"=开）：wkeLoadFileW 转发前先由 MbKit 自己读一遍文件。 */
static volatile LONG g_nvPreread = -1;
static int nv_preread_on(void)
{
    LONG v = g_nvPreread;
    if (v < 0) {
        char buf[16] = {0};
        LONG on = 1;
        if (GetEnvironmentVariableA("MBKIT_NAV_PREREAD", buf, sizeof(buf)))
            on = (buf[0] == '0') ? 0 : 1;
        InterlockedCompareExchange(&g_nvPreread, on, -1);
        v = g_nvPreread;
    }
    return v == 1;
}

/* MBKIT_NAV_HTML_MS（默认 600，0=关）：复查到点仍卡住则改由 MbKit 投递内容。 */
static volatile LONG g_nvHtmlMs = -1;
static int nv_html_ms(void)
{
    LONG v = g_nvHtmlMs;
    if (v < 0) {
        char buf[16] = {0};
        LONG ms = 600;
        if (GetEnvironmentVariableA("MBKIT_NAV_HTML_MS", buf, sizeof(buf))) {
            ms = (LONG)atoi(buf);
            if (ms < 0) ms = 0;
        }
        InterlockedCompareExchange(&g_nvHtmlMs, ms, -1);
        v = g_nvHtmlMs;
    }
    return (int)v;
}

/* MBKIT_NAV_HTML（默认 "1"=开；置 "0" 完全关闭内容投递，用于 A/B）。 */
static volatile LONG g_nvHtmlOn = -1;
static int nv_html_on(void)
{
    LONG v = g_nvHtmlOn;
    if (v < 0) {
        char buf[16] = {0};
        LONG on = 1;
        if (GetEnvironmentVariableA("MBKIT_NAV_HTML", buf, sizeof(buf)))
            on = (buf[0] == '0') ? 0 : 1;
        InterlockedCompareExchange(&g_nvHtmlOn, on, -1);
        v = g_nvHtmlOn;
    }
    return v == 1;
}

/* 在**属主线程**上唤醒内核一次（wkeWake(wv)，void）。调用方保证 onOwner && can_kernel。 */
static void nv_wake_view(unsigned wv)
{
    MBKCALL q;
    if (!wv) return;
    ZeroMemory(&q, sizeof(q));
    q.idx   = MBK_IDX_wkeWake;
    q.tramp = mbk_tramps[MBK_IDX_wkeWake];
    q.kind  = 0;                 /* void 返回（gen_thunk.c 同 kind） */
    q.a[0]  = (long long)wv;
    if (!q.tramp) return;
    mbk_kernel_run(marshal_trampoline, &q);
}

/* 导航返回后立刻唤醒一次（env MBKIT_NAV_WAKE）。仅属主线程调用。 */
static void nav_wake_once(const MBKCALL* c)
{
    unsigned wv = (unsigned)c->a[0];
    nv_wake_view(wv);
}

/* 反复踢动：对「首次导航」的 view，从导航返回起每 kickMs 毫秒在属主线程上唤醒一次
 * （最多 MBK_NAV_KICKS 次），completed||failed 非 0 即停。定时同样由后续编组入口驱动
 * （属主直通无 K，见上）；每次入口至多踢一次，避免突发。 */
static void nav_kick_tick(void)
{
    DWORD now    = GetTickCount();
    int   kickMs = 0;                     /* MBKIT_NAV_KICK_MS 解析器已删：恒为 0（关闭） */
    int   i, anyLeft = 0;

    if (kickMs <= 0) { InterlockedExchange(&g_nvKickAny, 0); return; }

    for (i = 0; i < MBK_NAV_MAXV; i++) {
        MBKNAV*  n = &g_nv[i];
        DWORD    due;
        unsigned loading, completed, failed;

        if (!n->kickOn) continue;
        if (n->kickNext >= MBK_NAV_KICKS) { n->kickOn = 0; continue; }
        due = (DWORD)(n->kickNext + 1) * (DWORD)kickMs;
        if (now - n->tick < due) { anyLeft = 1; continue; }   /* 未到下一次踢动 */

        nv_wake_view(n->wv);                                  /* 属主线程：先踢 */
        loading = completed = failed = 0;
        if (n->wv) {
            loading   = mbk_kernel_get_u32(MBK_IDX_wkeIsLoading,          n->wv);
            completed = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingCompleted, n->wv);
            failed    = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingFailed,    n->wv);
        }
        n->kickNext++;
        (void)loading; (void)completed; (void)failed;   /* 原诊断日志已删 */
        if (completed || failed || n->kickNext >= MBK_NAV_KICKS) n->kickOn = 0;
        if (n->kickOn) anyLeft = 1;
    }
    InterlockedExchange(&g_nvKickAny, anyLeft ? 1 : 0);
}

/* ==========================================================================
 * ★ 2026-10-11：白屏根因修复（写读竞争）——预读 + 内容投递
 *
 * 根因（决定性实验）：插件「写到文件(示例网页) → 1ms 后 载入文件(同一路径)」。
 *   写入这个动作破坏了内核对该 file:// 的读取：内核收下导航（loading=1）但任务
 *   永不执行（completed/failed 永远 0）；且重发同一条 wkeLoadFileW 无效
 *   ⇒ 内核疑似按 URL 记住「加载中」，必须换投递方式。
 *   修法两步：
 *     ① 预读：转发前先由 MbKit 用「全文共享」打开并读完全部字节，把「刚写完的
 *        文件第一次被读」消耗在 MbKit 侧；
 *     ② 内容投递：复查到点仍卡住 ⇒ 再读一遍文件，不经内核文件加载器，
 *        直接 wkeLoadHtmlWithBaseUrl(wv, 字节内容, file:///目录) 交给内核。
 * ========================================================================*/

/* 读整个文件（全文共享打开，不与「刚写完」的写入者互斥）。
 * 返回 0=成功（*outBuf 需调用方 HeapFree，*outLen=字节数）；1=打不开；2=读失败。
 * 共试 3 次（首试 + 重试 2 次），失败间隔 30ms。 */
static int nv_read_file_all(const wchar_t* path, unsigned char** outBuf, unsigned* outLen)
{
    int attempt;

    if (outBuf) *outBuf = NULL;
    if (outLen) *outLen = 0;
    if (!path || !path[0]) return 1;

    for (attempt = 0; attempt < 3; attempt++) {
        HANDLE h = CreateFileW(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               NULL, OPEN_EXISTING, 0, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (attempt < 2) { Sleep(30); continue; }
            return 1;
        }
        {
            DWORD          size = GetFileSize(h, NULL);
            unsigned char* buf;
            unsigned       done = 0;

            if (size == INVALID_FILE_SIZE || size == 0) { CloseHandle(h); return 2; }
            buf = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size + 1);
            if (!buf) { CloseHandle(h); return 2; }
            while (done < (unsigned)size) {
                DWORD got = 0;
                if (!ReadFile(h, buf + done, size - done, &got, NULL) || got == 0) break;
                done += got;
            }
            CloseHandle(h);
            if (done != (unsigned)size) {
                HeapFree(GetProcessHeap(), 0, buf);
                return 2;
            }
            buf[done] = 0;
            if (outBuf) *outBuf = buf; else HeapFree(GetProcessHeap(), 0, buf);
            if (outLen) *outLen = (unsigned)done;
            return 0;
        }
    }
    return 1;
}

/* 由宽路径取目录并构造 file:/// URL（宽字符，反斜杠归一为正斜杠）。返回字符数。 */
static int nv_base_url_wide(const wchar_t* pathW, wchar_t* outW, int capW)
{
    static const wchar_t kPre[] = L"file:///";
    int n = 0, m, p = 0, i;

    if (!pathW || !outW || capW < 16) return 0;
    outW[0] = 0;
    while (pathW[n]) n++;
    m = n;
    while (m > 0 && pathW[m - 1] != L'\\' && pathW[m - 1] != L'/') m--;
    if (m <= 0) return 0;

    for (i = 0; kPre[i] && p < capW - 1; i++, p++) outW[p] = kPre[i];
    for (i = 0; i < m && p < capW - 1; i++, p++)
        outW[p] = (pathW[i] == L'\\') ? L'/' : pathW[i];
    outW[p] = 0;
    return p;
}

/* 内容投递：不经内核文件加载器，把文件字节用 wkeLoadHtmlWithBaseUrl 直接交给内核。
 * ★ wkeLoadHtmlWithBaseUrl 真实签名（gen_fwd.h idx143 / fwd_list.json）：3 参 cdecl、
 *   (wkeWebView, const utf8* html, const utf8* baseUrl) —— 两串都是 char*（utf8）。
 *   故 html 传文件原始字节，base 由宽 URL 转 UTF-8。仅属主线程调用。 */
static void nav_deliver_html(MBKNAV* n, int elapsedMs)
{
    unsigned char* html   = NULL;
    unsigned       hlen   = 0;
    int            rc;
    wchar_t        baseW[600];
    char           base8[2400];
    MBKCALL        q;
    unsigned       loading = 0, completed = 0, failed = 0;

    if (!n->wv || !n->hasPath) return;

    rc = nv_read_file_all(n->pathW, &html, &hlen);
    if (rc != 0 || !html) {
        return;                                  /* 原诊断日志已删：读不到放弃投递 */
    }

    baseW[0] = 0;
    base8[0] = 0;
    if (nv_base_url_wide(n->pathW, baseW, 600) > 0)
        WideCharToMultiByte(CP_UTF8, 0, baseW, -1, base8, sizeof(base8), NULL, NULL);

    (void)hlen;                                  /* 原诊断日志已删 */

    ZeroMemory(&q, sizeof(q));
    q.idx   = MBK_IDX_wkeLoadHtmlWithBaseUrl;
    q.tramp = mbk_tramps[MBK_IDX_wkeLoadHtmlWithBaseUrl];
    q.kind  = 0;                                 /* void 返回 */
    q.a[0]  = (long long)n->wv;
    q.a[1]  = (long long)(UINT_PTR)html;
    q.a[2]  = (long long)(UINT_PTR)base8;
    if (q.tramp) mbk_kernel_run(marshal_trampoline, &q);
    HeapFree(GetProcessHeap(), 0, html);

    loading   = mbk_kernel_get_u32(MBK_IDX_wkeIsLoading,          n->wv);
    completed = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingCompleted, n->wv);
    failed    = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingFailed,    n->wv);
    (void)loading; (void)completed; (void)failed;   /* 原诊断日志已删：保留 getter 读序 */
    (void)elapsedMs;
}

/* 登记「该 view 的首次导航」。同一 view 只登记一次（used 粘性）。
 * 仅属主线程调用（与复查同线程 ⇒ 无需加锁）。 */
static void nav_track_begin(const MBKCALL* c)
{
    unsigned wv = (unsigned)c->a[0];
    int      i, slot = -1;

    if (!wv) return;
    for (i = 0; i < MBK_NAV_MAXV; i++) {
        if (g_nv[i].used && g_nv[i].wv == wv) return;    /* 该 view 已登记过首次 */
        if (!g_nv[i].used && slot < 0) slot = i;
    }
    if (slot < 0) return;                                /* 表满：放弃（不刷屏） */

    {
        MBKNAV* n = &g_nv[slot];
        int     k;
        for (k = 0; k < 12; k++) n->a[k] = c->a[k];
        n->wv       = wv;
        n->idx      = c->idx;
        n->tick     = GetTickCount();
        n->shots    = 0;
        n->retry    = 0;
        n->kickNext = 0;
        n->kickOn   = 0;      /* MBKIT_NAV_KICK_MS 解析器已删：恒为 0（关闭） */
        n->used     = 1;
        n->active   = 1;
        n->delivered = 0;
        /* ★ 留存原始宽路径（此刻调用方缓冲仍存活），供内容投递用。
         *   仅 wkeLoadFileW(idx140)：a[1] = wchar_t* 路径。 */
        n->hasPath = 0;
        n->pathW[0] = 0;
        if (c->idx == MBK_IDX_wkeLoadFileW && c->a[1] != 0) {
            const wchar_t* src = (const wchar_t*)(UINT_PTR)(unsigned)c->a[1];
            int i = 0;
            while (i < MBK_NAV_PATHW - 1 && src && src[i]) { n->pathW[i] = src[i]; i++; }
            n->pathW[i] = 0;
            n->hasPath = (i > 0) ? 1 : 0;
        }
        InterlockedExchange(&g_nvAny, 1);
        if (n->kickOn) InterlockedExchange(&g_nvKickAny, 1);
    }
}

/* 延迟复查：仅在属主线程、由后续 mbk_marshal_exec 入口调用。到点即回读三条 getter
 * （同一条编组路径）并落诊断；满足条件时按 env 设置自愈重发。 */
static void nav_timed_recheck(void)
{
    DWORD now = GetTickCount();
    int   i, anyLeft = 0;

    for (i = 0; i < MBK_NAV_MAXV; i++) {
        MBKNAV*  n = &g_nv[i];
        DWORD    elapsed, due;
        unsigned loading, completed, failed;

        if (!n->active) continue;
        if (n->shots >= MBK_NAV_SAMPLES) { n->active = 0; continue; }

        elapsed = now - n->tick;
        due     = kNvSampleMs[n->shots];
        if (elapsed < due) { anyLeft = 1; continue; }     /* 未到下一个采样点 */

        loading = completed = failed = 0;
        if (n->wv) {
            loading   = mbk_kernel_get_u32(MBK_IDX_wkeIsLoading,          n->wv);
            completed = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingCompleted, n->wv);
            failed    = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingFailed,    n->wv);
        }
        (void)loading;                                /* 原诊断日志已删 */
        n->shots++;

        /* ★ 内容投递（优先于重发，2026-10-11）：到点仍 completed==0&&failed==0 ⇒ 内核
         *   按 URL 记住「加载中」，重发同一条 wkeLoadFileW 无效 —— 改由 MbKit 自己读
         *   文件、不经内核文件加载器，用 wkeLoadHtmlWithBaseUrl 直接投递内容。
         *   同一 view 最多投递 1 次；投递后由后续的定时复查采样继续观察结果。 */
        if (nv_html_on() && nv_html_ms() > 0 && !n->delivered &&
            completed == 0 && failed == 0 && (int)elapsed >= nv_html_ms()) {
            n->delivered = 1;
            nav_deliver_html(n, (int)elapsed);
        }
        /* 自愈重发：MBKIT_NAV_RETRY_MS 解析器已删 → 恒不启用（分支保留为死代码）。 */
        else if (0 && completed == 0 && failed == 0 && n->retry < 3 &&
                 (int)elapsed >= 0) {
            MBKCALL  again;
            int      k;
            unsigned l2 = 0, c2 = 0, f2 = 0;

            ZeroMemory(&again, sizeof(again));
            again.idx   = n->idx;
            again.tramp = mbk_tramps[n->idx];
            again.kind  = 0;                              /* 导航类均为 void 返回 */
            for (k = 0; k < 12; k++) again.a[k] = n->a[k];

            n->retry++;
            mbk_kernel_run(marshal_trampoline, &again);

            if (n->wv) {
                l2 = mbk_kernel_get_u32(MBK_IDX_wkeIsLoading,          n->wv);
                c2 = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingCompleted, n->wv);
                f2 = mbk_kernel_get_u32(MBK_IDX_wkeIsLoadingFailed,    n->wv);
            }
            (void)l2; (void)c2; (void)f2;                 /* 原诊断日志已删 */
            if (c2 || f2) n->active = 0;                  /* 已了结即停 */
        }

        if (completed || failed) n->active = 0;           /* 已了结，停止复查 */
        if (n->shots >= MBK_NAV_SAMPLES) n->active = 0;   /* 每 view 最多 3 条 */
        if (n->active) anyLeft = 1;
    }
    InterlockedExchange(&g_nvAny, anyLeft ? 1 : 0);
}

/* 推迟重放队列：容量 32 的静态环形缓冲；仅在「兼容模式跨线程」的不可用分支里写入，
 * 在本 TU 静态锁（Interlocked 自旋）保护下操作。 */
#define MBK_DEFER_CAP 32
typedef struct {
    int       idx;
    int       kind;
    long long a[12];
} MBKDEFER;

static MBKDEFER      g_defer[MBK_DEFER_CAP];
static volatile LONG g_deferHead    = 0;   /* 生产位（写） */
static volatile LONG g_deferTail    = 0;   /* 消费位（读） */
static volatile LONG g_deferLock    = 0;   /* Interlocked 自旋锁 */
static volatile LONG g_deferPending = 0;   /* 非 0 = 队列里有待重放项 */
static volatile LONG g_deferDrops   = 0;   /* 队列满而丢弃的导航调用数（诊断） */
static volatile LONG g_replaying    = 0;   /* 重放进行中（防嵌套重放） */

static void mbk_defer_lock(void)
{
    int spin = 0;
    while (InterlockedCompareExchange(&g_deferLock, 1, 0) != 0) {
        if (++spin > 4000) { Sleep(0); spin = 0; }
    }
}
static void mbk_defer_unlock(void) { InterlockedExchange(&g_deferLock, 0); }

/* 入队（满则丢弃并计数）。返回 1=已入队。 */
static int mbk_defer_push(const MBKCALL* c)
{
    int ok = 1;
    mbk_defer_lock();
    if ((g_deferHead + 1) % MBK_DEFER_CAP == g_deferTail) {
        ok = 0;
        InterlockedIncrement(&g_deferDrops);
    } else {
        MBKDEFER* d = &g_defer[g_deferHead];
        int i;
        d->idx  = c->idx;
        d->kind = c->kind;
        for (i = 0; i < 12; i++) d->a[i] = c->a[i];
        g_deferHead = (g_deferHead + 1) % MBK_DEFER_CAP;
        InterlockedExchange(&g_deferPending, 1);
    }
    mbk_defer_unlock();
    /* 原诊断日志已删（推迟入队 成功/失败 各一条） */
    return ok;
}

/* 出队到 out。返回 1=取到；队列转空时清 pending。 */
static int mbk_defer_pop(MBKCALL* out)
{
    int got = 0;
    mbk_defer_lock();
    if (g_deferTail != g_deferHead) {
        MBKDEFER* d = &g_defer[g_deferTail];
        int i;
        out->idx  = d->idx;
        out->kind = d->kind;
        for (i = 0; i < 12; i++) out->a[i] = d->a[i];
        g_deferTail = (g_deferTail + 1) % MBK_DEFER_CAP;
        got = 1;
        if (g_deferTail == g_deferHead) InterlockedExchange(&g_deferPending, 0);
    }
    mbk_defer_unlock();
    return got;
}

/* 按序重放：仅在「内核可用」时调用（mbk_marshal_exec 入口处）。构造临时 MBKCALL
 * 走同一条正常执行路径（mbk_kernel_run → marshal_trampoline）；重放失败（载荷非法
 * 或内核缺该导出）落诊断。中途内核又不可用则停下，剩余项留给下一轮。 */
static void mbk_replay_deferred(void)
{
    MBKCALL tmp;
    int     replayed = 0;
    /* 防重入：重放某个导航调用时，内核可能同步回调业务函数，业务再经 mbk_marshal_exec
     * 进来 —— 用一个标志挡掉嵌套重放，避免无界递归（剩余项留给下一轮）。 */
    if (InterlockedCompareExchange(&g_replaying, 1, 0) != 0) {
        return;                                       /* 原诊断日志已删（跳过） */
    }
    for (;;) {
        if (!g_deferPending) break;
        if (mbk_owner_tid() == 0 || !mbk_can_kernel()) break;
        ZeroMemory(&tmp, sizeof(tmp));
        if (!mbk_defer_pop(&tmp)) break;
        if (tmp.idx < 0 || tmp.idx >= MBK_ALL_COUNT ||
            (tmp.tramp = mbk_tramps[tmp.idx]) == NULL) {
            continue;                                 /* 原诊断日志已删（载荷非法） */
        }
        if (!mbk_slots[tmp.idx] || mbk_slots[tmp.idx] == (void*)&mbk_missing_stub) {
            continue;                                 /* 原诊断日志已删（内核缺导出） */
        }
        mbk_kernel_run(marshal_trampoline, &tmp);
        replayed++;
    }
    (void)replayed;                                   /* 原诊断日志已删 */
    InterlockedExchange(&g_replaying, 0);
}

void mbk_marshal_exec(MBKCALL* c)
{
    InterlockedIncrement(&g_marshalTotal);

    if (!c || c->idx < 0 || c->idx >= MBK_ALL_COUNT || !c->tramp) {
        InterlockedIncrement(&g_marshalFail);
        if (c) c->ret.u = 0;
        return;
    }

    /* 本调用是否为导航类（热路径分类，原样保留；仅其追踪日志已删） */
    int nav_cls = is_navigation_class(mbk_fwd_names[c->idx]);
    (void)nav_cls;

    /* ★ 任务5（2026-10-10）：view 销毁「延迟回收」—— 把 wkeDestroyWebView 移出插件
     *   卸载窗口（同步销毁 → Chromium 拆线程抢加载器锁 → 与 LdrUnloadDll / LoadLibraryA
     *   三方死锁）。改为登记到延迟队列，由属主线程（或 K 模式回收线程）在 ~400ms 后回收。
     *   期间同一 view 又被绑定/使用（下一次内核调用以该 view 为首参）则取消回收。
     *   队列满 / 回收线程起不来 → mbk_defer_destroy 返回 0，落回同步执行（旧行为）。 */
    if (c->idx == MBK_IDX_wkeDestroyWebView) {
        if (mbk_defer_destroy((void*)(UINT_PTR)(unsigned)c->a[0])) { c->ret.u = 0; return; }
    } else if (mbk_destroy_pending()) {
        void* wv = (void*)(UINT_PTR)(unsigned)c->a[0];
        if (wv) mbk_cancel_destroy(wv);      /* 复用取消：该 view 又被使用了 */
    }

    /* ★ 新增（白屏修复）：上一轮内核不可用时被推迟的导航调用，见本轮内核已可用则
     *   按序重放。队列为空时仅一次 volatile 读（O(1) 快检），就绪路径不受影响。 */
    if (g_deferPending && mbk_owner_tid() != 0 && mbk_can_kernel())
        mbk_replay_deferred();

    /* ★ 2026-10-11：首次导航的延迟复查（t≈1/3/6s）与（可选）反复踢动。**只在属主线程**、
     *   由后续编组入口驱动（属主直通无 K，定时器线程 marshal 会投到不存在的 K 上等 60s
     *   且不执行，故不用定时器/后台线程）。两个 any 都是快检，未登记时热路径零开销。 */
    if (mbk_owner_tid() != 0 && mbk_owner_tid() == GetCurrentThreadId() && mbk_can_kernel()) {
        if (g_nvAny)     nav_timed_recheck();
        if (g_nvKickAny) nav_kick_tick();
    }

    /* ★ 2026-10-01 v1.4 重做「首次调用」的时序（关键）：
     *
     * 旧行为（v1.3）：把内核启动**投递**到常驻线程 K 上，调用线程同步等 K 完成
     *   LoadLibrary。这样虽然绕开了「调用方持着加载锁时、别的线程 LoadLibrary
     *   会死锁」的老事故，但把内核主线程钉在了 K 上 —— 实测**页面永远不加载、
     *   画面空白**（miniblink 要求内核主线程 == 宿主窗口线程 == 有消息循环的线程）。
     *
     * 新行为（v1.4）：
     *   ① 默认（属主直通）：内核就在**当前调用线程**上同步建起来（LoadLibrary +
     *      wkeInitialize）。本线程自己 LoadLibrary 用的是递归加载锁，不会死锁；
     *      而且真实宿主进程里 node.dll 通常已被宿主自己的 webview 加载过，
     *      这里只是引用计数 +1（实测 0~1 ms）。随后所有内核调用在该线程上
     *      **就地执行**（mbk_kernel_run 直通），渲染/消息循环全都对得上。
     *   ② MBKIT_K=1（旧 K 模式，离屏/无窗口场景的逃生通道）：初始化类导出只投递
     *      启动、立即返回；其它导出等到 K 起来再编组执行。 */
    if (mbk_owner_tid() == 0 /* 内核尚未就绪：先把内核建起来 */) {
        /* ★ v1.4.1（2026-10-02）：初始化类导出**只登记**，其它导出才真把内核
         *   建在**当前调用线程**上。这样「在 DllMain（易语言 `_启动子程序`）里调
         *   WKE初始化 ()」是安全的（不 LoadLibrary、不 wkeInitialize），而真正
         *   需要内核的调用（绑定/载入/建视图）恰好发生在启用回调或窗口线程上，
         *   那正是 miniblink 要求的内核属主线程。
         *   （旧 v1.4 把两类混在一起，导致 DllMain 首次调用同步 LoadLibrary
         *    node.dll → 加载锁互等 → 宿主卡死，实测 300s 不返回。） */
        if (is_init_class(mbk_fwd_names[c->idx])) {
            MbKit_EnsureKernelInit();          /* 投递/登记，立即返回 */
            if (c->kind == 0) c->ret.u = 0;
            return;
        }
        if (!mbk_k_mode_wanted()) {
            /* ★ 修订（2026-10-10）：属主直通模式下 MbKit_WaitKernel 等的是 **K 模式**
             *   的异步启动事件(g_bootDone，由 k_boot_task 置位)；而属主直通模式压根
             *   没有 k_boot_task —— 盲等 20s 必然满等，把「白屏」恶化成「每次内核调用
             *   卡 20 秒」。改为：重试 MbKit_EnsureOwner 最多 3 次、每次间隔 ~50ms，
             *   间隔里抽取并派发本线程消息（复用 mbk_wait_pump，不新造 Sleep 轮询）。
             *   3 次仍失败 → 导航类入队重放、其它类返回 0，并落诊断。 */
            int   rc = MBKIT_OK;
            int   attempt;
            for (attempt = 0; attempt < 3; attempt++) {
                rc = MbKit_EnsureOwner();
                if (rc == MBKIT_OK) break;
                if (attempt < 2) mbk_pump_pause(50);
            }
            if (rc != MBKIT_OK) {
                int   nav = is_navigation_class(mbk_fwd_names[c->idx]);
                DWORD gle = GetLastError();
                if (nav && mbk_defer_push(c)) {
                    /* 原诊断日志已删（导航调用已推迟重放） */
                } else {
                    InterlockedIncrement(&g_marshalFail);
                    /* 原诊断日志已删（丢弃调用） */
                }
                (void)gle;
                c->ret.u = 0;
                return;
            }
        } else {
            if (MbKit_WaitKernel(0) != MBKIT_OK) {
                InterlockedIncrement(&g_marshalFail);
                c->ret.u = 0;
                return;
            }
        }
    }

    mbk_ensure();        /* 槽表：此时内核已在 K 上加载完毕，这里只是取表（幂等） */

    /* 兼容模式（内核被某业务线程裸初始化绑死、无法迁移）且当前是别的线程：
     * 明确失败 —— 返回 0，绝不跨线程硬闯（硬约束：宁可降级不可崩宿主）。 */
    if (!mbk_can_kernel()) {
        /* ★ 新增（白屏修复）：兼容模式跨线程时，导航类调用不再静默丢弃 —— 压进
         *   静态环形队列，待内核可用时在下一次编组入口按序重放；非导航类保持原样。 */
        if (is_navigation_class(mbk_fwd_names[c->idx])) {
            if (!mbk_defer_push(c)) {
                /* 原诊断日志已删（推迟队列已满，丢弃） */
            } else {
                /* 原诊断日志已删（已入队待重放） */
            }
            c->ret.u = 0;
            return;
        }
        InterlockedIncrement(&g_marshalFail);
        c->ret.u = 0;
        return;
    }

    /* 统计「真正跨线程编组」的次数。
     * ★ v1.4.1：属主直通模式下**没有**常驻线程 K（mbk_kernel_tid()==0），此时
     *   所有调用都就地执行，不该记成跨线程 —— 否则自检里会出现「共 N 次（跨线程 N）」
     *   这种看着像编组的假象。 */
    if (mbk_kernel_tid() != 0 && GetCurrentThreadId() != mbk_kernel_tid())
        InterlockedIncrement(&g_marshalCross);

    /* 属主线程上调用 → 就地执行（回调重入安全）；否则投递到 K 同步等待 */

    /* ★ 2026-10-11（白屏根因=写读竞争）：wkeLoadFileW 转发前先由 MbKit 自己读一遍
     *   文件（全文共享打开），把「刚写完的文件第一次被读」消耗在 MbKit 侧。
     *   读不到不阻断 —— 照常按原路径转发。env MBKIT_NAV_PREREAD 默认开。 */
    if (c->idx == MBK_IDX_wkeLoadFileW && nv_preread_on()) {
        const wchar_t* p  = (const wchar_t*)(UINT_PTR)(unsigned)c->a[1];
        unsigned char* b  = NULL;
        unsigned       bl = 0;
        int            rc = nv_read_file_all(p, &b, &bl);
        (void)rc; (void)bl;                           /* 原诊断日志已删 */
        if (b) HeapFree(GetProcessHeap(), 0, b);
    }

    mbk_kernel_run(marshal_trampoline, c);
    if (nav_cls) {
        int onOwner = (mbk_owner_tid() != 0 && mbk_owner_tid() == GetCurrentThreadId());
        nav_after_trace(c);                              /* 原诊断采样：保留 getter 读序，日志已删 */
        if (onOwner && 0) nav_wake_once(c);              /* MBKIT_NAV_WAKE 解析器已删：恒不启用 */
        if (onOwner) nav_track_begin(c);                 /* 登记首次导航：复查 + 踢动 */
    }

    /* ★ 任务5：本次调用已在属主线程上跑完（can_kernel 已通过），顺带回收已到期的
     *   待销毁 view —— 保证 wkeDestroyWebView 的延迟回收由属主线程执行、且在卸载窗口外。 */
    if (mbk_destroy_pending())
        mbk_flush_deferred_destroy();
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
 * 「自定义内核位置」接口（2026-09-24 新增；2026-10-01 改为**幂等**）
 *
 * 供给易语言侧的 `WKE设置node路径` 使用。设计要点：
 *   · **内核尚未加载**：记下配置，返回 1。本函数刻意**不触发**内核加载
 *     （设置路径通常是宿主启动早期做的第一件事，此刻不该把 33 MB 的内核拖起来）。
 *   · **内核已经就绪**：同一条路径（或留空 = 用默认位置）视为**幂等成功，返回 1**；
 *     只有「想换一份别的内核」才返回 0。
 *
 * ★ 为什么幂等是必须的（2026-10-01 实测事故）：
 *   易语言侧的调用方把本命令的返回值当**门闩**用：
 *       .判断开始 (WKE设置node路径 (路径, 失败原因))
 *           mb.绑定 (...) → mb.载入 (...)
 *       .默认
 *           输出日志 (“node设置失败：” ＋ 失败原因)
 *   插件被**重复启用**（框架重启适配器 / 热重载 / 再点一次启用）时，第二次调用
 *   若返回 0，业务就整段跳过绑定与载入 —— 用户看到的是「窗口空白、点什么都没反应」。
 *   旧的内存载入版对同一路径就是幂等的，本函数必须保持同一契约。
 * ========================================================================*/
int mbk_set_node_path(const char* p)
{
    char req[MAX_PATH * 2];
    const char* cur;
    size_t n;

    if (!p) p = "";
    copy_str(req, sizeof(req), p);
    trim_inplace(req);

    if (g_state == 0) {                  /* 尚未加载：记下配置即可 */
        copy_str(g_cfgPath, sizeof(g_cfgPath), req);
        g_cfgSet = (g_cfgPath[0] != 0);
        return 1;
    }
    if (g_state != 1) return 0;          /* 正在加载 / 已失败：不接受改路径 */

    if (req[0] == 0) return 1;           /* 留空 = 默认位置；内核已在 → 满足 */

    /* 目录 → 补 node.dll（与加载器的解析规则一致） */
    n = strlen(req);
    while (n && (req[n - 1] == '\\' || req[n - 1] == '/')) req[--n] = 0;
    {
        DWORD a = GetFileAttributesA(req);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && n + 10 < sizeof(req)) {
            char full[MAX_PATH * 2];
            _snprintf(full, sizeof(full) - 1, "%s\\node.dll", req);
            full[sizeof(full) - 1] = 0;
            copy_str(req, sizeof(req), full);
        }
    }

    cur = mbk_current_kernel_path();
    if (cur && cur[0] && lstrcmpiA(req, cur) == 0) return 1;   /* 同路径：幂等成功 */
    return 0;                                                  /* 真想换内核：拒绝 */
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

/* 内核原始状态字：0=尚未加载 1=已就绪 2=已失败 0x7FFFFFFF=正在加载。
 * ★ 2026-09-25：给易语言 `WKE设置node路径` 的「失败原因」参数用 ——
 *   g_state≠0 时 SetNodePath 会被拒绝，但「已就绪常驻」（重载 DLL 后的正常现象）
 *   和「上次加载失败被卡死」是两种完全不同的问题，必须能区分。 */
int __stdcall MbKit_KernelState(void)
{
    return (int)g_state;
}

/* 一行式自检：把「为什么内核没进来 / 从哪儿进来的」一次说清。
 * 返回静态缓冲，调用方不要释放、不要长期持有。 */
const char* __stdcall MbKit_Diag(void)
{
    static char buf[2000];
    char exeDir[MAX_PATH], cwd[MAX_PATH];
    char bootLine[256];
    char destroyLine[256];
    const char* stateTxt =
        (g_state == 1) ? "已加载" :
        (g_state == 2) ? "加载失败" :
        (g_state == 0x7FFFFFFF) ? "正在加载" : "尚未加载";
    /* 内核属主模式：1=K 模式（编组） 0=属主直通（内核主线程=调用线程，可渲染） */
    DWORD ownerTid = mbk_owner_tid();
    const char* modeTxt = ownerTid
        ? (mbk_owner_mode() ? "K 模式（编组；页面渲染不了，仅离屏可用）"
                            : "属主直通（内核主线程=调用线程，可渲染）")
        : (mbk_k_mode_wanted() ? "未初始化（MBKIT_K=1 将落到线程 K）"
                               : (MbKit_BootState() == 1
                                  ? "未初始化（已登记启动；首个内核调用时在调用线程上建起来）"
                                  : "未初始化"));

    exeDir[0] = cwd[0] = 0;
    host_exe_dir(exeDir);
    GetCurrentDirectoryA(MAX_PATH, cwd);

    {
        /* ★ 2026-10-01 新增：把「内核启动状态」与「编组等待期间抽了多少消息」
         *   报出来。这是判断「宿主是不是卡在内核启动 / 窗口消息互等」的直接指标：
         *     启动中 且 抽消息数持续增长 = 调用线程在边等边抽（宿主不死）
         *     等待超时 > 0                = 内核起不来（多半在 DllMain 里调了 miniblink）
         */
        int bs = MbKit_BootState();
        const char* bootTxt =
            (bs == 0) ? "未启动" : (bs == 1) ? "启动中(异步)" :
            (bs == 2) ? "已完成" : "失败";
        _snprintf(bootLine, sizeof(bootLine) - 1,
                  "  内核启动     : %s    等启动次数 %d   编组等待抽消息 %d 条   等待超时 %d 次\n",
                  bootTxt, MbKit_BootWaits(), MbKit_PumpCount(), MbKit_WaitTimeouts());
        bootLine[sizeof(bootLine) - 1] = 0;
    }

    {
        /* ★ 任务5：view 延迟回收队列计数 —— 观察「卸载窗口外回收」是否生效 */
        long dfr = 0, cxl = 0, rcl = 0;
        int  ql  = 0;
        mbk_destroy_stats(&dfr, &cxl, &rcl, &ql);
        _snprintf(destroyLine, sizeof(destroyLine) - 1,
                  "  view 延迟回收 : 待回收 %d 个   累计登记 %ld   复用取消 %ld   已回收 %ld 批\n",
                  ql, dfr, cxl, rcl);
        destroyLine[sizeof(destroyLine) - 1] = 0;
    }

    _snprintf(buf, sizeof(buf) - 1,
        "MbKit 自检\n"
        "  内核状态     : %s\n"
        "%s"
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
        "%s"
        "  说明         : 被挡 > 0 表示有回调因「业务身份判不出/已失效」被拦；\n"
        "                 正常应为 0。若持续增长，请把本行反馈给维护者。\n",
        stateTxt,
        bootLine,
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
        MbKit_CbCount(), MbKit_CbLive(), MbKit_CbHits(), MbKit_CbBlocked(),
        destroyLine);
    buf[sizeof(buf) - 1] = 0;
    return buf;
}
