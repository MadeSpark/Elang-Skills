/* ==========================================================================
 * mbkit_veh.c —— 崩溃探针（向量异常处理器）
 *
 * 为什么放在 MbKit 里：MbKit 在进程启动早期就自钉（PIN），**业务插件 DLL 被宿主
 * 卸载/重载时它一定还在**。所以「卸载之后有代码跳进已释放页面」这类只在卸载窗口
 * 里发生的崩溃，只有 MbKit 的处理器抓得住 —— 宿主自己的致命错误处理会直接把
 * 进程收掉（表现为 ExitCode=0、日志断在半句），什么都留不下。
 *
 * 抓取内容：
 *   ① 异常码 + 出错指令地址（EIP）→ 归属模块（或「不属于任何已加载模块」★）
 *   ② 访问违例的故障地址 + VirtualQuery 状态（MEM_FREE = 那段映像已被卸载 ★）
 *   ③ 调用栈（RtlCaptureStackBackTrace，逐帧标注所属模块）
 *   ④ 事件探测：业务模块当前是否还挂在加载器链表上（对照 ① ）
 *
 * 写文件用 WriteFile（不在处理器里做分配），抓到就返回 EXCEPTION_CONTINUE_SEARCH，
 * 让宿主自己的处理器照常处理 —— 探针只观测，不改变行为。
 * ========================================================================*/
#include <windows.h>
#include <stdio.h>
#include <intrin.h>          /* _ReturnAddress（钉住调用者模块用） */
#include <tlhelp32.h>        /* 崩溃瞬间的模块快照 */
#include "mbkit_api.h"

#ifndef _MSC_VER
#include <stdarg.h>
#endif

typedef USHORT (WINAPI *PFN_CAPTURE)(ULONG, ULONG, PVOID*, PULONG);
typedef PVOID  (WINAPI *PFN_ADDVEH)(ULONG, PVOID);
typedef ULONG  (WINAPI *PFN_REMOVEVEH)(PVOID);

static PVOID g_veh = NULL;
static char  g_vehLog[MAX_PATH] = {0};
static LONG  g_vehCount = 0;

/* 前向声明：veh_probe 在文件前半段，窗口枚举在后面 */
static BOOL CALLBACK enum_cb(HWND h, LPARAM lp);
static void dump_process_windows(void);
static void dump_modules(void* target);

/* 地址 → 「模块名+偏移」，或「不属于任何已加载模块」 */
static const char* addr_owner(void* addr, char* buf, int cap)
{
    HMODULE hm = NULL;
    char    full[MAX_PATH];
    const char* base;
    const char* p;

    if (!addr) { _snprintf(buf, cap, "(空地址)"); return buf; }
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)(UINT_PTR)addr, &hm) && hm) {
        if (GetModuleFileNameA(hm, full, MAX_PATH)) {
            base = full; p = full;
            while (*p) { if (*p == '\\') base = p + 1; p++; }
            _snprintf(buf, cap, "%s+0x%X", base,
                      (unsigned)((char*)addr - (char*)hm));
            return buf;
        }
    }
    _snprintf(buf, cap, "%p ★不属于任何已加载模块（代码页已被卸载/释放）", addr);
    return buf;
}

static void vlog(const char* fmt, ...)
{
    char    line[900];
    int     n;
    va_list ap;
    HANDLE  h;

    va_start(ap, fmt);
    n = _vsnprintf(line, sizeof(line) - 3, fmt, ap);
    va_end(ap);
    if (n < 0) n = (int)sizeof(line) - 3;
    line[n]     = '\r';
    line[n + 1] = '\n';
    line[n + 2] = 0;

    h = CreateFileA(g_vehLog, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD w = 0;
    WriteFile(h, line, (DWORD)(n + 2), &w, NULL);
    CloseHandle(h);
}

/* 该「业务模块」现在还在加载器链表里吗？（卸载窗口的核心判据） */
static void log_module_presence(const char* label, void* addr)
{
    HMODULE hm = NULL;
    char    b[200];
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                 | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(UINT_PTR)addr, &hm) && hm) {
        vlog("      %s：%s  ← 仍在加载器链表里", label, addr_owner(addr, b, sizeof(b)));
    } else {
        _snprintf(b, sizeof(b), "%p", addr);
        vlog("      %s：%s  ← ★已不在加载器链表（映像已被卸载）", label, b);
    }
}

