/* ============================================================================
 * mbk_marshal.h —— 导出边界编组的共享载荷与入口（2026-09-25）
 * ----------------------------------------------------------------------------
 * 背景：miniblink 内核线程亲和（属主不可迁移、finalize 空实现、换拷贝停摆，
 *       全部实测）。唯一通路 = 内核永远落在 MbKit 自有的常驻线程 K 上，
 *       其余线程的内核调用全部同步编组过去。
 *
 * 编组做在 **MbKit.dll 导出边界**：gen_fwd.py 按 .DLL命令 签名表为每个
 * wke* / js* 导出生成「编组 thunk」（gen_thunk.c），调用线程收参打包成
 * MBKCALL，经 mbk_marshal_exec 投递到属主线程（K）上执行，返回值带回。
 * —— 这是唯一能同时救 demo 与用户真实工程（MB浏览框 裸调 wke*）的层。
 * ==========================================================================*/
#ifndef MBK_MARSHAL_H
#define MBK_MARSHAL_H

/* 编组调用载荷：每参数占一个 8 字节槽。
 *   4 字节参数（整数/字节/逻辑/文本/字节集/结构指针）→ 零扩展存入；
 *   小数型(float)      → 按 32 位位型存入（不经浮点寄存器，避免失真）；
 *   长整数/双精度      → 原值/位型直接存入。
 * 返回值按 kind 从 ret 取（0=void 1=u32 2=s64 3=f32 4=f64）。 */
typedef struct MBKCALL {
    int  idx;                              /* MBK_IDX_*（gen_fwd.h） */
    void (*tramp)(struct MBKCALL*);        /* K 侧执行体（gen_thunk.c 逐 API 生成） */
    void* real;                            /* 派发时从 mbk_slots[idx] 取（保留拦截层） */
    long long a[12];                       /* 参数包（当前最大 11 参） */
    int  kind;
    union { unsigned u; long long l; float f; double d; } ret;
} MBKCALL;

/* 把一次内核调用编组到属主线程执行。
 *   · 属主线程上调用 → 就地执行（回调重入安全，不自等死锁）；
 *   · 其它线程       → 投递到 K 并同步等待；
 *   · 兼容模式跨线程 → 明确失败（ret = 0），绝不跨线程硬闯；
 *   · 内核尚未启动   → 先 EnsureKernelInit（首个内核调用即把内核落 K）。 */
void mbk_marshal_exec(MBKCALL* c);

/* K 侧执行体：gen_thunk.c 逐 API 生成，按真实签名从 c->a[] 解包调用真函数 */
typedef void (*mbk_tramp_fn)(MBKCALL*);

/* K 侧执行体分发表（gen_thunk.c 定义，下标 = MBK_IDX_*；
 * NULL = 不经编组的语义特例，即 wkeInitialize / wkeInitializeEx） */
extern const mbk_tramp_fn mbk_tramps[];

/* (void*,void*,void*) -> unsigned 三参便捷版（回调登记层 mbkit_cb.c 用） */
unsigned mbk_marshal_3(int idx, unsigned a0, unsigned a1, unsigned a2);

#endif /* MBK_MARSHAL_H */
