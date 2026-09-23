#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成第二个探针工程：用**模块自身的易语言代码**跑一遍 MbKit 链路。

和第一个探针（re/opt/e2e/proj）的区别：
    第一个探针手写了等价的 `.DLL命令` + 释放逻辑，验证的是「机制」；
    本工程把 miniblink 模块里那 8 个新子程序（`WKE设置node路径` / `WKE载入宿主库` /
    `WKE取node路径` / `WKE宿主自检` / `_确保宿主库已载入` / `_取宿主库文件路径` /
    `_尝试从目录取宿主库` / `_释放宿主库文件`）**逐字搬过来**，验证的是「模块自己
    那段易语言代码的运行结果」—— 特别是「先试宿主 exe 目录、写不出去才退
    %TEMP%\\ElangMbKit」这条兜底路径，手写探针覆盖不到。

逐字 = 直接从 `re/opt/build_txt/class/程序集_全局.static.e.txt` 里按 `.子程序` 边界
切片，不做任何改写（连缩进、全角引号、注释都原样）。改写过就失去意义了。

用法： python e2e2_setup.py            # 生成 re/opt/e2e2/proj
"""
import pathlib
import shutil
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = pathlib.Path(__file__).resolve().parent
SRC_PROJ = ROOT / "e2e" / "proj"                     # 第一个探针（已知能编译）
MODTXT = ROOT / "build_txt"                          # 新模块的文本（e2t 产物）
MOD_GLOBAL = MODTXT / "class" / "程序集_全局.static.e.txt"
MOD_DLL = MODTXT / "dll.e.txt"
DST = ROOT / "e2e2"
DST_PROJ = DST / "proj"

Q_L, Q_R = "\u201c", "\u201d"                        # 全角引号，易语言文本里必须用这个


def q(s):
    return Q_L + s + Q_R


def extract_subs(text, first_sub, stop_sub):
    """从 `first_sub` 那一行切到 `stop_sub` 那一行（不含），逐字保留。"""
    lines = text.splitlines()
    beg = end = None
    for i, ln in enumerate(lines):
        if beg is None and ln.startswith(".子程序 " + first_sub):
            beg = i
        elif beg is not None and ln.startswith(".子程序 " + stop_sub):
            end = i
            break
    if beg is None or end is None:
        raise SystemExit("切不出 %s .. %s（模块文本变了？）" % (first_sub, stop_sub))
    block = lines[beg:end]
    while block and block[-1].strip() == "":         # 去掉尾部空行，避免多余
        block.pop()
    return block


MAIN = """
.程序集 程序集1
.程序集变量 _宿主库句柄, 整数型
.程序集变量 _日志, 文本型