/* 这段地址现在可读吗？（不抛异常的探法，避免在处理器里再崩一次） */
static int rd_ok(const void* p)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!p) return 0;
    if (VirtualQuery(p, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return 1;
}

/* ★ 关键：从**故障线程的原始栈**里找「是谁 call 进那段已卸载代码的」。
 *   VEH 是在故障线程上跑的，所以 RtlCaptureStackBackTrace 抓到的是处理器自己的栈；
 *   真正有用的是 ContextRecord 里的 Esp/Ebp —— 那才是「调用现场」。 */
static void dump_fault_stack(EXCEPTION_POINTERS* ep)
{
    char  n1[220];
    DWORD i;
    const DWORD* sp;

    if (!ep->ContextRecord) return;
    sp = (const DWORD*)(UINT_PTR)ep->ContextRecord->Esp;
    vlog("   故障现场栈（Esp=0x%08X，前 20 个 DWORD）：", (unsigned)(UINT_PTR)sp);
    for (i = 0; i < 20; i++) {
        const DWORD* p = sp + i;
        DWORD v = 0;
        if (!rd_ok(p)) { vlog("      Esp+%02X  <不可读>", (unsigned)(i * 4)); break; }
        v = *p;
        vlog("      Esp+%02X  %08X  %s", (unsigned)(i * 4), (unsigned)v,
             addr_owner((void*)(UINT_PTR)v, n1, sizeof(n1)));
    }

    /* 顺着 Ebp 帧链再走一段（老式帧指针；有就用，没有就停） */
    {
        const DWORD* ebp = (const DWORD*)(UINT_PTR)ep->ContextRecord->Ebp;
        vlog("   帧链（Ebp=0x%08X）：", (unsigned)(UINT_PTR)ebp);
        for (i = 0; i < 16; i++) {
            const DWORD* next;
            if (!rd_ok(ebp) || !rd_ok(ebp + 1)) break;
            vlog("      [%02u] 返回地址 %s", (unsigned)i,
                 addr_owner((void*)(UINT_PTR)ebp[1], n1, sizeof(n1)));
            next = (const DWORD*)(UINT_PTR)ebp[0];
            if (next <= ebp) break;
            ebp = next;
        }
    }
}

static LONG WINAPI veh_probe(EXCEPTION_POINTERS* ep)
{
    HMODULE ntdll = NULL;
    PFN_CAPTURE cap;
    void*  frames[24];
    USHORT got = 0, i;
    DWORD  code;
    void*  where;
    char   n1[220];
    LONG   n = InterlockedIncrement(&g_vehCount);

    if (!ep || !ep->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    code  = ep->ExceptionRecord->ExceptionCode;
    where = ep->ExceptionRecord->ExceptionAddress;

    vlog("========================================================================");
    vlog("[VEH #%ld] 异常 0x%08X  线程 tid=%u", n, (unsigned)code, GetCurrentThreadId());
    vlog("   出错指令：%s", addr_owner(where, n1, sizeof(n1)));

    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR)
        && ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR how = ep->ExceptionRecord->ExceptionInformation[0];
        void*     bad = (void*)ep->ExceptionRecord->ExceptionInformation[1];
        MEMORY_BASIC_INFORMATION mbi;
        const char* st = "?";
        char  info[300];
        if (VirtualQuery(bad, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            st = (mbi.State == MEM_FREE)    ? "MEM_FREE（映像已被卸载/释放）"
               : (mbi.State == MEM_COMMIT)  ? "MEM_COMMIT"
               : (mbi.State == MEM_RESERVE) ? "MEM_RESERVE" : "?";
            _snprintf(info, sizeof(info), "protect=0x%X type=0x%X size=%uKB",
                      (unsigned)mbi.Protect, (unsigned)mbi.Type,
                      (unsigned)(mbi.RegionSize / 1024));
        } else {
            _snprintf(info, sizeof(info), "VirtualQuery 失败");
        }
        vlog("   访问类型：%s   故障地址：%p   [%s  %s]",
             how == 0 ? "读" : (how == 1 ? "写" : "执行"), bad, st, info);
        if (mbi.State == MEM_FREE)
            vlog("   ★ 结论：目标页面已 MEM_FREE —— 有代码指针指向了**已被卸载的模块**。");
    } else {
        vlog("   异常参数个数：%u", (unsigned)ep->ExceptionRecord->NumberParameters);
    }

    if (ep->ContextRecord && (void*)(UINT_PTR)ep->ContextRecord->Eip != where)
        vlog("   Eip=0x%08X  Esp=0x%08X  Ebp=0x%08X",
             (unsigned)ep->ContextRecord->Eip, (unsigned)ep->ContextRecord->Esp,
             (unsigned)ep->ContextRecord->Ebp);

    /* 调用栈：先记原始地址（哪怕落在无模块区），再逐帧解析 */
    ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        cap = (PFN_CAPTURE)GetProcAddress(ntdll, "RtlCaptureStackBackTrace");
        if (cap) got = cap(0, 24, frames, NULL);
    }
    vlog("   调用栈（%u 帧）：", (unsigned)got);
    for (i = 0; i < got; i++)
        vlog("      [%02u] %s", (unsigned)i, addr_owner(frames[i], n1, sizeof(n1)));

    log_module_presence("出错指令所属模块", where);
    if (got) log_module_presence("栈顶帧所属模块", frames[0]);
    dump_fault_stack(ep);
    vlog("   崩溃瞬间仍在加载器链表里的模块（★段内就是「出错指令」所属的那一段）：");
    dump_modules((void*)(UINT_PTR)(ep->ContextRecord ? ep->ContextRecord->Eip : 0));
    vlog("   崩溃瞬间进程内仍存在的窗口：");
    dump_process_windows();

    return EXCEPTION_CONTINUE_SEARCH;   /* 只观测，交给宿主自己的处理器 */
}

