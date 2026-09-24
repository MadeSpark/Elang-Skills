/* ============================================================================
 * mbk_thread.c —— MbKit 常驻内核线程 K + 同步任务编组
 * ----------------------------------------------------------------------------
 * 设计要点（为什么必须这么写）：
 *   1) K 在**首次使用时**由业务线程 CreateThread 创建（绝不在 DllMain 里建 ——
 *      loader lock）。建好后永不退出：进程退出时随进程消亡，内核也一并消亡，
 *      与「MbKit 从不 finalize 内核」的既有契约一致。
 *   2) K 的消息循环用 GetMessage：既承接任务投递，也天然充当内核消息窗口的
 *      泵（miniblink 把内部任务投到初始化线程的消息队列上，见 marshal_probe）。
 *   3) 同步编组用 PostThreadMessage + 事件等待，60s 超时只记日志不强杀 ——
 *      强杀会让内核对象悬挂，比超时更糟。
 *   4) K 上重入（内核回调里再调内核 API）必须**就地执行**，否则自己等自己死锁。
 * ==========================================================================*/
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "mbk_thread.h"

#define MBK_WM_TASK (WM_APP + 0x4D42)

typedef struct {
    mbk_task_fn fn;
    void*       arg;
    HANDLE      done;
} MBKTASK;

static volatile DWORD g_kTid = 0;

static DWORD WINAPI k_body(LPVOID pv)
{
    HANDLE ready = (HANDLE)pv;
    MSG    m;

    g_kTid = GetCurrentThreadId();
    /* 强制建立消息队列（PostThreadMessage 的前置条件） */
    PeekMessageA(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    SetEvent(ready);

    for (;;) {
        int r = (int)GetMessageA(&m, NULL, 0, 0);
        if (r <= 0) break;                       /* WM_QUIT / 错误 */
        if (m.message == MBK_WM_TASK && m.lParam) {
            MBKTASK* t = (MBKTASK*)m.lParam;
            t->fn(t->arg);
            if (t->done) SetEvent(t->done);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}

int mbk_kernel_start(void)
{
    static volatile LONG starting = 0;
    HANDLE ready, th;

    if (g_kTid) return 1;
    if (InterlockedCompareExchange(&starting, 1, 0) != 0) {
        int spin = 0;
        while (!g_kTid && spin++ < 10000) Sleep(1);   /* 等并发的那次建好 */
        return g_kTid != 0;
    }

    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!ready) { InterlockedExchange(&starting, 0); return 0; }
    th = CreateThread(NULL, 0, k_body, ready, 0, NULL);
    if (!th) { CloseHandle(ready); InterlockedExchange(&starting, 0); return 0; }
    CloseHandle(th);

    WaitForSingleObject(ready, 10000);   /* 等 K 建好消息队列 */
    CloseHandle(ready);
    if (!g_kTid) { InterlockedExchange(&starting, 0); return 0; }

    OutputDebugStringA("[MbKit] 常驻内核线程 K 已启动\n");
    return 1;
}

DWORD mbk_kernel_tid(void)       { return g_kTid; }
int   mbk_on_kernel_thread(void) { return g_kTid && GetCurrentThreadId() == g_kTid; }

void mbk_kernel_run(mbk_task_fn fn, void* arg)
{
    MBKTASK t;

    if (mbk_on_kernel_thread()) { fn(arg); return; }   /* K 上重入：就地执行 */

    t.fn = fn; t.arg = arg;
    t.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!t.done) { fn(arg); return; }                  /* 兜底：宁直通不丢失 */

    if (!PostThreadMessageA(g_kTid, MBK_WM_TASK, 0, (LPARAM)&t)) {
        CloseHandle(t.done);
        fn(arg);                                       /* K 未就绪：就地执行 */
        return;
    }
    if (WaitForSingleObject(t.done, 60000) == WAIT_TIMEOUT)
        OutputDebugStringA("[MbKit] 内核任务超时(60s)：内核可能已挂死\n");
    CloseHandle(t.done);
}
