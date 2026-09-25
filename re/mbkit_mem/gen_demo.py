#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_demo.py —— 生成自包含演示工程：内存加载 MbKit → 绑主窗口句柄 → 载网页

产物目录：re/mbkit_mem/demo_proj/（e2t 文本布局，可直接 t2e 回 .e）
演示行为：创建主窗口 → 把内嵌的 MbKit.dll 手工映射进内存 → 走 wke* 链路
          （全部经「固定 VA 槽 + 机器码尾跳」调用）→ 载入 https://www.madespark.cc/
          → 主循环里 wkeRepaintIfNeeded + BitBlt(wkeGetViewDC) 贴到窗口
          → 写 demo.log
"""
import io
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import memgen as G                                    # noqa: E402

ROOT = G.ROOT
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")

# ---- 二分定位用的开关（默认全量、带 blob） --------------------------------------
#   DEMO_LIMIT=26  只保留演示真正调用的那批导出（隔离「包装子程序数量」这一维）
#   DEMO_NOBLOB=1  把映射器 blob 换成空壳（隔离「10 KB 单行置入代码数组」这一维）
#   DEMO_TAG=xxx   产物目录后缀，便于一次跑多个变体
LIMIT_ONLY = int(os.environ.get("DEMO_LIMIT", "0") or 0)
NOBLOB = os.environ.get("DEMO_NOBLOB") == "1"
OUT = os.path.join(HERE, "demo_proj" + (os.environ.get("DEMO_TAG") or ""))

K32 = "kernel32.dll"
U32 = "user32.dll"
G32 = "gdi32.dll"


# ---- 系统库声明：参数名一律用官方约定 `_参数N` ------------------------------------
# ★★ 2026-09-24 血泪结论（有一手对照）★★
#   原模块里那 78 条系统库命令（含 8 参的 WideCharToMultiByte、6 参的 MultiByteToWideChar、
#   4 参的 VirtualAlloc）参数名**清一色是 `_参数1.._参数N`**，它能被用户正常编译。
#   我按「可读性」把它们改写成中文名（窗口 / 参数 / 菜单 / 类型 / 实例 / 命令 …）之后，
#   e.exe 在**解析工程**阶段直接 0xC0000005 崩掉（无任何 stdout、trace 停在 COMPILE trigger）。
#   原因是这些名字撞上了易语言的关键字/类型名：
#       `参数` = `.参数` 声明关键字        `窗口` / `菜单` = 内置组件类型名
#       `类型` / `实例` = 保留概念名
#   ⚠️ 只有「恰好等于关键字」才致命：`参数_文本` / `参数_VirtualAlloc` 这类前缀名都没事
#      （B1 用例实测通过）。所以规则是：**DLL 命令的参数名从头到尾只用 `_参数N`**。
#   为了不丢可读性，人类友好的名字留在下面这个表里，只在生成时打印，不写进 DLL.e.txt。
PARAM_DOC = {
    "_VA": "地址, 长度, 类型, 保护",
    "_VF": "地址, 长度, 类型",
    "_GMF": "模块名",
    "_GPA": "模块句柄, 函数名",
    "_LLA": "文件",
    "_SL": "毫秒",
    "_CWEA": "扩展风格, 类名, 标题, 风格, 左, 上, 宽, 高, 父窗口, 菜单, 实例, 参数",
    "_SW": "窗口, 命令",
    "_SFW": "窗口",
    "_UW": "窗口",
    "_DW": "窗口",
    "_PMW": "消息, 窗口, 最小, 最大, 移除",
    "_TMW": "消息",
    "_DMW": "消息",
    "_GDC": "窗口",
    "_RDC": "窗口, 场景",
    "_BB": "目标, x, y, 宽, 高, 源, 源x, 源y, 光栅",
    "_WCMB": "代码页, 标志, 源, 源长, 目标, 目标容量, 默认字符, 已用默认",
    "_RTLMOVE": "目标, 源, 长度（RtlMoveMemory，用来把 #Mapper 资源拷进固定 VA 池）",
}


def dll_cmd(name, ret, lib, entry, params=()):
    L = ['.DLL命令 %s, %s, "%s", "%s"' % (name, ret, lib, entry)]
    for i, t in enumerate(params, 1):
        typ = t[1] if isinstance(t, tuple) else "整数型"
        L.append("    .参数 _参数%d, %s" % (i, typ))
    L.append("")
    return "\n".join(L)


DLL = "\n".join([
    ".版本 2",
    "",
    dll_cmd("_VA", "整数型", K32, "VirtualAlloc", ["地址", "长度", "类型", "保护"]),
    dll_cmd("_VF", "整数型", K32, "VirtualFree", ["地址", "长度", "类型"]),
    dll_cmd("_GMF", "整数型", K32, "GetModuleHandleA", [("模块名", "文本型")]),
    dll_cmd("_GPA", "整数型", K32, "GetProcAddress",
            [("模块句柄", "整数型"), ("函数名", "文本型")]),
    dll_cmd("_LLA", "整数型", K32, "LoadLibraryA", [("文件", "文本型")]),
    dll_cmd("_SL", "整数型", K32, "Sleep", ["毫秒"]),
    # 用来把 #Mapper 资源搬进固定 VA 池（无返回值，写法照官方 `_P_wkeInitialize, , "MbKit.dll"`）
    dll_cmd("_RTLMOVE", "", K32, "RtlMoveMemory", ["目标", "源", "长度"]),
    # ★ 建窗口走 ANSI + Windows 预注册的对话框类 "#32770"：
    #   不需要 RegisterClassW / WNDCLASSW，也不需要 LoadCursorW / MultiByteToWideChar，
    #   更不会出现「把整数字面量 0 传给文本型参数」这种会让 e.exe 解析器崩掉的写法。
    dll_cmd("_CWEA", "整数型", U32, "CreateWindowExA",
            ["扩展风格", "类名", "标题", "风格", "左", "上", "宽", "高",
             "父窗口", "菜单", "实例", "参数"]),
    dll_cmd("_SW", "整数型", U32, "ShowWindow", ["窗口", "命令"]),
    dll_cmd("_SFW", "整数型", U32, "SetForegroundWindow", ["窗口"]),
    dll_cmd("_UW", "整数型", U32, "UpdateWindow", ["窗口"]),
    dll_cmd("_DW", "整数型", U32, "DestroyWindow", ["窗口"]),
    dll_cmd("_PMW", "整数型", U32, "PeekMessageW",
            ["消息", "窗口", "最小", "最大", "移除"]),
    dll_cmd("_TMW", "整数型", U32, "TranslateMessage", ["消息"]),
    dll_cmd("_DMW", "整数型", U32, "DispatchMessageW", ["消息"]),
    dll_cmd("_GDC", "整数型", U32, "GetDC", ["窗口"]),
    dll_cmd("_RDC", "整数型", U32, "ReleaseDC", ["窗口", "场景"]),
    dll_cmd("_BB", "整数型", G32, "BitBlt",
            ["目标", "x", "y", "宽", "高", "源", "源x", "源y", "光栅"]),
    dll_cmd("_WCMB", "整数型", K32, "WideCharToMultiByte",
            ["代码页", "标志", "源", "源长", "目标", "目标容量", "默认字符", "已用默认"]),
])

# ------------------------------------------------------------------ 演示用到的导出
DEMO_NEED = [
    "wkeIsInitialize", "wkeInitialize", "wkeCreateWebView", "wkeSetHandle",
    "wkeSetHandleOffset", "wkeResize", "wkeShowWindow", "wkeLoadURL",
    "wkeWake", "wkeRepaintIfNeeded", "wkeGetViewDC", "wkeGetTitleW",
    "wkeGetURL", "wkeIsLoadingCompleted", "wkeIsLoading", "wkeIsDocumentReady",
    "wkeGetWidth", "wkeGetHeight", "wkeDestroyWebView", "wkeFinalize",
    "MbKit_SetNodePath", "MbKit_KernelPath", "MbKit_Diag", "MbKit_IsReady",
    "MbKit_KernelLoaded", "MbKit_ConfigNodePath",
]


def _entry(d):
    e = d["entry"]
    return e[1:] if e.startswith("@") else e


# 置入代码约定自检用的 12 字节机器码：
#   mov eax,[ebp+8] / add eax,[ebp+12] / mov esp,ebp / pop ebp / ret 8
ADD_SUB = [0x8B, 0x45, 0x08, 0x03, 0x45, 0x0C, 0x8B, 0xE5, 0x5D, 0xC2, 0x08, 0x00]


def mapper_code():
    """映射器机器码：现在是 49 B 的小前导（blob 改走 #Mapper 资源 + RtlMoveMemory）。
    DEMO_NOBLOB=1 时只留收尾（等价于「不干活」），用来隔离 blob 这一维。"""
    if NOBLOB:
        return [0x8B, 0xE5, 0x5D, 0xC2, 0x30, 0x00]      # mov esp,ebp; pop ebp; ret 48
    return G.mapper_code()


def build():
    info = G.pe_exports(G.DLL)
    ex = info["exports"]
    decls = G.parse_decls(os.path.join(G.CHK, "dll.e.txt"))
    mb = [d for d in decls if "MbKit" in d["lib"]]
    if LIMIT_ONLY:
        mb = [d for d in mb if _entry(d) in DEMO_NEED]
    structs = {}
    for f in (os.path.join(G.CHK, "struct.e.txt"),):
        t = io.open(f, encoding="utf-8-sig").read().replace("\r\n", "\n")
        for ln in t.split("\n"):
            m = __import__("re").match(r"^\.数据类型\s+(\S+?)(?:,|$)", ln.strip())
            if m:
                structs[m.group(1)] = 1

    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    shutil.copytree(SCAFFOLD, OUT)

    # ---- 每个声明：槽号 / 参数 / 机器码 -------------------------------------------------
    slots, code_blocks, byname = [], [], {}
    slot = 0
    ftmp = G.POOL + 0x800
    for d in mb:
        params, nbytes, risks = G.layout(d, structs)
        if params is None or risks:
            raise SystemExit("布局失败：%s %s" % (d["name"], risks))
        entry = d["entry"][1:] if d["entry"].startswith("@") else d["entry"]
        if entry not in ex:
            raise SystemExit("导出表里没有 %s" % entry)
        code = G.wrap_code(params, nbytes, slot, d["ret"], ftmp)
        if d["ret"] in ("小数型", "双精度小数型"):
            ftmp += 8
        slots.append((slot, d["name"], entry, ex[entry]))
        code_blocks.append(G.sub(d["name"], [(p["name"], p["type"] + (", " + p["flags"] if p["flags"] else ""))
                                             for p in params], code, d["ret"]))
        byname[entry] = d["name"]
        slot += 1

    # ---- 程序集文本 -------------------------------------------------------------
    head = "\n".join([
        ".版本 2",
        "",
        ".程序集 程序集1",
        ".程序集变量 _池, 整数型",
        ".程序集变量 _映像, 整数型",
        ".程序集变量 _状态, 整数型",
        ".程序集变量 _窗口, 整数型",
        ".程序集变量 _浏览框, 整数型",
        ".程序集变量 _日志, 文本型",
        "",
        G.plain_sub("报告", [("参数_文本", "文本型")], [
            "    _日志 ＝ _日志 ＋ 参数_文本 ＋ 字符 (13) ＋ 字符 (10)",
            "    写到文件 (取运行目录 () ＋ “\\demo.log”, 到字节集 (_日志))",
        ]),
        G.sub("_取字节集指针", [("数据", "字节集")], G.BI_PTR, "整数型"),
        G.sub("_取文本指针", [("文本", "文本型")], G.TEXT_PTR, "整数型"),
        G.sub("_写内存整数", [("地址", "整数型"), ("偏移", "整数型"), ("值", "整数型")], G.WRITE_I),
        G.sub("_读内存整数", [("地址", "整数型"), ("偏移", "整数型")], G.READ_I, "整数型"),
        G.sub("_写槽", [("槽号", "整数型"), ("值", "整数型")], G.SLOT_SET),
        G.sub("_读槽", [("槽号", "整数型")], G.SLOT_GET, "整数型"),
        # ★ 置入代码约定自检：3 + 4 必须等于 7。用来把「置入代码的栈约定错了」与
        #   「超大置入代码数组不可靠」这两件事分开（一次编译就能判定）。
        G.sub("_测加法", [("_参数1", "整数型"), ("_参数2", "整数型")], ADD_SUB, "整数型"),
        G.plain_sub("_初始化映射器", [], [
            "    报告 (“[00g] 映射器 blob 长度 = ” ＋ 到文本 (取字节集长度 (#Mapper)))",
            "    _RTLMOVE (1342179328, _取字节集指针 (#Mapper), 取字节集长度 (#Mapper))",
            "    报告 (“[00h] 映射器已拷贝到 0x50000800”)",
        ]),
        G.sub("_映射映像", G.MAPPER_PARAMS, mapper_code()),
    ])

    blank = "\n"
    wrappers = blank.join(code_blocks)

    # ---- 填槽 & 载入 -------------------------------------------------------------
    fill = []
    for s, name, entry, rva in slots:
        fill.append("    _写槽 (%d, _映像 ＋ %d)" % (s, rva))
    n_by = {e: n for _, n, e, _ in slots}

    def C(entry, args=None):
        """只返回该导出对应的**包装子程序名**（括号与实参一律由调用点的字面量给出）。
        ★ 早期版本这里会自行补 " ()"，而调用点又写 "%s ()" ⇒ 生成出 `名 () ()`，
          e2t 直接判语法错，务必保持「只给名字」。"""
        return n_by[entry]

    tl = [".子程序 _内存载入宿主库, 逻辑型",
          "    .局部变量 内核32, 整数型",
          "    .局部变量 状态, 整数型",
          "    .局部变量 函数VA, 整数型",
          "    .局部变量 函数VF, 整数型",
          "    .局部变量 函数LLA, 整数型",
          "    .局部变量 函数GPA, 整数型",
          "    .局部变量 函数TA, 整数型",
          "    .局部变量 函数TG, 整数型",
          "    .局部变量 函数TS, 整数型",
          "    .局部变量 函数GM, 整数型",
          "",
          "    报告 (“[00] 开始内存载入宿主库”)",
          "    .如果真 (_池 ＝ 0)",
          "        _池 ＝ _VA (1342177280, 8192, 12288, 4)",
          "    .如果真结束",
          "    报告 (“[00a] 固定 VA 池 = 0x” ＋ 取十六进制文本 (_池))",
          "    .如果真 (_池 ＝ 0)",
          "        报告 (“[X] 固定 VA 池分配失败”)",
          "        返回 (假)",
          "    .如果真结束",
          "    内核32 ＝ _GMF (“kernel32.dll”)",
          "    报告 (“[00b] kernel32 模块句柄 = 0x” ＋ 取十六进制文本 (内核32))",
          "    ' ★ 先把 8 个 API 地址各自取到一个局部变量，再一次性传给映射器。",
          "    '   早期版本把这 8 个 _GPA 调用全部嵌在实参位置写成一行，虽然能编过，但可读性差。",
          "    函数VA ＝ _GPA (内核32, “VirtualAlloc”)",
          "    函数VF ＝ _GPA (内核32, “VirtualFree”)",
          "    函数LLA ＝ _GPA (内核32, “LoadLibraryA”)",
          "    函数GPA ＝ _GPA (内核32, “GetProcAddress”)",
          "    函数TA ＝ _GPA (内核32, “TlsAlloc”)",
          "    函数TG ＝ _GPA (内核32, “TlsGetValue”)",
          "    函数TS ＝ _GPA (内核32, “TlsSetValue”)",
          "    函数GM ＝ _GPA (内核32, “GetModuleHandleA”)",
          "    报告 (“[00c] 8 个 API 地址 = 0x” ＋ 取十六进制文本 (函数VA) ＋ “ 0x” ＋ 取十六进制文本 (函数GPA) ＋ “ 0x” ＋ 取十六进制文本 (函数GM))",
          "    _状态 ＝ -1",
          "    报告 (“[00d] 内嵌宿主库长度 = ” ＋ 到文本 (取字节集长度 (#MbKit)))",
          "    报告 (“[00e] 内嵌宿主库数据指针 = 0x” ＋ 取十六进制文本 (_取字节集指针 (#MbKit)))",
          "    报告 (“[00f] 置入代码约定自检：3 + 4 = ” ＋ 到文本 (_测加法 (3, 4)))",
          "    _初始化映射器 ()",
          "    报告 (“[00i] 即将调用手工映射器（两个出参直接写 0x50001800 / 0x50001804）”)",
          "    ' ★ 易语言命令行不允许跨行 ⇒ 这一句必须写成一行",
          "    _映射映像 (函数VA, 函数VF, 函数LLA, 函数GPA, 函数TA, 函数TG, 函数TS, 函数GM, _取字节集指针 (#MbKit), 取字节集长度 (#MbKit), 1342180608, 1342180612)",
          "    _映像 ＝ _读内存整数 (1342180608, 0)",
          "    _状态 ＝ _读内存整数 (1342180612, 0)",
          "    报告 (“[01] 映射器已返回：映像基址 = 0x” ＋ 取十六进制文本 (_映像) ＋ “，状态码 = ” ＋ 到文本 (_状态))",
          "    .如果真 (_映像 ＝ 0 或 _状态 ≠ 0)",
          "        报告 (“[X] 手工映射失败”)",
          "        返回 (假)",
          "    .如果真结束",
          "    报告 (“[01b] 手工映射成功，导出表已就位”)"]
    tl += ["    报告 (“[02] 开始写入 ” ＋ 到文本 (%d) ＋ “ 个函数地址槽”)" % len(slots)]
    tl += fill
    tl += ["    报告 (“[03] 槽位写入完成；抽样核验 槽0 = 0x” ＋ 取十六进制文本 (_读槽 (0)) ＋ “，槽 %d = 0x” ＋ 取十六进制文本 (_读槽 (%d)))"
           % (len(slots) - 1, len(slots) - 1),
           "    返回 (真)",
           ""]
    loader = "\n".join(tl)

    # ---- 分段构造主程序：便于把「到底是哪一块让 e.exe 解析时崩」二分出来 -------
    #   DEMO_STAGE=wrap 只放 258 个包装子程序，主程序一条也不调用
    #   DEMO_STAGE=load 再加「内存载入 + 填函数地址槽」
    #   DEMO_STAGE=win  再加「建窗口」
    #   DEMO_STAGE=wke  再加「建浏览框 / 绑句柄 / 载网页」
    #   DEMO_STAGE=full 全量（默认）
    # 为什么要有这个开关：一次 e.exe 检查 = 用户那边一次 5~10 秒整机卡顿，
    # 所以要把「一次问一个问题」做到极致，而不是每换一个猜测就全量重跑。
    STAGE = os.environ.get("DEMO_STAGE", "full")

    S_HEAD = [
        ".子程序 _启动子程序, 整数型",
        "    .局部变量 消息, 整数型",
        "    .局部变量 类名, 文本型",
        "    .局部变量 标题, 文本型",
        "    .局部变量 网址, 文本型",
        "    .局部变量 显示开关, 字节型",
        "    .局部变量 帧, 整数型",
        "    .局部变量 计数, 整数型",
        "    .局部变量 轮次, 整数型",
        "",
        "    _日志 ＝ “”",
        "    报告 (“==== MbKit 内存加载演示 ====”)",
        "    .如果真 (_内存载入宿主库 () ＝ 假)",
        "        报告 (“[X] 宿主库内存载入失败，终止”)",
        "        返回 (1)",
        "    .如果真结束",
        "    报告 (“[04] 宿主自检：” ＋ 指针到文本 (%s ()))" % C("MbKit_Diag"),
        "    报告 (“[05] 内核路径 = ” ＋ 指针到文本 (%s ()))" % C("MbKit_KernelPath"),
        "",
    ]
    S_WRAPONLY = [
        ".子程序 _启动子程序, 整数型",
        "",
        "    _日志 ＝ “”",
        "    报告 (“==== 仅包装子程序（一条也不调用）====”)",
        "    报告 (“[w1] 槽 0 = 0x” ＋ 取十六进制文本 (_读槽 (0)))",
        "    返回 (0)",
        "",
    ]
    S_WIN = [
        "    ' ---- 建主窗口：直接用 Windows 预注册的对话框类 “#32770” ----",
        "    '   不需要 RegisterClassW / WNDCLASSW；预注册类可以传 0 实例句柄",
        "    类名 ＝ “#32770”",
        "    标题 ＝ “MbKit 内存加载演示 - madespark.cc”",
        "    '   WS_OVERLAPPEDWINDOW = 0x00CF0000，再或上 WS_VISIBLE = 0x10000000 ⇒ 281935872",
        "    _窗口 ＝ _CWEA (0, _取文本指针 (类名), _取文本指针 (标题), 281935872, 60, 60, 1000, 780, 0, 0, 0, 0)",
        "    .如果真 (_窗口 ＝ 0)",
        "        报告 (“[X] CreateWindowExA 建窗口失败”)",
        "        返回 (1)",
        "    .如果真结束",
        "    _SW (_窗口, 5)",
        "    _SFW (_窗口)",
        "    _UW (_窗口)",
        "    报告 (“[06] 主窗口句柄 = 0x” ＋ 取十六进制文本 (_窗口))",
        "",
    ]
    S_WKE = [
        "    ' ---- 初始化内核（等价于模块的 WKE初始化） ----",
        "    .如果真 (%s () ＝ 0)" % C("wkeIsInitialize"),
        "        %s ()" % C("wkeInitialize"),
        "    .如果真结束",
        "    报告 (“[06.5] wkeIsInitialize = ” ＋ 到文本 (%s ()))" % C("wkeIsInitialize"),
        "",
        "    ' ---- 浏览框 + 绑主窗口句柄 ----",
        "    _浏览框 ＝ %s ()" % C("wkeCreateWebView"),
        "    .如果真 (_浏览框 ＝ 0)",
        "        报告 (“[X] 创建浏览框失败”)",
        "        返回 (1)",
        "    .如果真结束",
        "    %s (_浏览框, _窗口)" % C("wkeSetHandle"),
        "    %s (_浏览框, 0, 0)" % C("wkeSetHandleOffset"),
        "    %s (_浏览框, 980, 720)" % C("wkeResize"),
        "    ' ★ wkeShowWindow 的第 2 个参数在原始声明里是**字节型**，所以用字节型量传，别塞整数字面量",
        "    显示开关 ＝ 1",
        "    %s (_浏览框, 显示开关)" % C("wkeShowWindow"),
        "    报告 (“[07] 浏览框 = 0x” ＋ 取十六进制文本 (_浏览框) ＋ “，已把主窗口句柄绑上去”)",
        "    报告 (“[08] 内核路径 = ” ＋ 指针到文本 (%s ()))" % C("MbKit_KernelPath"),
        "",
        "    ' ---- 载网页（走 ANSI 版 wkeLoadURL）----",
        "    ' ★ 它的第 2 个参数是**文本型**：必须直接把文本型量传进去（包装体自己会解引用一次取数据指针）。",
        "    '   早先写成 `_取文本指针 (网址)`（一个整数）⇒ e.exe 解析时 0xC0000005。",
        "    '   对照：老建窗口块里的 `_GMF (0)`（整数字面量喂文本型参数）是同一类错误。",
        "    网址 ＝ “https://www.madespark.cc/”",
        "    %s (_浏览框, 网址)" % C("wkeLoadURL"),
        "    报告 (“[09] 已请求载入 ” ＋ 网址)",
        "",
    ]
    S_LOOP = [
        "    ' ---- 消息循环 + 主循环贴图 ----",
        "    消息 ＝ _VA (0, 64, 12288, 4)",
        "    轮次 ＝ 0",
        "    .计次循环首 (400, 计数)",
        "        .判断循环首 (_PMW (消息, 0, 0, 0, 1) ≠ 0)",
        "            _TMW (消息)",
        "            _DMW (消息)",
        "        .判断循环尾 ()",
        "",
        "        %s (_浏览框)" % C("wkeWake"),
        "        %s (_浏览框)" % C("wkeRepaintIfNeeded"),
        "        帧 ＝ _GDC (_窗口)",
        "        _BB (帧, 0, 0, 980, 720, %s (_浏览框), 0, 0, 13369376)" % C("wkeGetViewDC"),
        "        _RDC (_窗口, 帧)",
        "        轮次 ＝ 轮次 ＋ 1",
        "        .如果真 (轮次 ＝ 1 或 轮次 ＝ 40 或 轮次 ＝ 120 或 轮次 ＝ 300)",
        "            ' ★ wkeGetURL 返回**整数型**（UTF-8 字符串指针）⇒ 必须先 指针到文本 再拼字符串，",
        "            '   直接把整数型结果拿去做文本加法是类型不符（同族会让 e.exe 解析崩）。",
        "            报告 (“[10] 第 ” ＋ 到文本 (轮次) ＋ “ 轮：标题 = ” ＋ 取标题 () ＋ “ / URL = ” ＋ 指针到文本 (%s (_浏览框)))" % C("wkeGetURL"),
        "        .如果真结束",
        "        _SL (25)",
        "    .计次循环尾 ()",
        "",
    ]
    S_STOP = [
        "    报告 (“== 到此为止 ==”)",
        "    返回 (0)",
        "",
    ]
    S_END = [
        "    报告 (“[11] 循环结束：IsLoadingCompleted = ” ＋ 到文本 (%s (_浏览框)) ＋ “；IsLoading = ” ＋ 到文本 (%s (_浏览框)))" % (C("wkeIsLoadingCompleted"), C("wkeIsLoading")),
        "    报告 (“[12] 取宽高 = ” ＋ 到文本 (%s (_浏览框)) ＋ “ x ” ＋ 到文本 (%s (_浏览框)))" % (C("wkeGetWidth"), C("wkeGetHeight")),
        "    报告 (“[13] 最终标题 = ” ＋ 取标题 ())",
        "    %s (_浏览框)" % C("wkeDestroyWebView"),
        "    _DW (_窗口)",
        "    报告 (“==== 演示结束 ====”)",
        "    返回 (0)",
        "",
        ".子程序 取标题, 文本型",
        "    .局部变量 宽, 整数型",
        "    .局部变量 窄, 整数型",
        "    .局部变量 结果, 文本型",
        "",
        "    宽 ＝ %s (_浏览框)" % C("wkeGetTitleW"),
        "    .如果真 (宽 ＝ 0)",
        "        返回 (“”)",
        "    .如果真结束",
        "    窄 ＝ _VA (0, 2048, 12288, 4)",
        "    _WCMB (0, 0, 宽, -1, 窄, 1024, 0, 0)",
        "    结果 ＝ 指针到文本 (窄)",
        "    返回 (结果)",
        "",
    ]
    ORDER = {
        "wrap": [S_WRAPONLY],
        "load": [S_HEAD, S_STOP],
        "win":  [S_HEAD, S_WIN, S_STOP],
        "wke":  [S_HEAD, S_WIN, S_WKE, S_STOP],
        "full": [S_HEAD, S_WIN, S_WKE, S_LOOP, S_END],
    }
    if STAGE not in ORDER:
        raise SystemExit("未知 DEMO_STAGE=%r（可选 %s）" % (STAGE, "/".join(ORDER)))
    main = "\n".join(l for seg in ORDER[STAGE] for l in seg)

    text = head + "\n" + wrappers + "\n" + loader + "\n" + main

    G.w(os.path.join(OUT, "DLL.e.txt"), DLL)
    G.w(os.path.join(OUT, "代码", "程序集1.static.e.txt"), text)
    G.w(os.path.join(OUT, "代码", "排序.list.txt"), "程序集1\n")
    G.w(os.path.join(OUT, "常量.e.txt"), ".版本 2\n\n.常量 演示名称, \"“MbKit 内存加载演示”\"\n")
    # ---- 资源：① MbKit 宿主库（scaffold 自带的 MbKit.image.dat）② 映射器 blob ----
    # ★ 映射器不能再走「置入代码」大数组（2007 B 实测运行期崩），改走资源机制：
    #   资源机制连 56320 B 的宿主库都能内嵌，几千字节的机器码自然没问题。
    with open(os.path.join(OUT, "资源", "Mapper.image.dat"), "wb") as f:
        f.write(G.MAPPER_BLOB)
    desc = {
        "MbKit": {"Comment": "MbKit 宿主层（内嵌验证）", "IsPublic": False},
        "Mapper": {"Comment": "PE 手工映射器机器码（%d B，运行时拷到固定 VA 池）"
                              % len(G.MAPPER_BLOB),
                   "IsPublic": False},
    }
    io.open(os.path.join(OUT, "资源", "资源.desc.json"), "w",
            encoding="utf-8-sig", newline="\r\n").write(
        json.dumps(desc, ensure_ascii=False, indent=4) + "\n")
    # 项目.etprj 由 scaffold 原样带过来即可：GBK 无 BOM，"Source": "代码.e"
    # （t2e 的输入/输出目录走命令行 -src/-dst，不读这里，切勿改成 UTF-8 写回）

    print("demo_proj 已生成：%s" % OUT)
    print("  包装子程序 %d 个（槽 0..%d），导出 %d 个" % (len(code_blocks), slot - 1, len(slots)))
    print("  映像基址 0x%08X，SizeOfImage 0x%X，导出表 %d 项" %
          (info["image_base"], info["size_of_image"], len(ex)))
    print("  演示直接调用：%s" % ", ".join(e for e in DEMO_NEED if e in byname))
    # 参数名的「人类友好对照表」只写到这里，绝不写进 DLL.e.txt（见 PARAM_DOC 的注释）
    docp = os.path.join(HERE, "out", "DLL参数对照.md")
    os.makedirs(os.path.dirname(docp), exist_ok=True)
    io.open(docp, "w", encoding="utf-8", newline="").write(
        "# 演示工程 DLL 命令的参数名对照\n\n"
        "> 生成物里一律写作 `_参数1`…`_参数N`（官方约定）。中文名**只存在于本表**，\n"
        "> 因为它们（`参数`/`窗口`/`菜单`/`类型`/`实例`）会撞易语言关键字，导致 e.exe 解析崩溃。\n\n"
        "| 命令 | 参数（人类可读） |\n|---|---|\n" +
        "".join("| `%s` | %s |\n" % (k, v) for k, v in PARAM_DOC.items()))
    print("  参数名对照表：%s" % docp)
    return 0


if __name__ == "__main__":
    raise SystemExit(build())