const char* __stdcall MbKit_InstallVehProbe(void)
{
    char dir[MAX_PATH];
    PFN_ADDVEH add;

    if (g_veh) return g_vehLog;

    dir[0] = 0;
    if (!GetModuleFileNameA(NULL, dir, MAX_PATH)) return "";
    {   /* 砍到目录 */
        char* p = dir;
        char* last = dir;
        while (*p) { if (*p == '\\') last = p; p++; }
        if (last > dir) *last = 0;
    }
    _snprintf(g_vehLog, sizeof(g_vehLog) - 1, "%s\\mb_veh_%u.log",
              dir, (unsigned)GetCurrentProcessId());
    g_vehLog[sizeof(g_vehLog) - 1] = 0;

    /* 开一枪「活着的标记」，便于确认探针确实装上了 */
    vlog("=========================================================================");
    vlog("[VEH] 探针已安装  pid=%u  MbKit=%s", (unsigned)GetCurrentProcessId(), MbKit_Version());
    vlog("[VEH] 日志路径：%s", g_vehLog);

    add = (PFN_ADDVEH)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                     "AddVectoredExceptionHandler");
    if (!add) { vlog("[VEH] 找不到 AddVectoredExceptionHandler，探针未生效"); return g_vehLog; }
    g_veh = add(1 /* 先于我之外的处理器 */, (PVOID)veh_probe);
    vlog("[VEH] AddVectoredExceptionHandler -> %p（先手处理 %s）", g_veh, g_veh ? "已生效" : "失败");

    /* 顺带把消息跟踪也架上：空窗期里 user32 派发到什么窗口、什么消息，先记一笔 */
    MbKit_InstallMsgTrace();
    return g_vehLog;
}

int __stdcall MbKit_VehHits(void) { return (int)g_vehCount; }

/* ==========================================================================
 * ★ v1.4.2：热重载安全 —— 钉住「调用者所在的模块」（通常是业务插件 DLL）
 *
 * 宿主的 `重载自身` = FreeLibrary(旧) → LoadLibrary(新)，两次之间有一小段
 * 「空窗期」。此刻只要用户态还有任何指针指向旧映像（窗口过程、内核回调、
 * user32/kernel 内部记下的地址……），一跳进去就是访问违例，而宿主自己的
 * 致命错误处理会直接把进程收掉（ExitCode=0，日志断在半句）。
 *
 * 从插件侧没法保证那些指针被清干净（它们不在我们的控制范围：内核、user32、
 * 宿主自己的代码都可能记着）。但**可以不让那段映像被卸载**：
 * 用 GetModuleHandleExA(..., PIN) 给插件自己的映像加一个引用，宿主的
 * FreeLibrary 只会把引用计数减 1，映像仍留在进程里 ⇒ 所有悬挂指针都还指向
 * 有效代码 ⇒ 空窗期不再有命案。
 *
 * 代价（有意为之）：每次重载会多留一份旧映像（约几 MB）在进程里，进程退出才
 * 回收。拿内存换「重载不炸宿主」，对调试/迭代场景是稳赚的。
 * ========================================================================*/
