/* ============================================================================
 * mbk_thread.c —— MbKit 常驻内核线程 K + 同步任务编组
 * ----------------------------------------------------------------------------
 * 设计要点（为什么必须这么写）：
 *
 *   1) K 在**首次使用时**由业务线程 CreateThread 创建，建好后永不退出：
 *      进程退出时随进程消亡，内核也一并消亡 —— 与「MbKit 从不 finalize 内核」
 *      的既有契约一致。
 *
 *   2) ★ 2026-10-01 重做「任务投递」：**不再依赖 K 的消息队列**，改用
 *      「事件 + 任务链」。
 *      为什么：易语言 DLL 的 `_启动子程序` 跑在 DllMain（DLL_PROCESS_ATTACH）
 *      里，而**新线程要等 DllMain 返回、加载锁释放后才真正开始执行**
 *      （LdrInitializeThunk 要做线程 attach、跑各模块的 DLL_THREAD_ATTACH）。
 *      旧实现用 PostThreadMessage 投递到 K 的线程消息队列，并且
 *      `mbk_kernel_start` 还要**等 K 建好消息队列**才返回 —— 在 DllMain 里这
 *      两件事都必然失败/超时，实测：
 *         宿主 LoadLibrary(插件DLL) → 插件 DllMain 调 MbKit_Init
 *         旧版：第 1 轮卡 10,016 ms、第 2 轮卡 60,016 ms，node.dll 始终为 0
 *      现在：任务链 + 事件，`mbk_kernel_start` **不等待**、投递**不依赖队列**，
 *      内核启动任务先排队；DllMain 一返回，K 立刻起来把排队的任务做掉。
 *
 *   3) K 的循环用 MsgWaitForMultipleObjects：既接任务，也充当内核消息窗口的泵
 *      （miniblink 把内部任务投到初始化线程的消息队列上）。
 *
 *   4) 同步编组（mbk_kernel_run）等待时**抽本线程消息队列**（mbk_wait_pump）：
 *      宿主的 UI 线程常常一边被我们挡住、一边又是内核要用的窗口线程，
 *      纯事件等待会让两边互等 = 宿主「未响应」。带泵等待化解这一类互等。
 *
 *   5) K 上重入（内核回调里再调内核 API）必须**就地执行**，否则自己等自己死锁。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "mbk_thread.h"

/* mbkit.c：属主判定（v1.4「属主直通」= 内核主线程就是调用线程，就地执行） */
extern int   mbk_owner_mode(void);       /* 1=K 模式（编组） 0=属主直通 */
extern DWORD mbk_owner_tid(void);        /* 内核主线程 tid（0=未初始化） */

typedef struct MBKTASK {
    mbk_task_fn      fn;
    void*            arg;
    HANDLE           done;      /* NULL = 投递（不等），由 K 侧释放 */
    struct MBKTASK*  next;
} MBKTASK;

static volatile DWORD g_kTid        = 0;
static volatile LONG  g_pumpMsgs    = 0;
static volatile LONG  g_runTimeouts = 0;
/* ★ 2026-10-01 改为**默认关闭**「编组等待期间抽消息」（-1 = 未定，按 MBKIT_PUMP=1 解析）。
 *   实测证据（真实框架里跑）：内核启动正常、自检显示 `等待超时 0 次`，压根不需要抽消息；
 *   而抽消息会在插件调用中途**把宿主自己的消息派发进去**（实测一次调用里抽掉 253 条），
 *   等于在宿主初始化还没走完时重入它的窗口过程 —— 用户看到的是「不标未响应，但点什么
 *   都没反应」。默认不抽；确认遇到 UI 线程与内核互等的环境再 MBKIT_PUMP=1。 */
static volatile LONG  g_pumpEnabled = -1;   /* -1=未定 0=关 1=开 */

