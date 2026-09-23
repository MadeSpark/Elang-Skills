#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fwd.py —— 由「易语言 .DLL命令 声明表」生成 MbKit 的兼容转发层。

输入：一份 JSON 清单（由 e2t 产物 dll.e.txt 解析得到），每项：
    { "name": "wkeCreateWebView", "ret": "整数型"|"", "params": [[名,类型,标志], ...] }

输出（默认写入脚本同目录）：
    gen_fwd.S    每个 API 一个汇编 stub —— 纯跳板，不碰栈、不碰寄存器（除 eax）
    gen_fwd.def  导出名表（含被 C 侧接管的拦截项）
    gen_fwd.h    名字表，供 C 侧 lazily 解析 node.dll 导出

设计要点（决定了它为什么必须这么写）：
  * 转发必须**完全透明**：调用约定（cdecl）、参数个数、返回值、栈平衡
    一律不碰，所以 stub 里只有 `jmp [slot]`，绝不写 push/pop。
  * **懒加载**：首次调用时才 LoadLibrary(node.dll) 并填表，
    避免在 DllMain 里加载依赖（loader lock 风险）。
  * 被拦截的 API（回调注册类）不出现在 .S 里，改由 C 实现后同名导出。
"""
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# 这些 API 由 C 侧接管（关心登记/存活校验），不做透明转发
INTERCEPT = set([
    "wkeOnAlertBox", "wkeOnConfirmBox", "wkeOnConsole", "wkeOnCreateView",
    "wkeOnDidCreateScriptContext", "wkeOnDocumentReady", "wkeOnDocumentReady2",
    "wkeOnDownload", "wkeOnLoadUrlBegin", "wkeOnLoadUrlEnd", "wkeOnLoadingFinish",
    "wkeOnMouseOverUrlChanged", "wkeOnNavigation", "wkeOnPaintUpdated",
    "wkeOnPromptBox", "wkeOnTitleChanged", "wkeOnURLChanged", "wkeOnURLChanged2",
    "wkeOnWillMediaLoad", "wkeOnWillReleaseScriptContext", "wkeOnWindowClosing",
    "wkeOnWindowDestroy",
])

HEADER = """/* 本文件由 gen_fwd.py 自动生成 —— 请勿手改。
 * 数据来源：易语言模块「miniblink」的 .DLL命令 声明表（e2t 产物）。
 * 重新生成：python gen_fwd.py <清单.json>
 */
"""


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "fwd_list.json")
    data = json.load(io.open(src, encoding="utf-8"))

    # 按名字去重，保持稳定顺序
    by = {}
    for c in data:
        by.setdefault(c["name"], c)
    names = sorted(by)

    fwd = [n for n in names if n not in INTERCEPT]
    itc = [n for n in names if n in INTERCEPT]
    idx = {n: i for i, n in enumerate(names)}   # 全量索引（含被接管项）

    # ---------- gen_fwd.S ----------
    s = [HEADER, "\t.text\n"]
    for n in fwd:
        sym = "_" + n
        off = idx[n] * 4
        s.append("\t.globl %s\n" % sym)
        s.append("%s:\n" % sym)
        s.append(f"\tmovl\t_mbk_slots+{off}, %eax\n")
        s.append("\ttestl\t%eax, %eax\n")
        s.append("\tjne\t1f\n")
        s.append("\tcall\t_mbk_ensure\n")
        s.append(f"\tmovl\t_mbk_slots+{off}, %eax\n")
        s.append("1:\n")
        s.append("\tjmp\t*%eax\n")
    io.open(os.path.join(HERE, "gen_fwd.S"), "w", encoding="utf-8", newline="\n").write("".join(s))

    # ---------- gen_fwd.def ----------
    # 注意：DEF 文件的注释符是 ';'，写成 /* */ 会让 ld 把整个文件当链接脚本解析
    d = ["; 本文件由 gen_fwd.py 自动生成 —— 请勿手改。\n",
         "; 导出名必须是朴素名（无 _ 前缀、无 @N 后缀）—— 易语言 .DLL命令 按此解析。\n",
         "EXPORTS\n"]
    for n in names:
        d.append("    %s\n" % n)
    # 也导出 MbKit 自己的接口（由 mbkit.c / mbkit_fwd.c / mbkit_cb.c 提供）
    d.append("    MbKit_Init\n    MbKit_IsReady\n    MbKit_DetachAll\n"
             "    MbKit_LastError\n    MbKit_SuppressedCount\n    MbKit_CbHits\n"
             "    MbKit_FwdIndex\n    MbKit_KernelPath\n    MbKit_KernelModule\n"
             "    MbKit_FwdError\n    MbKit_CbCount\n    MbKit_CbBlocked\n"
             "    MbKit_CbLive\n"
             "    MbKit_DetachView\n    MbKit_MissingCount\n    MbKit_MissingHits\n    MbKit_MissingList\n"
             "    MbKit_SetNodePath\n    MbKit_ConfigNodePath\n"
             "    MbKit_KernelLoaded\n    MbKit_Diag\n")
    io.open(os.path.join(HERE, "gen_fwd.def"), "w", encoding="utf-8", newline="\n").write("".join(d))

    # ---------- gen_fwd.h ----------
    h = [HEADER, "#ifndef MBK_GEN_FWD_H\n#define MBK_GEN_FWD_H\n\n"]
    h.append("/* _mbk_slots[] 覆盖全部 API（含被 C 侧接管者），下标见 MBK_IDX_* */\n")
    h.append("#define MBK_ALL_COUNT %d\n" % len(names))
    h.append("#define MBK_FWD_COUNT %d   /* 其中走汇编转发桩的 */\n\n" % len(fwd))
    h.append("/* 由 mbkit_fwd.c 提供 */\n")
    h.append("extern void* mbk_slots[MBK_ALL_COUNT];\n")
    h.append("extern int   mbk_ensure(void);\n\n")
    h.append("static const char* const mbk_fwd_names[MBK_ALL_COUNT]\n"
             "    __attribute__((unused)) = {\n")
    for n in names:
        h.append('    "%s",\n' % n)
    h.append("};\n\n")
    h.append("/* 名字 -> _mbk_slots 下标（拦截层用它取内核真函数） */\n")
    for n in names:
        h.append("#define MBK_IDX_%-36s %3d\n" % (n, idx[n]))
    h.append("\n#define MBK_INTERCEPT_COUNT %d\n" % len(itc))
    h.append("/* 内核真函数取值宏 */\n")
    h.append("#define MBK_REAL(idx) (mbk_slots[idx])\n")
    h.append("\n#endif\n")
    io.open(os.path.join(HERE, "gen_fwd.h"), "w", encoding="utf-8", newline="\n").write("".join(h))

    print("生成完毕：")
    print("  转发 %d 项 -> gen_fwd.S / gen_fwd.h" % len(fwd))
    print("  接管 %d 项 -> 由 mbkit_cb.c 实现：" % len(itc))
    for n in itc:
        print("      ", n)
    print("  导出   %d 项 -> gen_fwd.def" % len(names))


if __name__ == "__main__":
    main()