unsigned int __stdcall MbKit_PinCaller(void)
{
    HMODULE h = NULL;
#if defined(_MSC_VER)
    void*   caller = _ReturnAddress();      /* 调用者模块内的一个地址 */
#else
    void*   caller = __builtin_return_address(0);   /* MinGW/GCC */
#endif
    if (!caller) return 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCSTR)(UINT_PTR)caller, &h) && h) {
        vlog("[PIN] 已钉住调用者模块 0x%08X（调用点 0x%08X）",
             (unsigned)(UINT_PTR)h, (unsigned)(UINT_PTR)caller);
        return (unsigned)(UINT_PTR)h;
    }
    vlog("[PIN] 钉住失败（调用点 0x%08X）", (unsigned)(UINT_PTR)caller);
    return 0;
}

/* 钉住某个地址所属的模块（业务侧可以传自己某个子程序的地址） */
unsigned int __stdcall MbKit_PinAddress(unsigned int addr)
{
    HMODULE h = NULL;
    if (!addr) return 0;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                         | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           (LPCSTR)(UINT_PTR)addr, &h) && h)
        return (unsigned)(UINT_PTR)h;
    return 0;
}

/* ==========================================================================
 * 消息跟踪：把「卸载空窗期里到底是谁给哪个窗口发了什么消息」抓出来。
 *
 * 为什么可行：钩子过程在 **MbKit 里**（已自钉 ⇒ 业务 DLL 被卸载后它仍在），
 * 而 WH_GETMESSAGE 是 user32 在**派发之前**调用的 —— 所以我们总能先记一笔，
 * 再让 user32 去 call 那个（可能已经失效的）窗口过程。
 * ========================================================================*/
static HHOOK g_msgHook = NULL;
static LONG  g_msgOn   = 0;
static LONG  g_msgSeen = 0;

static const char* msg_name(DWORD m, char* b, int cap)
{
    switch (m) {
    case WM_PAINT:            return "WM_PAINT";
    case WM_ERASEBKGND:       return "WM_ERASEBKGND";
    case WM_NCPAINT:          return "WM_NCPAINT";
    case WM_PRINT:            return "WM_PRINT";
    case WM_PRINTCLIENT:      return "WM_PRINTCLIENT";
    case WM_SIZE:             return "WM_SIZE";
    case WM_MOVE:             return "WM_MOVE";
    case WM_WINDOWPOSCHANGED: return "WM_WINDOWPOSCHANGED";
    case WM_SHOWWINDOW:       return "WM_SHOWWINDOW";
    case WM_DESTROY:          return "WM_DESTROY";
    case WM_CLOSE:            return "WM_CLOSE";
    case WM_NCDESTROY:        return "WM_NCDESTROY";
    case WM_TIMER:            return "WM_TIMER";
    case WM_MOUSEMOVE:        return "WM_MOUSEMOVE";
    case WM_SETCURSOR:        return "WM_SETCURSOR";
    case WM_ACTIVATE:         return "WM_ACTIVATE";
    case WM_ACTIVATEAPP:      return "WM_ACTIVATEAPP";
    case WM_KILLFOCUS:        return "WM_KILLFOCUS";
    case WM_SETFOCUS:         return "WM_SETFOCUS";
    case WM_NCACTIVATE:       return "WM_NCACTIVATE";
    case WM_GETTEXT:          return "WM_GETTEXT";
    case WM_GETMINMAXINFO:    return "WM_GETMINMAXINFO";
    case WM_NCHITTEST:        return "WM_NCHITTEST";
    case WM_INITMENU:         return "WM_INITMENU";
    case WM_DEVICECHANGE:     return "WM_DEVICECHANGE";
    default: break;
    }
    _snprintf(b, cap, "0x%04X", (unsigned)m);
    return b;
}