static int pump_on(void)
{
    LONG m = g_pumpEnabled;
    if (m < 0) {
        char v[8] = {0};
        LONG want = 0;
        if (GetEnvironmentVariableA("MBKIT_PUMP", v, sizeof(v)) && v[0] == '1') want = 1;
        InterlockedCompareExchange(&g_pumpEnabled, want, -1);
        m = g_pumpEnabled;
    }
    return m == 1;
}

static HANDLE            g_taskEvent = NULL;
static CRITICAL_SECTION  g_taskLock;
static MBKTASK*          g_head = NULL;
static MBKTASK*          g_tail = NULL;
static volatile LONG     g_taskInit = 0;
static volatile LONG     g_taskReady = 0;

/* 任务链/事件的惰性初始化：可在 DllMain 里安全调用（不做任何 LoadLibrary） */
static void task_init(void)
{
    if (InterlockedCompareExchange(&g_taskInit, 1, 0) == 0) {
        InitializeCriticalSection(&g_taskLock);
        g_taskEvent = CreateEventA(NULL, TRUE /*手动重置*/, FALSE, NULL);
        InterlockedExchange(&g_taskReady, 1);
    } else {
        int spin = 0;
        while (!g_taskReady && spin++ < 20000) Sleep(1);
    }
}

/* 入队 + 唤醒 K（不依赖 K 是否已开始执行） */
static int task_push(mbk_task_fn fn, void* arg, HANDLE done)
{
    MBKTASK* t;

    if (!g_taskReady) task_init();
    t = (MBKTASK*)HeapAlloc(GetProcessHeap(), 0, sizeof(MBKTASK));
    if (!t) return 0;
    t->fn = fn; t->arg = arg; t->done = done; t->next = NULL;

    EnterCriticalSection(&g_taskLock);
    if (g_tail) g_tail->next = t; else g_head = t;
    g_tail = t;
    LeaveCriticalSection(&g_taskLock);

    if (g_taskEvent) SetEvent(g_taskEvent);
    return 1;
}

/* K 侧：取一个任务（无则返回 NULL） */
static MBKTASK* task_pop(void)
{
    MBKTASK* t = NULL;

    EnterCriticalSection(&g_taskLock);
    t = g_head;
    if (t) {
        g_head = t->next;
        if (!g_head) {
            g_tail = NULL;
            if (g_taskEvent) ResetEvent(g_taskEvent);   /* 队列空：关掉事件 */
        }
    }
    LeaveCriticalSection(&g_taskLock);
    return t;
}

/* --------------------------------------------------------------------------
 * 带消息泵的等待（详见文件头 4)）
 * ------------------------------------------------------------------------*/
int mbk_wait_pump(HANDLE done, DWORD ms)
{
    DWORD t0 = GetTickCount();

    if (!done) { Sleep(ms); return 0; }

    for (;;) {
        DWORD left, r;
        MSG   m;

        if (WaitForSingleObject(done, 0) == WAIT_OBJECT_0) return 1;
        if ((int)(GetTickCount() - t0) >= (int)ms) return 0;
        left = ms - (DWORD)(GetTickCount() - t0);

        if (pump_on()) {
            DWORD slice = left > 30 ? 30 : left;
            r = MsgWaitForMultipleObjects(1, &done, FALSE, slice, QS_ALLINPUT);
        } else {
            r = WaitForSingleObject(done, left);
        }

        if (r == WAIT_OBJECT_0) return 1;
        if (r == WAIT_FAILED)   return 0;
        if (r == WAIT_TIMEOUT && !pump_on())
            return 0;

        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            InterlockedIncrement(&g_pumpMsgs);
            if (m.message == WM_QUIT) {
                PostQuitMessage((int)m.wParam);      /* 归还给宿主，绝不吞 */
                continue;
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        if (WaitForSingleObject(done, 0) == WAIT_OBJECT_0) return 1;
    }
}

void mbk_pump_enable(int on) { InterlockedExchange(&g_pumpEnabled, on ? 1 : 0); }
int  mbk_pump_count(void)    { return (int)g_pumpMsgs; }
int  mbk_run_timeouts(void)  { return (int)g_runTimeouts; }

static DWORD WINAPI k_body(LPVOID pv)
{
    MSG m;

    (void)pv;
    /* 建线程消息队列（内核把内部任务投到初始化线程的队列上；也便于抽消息） */
    PeekMessageA(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    for (;;) {
        MBKTASK* t;
        DWORD    r;

        while ((t = task_pop()) != NULL) {
            t->fn(t->arg);
            if (t->done) SetEvent(t->done);          /* 同步任务：唤醒等待者 */
            else HeapFree(GetProcessHeap(), 0, t);   /* 投递任务：自己回收 */
        }

        /* 等「新任务」或「本线程消息」—— 两者都处理，内核消息窗口才不饿死 */
        r = MsgWaitForMultipleObjects(1, &g_taskEvent, FALSE, 100, QS_ALLINPUT);
        if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT) {
            while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) return 0;
                TranslateMessage(&m);
                DispatchMessageA(&m);
            }
        } else if (r == WAIT_FAILED) {
            Sleep(20);
        }
    }
}

