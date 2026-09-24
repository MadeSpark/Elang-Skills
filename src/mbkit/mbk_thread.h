/* ============================================================================
 * mbk_thread.h —— MbKit 常驻内核线程 K + 同步任务编组（2026-09-25）
 * ----------------------------------------------------------------------------
 * 背景：miniblink 内核**线程亲和** —— wkeInitialize 在哪条线程执行，内核的
 *       V8 isolate / 消息窗口就归哪条线程；从别的线程碰内核 = 挂死或 0xC0000005
 *       （实测见 re/reinit_poc/marshal_*.txt）。
 *
 * 结论：让 MbKit 自有的一条**常驻线程 K** 持有内核（wkeInitialize 只在 K 上
 *       执行），业务线程生死从此无关 —— 这是「不重启宿主重载 biz DLL 也能再次
 *       初始化」的唯一通路（换内核代际 / finalize / 重载拷贝均已被实测否定）。
 *
 * 编组语义（mbk_kernel_run）：
 *   · 当前就在 K 上        → 就地执行（回调重入，避免自等死锁）
 *   · 其它线程             → PostThreadMessage 投递 + 事件同步等待结果
 * ==========================================================================*/
#ifndef MBK_THREAD_H
#define MBK_THREAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*mbk_task_fn)(void* arg);

/* 启动常驻内核线程（幂等）。返回 1=K 已就绪（消息队列已建立）。 */
int   mbk_kernel_start(void);

/* K 的线程 id（未启动返回 0） */
DWORD mbk_kernel_tid(void);

/* 当前线程是否就是 K */
int   mbk_on_kernel_thread(void);

/* 在 K 上同步执行 fn(arg)。K 上重入时就地执行；K 未就绪时兜底就地执行。 */
void  mbk_kernel_run(mbk_task_fn fn, void* arg);

#ifdef __cplusplus
}
#endif
#endif /* MBK_THREAD_H */