static void describe_window(HWND h, char* out, int cap)
{
    char cls[80] = "?", title[64] = "";
    char proc[220] = "";
    DWORD pid = 0, tid;
    if (!h || !IsWindow(h)) { _snprintf(out, cap, "%p(无效窗口)", h); return; }
    GetClassNameA(h, cls, sizeof(cls));
    GetWindowTextA(h, title, sizeof(title));
    tid = GetWindowThreadProcessId(h, &pid);
    /* ★ 窗口过程（链首）落在哪个模块 —— 空窗期命案就看这一列 */
    addr_owner((void*)(UINT_PTR)GetWindowLongPtrA(h, GWLP_WNDPROC), proc, sizeof(proc));
    _snprintf(out, cap, "%p 类=%s tid=%u%s%s  [proc=%s]", h, cls, (unsigned)tid,
              title[0] ? " 标题=" : "", title, proc);
}

static LRESULT CALLBACK msg_hook(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && InterlockedCompareExchange(&g_msgOn, 1, 1) == 1 && lp) {
        const MSG* m = (const MSG*)lp;
        char nm[24], win[220], proc[220];
        LONG n = InterlockedIncrement(&g_msgSeen);
        if (n <= 400) {                       /* 上限，避免日志失控 */
            WNDPROC target = (WNDPROC)GetWindowLongPtrA(m->hwnd, GWLP_WNDPROC);
            describe_window(m->hwnd, win, sizeof(win));
            vlog("[MSG #%ld] %s  hwnd=%s  wParam=0x%X lParam=0x%X",
                 n, msg_name(m->message, nm, sizeof(nm)), win,
                 (unsigned)m->wParam, (unsigned)m->lParam);
            /* ★ 这一行的「目标过程」就是 user32 马上要 call 的地址：
             *   它若落在已卸载模块里，这条消息就是命案现场。 */
            vlog("          目标窗口过程(链首) = %s",
                 addr_owner((void*)(UINT_PTR)target, proc, sizeof(proc)));
        }
    }
    return CallNextHookEx(g_msgHook, code, wp, lp);
}

/* 所有属于本进程的窗口（顶层 + 子窗口）记一笔：类名/tid/标题/可见性 */
static BOOL CALLBACK enum_cb(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    char  win[220];
    GetWindowThreadProcessId(h, &pid);
    if (pid == GetCurrentProcessId()) {
        describe_window(h, win, sizeof(win));
        vlog("      %s  visible=%d", win, IsWindowVisible(h) ? 1 : 0);
        if (!lp) EnumChildWindows(h, enum_cb, (LPARAM)1);
    }
    return TRUE;
}

/* ── ★ v1.4.4：消息专用窗口（HWND_MESSAGE 的子窗口）────────────────────
 * 探针实测的铁证：AV 时窗口快照里**没有任何窗口的过程落在插件映像内**，
 * 而故障现场栈 Esp+00 = USER32.dll+0x3530F —— 那正是 user32 `call [窗口过程]`
 * 的返回点。两者只能同时成立，如果被派发的那个窗口**根本不在 EnumWindows 的
 * 视野里**：消息专用窗口（CreateWindowEx(..., HWND_MESSAGE, ...)）不属于任何
 * 顶层窗口，EnumWindows/EnumChildWindows 都看不到它，但 user32 照样给它派发
 * 消息。miniblink 的离屏定时器/宿主消息窗口恰恰常用 HWND_MESSAGE 创建。
 * FindWindowEx 以 HWND_MESSAGE 为父窗口即可枚举这一类窗口。 */
#define MBK_HWND_MESSAGE ((HWND)(INT_PTR)-3)

static void enum_message_only(BOOL (CALLBACK *cb)(HWND, LPARAM))
{
    HWND h = NULL;
    int guard = 0;
    while ((h = FindWindowExA(MBK_HWND_MESSAGE, h, NULL, NULL)) != NULL) {
        cb(h, (LPARAM)1);            /* 1 = 不再递归子窗口 */
        if (++guard > 8192) break;   /* 防御：链表异常时不至于死循环 */
    }
}

