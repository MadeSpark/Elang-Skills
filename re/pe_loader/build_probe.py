#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 PE加载器.e 里那几个「手写机器码」子程序**逐字**搬进一个实验工程，
再加我自己的判据：

  E1  映射器能不能吃下 expdll.dll（真实一点：17 个节、TLS、多导入）
  E2  映射后 base 是不是有效（MZ 头 / 是否有自己的函数地址）
  E3  MyGetProcAddress 能不能按名取到导出
  E4  取到的地址能不能真的调用（cdecl / stdcall 各一个）
  E5  ★ 关键：映射后的模块在**真实加载器链表**里吗？
        —— GetModuleHandleA("expdll.dll") 是否等于映射 base
        —— LoadLibraryA("expdll.dll") 是否复用同一份
      E5 决定「易语言 .DLL命令 能不能用内存映射的 DLL」。
"""
import pathlib
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

HERE = pathlib.Path(__file__).resolve().parent          # re/pe_loader
ROOT = HERE.parent.parent                               # 测试
SRC = HERE / "txt" / "class" / "程序集1.static.e.txt"
SCAFFOLD = ROOT / "re" / "opt" / "e2e" / "proj"
PROJ = HERE / "proj"

WANT = ["加载内存DLL", "卸载内存DLL", "MyGetProcAddress"]


def extract_subs(text: str, names):
    """按 `.子程序 <名>` 切块；名字必须精确匹配（允许后面紧跟逗号）。"""
    lines = text.split("\n")
    starts = []
    for i, ln in enumerate(lines):
        if ln.startswith(".子程序 "):
            head = ln[len(".子程序 "):].split(",")[0].strip()
            starts.append((i, head))
    out = {}
    for idx, (i, head) in enumerate(starts):
        end = starts[idx + 1][0] if idx + 1 < len(starts) else len(lines)
        if head in names:
            out[head] = "\n".join(lines[i:end]).rstrip("\n")
    return out


PROBE = '''.版本 2

.程序集 程序集1
.程序集变量 _日志, 文本型

.子程序 _启动子程序, 整数型
    .局部变量 数据, 字节集
    .局部变量 基址, 整数型
    .局部变量 句柄_按名, 整数型
    .局部变量 句柄_加载, 整数型
    .局部变量 映射_Add2, 整数型
    .局部变量 映射_Add2S, 整数型
    .局部变量 映射_Const7, 整数型
    .局部变量 映射_Echo1, 整数型
    .局部变量 映射_Echo2, 整数型
    .局部变量 映射_Self, 整数型
    .局部变量 磁盘_Add2, 整数型
    .局部变量 磁盘_Const7, 整数型
    .局部变量 磁盘_Echo1, 整数型
    .局部变量 磁盘_Echo2, 整数型

    报告 (“==== PE 映射器实验 ====”)
    报告 (“00 运行目录 = ” ＋ 取运行目录 ())
    数据 ＝ 读入文件 (取运行目录 () ＋ “\\expdll.dll”)
    报告 (“01 待映射重量 = ” ＋ 到文本 (取字节集长度 (数据)))

    基址 ＝ 加载内存DLL (数据)
    报告 (“02 加载内存DLL 返回基址 = 0x” ＋ 取十六进制文本 (基址))

    .如果真 (基址 ≠ 0)
        报告 (“03 基址处签名 = [” ＋ 取文本左边 (指针到文本 (基址), 2) ＋ “]（应为 MZ）”)
        映射_Add2 ＝ MyGetProcAddress (基址, “Add2”)
        映射_Add2S ＝ MyGetProcAddress (基址, “Add2S”)
        映射_Const7 ＝ MyGetProcAddress (基址, “Const7”)
        映射_Echo1 ＝ MyGetProcAddress (基址, “Echo1”)
        映射_Echo2 ＝ MyGetProcAddress (基址, “Echo2”)
        映射_Self ＝ MyGetProcAddress (基址, “SelfByName”)
        报告 (“04 按名取地址 Add2   = 0x” ＋ 取十六进制文本 (映射_Add2) ＋ “（应 = 基址 + 0x1480 附近）”)
        报告 (“05 按名取地址 Const7 = 0x” ＋ 取十六进制文本 (映射_Const7))
        报告 (“06 按名取地址 Echo2  = 0x” ＋ 取十六进制文本 (映射_Echo2))

        ' ---------- 对照 A：映射副本 ----------
        ' Const7/Echo* 的返回值不依赖任何内存读写 ⇒ 能区分「代码没拷进去」和「调用约定错」
        报告 (“07 [映射] Const7()      = ” ＋ 到文本 (调用0 (映射_Const7)) ＋ “（期望 7）”)
        报告 (“08 [映射] Echo1(12345) = ” ＋ 到文本 (调用1 (映射_Echo1, 12345)) ＋ “（期望 12345）”)
        报告 (“09 [映射] Echo2(1,2)   = ” ＋ 到文本 (调用2 (映射_Echo2, 1, 2)) ＋ “（期望 102）”)
        报告 (“10 [映射] Add2(1,2)    = ” ＋ 到文本 (调用2 (映射_Add2, 1, 2)) ＋ “（期望 3）”)
        报告 (“11 [映射] Add2S(1,2)   = ” ＋ 到文本 (调用2 (映射_Add2S, 1, 2)) ＋ “（期望 1003）”)
        报告 (“12 [映射] SelfByName() = 0x” ＋ 取十六进制文本 (调用0 (映射_Self)) ＋ “（映射副本自己按名查自己）”)

        ' ---------- 对照 B：同一份 DLL 的**磁盘副本** ----------
        ' 同一套调用外壳作用在磁盘副本上：若这里对、映射那里错，问题就出在映射器。
        句柄_加载 ＝ _P_LoadLibraryA (“expdll.dll”)
        报告 (“13 [磁盘] LoadLibraryA 句柄 = 0x” ＋ 取十六进制文本 (句柄_加载))
        磁盘_Const7 ＝ _P_GetProcAddress (句柄_加载, “Const7”)
        磁盘_Echo1 ＝ _P_GetProcAddress (句柄_加载, “Echo1”)
        磁盘_Echo2 ＝ _P_GetProcAddress (句柄_加载, “Echo2”)
        磁盘_Add2 ＝ _P_GetProcAddress (句柄_加载, “Add2”)
        报告 (“14 [磁盘] Const7()      = ” ＋ 到文本 (调用0 (磁盘_Const7)) ＋ “（期望 7）  ← 调用外壳自检”)
        报告 (“15 [磁盘] Echo1(12345) = ” ＋ 到文本 (调用1 (磁盘_Echo1, 12345)) ＋ “（期望 12345） ← 调用外壳自检”)
        报告 (“16 [磁盘] Echo2(1,2)   = ” ＋ 到文本 (调用2 (磁盘_Echo2, 1, 2)) ＋ “（期望 102） ← 调用外壳自检”)
        报告 (“17 [磁盘] Add2(1,2)    = ” ＋ 到文本 (调用2 (磁盘_Add2, 1, 2)) ＋ “（期望 3）”)

        ' ---------- E5：映射模块能不能被「按名」找到 ----------
        句柄_按名 ＝ _P_GetModuleHandleA (“expdll.dll”)
        报告 (“18 ★ GetModuleHandleA(expdll.dll) = 0x” ＋ 取十六进制文本 (句柄_按名) ＋ “   [映射基址 0x” ＋ 取十六进制文本 (基址) ＋ “]”)
        .如果真 (句柄_按名 ＝ 基址)
            报告 (“19 ★ 结论：映射模块**已在真实加载器链表里**”)
        .如果真结束
        .如果真 (句柄_按名 ≠ 基址)
            报告 (“19 ★ 结论：映射模块**不在真实加载器链表里** -> 按名命中不了它”)
        .如果真结束
        .如果真 (句柄_加载 ＝ 基址)
            报告 (“20 ★ LoadLibrary 复用了映射的那一份”)
        .如果真结束
        .如果真 (句柄_加载 ≠ 基址)
            报告 (“20 ★ LoadLibrary 又加载了第二份（磁盘上的），两份并存”)
        .如果真结束

        报告 (“21 卸载内存DLL = ” ＋ 到文本 (卸载内存DLL (基址)))
    .如果真结束
    .如果真 (基址 ＝ 0)
        报告 (“03 加载内存DLL 返回 0 —— 映射器拒绝了这份镜像”)
    .如果真结束

    报告 (“== 实验结束 ==”)
    返回 (0)

.子程序 报告
    .参数 参数_文本, 文本型

    _日志 ＝ _日志 ＋ 参数_文本 ＋ 字符 (13) ＋ 字符 (10)
    写到文件 (取运行目录 () ＋ “\\peexp.log”, 到字节集 (_日志))
    标准输出 (1, 参数_文本)

.子程序 调用0, 整数型
    .参数 函数地址, 整数型

    ' ★ 关键：置入代码必须是**完整函数体**，末尾自带 leave; ret N。
    '   易语言会给「返回 (值)」生成 `mov eax, 0`，那句在 _置入代码 之后执行，
    '   会把 call 的返回值冲掉。原文那三个子程序之所以能用，正是因为它们的
    '   机器码块自己以 `leave; ret N` 结尾，易语言那段 `mov eax,0` 永远执行不到。
    置入代码 ({ 139, 69, 8, 255, 208, 201, 194, 4, 0 })
    返回 (0)

.子程序 调用1, 整数型
    .参数 函数地址, 整数型
    .参数 参数一, 整数型

    置入代码 ({ 255, 117, 12, 139, 69, 8, 255, 208, 201, 194, 8, 0 })
    返回 (0)

.子程序 调用2, 整数型
    .参数 函数地址, 整数型
    .参数 参数一, 整数型
    .参数 参数二, 整数型

    置入代码 ({ 255, 117, 16, 255, 117, 12, 139, 69, 8, 255, 208, 201, 194, 12, 0 })
    返回 (0)
'''


def main() -> int:
    if not SRC.exists():
        print("缺源文件:", SRC)
        return 2
    text = SRC.read_text(encoding="utf-8-sig", errors="replace").replace("\r\n", "\n")
    got = extract_subs(text, WANT)
    missing = [w for w in WANT if w not in got]
    if missing:
        print("提取失败:", missing)
        return 1
    print("提取到的子程序:")
    for k in WANT:
        print("  %-16s %4d 行" % (k, got[k].count("\n") + 1))

    if PROJ.exists():
        shutil.rmtree(PROJ)
    shutil.copytree(SCAFFOLD, PROJ)

    dll = '''.版本 2

.DLL命令 _P_GetModuleHandleA, 整数型, "kernel32.dll", "GetModuleHandleA"
    .参数 _参数1, 文本型

.DLL命令 _P_LoadLibraryA, 整数型, "kernel32.dll", "LoadLibraryA"
    .参数 _参数1, 文本型

.DLL命令 _P_GetProcAddress, 整数型, "kernel32.dll", "GetProcAddress"
    .参数 _参数1, 整数型
    .参数 _参数2, 文本型

'''
    (PROJ / "DLL.e.txt").write_bytes(b"\xef\xbb\xbf" + dll.replace("\n", "\r\n").encode("utf-8"))

    body = PROBE + "\n\n" + "\n\n".join(got[k] for k in WANT) + "\n"
    (PROJ / "代码" / "程序集1.static.e.txt").write_bytes(
        b"\xef\xbb\xbf" + body.replace("\n", "\r\n").encode("utf-8"))
    print("\n写好:", PROJ)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
