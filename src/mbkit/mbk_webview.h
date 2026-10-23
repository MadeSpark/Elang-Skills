/* ============================================================================
 * mbk_webview.h —— edgewebview（WebView2）内核后端的 C 契约
 * ----------------------------------------------------------------------------
 * 【这是什么】
 *   MbKit 原来只有一个内核：miniblink 的 node.dll（33 MB）。
 *   本模块是**第二个内核后端**：用本机已安装的 Microsoft Edge WebView2 Runtime
 *   跑页面，wke* 的语义由 mbk_webview.cpp 里的一层适配实现。
 *
 * 【怎么选内核】（见 mbkit_fwd.c 的 bind_all）
 *   1) 「强制使用node内核」= 真               -> 一定走 node.dll
 *   2) 本机检测到 WebView2 Runtime            -> 走 edgewebview（默认）
 *   3) 都不满足 / edgewebview 起不来          -> 走 node.dll（原路）
 *   检测与启动失败都会写进 mbkwv_diag()，并回退 node，不会把宿主丢在半路。
 *
 * 【为什么槽表由本层填】
 *   mbk_slots[] 是唯一的内核调用入口（gen_thunk.c 的 K 侧执行体按真实签名
 *   强制转型后调它）。把本层的实现填进去，编组、属主线程、回调存活校验
 *   （mbkit_cb.c 的 wrapper）全部原样复用 —— 一行都不用改。
 *
 * 【线程】
 *   全部实现都只会被 mbk_marshal_exec 派到「内核属主线程」上执行；
 *   WebView2 的异步完成回调也投回同一条线程的消息队列，由宿主的消息循环泵。
 * ==========================================================================*/
#ifndef MBK_WEBVIEW_H
#define MBK_WEBVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/* 「强制使用node内核」开关（易语言 `WKE设置node路径` 的第三个参数）。
 * 只在内核**尚未加载**时接受修改；加载后只能查询。 */
int          mbkwv_is_forced(void);
int          mbkwv_set_force(int on);

/* 本机是否有可用的 WebView2 Runtime（1=有）。只读探测，不加载任何东西。 */
int          mbkwv_available(void);
const char*  mbkwv_runtime_version(void);   /* 版本串；不可用时返回空串 */

/* 决策 + 装填槽表。返回 1 = edgewebview 已接管（调用方随后应把 g_hNode 指到
 * 自身模块、并跳过 node.dll 的加载）；返回 0 = 用 node.dll（原路继续）。
 *   slots      —— mbk_slots[]
 *   count      —— 槽位个数（MBK_ALL_COUNT）
 *   names      —— mbk_fwd_names[]（与槽位同下标）
 *   selfModule —— MbKit.dll 自己的 HMODULE（bind_symbols 靠它取符号） */
int          mbkwv_bind(void** slots, int count, const char* const* names,
                        void* selfModule);

/* 已接管 = 1（供诊断与 mbk_set_node_path 的幂等判定） */
int          mbkwv_active(void);

/* 一行式诊断：为什么是/不是 edgewebview、Runtime 版本、启动结果 */
const char*  mbkwv_diag(void);

/* 当前实际使用的内核名字（给 MbKit_NodePath / 版本串用） */
const char*  mbkwv_kernel_name(void);

#ifdef __cplusplus
}
#endif
#endif /* MBK_WEBVIEW_H */