static void dump_process_windows(void)
{
    EnumWindows(enum_cb, 0);
    vlog("      —— 消息专用窗口（HWND_MESSAGE，EnumWindows 看不到）——");
    enum_message_only(enum_cb);
}

/* ==========================================================================
 * ★ v1.4.2：清理「僵尸窗口」—— 热重载命案的直接凶手
 *
 * 插件的按钮/窗体，其**窗口过程编译在插件 DLL 自己的映像里**。宿主执行
 * `重载自身` 时卸载了插件映像，却没有销毁插件的窗口 ⇒ 窗口还挂在窗口链上，
 * 而它的窗口过程已经落在被释放的代码页里。此后任何消息（miniblink 的离屏
 * 重绘 tick 约 16ms 一次，源源不断）被 user32 派发给这个窗口，就是一次
 * 「跳进已卸载代码」⇒ 访问违例 ⇒ 宿主自己的致命错误处理收掉进程（ExitCode=0）。
 *
 * 判据：取窗口过程地址，若它落在 **MEM_FREE** 区域（那段映像确实已经卸载），
 * 该窗口就是僵尸 —— 它的过程永远不可能再被正确执行，销毁它是绝对安全的；
 * 反之，落在已提交内存里的窗口一律不动（避免误伤宿主/系统窗口）。
 * 系统类窗口的过程是 0xFFFFxxxx 这类 USER32 内部标记，虚拟地址区间不在用户
 * 代码段，直接排除。
 * ==========================================================================*/
static int g_zombieKilled = 0;
static int g_zombieScanned = 0;

static int proc_is_dead(void* proc)
{
    MEMORY_BASIC_INFORMATION mbi;
    UINT_PTR p = (UINT_PTR)proc;
    if (p < 0x10000 || p > 0x7FFFFFFF) return 0;        /* 0/系统标记，不碰 */
    if (VirtualQuery(proc, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    return mbi.State == MEM_FREE;                        /* 映像已被卸载 ⇒ 过程必死 */
}

static BOOL CALLBACK zombie_cb(HWND h, LPARAM lp)
{
    void* proc;
    if (lp) EnumChildWindows(h, zombie_cb, 1);
    if (!h || !IsWindow(h)) return TRUE;
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
    }
    proc = (void*)(UINT_PTR)GetWindowLongPtrA(h, GWLP_WNDPROC);
    g_zombieScanned++;
    if (!proc_is_dead(proc)) return TRUE;

    {
        char cls[80] = "?", title[64] = "";
        GetClassNameA(h, cls, sizeof(cls));
        GetWindowTextA(h, title, sizeof(title));
        vlog("[ZOMBIE] 窗口 %p 类=%s 标题=%s 的过程 %p 落在已释放映像里，已销毁",
             h, cls, title, proc);
    }
    if (DestroyWindow(h)) g_zombieKilled++;
    return TRUE;
}

/* 返回：销毁掉的僵尸窗口数（0 = 没有僵尸） */
int __stdcall MbKit_KillZombieWindows(void)
{
    g_zombieKilled = 0;
    g_zombieScanned = 0;
    EnumWindows(zombie_cb, 0);
    enum_message_only(zombie_cb);
    vlog("[ZOMBIE] 扫描 %d 个窗口（含消息专用窗口），销毁僵尸 %d 个", g_zombieScanned, g_zombieKilled);
    return g_zombieKilled;
}

int __stdcall MbKit_ZombieScanned(void) { return g_zombieScanned; }

/* ==========================================================================
 * ★ v1.4.2：销毁「窗口过程还在调用者映像里」的窗口 —— 热重载的真正解药
 *
 * 上面 MbKit_KillZombieWindows 只能清「过程已经掉进已释放区域」的窗口，而那些窗口
 * 是**已经出过事**的残留；真正致命的是**当下还活着、窗口过程就在插件 DLL 映像里**
 * 的那些窗口（探针实测：`Button 标题=重载插件 [proc=657bc290….dll+0xF0E84]`）。
 * 插件映像一旦被卸载，它们就是下一次消息派发的坟墓。
 *
 * 本命令以**调用者所在模块**（即本插件 DLL，用返回地址反查）的映像范围为界，
 * 把窗口过程落在该范围内的窗口全部销毁 —— 卸载前调一次，卸载后就再没有可踩空的
 * 插件窗口了。
 * ==========================================================================*/
static UINT_PTR g_selfLo = 0, g_selfHi = 0;
static int g_callerKilled = 0, g_callerScanned = 0;

static BOOL CALLBACK kill_caller_cb(HWND h, LPARAM lp)
{
    void* proc;
    if (lp) EnumChildWindows(h, kill_caller_cb, 1);
    if (!h || !IsWindow(h)) return TRUE;
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
    }
    proc = (void*)(UINT_PTR)GetWindowLongPtrA(h, GWLP_WNDPROC);
    g_callerScanned++;
    if ((UINT_PTR)proc < g_selfLo || (UINT_PTR)proc >= g_selfHi) return TRUE;

    {
        char cls[80] = "?", title[64] = "";
        GetClassNameA(h, cls, sizeof(cls));
        GetWindowTextA(h, title, sizeof(title));
        vlog("[KILLCALLER] 窗口 %p 类=%s 标题=%s 过程 %p 在插件映像内，已销毁",
             h, cls, title, proc);
    }
    if (DestroyWindow(h)) g_callerKilled++;
    return TRUE;
}