.子程序 _启动子程序, 整数型
    .局部变量 运行目录, 文本型
    .局部变量 自定义, 文本型

    _日志 ＝ {0}
    运行目录 ＝ 取运行目录 ()
    报告 ({1} ＋ 运行目录)
    报告 ({2} ＋ 到文本 (取字节集长度 (#MbKit)))
    自定义 ＝ 读环境变量 ({3})
    报告 ({4} ＋ 自定义 ＋ {5})
    .如果真 (自定义 ≠ {6})
        报告 ({7} ＋ 到文本 (WKE设置node路径 (自定义)))
    .如果真结束
    报告 ({8} ＋ 到文本 (WKE载入宿主库 ()))
    报告 ({9} ＋ 到文本 (WKE载入宿主库 ()))
    报告 ({10} ＋ 到文本 (取文件尺寸 (运行目录 ＋ {11})))
    报告 ({12} ＋ 到文本 (_P_wkeIsInitialize ()))
    报告 ({13})
    报告 (WKE宿主自检 ())
    _P_wkeInitialize ()
    报告 ({14} ＋ 到文本 (_P_wkeIsInitialize ()))
    报告 ({15} ＋ WKE取node路径 ())
    报告 ({16})
    报告 (WKE宿主自检 ())
    报告 ({17})
    返回 (0)

.子程序 报告
    .参数 参数_文本, 文本型

    _日志 ＝ _日志 ＋ 参数_文本 ＋ 字符 (13) ＋ 字符 (10)
    写到文件 (取运行目录 () ＋ {18}, 到字节集 (_日志))
    标准输出 (1, 参数_文本)
""".format(
    q(""), q("01 启动；宿主程序目录 = "), q("02 模块内置 MbKit 资源长度 = "),
    q("MBKIT_TEST_NODE"), q("03 环境变量 MBKIT_TEST_NODE = ["), q("]"),
    q(""), q("04 WKE设置node路径 返回 = "),
    q("05 WKE载入宿主库 返回 = "), q("06 再次 WKE载入宿主库（幂等）= "),
    q("07 宿主库落盘尺寸（应为 56320）= "), q("\\MbKit.dll"),
    q("08 [模块裸名命令] wkeIsInitialize = "), q("09 加载前宿主自检："),
    q("10 wkeInitialize 后 wkeIsInitialize = "), q("11 WKE取node路径 = "),
    q("12 加载后宿主自检："), q("== 探测结束 =="),
    q("\\probe2.log"),
)

MBKIT_DLL = """
.DLL命令 _MbKit_LoadLibraryA, 整数型, "kernel32.dll", "LoadLibraryA"
    .参数 _参数1, 文本型

.DLL命令 _MbKit_SetNodePath, 整数型, "MbKit.dll", "MbKit_SetNodePath"
    .参数 _参数1, 文本型

.DLL命令 _MbKit_ConfigNodePath, 整数型, "MbKit.dll", "MbKit_ConfigNodePath"

.DLL命令 _MbKit_KernelPath, 整数型, "MbKit.dll", "MbKit_KernelPath"

.DLL命令 _MbKit_KernelLoaded, 整数型, "MbKit.dll", "MbKit_KernelLoaded"

.DLL命令 _MbKit_IsReady, 整数型, "MbKit.dll", "MbKit_IsReady"

.DLL命令 _MbKit_Diag, 整数型, "MbKit.dll", "MbKit_Diag"
"""


def w(path: pathlib.Path, text: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\xef\xbb\xbf" + text.replace("\n", "\r\n").encode("utf-8"))


def main() -> int:
    if not SRC_PROJ.is_dir():
        raise SystemExit("缺少源探针 %s" % SRC_PROJ)
    if not MOD_GLOBAL.is_file():
        raise SystemExit("缺少模块文本 %s" % MOD_GLOBAL)

    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC_PROJ, DST_PROJ)

    mod_txt = MOD_GLOBAL.read_text(encoding="utf-8-sig")
    subs = extract_subs(mod_txt, "WKE设置node路径", "WKE取版本")
    print("[e2e2] 从模块逐字抽取 %d 行（WKE设置node路径 .. _释放宿主库文件）" % len(subs))

    body = [".版本 2", "", MAIN.strip("\n")]
    body += [""]                                     # 启动子程序与模块子程序之间空一行
    body += subs
    w(DST_PROJ / "代码" / "程序集1.static.e.txt", "\n".join(body) + "\n")

    # 追加模块的 _MbKit_* 声明（第一个探针用的是自己的 _P_* 名字，两者并存）
    dll = (DST_PROJ / "DLL.e.txt").read_text(encoding="utf-8-sig").rstrip("\n")
    w(DST_PROJ / "DLL.e.txt", dll + "\n" + MBKIT_DLL)

    # 资源：模块内嵌的 MbKit 就是靠 #MbKit 引用的
    dat = DST_PROJ / "资源" / "MbKit.image.dat"
    if not dat.is_file():
        raise SystemExit("探针工程里没有 资源/MbKit.image.dat")
    print("[e2e2] 内嵌资源 %d 字节" % dat.stat().st_size)
    if dat.stat().st_size != 56320:
        print("[e2e2] ⚠ 内嵌资源不是 56320 字节，可能导致用例判据不对")

    print("[e2e2] 生成 %s" % DST_PROJ)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