int mbk_kernel_start(void)
{
    static volatile LONG starting = 0;
    HANDLE th = NULL;
    DWORD  tid = 0;

    if (g_kTid) return 1;
    if (InterlockedCompareExchange(&starting, 1, 0) != 0) {
        int spin = 0;
        while (!g_kTid && spin++ < 10000) Sleep(1);
        return g_kTid != 0;
    }

    task_init();                                   /* ★ 队列先就绪，投递不依赖 K */
    th = CreateThread(NULL, 0, k_body, NULL, 0, &tid);
    if (!th) { InterlockedExchange(&starting, 0); return 0; }
    CloseHandle(th);

    /* ★ 立刻记账 tid、**绝不等待 K 开始执行**：
     *   在 DllMain（加载锁）里，新线程要等本线程退出 DllMain 才能真正开跑；
     *   在这里等待只会白白卡住宿主。任务已经排在链上，K 一起来就会做。 */
    g_kTid = tid;
    OutputDebugStringA("[MbKit] 常驻内核线程 K 已创建（启动异步，不等就绪）\n");
    return 1;
}

DWORD mbk_kernel_tid(void)       { return g_kTid; }
int   mbk_on_kernel_thread(void) { return g_kTid && GetCurrentThreadId() == g_kTid; }

void mbk_kernel_run(mbk_task_fn fn, void* arg)
{
    HANDLE done;

    /* ★ v1.4 属主直通模式：内核主线程就是调用线程 → **就地执行**。
     *   编组到 K 会把 wke* 调用搬到别的线程上，miniblink 的渲染/消息循环就废了
     *   （实测 K 模式页面永不加载）。 */
    if (!mbk_owner_mode() && mbk_owner_tid() && GetCurrentThreadId() == mbk_owner_tid()) {
        fn(arg);
        return;
    }

    if (mbk_on_kernel_thread()) { fn(arg); return; }   /* K 上重入：就地执行 */

    done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!done) { fn(arg); return; }                    /* 兜底：宁直通不丢失 */

    if (!task_push(fn, arg, done)) {                   /* 排队失败：就地执行 */
        CloseHandle(done);
        fn(arg);
        return;
    }
    if (!mbk_wait_pump(done, 60000)) {
        InterlockedIncrement(&g_runTimeouts);
        OutputDebugStringA("[MbKit] 内核任务超时(60s)：内核可能已挂死\n");
    }
    CloseHandle(done);
}

/* 投递但不等待：用于「内核启动」——调用方可能在 DllMain / UI 线程里，
 * 绝不能让它在这里等 LoadLibrary(node.dll)。 */
int mbk_kernel_post(mbk_task_fn fn, void* arg)
{
    if (mbk_on_kernel_thread()) { fn(arg); return 1; }
    return task_push(fn, arg, NULL);
}