int __stdcall MbKit_KillCallerWindows(void)
{
    HMODULE hm = NULL;
    IMAGE_DOS_HEADER* dos;
    IMAGE_NT_HEADERS* nt;
    void* ret = __builtin_return_address(0);

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)ret, &hm) || !hm)
        return -1;
    dos = (IMAGE_DOS_HEADER*)hm;
    nt = (IMAGE_NT_HEADERS*)((char*)hm + dos->e_lfanew);
    g_selfLo = (UINT_PTR)hm;
    g_selfHi = g_selfLo + nt->OptionalHeader.SizeOfImage;

    g_callerKilled = 0;
    g_callerScanned = 0;
    EnumWindows(kill_caller_cb, 0);
    enum_message_only(kill_caller_cb);
    vlog("[KILLCALLER] 插件映像 %p-%p，扫描 %d 个窗口（含消息专用窗口），销毁 %d 个",
         (void*)g_selfLo, (void*)g_selfHi, g_callerScanned, g_callerKilled);
    return g_callerKilled;
}

int __stdcall MbKit_CallerWindowScanned(void) { return g_callerScanned; }

/* ── 崩溃瞬间的模块清单 ────────────────────────────────────────────────
 * 命案的关键是「出错指令所在的映像**已经不在**加载器链表里」—— 那份清单里没有
 * 它，就等于点名了「凶手是被卸载掉的那个模块」。这里把当时还在链表里的模块全列
 * 出来，并标注目标地址是否落在其中。 */
static void dump_modules(void* target)
{
    HANDLE snap;
    MODULEENTRY32 me;
    HMODULE hm;
    int inside = 0;

    if (target && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                   | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                     (LPCSTR)(UINT_PTR)target, &hm) && hm)
        inside = 1;
    vlog("      目标 %p %s", target,
         inside ? "→ **仍在某个已加载模块里**（那就不是「映像被卸载」型命案）"
                : "→ ★不属于以下任何模块：它属于**已被卸载的那个映像**");

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) { vlog("      （模块枚举失败）"); return; }
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            vlog("      %-34s base=0x%08X size=%u", me.szModule,
                 (unsigned)(UINT_PTR)me.modBaseAddr, (unsigned)me.modBaseSize);
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
}

int __stdcall MbKit_InstallMsgTrace(void)
{
    DWORD tid = GetCurrentThreadId();
    if (g_msgHook) return 1;
    vlog("------------------------------------------------------------------------");
    vlog("[TRACE] 装消息跟踪钩子（WH_GETMESSAGE，线程 tid=%u）", (unsigned)tid);
    g_msgHook = SetWindowsHookExA(WH_GETMESSAGE, msg_hook, NULL, tid);
    vlog("[TRACE] SetWindowsHookExA -> %p（%s）", g_msgHook, g_msgHook ? "已生效" : "失败");
    InterlockedExchange(&g_msgOn, 1);
    vlog("[TRACE] 本进程当前窗口（EnumWindows + EnumChildWindows）：");
    dump_process_windows();
    vlog("------------------------------------------------------------------------");
    return g_msgHook ? 1 : 0;
}

