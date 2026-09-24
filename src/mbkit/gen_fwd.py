#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_fwd.py —— 由「易语言 .DLL命令 声明表」生成 MbKit 的兼容转发层。

输入：一份 JSON 清单（由 e2t 产物 dll.e.txt 解析得到），每项：
    { "name": "wkeCreateWebView", "ret": "整数型"|"", "params": [[名,类型,标志], ...] }

输出（默认写入脚本同目录）：
    gen_thunk.c  每个 API 一个「编组 thunk」+ K 侧签名化执行体（2026-09-25 起
                 取代 gen_fwd.S 的纯 jmp 直通 —— 内核线程亲和要求全部内核调用
                 编组到常驻线程 K，见 mbk_marshal.h）
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
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def mbkit_api_exports():
    """抽出全部需要导出的 `MbKit_*` 名字（保序去重）。

    数据来源 = **声明与实现两处取并集**：
      * mbkit_api.h                 —— 对外契约（声明）
      * mbkit.c / mbkit_fwd.c / mbkit_cb.c —— 实际实现（有些内部工具接口只写在 .c 里，
                                              从没进过头文件，例如 MbKit_CbHits、
                                              MbKit_KernelPath、MbKit_MissingList）
    只扫头文件会漏掉后者 —— 2026-09-25 实测漏了 12 个，会让既有导出凭空消失。
    两边一起扫，才能保证「旧的不丢、新的一件不漏」。

    ★ 背景：此处原先是**手工硬编码**的 21 个名字，漏了
      Create / Destroy / LoadUrl / LoadHtml / RunJs / Resize / SetZoom /
      SetHeadless / IsLoaded / GetTitle / ForcePaint / Pump / Version /
      NodePath / NodeModule / SelfModule / KernelInitCount / Pinned /
      AliveModule 共 19 个 —— 于是 host_reload.exe 侧 mbk_load() 的
      GetProcAddress 必定失败，host_reload 直接报「加载 MbKit.dll 失败」，
      run_tests.sh 整条回归链**静默失效**（最坏的那种坏：看起来在跑）。
    """
    srcs = ["mbkit_api.h", "mbkit.c", "mbkit_fwd.c", "mbkit_cb.c"]
    seen, out = set(), []
    for name in srcs:
        p = os.path.join(HERE, name)
        if not os.path.exists(p):
            continue
        txt = io.open(p, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"__stdcall\s+(MbKit_[A-Za-z0-9_]+)\s*\(", txt):
            n = m.group(1)
            if n not in seen:
                seen.add(n)
                out.append(n)
    return out

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

    # ---------- gen_thunk.c（2026-09-25 取代 gen_fwd.S）----------
    # 每个转发导出生成一个「编组 thunk」+ 一个 K 侧执行体（tramp）：
    #   thunk（调用线程）：按**真实签名**收参 → 打包进 MBKCALL → mbk_marshal_exec
    #   tramp（属主线程）：按**真实签名**解包 → 调内核真函数 → 回填返回值
    # 为什么必须逐 API 生成而不是盲抄栈：参数宽度不同（长整数 8 字节、小数型
    # 4 字节位型、双精度 8 字节），且返回值宽度不同（wkeRunJSW 返回 64 位）。
    # 签名来源 = fwd_list.json（.DLL命令 声明表，与 gen_module.py 同源）。
    #
    # 语义特例（2026-09-25 团队定案）：
    #   wkeInitialize / wkeInitializeEx 不再 passthrough —— 改为「确保内核已在
    #   常驻线程 K 上启动」（等价 MbKit_EnsureKernelInit）。自此内核**永远**落 K，
    #   业务线程生死与内核归属彻底解耦（这正是修「重载 DLL 无法二次初始化」的核心）。
    #   ⚠️ wkeInitializeEx 的配置参数被忽略（如需透传，后续在 k_boot 里接）。
    # ----------------------------------------------------------------
    RET_KIND = {"": 0, "整数型": 1, "逻辑型": 1, "文本型": 1, "字节集": 1,
                "短整数型": 1, "字节型": 1, "长整数型": 2, "小数型": 3,
                "双精度小数型": 4}

    def c_type(t):
        """易语言类型 -> (C 类型, 打包方式)  4 字节一律 unsigned；8 字节见名"""
        if t == "长整数型":     return "long long"
        if t == "双精度小数型": return "double"
        if t == "小数型":       return "float"
        return "unsigned"       # 整数型/字节型/逻辑型/文本型/字节集/短整数型/结构(指针)

    def ret_access(t):
        """tramp 内回填返回值的左值 —— 直接写进 MBKCALL 的 ret 成员（K 侧）"""
        k = RET_KIND[t]
        return {0: None, 1: "c->ret.u", 2: "c->ret.l",
                3: "c->ret.f", 4: "c->ret.d"}[k]

    # 位型打包/解包助手（小数型按 4 字节位型搬运，避免经过浮点寄存器失真）
    c = [HEADER,
         "/* 编组 thunk —— 本文件由 gen_fwd.py 自动生成，请勿手改。\n"
         " * 每个导出 = 调用线程侧收参打包(tramp 指针+参数包) + K 侧签名化执行体。\n"
         " * 详见 mbk_marshal.h 与 mbkit_fwd.c 的 mbk_marshal_exec。\n"
         " */\n",
         "#define WIN32_LEAN_AND_MEAN\n",
         "#include <windows.h>\n",
         "#include \"gen_fwd.h\"\n",
         "#include \"mbk_marshal.h\"\n\n",
         "/* 小数型按位型搬运（4/8 字节，不经浮点寄存器；memcpy 免严格别名问题） */\n",
         "static long long mbk_f32bits(float f)  { unsigned u; memcpy(&u, &f, 4); return (long long)u; }\n",
         "static float     mbk_bitsf32(long long v) { unsigned u = (unsigned)(unsigned long long)v; float f; memcpy(&f, &u, 4); return f; }\n",
         "static long long mbk_f64bits(double d) { long long r; memcpy(&r, &d, 8); return r; }\n",
         "static double    mbk_bitsf64(long long v){ double d; memcpy(&d, &v, 8); return d; }\n\n"]

    # 被接管的 22 个回调登记类不生成公开 thunk（导出由 mbkit_cb.c 实现），
    # 但**必须**生成 K 侧执行体（tr_xxx）：mbkit_cb.c 的登记/注销也要过编组 ——
    # 否则它们对内核的直调在跨线程时会挂死（marshal_probe r2/r3/r4 实测）。
    gen_thunk = []      # (name, is_intercept)
    for n in fwd:
        if n in ("wkeInitialize", "wkeInitializeEx"):
            continue                     # 语义特例，文件尾单独生成
        gen_thunk.append((n, False))
    for n in itc:
        gen_thunk.append((n, True))

    table_entries = []                  # mbk_tramps[] 的 designated initializer
    for n, is_itc in gen_thunk:
        info = by[n]
        ps = info.get("params", [])
        rt = info.get("ret", "")
        rct = c_type(rt) if rt else "void"
        pcts = [c_type(p[1]) for p in ps]

        # ---- K 侧执行体：按真实签名解包并调用真函数 ----
        arglist = ", ".join("%s a%d" % (t, i) for i, t in enumerate(pcts))
        if not arglist:
            arglist = "void"
        proto = "%s (*fn)(%s)" % (rct, arglist) if arglist != "void" else "%s (*fn)(void)" % rct
        cast  = "(%s (__cdecl *)(%s))c->real" % (rct, arglist if arglist != "void" else "void")
        call_args = []
        for i, t in enumerate(pcts):
            if t == "unsigned":      call_args.append("(unsigned)c->a[%d]" % i)
            elif t == "long long":   call_args.append("c->a[%d]" % i)
            elif t == "float":       call_args.append("mbk_bitsf32(c->a[%d])" % i)
            elif t == "double":      call_args.append("mbk_bitsf64(c->a[%d])" % i)
        c.append("static void tr_%s(MBKCALL* c)\n{\n" % n)
        c.append("    %s = %s;\n" % (proto, cast))
        c.append("    %sfn(%s);\n" % (
            ("%s = " % ret_access(rt)) if rt else "", ", ".join(call_args)))
        c.append("}\n\n")
        table_entries.append((idx[n], n))

        if is_itc:
            continue                     # 被接管项：只要执行体，不要公开 thunk

        # ---- 调用线程侧 thunk：按真实签名收参打包（返回类型也用真实类型） ----
        if arglist == "void":
            pub = "%s __cdecl %s(void)" % (rct, n)
        else:
            pub = "%s __cdecl %s(%s)" % (rct, n, ", ".join(
                "%s a%d" % (t, i) for i, t in enumerate(pcts)))
        c.append("%s\n{\n" % pub)
        c.append("    MBKCALL c;\n")
        c.append("    ZeroMemory(&c, sizeof(c));\n")
        c.append("    c.idx = MBK_IDX_%s; c.tramp = tr_%s; c.kind = %d;\n"
                 % (n, n, RET_KIND[rt]))
        for i, t in enumerate(pcts):
            if t == "unsigned":    c.append("    c.a[%d] = (long long)(unsigned)a%d;\n" % (i, i))
            elif t == "long long": c.append("    c.a[%d] = a%d;\n" % (i, i))
            elif t == "float":     c.append("    c.a[%d] = mbk_f32bits(a%d);\n" % (i, i))
            elif t == "double":    c.append("    c.a[%d] = mbk_f64bits(a%d);\n" % (i, i))
        if rt:
            c.append("    mbk_marshal_exec(&c);\n")
            acc = ret_access(rt)             # 形如 c->ret.u；调用线程侧改用点号
            if rt == "字节型":
                c.append("    return (unsigned char)%s;\n" % acc.replace("->", "."))
            else:
                c.append("    return %s;\n" % acc.replace("->", "."))
        else:
            c.append("    mbk_marshal_exec(&c);\n")
        c.append("}\n\n")

    # ---- 语义特例：初始化类导出改为「确保内核落 K」 ----
    c.append("/* ---- 语义特例（2026-09-25）：初始化不再绑定调用线程 ---- */\n")
    c.append("void __cdecl wkeInitialize(void)\n{\n")
    c.append("    extern int __stdcall MbKit_EnsureKernelInit(void);\n")
    c.append("    MbKit_EnsureKernelInit();   /* 内核落常驻线程 K，调用线程从此无关 */\n")
    c.append("}\n\n")
    c.append("void __cdecl wkeInitializeEx(unsigned a0)\n{\n")
    c.append("    extern int __stdcall MbKit_EnsureKernelInit(void);\n")
    c.append("    (void)a0;                    /* 配置参数暂被忽略（见文件头说明） */\n")
    c.append("    MbKit_EnsureKernelInit();\n")
    c.append("}\n\n")

    # ---- K 侧执行体分发表：mbkit_fwd.c 的 mbk_marshal_exec / mbk_marshal_3
    #      按 MBK_IDX_* 取执行体。初始化特例两项目为 NULL（不经编组）。 ----
    c.append("/* K 侧执行体分发表（下标 = MBK_IDX_*；NULL = 不经编组的特例） */\n")
    c.append("const mbk_tramp_fn mbk_tramps[MBK_ALL_COUNT] = {\n")
    for i, n in sorted(table_entries):
        c.append("    [%d] = tr_%s,\n" % (i, n))
    c.append("};\n\n")

    io.open(os.path.join(HERE, "gen_thunk.c"), "w", encoding="utf-8", newline="\n").write("".join(c))

    # ---------- gen_fwd.def ----------
    # 注意：DEF 文件的注释符是 ';'，写成 /* */ 会让 ld 把整个文件当链接脚本解析
    d = ["; 本文件由 gen_fwd.py 自动生成 —— 请勿手改。\n",
         "; 导出名必须是朴素名（无 _ 前缀、无 @N 后缀）—— 易语言 .DLL命令 按此解析。\n",
         "EXPORTS\n"]
    for n in names:
        d.append("    %s\n" % n)
    # 也导出 MbKit 自己的接口（由 mbkit.c / mbkit_fwd.c / mbkit_cb.c 提供）
    # —— ★ 由 mbkit_api.h 自动抽取（见 mbkit_api_exports 的说明），不再手写清单
    api = mbkit_api_exports()
    if not api:
        raise SystemExit("gen_fwd.py: 没能从 mbkit_api.h 抽出任何 MbKit_* 导出，生成中止")
    for n in api:
        d.append("    %s\n" % n)
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
    print("  编组 %d 项 -> gen_thunk.c（含 K 侧执行体；gen_fwd.S 已废弃）" % len(fwd))
    print("  接管 %d 项 -> 由 mbkit_cb.c 实现：" % len(itc))
    for n in itc:
        print("      ", n)
    print("  导出   %d 项 -> gen_fwd.def" % len(names))


if __name__ == "__main__":
    main()
