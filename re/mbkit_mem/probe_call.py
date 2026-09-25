#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""运行时探针：验证「易语言子程序 + 置入代码 → 固定 VA 槽表 → 真实 cdecl 函数」这条链路。

要回答的问题（每条都有对照）：
  Q-a 固定 VA（0x50000000）能否用 VirtualAlloc 拿到？
  Q-b 子程序里「整块机器码自带 mov esp,ebp; pop ebp; ret N」能否正确返回给易语言调用方？
  Q-c 0 参数子程序是否也建立了 ebp 帧（决定这类包装能不能写）？
  Q-d 文本型参数在子程序里是「指向槽的指针」还是值？（已知静态证据：是指针，需解引用一次）
  Q-e 字节集参数是否「解引用再 +8」？
  Q-f 传址参数：目标要的是「值的地址」还是「槽内容」？（两种都实现，与 .DLL命令 对照）
  Q-g 长整数型返回值走 edx:eax 吗（子程序 ABI == .DLL命令 ABI）？
  Q-h 双精度小数型返回值走 st(0) 吗？
  Q-i 包装函数调用之后，调用方栈帧是否完好（哨兵局部变量不变）？
"""
import os
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
SCAFFOLD = os.path.join(ROOT, "re", "opt", "e2e", "proj")
PROJ = os.path.join(HERE, "pcall")

BOM = "\ufeff"
POOL = 0x50000000
POOL_SIZE = 4096


def w(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write((BOM + text.replace("\n", "\r\n")).encode("utf-8"))


def b(*xs):
    out = []
    for x in xs:
        if isinstance(x, (list, tuple)):
            out.extend(x)
        elif isinstance(x, int):
            out.append(x & 0xFF)
        elif isinstance(x, str):
            out.extend(bytes.fromhex(x.replace(" ", "")))
        else:
            raise TypeError(x)
    return out


def imm32(v):
    return [(v >> (8 * i)) & 0xFF for i in range(4)]


def call_slot(i):
    """call dword ptr [POOL + 4*i]"""
    return b("FF 15", imm32(POOL + 4 * i))


TAIL = b("8B E5", "5D")          # mov esp,ebp ; pop ebp


def wrap(pushes, slot, nargs, ret_type):
    return pushes + call_slot(slot) + TAIL + b("C2", [nargs & 0xFF, (nargs >> 8) & 0xFF])


DLL = '''.版本 2

.DLL命令 _VirtualAlloc, 整数型, "kernel32.dll", "VirtualAlloc"
    .参数 地址, 整数型
    .参数 长度, 整数型
    .参数 类型, 整数型
    .参数 保护, 整数型

.DLL命令 _GetModuleHandleA, 整数型, "kernel32.dll", "GetModuleHandleA"
    .参数 模块名, 文本型

.DLL命令 _GetProcAddress, 整数型, "kernel32.dll", "GetProcAddress"
    .参数 模块句柄, 整数型
    .参数 函数名, 文本型

.DLL命令 参考_strlen, 整数型, "msvcrt.dll", "@strlen"
    .参数 文本, 文本型

.DLL命令 参考_memset, 整数型, "msvcrt.dll", "@memset"
    .参数 目标, 整数型, 传址
    .参数 值, 整数型
    .参数 长度, 整数型

.DLL命令 参考_atoi64, 长整数型, "msvcrt.dll", "@_atoi64"
    .参数 文本, 文本型

.DLL命令 参考_atof, 双精度小数型, "msvcrt.dll", "@atof"
    .参数 文本, 文本型

.DLL命令 参考_strlen指针, 整数型, "msvcrt.dll", "@strlen"
    .参数 指针, 整数型
'''


def bl(xs):
    return "{ " + ", ".join(str(v) for v in xs) + " }"


# ---- 机器码 ----
CODE = {
    # 文本型 -> 解引用一次 -> push
    "W_strlen": wrap(b("8B 45 08", "8B 00", "50"), 0, 4, "整数型"),
    # 字节集 -> 解引用 + 8 -> push（假设：槽=描述符指针，描述符 [0]=引用计数 [4]=长度 [8]=数据指针）
    "W_字节集": wrap(b("8B 45 08", "8B 00", "83 C0 08", "50"), 6, 4, "整数型"),
    # 字节集 -> 原样推槽（对照：若槽真是数据指针，strlen 会给出 10；若是描述符，只会给出 0/1）
    "W_字节集原样": wrap(b("8B 45 08", "50"), 6, 4, "整数型"),
    # 传址（假设 H1：槽里存的是值，push &槽）
    "W_memset甲": wrap(b("FF 75 10", "FF 75 0C", "8D 45 08", "50"), 1, 12, "整数型"),
    # 传址（假设 H2：槽里存的是地址，push 槽内容）
    "W_memset乙": wrap(b("FF 75 10", "FF 75 0C", "FF 75 08"), 5, 12, "整数型"),
    # 零参数
    "W_tick": wrap(b(), 2, 0, "整数型"),
    # 长整数型返回
    "W_atoi64": wrap(b("8B 45 08", "8B 00", "50"), 3, 4, "长整数型"),
    # 双精度返回
    "W_atof": wrap(b("8B 45 08", "8B 00", "50"), 4, 4, "双精度小数型"),
    # 自证：不做任何转发，直接把参数原样返回（测子程序返回值 ABI）
    "_自返8": b("8B 45 08", "8B 55 0C", "8B E5", "5D", "C2 08 00"),
    # 存槽：*(POOL + 4*槽号) = 值
    "_存槽": b("8B 45 08", "C1 E0 02", "05", imm32(POOL), "8B 4D 0C", "89 08", "8B E5", "5D", "C2 08 00"),
    # ---- 返回类型 ABI 探针：机器码把已知常量塞进 eax / edx:eax / st(0)，看调用方到底读哪里 ----
    # 整数型/字节型：eax = 0x12345678 → 整数型应读 305419896，字节型应只读低 8 位 120
    "_返整数": b("B8 78 56 34 12", "8B E5", "5D", "C2 00 00"),
    "_返字节": b("B8 78 56 34 12", "8B E5", "5D", "C2 00 00"),
    # 长整数型：edx:eax = 0x0123456789ABCDEF
    "_返长整": b("B8 EF CD AB 89", "BA 67 45 23 01", "8B E5", "5D", "C2 00 00"),
    # 小数型：push 0x40400000(=3.0f); fld dword[esp]; add esp,4
    "_返小数": b("68 00 00 40 40", "D9 04 24", "83 C4 04", "8B E5", "5D", "C2 00 00"),
    # 双精度：push 0x400A0000; push 0; fld qword[esp]; add esp,8  → 3.25
    "_返双精": b("68 00 00 0A 40", "68 00 00 00 00", "DD 04 24", "83 C4 08", "8B E5", "5D", "C2 00 00"),
    # ---- 浮点返回搬运配方：st(0) → 池槽 → edx:eax（因为易语言子程序只用寄存器收返回值）----
    # fstp dword ptr [池+0x48] ; mov eax,[池+0x48]
    "_返小数2": b("68 00 00 40 40", "D9 04 24", "83 C4 04",
                 "D9 1D", imm32(POOL + 0x48), "A1", imm32(POOL + 0x48),
                 "8B E5", "5D", "C2 00 00"),
    # fstp qword ptr [池+0x40] ; mov eax,[池+0x40] ; mov edx,[池+0x44]
    "_返双精2": b("68 00 00 0A 40", "68 00 00 00 00", "DD 04 24", "83 C4 08",
                 "DD 1D", imm32(POOL + 0x40), "A1", imm32(POOL + 0x40), "8B 15", imm32(POOL + 0x44),
                 "8B E5", "5D", "C2 00 00"),
}

MAIN = '''.版本 2

.程序集 程序集1

    .子程序 _启动子程序, 整数型
    .局部变量 池, 整数型
    .局部变量 内核, 整数型
    .局部变量 数据, 字节集
    .局部变量 文本, 文本型
    .局部变量 参考值, 整数型
    .局部变量 包装值, 整数型
    .局部变量 哨兵, 整数型
    .局部变量 甲, 整数型
    .局部变量 乙, 整数型
    .局部变量 丙, 整数型
    .局部变量 大数1, 长整数型
    .局部变量 大数2, 长整数型
    .局部变量 字节甲, 字节型
    .局部变量 小数甲, 小数型
    .局部变量 小数1, 双精度小数型
    .局部变量 小数2, 双精度小数型

    哨兵 ＝ 286331153
    文本 ＝ “QQQQQQQQQQ”
    池 ＝ _VirtualAlloc (1342177280, 4096, 12288, 4)
    内核 ＝ _GetModuleHandleA (“msvcrt.dll”)
    文本 ＝ 文本 ＋ #换行符 ＋ “池 ＝ ” ＋ 到文本 (池) ＋ “（期望 1342177280）” ＋ #换行符
    文本 ＝ 文本 ＋ “msvcrt 句柄 ＝ ” ＋ 到文本 (内核) ＋ #换行符
    _存槽 (0, _GetProcAddress (内核, “strlen”))
    _存槽 (1, _GetProcAddress (内核, “memset”))
    _存槽 (5, _GetProcAddress (内核, “memset”))
    _存槽 (2, _GetProcAddress (_GetModuleHandleA (“kernel32.dll”), “GetTickCount”))
    _存槽 (3, _GetProcAddress (内核, “_atoi64”))
    _存槽 (4, _GetProcAddress (内核, “atof”))
    _存槽 (6, _GetProcAddress (内核, “strlen”))
    文本 ＝ 文本 ＋ “槽0（strlen）＝ ” ＋ 到文本 (读取槽_探针 (0)) ＋ #换行符

    ' Q-b/Q-d 文本型解引用
    参考值 ＝ 参考_strlen (“QQQQQQQQQQ”)
    包装值 ＝ W_strlen (“QQQQQQQQQQ”)
    文本 ＝ 文本 ＋ “Q-d strlen：参考 ＝ ” ＋ 到文本 (参考值) ＋ “ 包装 ＝ ” ＋ 到文本 (包装值) ＋ #换行符

    ' Q-f 传址：.DLL命令 的「传址」与两种包装假设对照（全整数型，最安全）
    甲 ＝ 12345
    参考值 ＝ 参考_memset (甲, 0, 4)
    乙 ＝ 12345
    丙 ＝ 12345
    包装值 ＝ W_memset甲 (乙, 0, 4)
    W_memset乙 (丙, 0, 4)
    文本 ＝ 文本 ＋ “Q-f memset：参考后甲 ＝ ” ＋ 到文本 (甲) ＋ “ 甲式包装后乙 ＝ ” ＋ 到文本 (乙) ＋ “ 乙式包装后丙 ＝ ” ＋ 到文本 (丙) ＋ #换行符

    ' Q-c 零参数子程序有没有 ebp 帧
    包装值 ＝ W_tick ()
    文本 ＝ 文本 ＋ “Q-c 零参数 W_tick ＝ ” ＋ 到文本 (包装值) ＋ #换行符

    ' Q-i 经过上述包装调用后，调用方栈帧是否完好
    文本 ＝ 文本 ＋ “Q-i 哨兵 ＝ ” ＋ 到文本 (哨兵) ＋ “（期望 286331153）” ＋ #换行符

    ' Q-g 长整数型返回（子程序 ABI 是否 == .DLL命令 ABI）
    大数1 ＝ 参考_atoi64 (“1234567890123”)
    大数2 ＝ W_atoi64 (“1234567890123”)
    文本 ＝ 文本 ＋ “Q-g 长整型：参考 ＝ ” ＋ 到文本 (大数1) ＋ “ 包装 ＝ ” ＋ 到文本 (大数2) ＋ #换行符
    大数1 ＝ _自返8 (4294967297)
    文本 ＝ 文本 ＋ “Q-g 子程序自返8(4294967297) ＝ ” ＋ 到文本 (大数1) ＋ #换行符

    ' Q-h 双精度返回
    小数1 ＝ 参考_atof (“3.25”)
    小数2 ＝ W_atof (“3.25”)
    文本 ＝ 文本 ＋ “Q-h 双精度：参考 ＝ ” ＋ 到文本 (小数1) ＋ “ 包装 ＝ ” ＋ 到文本 (小数2) ＋ #换行符

    ' Q-j 返回类型 ABI：机器码把已知常量塞进 eax / edx:eax / st(0)，
    '     然后看易语言调用方到底从哪儿读返回值。这一步决定「尾跳包装」能不能覆盖全部返回类型。
    包装值 ＝ _返整数 ()
    字节甲 ＝ _返字节 ()
    大数1 ＝ _返长整 ()
    小数甲 ＝ _返小数 ()
    小数2 ＝ _返双精 ()
    文本 ＝ 文本 ＋ “Q-j 整数型（应 305419896）＝ ” ＋ 到文本 (包装值) ＋ “ 字节型（应 120）＝ ” ＋ 到文本 (字节甲) ＋ #换行符
    文本 ＝ 文本 ＋ “Q-j 长整数型（应 81985529216486895）＝ ” ＋ 到文本 (大数1) ＋ #换行符
    文本 ＝ 文本 ＋ “Q-j 小数型（应 3）＝ ” ＋ 到文本 (小数甲) ＋ “ 双精度小数型（应 3.25）＝ ” ＋ 到文本 (小数2) ＋ #换行符
    小数甲 ＝ _返小数2 ()
    小数1 ＝ _返双精2 ()
    文本 ＝ 文本 ＋ “Q-j 搬运后 小数型（应 3）＝ ” ＋ 到文本 (小数甲) ＋ “ 双精度小数型（应 3.25）＝ ” ＋ 到文本 (小数1) ＋ #换行符

    ' Q-e 字节集：放在最后 —— 这一路最容易把探针自己搞崩，别拖累上面的结论。
    '   字面量末尾显式补一个 0 字节，保证 strlen 不会读过界。
    '   两条包装互为对照：原样推槽 vs 解引用+8（预期 10）。
    数据 ＝ { 81, 81, 81, 81, 81, 81, 81, 81, 81, 81, 0 }
    包装值 ＝ W_字节集原样 (数据)
    参考值 ＝ W_字节集 (数据)
    文本 ＝ 文本 ＋ “Q-e 字节集（应 10）：原样推槽 ＝ ” ＋ 到文本 (包装值) ＋ “ 解引用+8 ＝ ” ＋ 到文本 (参考值) ＋ “ 取字节集长度 ＝ ” ＋ 到文本 (取字节集长度 (数据)) ＋ #换行符

    文本 ＝ 文本 ＋ “结论行结束” ＋ #换行符

    写到文件 (取运行目录 () ＋ “/probe_run.txt”, 到字节集 (文本))
    返回 (0)

'''


def wrap_decl(name, params, ret, code):
    lines = []
    head = ".子程序 " + name + (", " + ret if ret else "")
    lines.append(head)
    for p, extra in params:
        lines.append("    .参数 " + p + (", " + extra if extra else ""))
    # ★ 声明块与第一条语句之间必须有空行！实测：漏掉它 e.exe 直接 0xC0000005 崩。
    #   真实 e2t 产出统计（211 份产物 / 6521 个子程序）：5357 个是真·空行(0 字符)、3 个是 4 空格空白行、
    #   0 个「紧贴语句」。无声明时不加（与 e2t 一致：1161 个无声明子程序均无此空行）。
    if params:
        lines.append("")
    lines.append("    置入代码 (" + bl(code) + ")")
    if ret:
        lines.append("    返回 (0)")
    lines.append("")
    return "\n".join(lines)


MAIN += wrap_decl("W_strlen", [("文本", "文本型")], "整数型", CODE["W_strlen"])
MAIN += wrap_decl("W_字节集", [("数据", "字节集")], "整数型", CODE["W_字节集"])
MAIN += wrap_decl("W_字节集原样", [("数据", "字节集")], "整数型", CODE["W_字节集原样"])
MAIN += wrap_decl("W_memset甲", [("目标", "整数型, 参考"), ("值", "整数型"), ("长度", "整数型")], "整数型", CODE["W_memset甲"])
MAIN += wrap_decl("W_memset乙", [("目标", "整数型, 参考"), ("值", "整数型"), ("长度", "整数型")], "整数型", CODE["W_memset乙"])
MAIN += wrap_decl("W_tick", [], "整数型", CODE["W_tick"])
MAIN += wrap_decl("W_atoi64", [("文本", "文本型")], "长整数型", CODE["W_atoi64"])
MAIN += wrap_decl("W_atof", [("文本", "文本型")], "双精度小数型", CODE["W_atof"])
MAIN += wrap_decl("_自返8", [("甲", "长整数型")], "长整数型", CODE["_自返8"])
MAIN += wrap_decl("_存槽", [("槽号", "整数型"), ("值", "整数型")], "", CODE["_存槽"])

# 读取槽（整数型）—— 用同样的机器码技巧，但走「参考」写法不安全，这里改用 参考_memset 读不现实；
# 用一个小机器码子程序把槽内容当整数返回（同时也是「动态读内存」能力的验证）
READ = b("8B 45 08", "C1 E0 02", "05", imm32(POOL), "8B 00", "8B E5", "5D", "C2 04 00")
MAIN += wrap_decl("读取槽_探针", [("槽号", "整数型")], "整数型", READ)
for _n, _r in (("_返整数", "整数型"), ("_返字节", "字节型"), ("_返长整", "长整数型"),
               ("_返小数", "小数型"), ("_返双精", "双精度小数型"),
               ("_返小数2", "小数型"), ("_返双精2", "双精度小数型")):
    MAIN += wrap_decl(_n, [], _r, CODE[_n])


def inject_flush(text):
    """每累加一次日志就落盘一次。

    理由：探针是「跑起来才可能崩」的，而 写到文件 只在结尾执行 ⇒ 一旦中途崩，
    前面已经算出来的结论全部丢失，等于白跑。逐句落盘后，最后一个成功写入的行
    就精确指出「崩在哪一步之后」。
    """
    flush = "    写到文件 (取运行目录 () ＋ “/probe_run.txt”, 到字节集 (文本))"
    out = []
    for ln in text.split("\n"):
        out.append(ln)
        if ln.strip().startswith("文本 ＝"):
            out.append(flush)
    return "\n".join(out)


def main():
    if os.path.isdir(PROJ):
        shutil.rmtree(PROJ)
    shutil.copytree(SCAFFOLD, PROJ)
    for junk in ("资源",):
        p = os.path.join(PROJ, junk)
        if os.path.isdir(p):
            shutil.rmtree(p)
    w(os.path.join(PROJ, "DLL.e.txt"), DLL)
    w(os.path.join(PROJ, "代码", "程序集1.static.e.txt"), inject_flush(MAIN))
    w(os.path.join(PROJ, "代码", "排序.list.txt"), "程序集1\n")
    print("写好:", PROJ)
    print("池 = 0x%08X" % POOL)
    for k, v in CODE.items():
        print("  %-10s %d 字节: %s" % (k, len(v), " ".join("%02X" % x for x in v)))
    print("  读取槽_探针", " ".join("%02X" % x for x in READ))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
