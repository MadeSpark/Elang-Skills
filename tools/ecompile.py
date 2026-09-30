#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""无头编译易语言 .e -> 原生 exe/dll（走本套装自己的宿主支持库链路，零第三方依赖）。

    python ecompile.py <输入.e|文本目录> <输出.exe|输出.dll> [--kind 静态编译种类]
                       [--fne <elang_addin.fne>] [--home <易语言目录>] [--etool <QAQ-ETool.exe>]
                       [--timeout 300] [--workdir <目录>] [--json] [-q]
    python ecompile.py <输入.e|文本目录> --check [--fast] [--json] [-q]
                       # 只要语法检查、不要产物（--fast = 毫秒级但不充分，见下）
    python ecompile.py --doctor [--json]

`--check`：**把编译器当语法/语义检查器用**（不产生产物，只回答"源码有没有问题"）
=========================================================================
判据（实测）：易语言编译的第一阶段是整个工程的语法/语义静态检查，**只有它全过了，
编译器才会弹「请输入欲编译到的可执行文件名：」保存框**。于是——

    保存框出现  ⇒ 源码静态检查通过（宿主库随即按 PID 收尾，等于"直接取消编译"）
    没等到保存框 / 面板出现 `错误(NNNN): …` / IDE 自崩 ⇒ **不能判通过**

所以 `--check` 的默认实现是「真编译 + 保存框为判据」，比真编译省掉的是产物落盘与链接之后
的一切；拿到结论就收工。

`--check --fast` 改走官方 `FN_PRE_COMPILE`（毫秒级），但**实测不可靠**：对本机确定有
问题的源码（易语言自己回读标 `!!!无法识别` 的那种）它也返回成功。它只适合当「IDE 能不能
吞下这份源码」的超快冒烟测试，**别拿它当通过判据**。

这是**唯一**能验出下面这类问题的手段，纯文本静态检查（`echeck.py`）看不到：

  错误(10022): 子程序“_OnGuildPush”具有返回值定义，但实际上却没有返回数据或者并不是
               所有程序分支都返回了数据。
  错误(10044): 不能将 "整数型" 数据转换到 "逻辑型" 数据

⚠️ 本机的已知限制：**第三方 IDE 插件会在报错路径上把 IDE 带崩**（`eExitCode=0xC0000005`），
此时错误原文可能来不及写进输出面板 —— 脚本会如实报 `failed` / 崩溃退出码（**不会**误判成
通过），但拿不到 `错误(NNNN)` 的具体文字。要读原文就开 IDE 手动编译一次。

⇒ **改完代码先 `--check` 一遍**，通过了再决定是否真编译。

工作目录（临时目录里的 `ecompile_<随机>`，放加壳副本、编译面板日志、运行轨迹）的保留规则
是**按「有没有值得回看的日志」**判定的，不看你是不是用了 `--check`：

  * 成功（`ok=true`）             → **删**。日志没有回看价值。
  * 还没跑到编译器就失败（加壳/转换挂了）→ **删**。原因已经在 `error` 字段里，
                                    目录里没有额外信息，留着只会制造「查不到路径的孤儿目录」。
  * 跑到了编译器才失败            → **保留**，并把路径写进结果 JSON 的 `workdir` / `logUtf8`，
                                    里面有编译面板日志的 UTF-8 视图，可直接回看原始报错。

保留的目录**最多留最近 10 个**（每次调用顺手清理更旧的），所以不会把 `%TEMP%` 堆满。
需要固定位置就自己给 `--workdir`（给了就从不删）。

流程（真编译；`--check` 省掉产物等待）：

  1. 输入是文本目录（含 代码/ 或 class/）时先 t2e 出 .e（QAQ-ETool，由同装的技能
     qaq-etool-cli 提供；定位顺序见 etool.py）。
  2. mkcage 加壳：把本套装宿主库 elang_addin 登记进工程库表（绝不改原 .e）。
  3. elaunch --compile：临时投放 .fne 到 lib\\ → 启动独立 IDE → 宿主库进程内触发
     静态编译功能号（官方 FNST_RUN 家族，见 elang-debug 技能《接口与调用约定清单》）→
     自动填「请输入欲编译到的可执行文件名」保存框 → 监视期望产物出现 → 按 PID 收尾，
     并对易语言安装目录做 before/after diff 自证还原。

无头（默认）：**不会弹出易语言窗口**。IDE 以 `SW_HIDE` 启动 + 后台线程把它的顶层窗口
隐藏并移到屏幕外（宿主库侧 `ELANG_AI_HIDE` 再做一层）。排障要看界面时加 `--visible`。
（别改成「独立 Windows 桌面」：实测 e.exe 在非默认桌面上 0.04 秒就自行退出。）

静态编译种类（--kind，缺省 static_console）：

  static         FN_COMPILE_STATIC(0x05020007)   按 IDE 当前工程类型静态编译
  static_exe     FN_STATIC_COMPILE_WINDOWS_EXE   静态编译为 Windows 程序
  static_console FN_STATIC_COMPILE_WINDOWS_CONOLE_EXE  静态编译为 Windows 控制台程序
  static_dll     FN_STATIC_COMPILE_WINDOWS_DLL   静态编译为 Windows 动态链接库
  exe/console/dll                                    对应的非静态编译
  ecom           FN_COMPILE_WINDOWS_ECOM         编译为易模块（.ec）—— 易模块工程用这个

易模块（--kind ecom）交付门禁（--ecom-validate 默认开启）
=========================================================================
编译成功 ≠ 可以交付。.ec 产物默认自动过两道门禁，不过就删掉候选并返回失败：
  门禁1  头 8 字节 = CNWTEPRG（合法已编译易模块；t2e 产物伪装的一律拦下）
  门禁3  反向抽取（e2t）成功、无 [错误] 且退出码 0；给了 --guard 还要命中
         全部关键语句（丢代码的模块在这里现形，历史上已 3 次静默丢代码）。
红线详见 --help 尾部。

结果 JSON（--json；stdout 只放 JSON）：
  真编译 {"ok":true,"status":"ok","output":"…","outputSize":N,…}
  --check {"ok":true,"status":"syntax-ok","errors":null,…}
          {"ok":false,"status":"syntax-error","errors":["错误(10022): …",…],"errorTail":[…],…}
退出码：0 成功/语法通过；2 环境或输入错误；3 编译失败、语法错误或超时；4 自检未通过。

⚠️ 前提：静态编译需要本机 IDE 已配置过「静态编译链接器」（首次请在 IDE 里手动
   静态编译一次完成配置）；本套装实测通过的环境即满足该前提。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ec_guard                                             # noqa: E402
import etool                                                # noqa: E402

EXIT_OK, EXIT_ENV, EXIT_COMPILE, EXIT_DOCTOR = 0, 2, 3, 4
SKILL_SCRIPTS = Path(__file__).resolve().parent
SKILL_ROOT = SKILL_SCRIPTS.parent

KINDS = ("static", "static_exe", "static_console", "static_dll", "exe", "console", "dll", "ecom")

# `--check` 失败时要保留现场（编译器的面板日志就在工作目录里），但**必须限量**：
# 无限堆积会把 %TEMP% 撑满（实测一次会话就留了 73 个目录 / 6.6 MB）。
KEEP_WORKDIRS = 10

# 红线（2026-09-26 用户当面要求写进 skill/工具链，--help 里也展示）
RED_LINES = """\n交付红线（违反任何一条都不准交付）:
  1. .ec 必须由 .e 经官方 IDE 编译产出（头 8 字节 = CNWTEPRG）；
     t2e 产物只是 .e 源码，严禁改扩展名/伪装成 .ec 部署。
  2. QAQ-ETool 报 [错误] 或退出码 2（无法解析 / 未验证代码）⇒ 代码没转全 ⇒ 立即失败，
     严禁继续编译。
  3. 候选 .ec 部署前必须反向抽取（e2t）验证关键语句仍在；缺失 = 丢代码，不准交付
     （--ecom-validate 默认开启；关键语句清单用 --guard 指定）。
  4. 覆盖常用模块前必须先备份旧版。
"""


def out(msg: str) -> None:
    print(msg, file=sys.stderr)


def prune_workdirs(keep: int = KEEP_WORKDIRS) -> None:
    """只保留最新的 keep 个 `ecompile_*` 临时工作目录，其余删掉。

    这些目录里放的是 `--check` 失败时的现场记录（面板日志、加壳中间件），
    留最近几份足够回看问题；旧的一律清掉。失败不可怕，静默堆积才可怕 ——
    所以整体吞掉异常，绝不影响主流程结论。
    """
    try:
        cand: list[tuple[float, Path]] = []
        for p in Path(tempfile.gettempdir()).glob("ecompile_*"):
            try:
                if p.is_dir():
                    cand.append((p.stat().st_mtime, p))
            except OSError:
                pass
        cand.sort(key=lambda t: t[0], reverse=True)
        for _, p in cand[keep:]:
            shutil.rmtree(p, ignore_errors=True)
    except Exception:
        pass


def find_tool(name: str, sub: str) -> Path | None:
    """定位工具时优先工作区真源，再回退到已安装技能副本。"""
    cands: list[Path] = []
    # 开发工作区布局：tools/ 保存 Python 真源，src/addin/ 保存 FNE 真源产物。
    # 不可优先命中 ~/.agents 的旧安装副本，否则本次修复不会实际生效。
    if "scripts" in sub:
        cands.append(SKILL_SCRIPTS / name)
    elif "assets" in sub:
        cands.append(SKILL_ROOT / "src" / "addin" / name)
    cands += [SKILL_ROOT / sub / name,
             SKILL_ROOT.parent / sub / name,
             Path.home() / ".agents" / "skills" / sub / name]
    for c in cands:
        if c.is_file():
            return c
    return None


def find_etool(explicit: str | None) -> str | None:
    """QAQ-ETool：--etool → QAQ_ETOOL → 同级技能 qaq-etool-cli → ~/.agents → PATH。"""
    return etool.find(explicit)


def find_elang_dir(explicit: str | None) -> Path | None:
    cands: list[Path] = []
    if explicit:
        cands.append(Path(explicit))
    env = os.environ.get("ELANG_HOME")
    if env:
        cands.append(Path(env))
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            r"Software\FlySky\E\Install") as k:
            val = winreg.QueryValueEx(k, "Path")[0]
            if val:
                p = Path(val)
                # 该注册表值实测指向 lib\\ 子目录（也可能直接是安装根）
                cands.append(p.parent / "e.exe")
                cands.append(p / "e.exe")
    except OSError:
        pass
    try:
        assoc = subprocess.run(["cmd", "/c", "ftype", "E.Document"],
                               capture_output=True, text=True).stdout
        m = re.search(r'"([^"]+e\.exe)"', assoc, re.I)
        if m:
            cands.append(Path(m.group(1)))
    except OSError:
        pass
    for c in cands:
        if c.is_file():
            return c.parent
    return None


def ec_internal_name(path: Path) -> str | None:
    """读易模块(.ec) / 易语言工程(.e) 在文件头记录的**内部模块名**。

    头部结构：`CNWTEPRG` + 8 字节版本 + …，随后在 0x100~0x180 之间有一处
    `[uint32 长度][GBK 名字]`。实测该字段就是模块名（用文件名当模块名的那个）。

    ⚠️ 易语言 IDE 会**强制把内部名同步成文件名**（打开 .ec 再保存即被改写）。
    而宿主工程里所有「类 / 公开命令」都挂在这个名字上，名字一变引用就断，
    症状是编译期 IDE 直接 0xc0000005 崩溃——**没有任何报错信息**。见 nameMismatch。
    """
    try:
        with open(path, "rb") as f:
            head = f.read(0x200)
    except OSError:
        return None
    if not head.startswith(b"CNWTEPRG"):
        return None
    for off in range(0x100, max(0x100, min(len(head) - 4, 0x180))):
        n = int.from_bytes(head[off:off + 4], "little")
        if not (1 <= n <= 64):
            continue
        raw = head[off + 4:off + 4 + n]
        if len(raw) < n:
            break
        try:
            s = raw.decode("gbk")
        except UnicodeDecodeError:
            continue
        if s and all(ch.isprintable() for ch in s):
            return s
    return None


def validate_ec(dst: Path, tool: str | None, guard: str | None, work: Path) -> dict:
    """候选 .ec 交付门禁（红线1+红线3）。返回 {"ok": bool, "error": str|None, ...}。

    门禁1：头 8 字节必须是 CNWTEPRG（合法已编译易模块；t2e 产物伪装的一律不是）。
    门禁3：反向抽取（QAQ-ETool -mode e2t）必须成功、无 [错误] 且退出码 0；
           给了 --guard 时还必须命中全部关键语句/子程序体量规则。
    失败时调用方必须把候选 .ec 删掉，绝不让坏模块流到部署位。
    """
    r: dict[str, object] = {"ok": True, "error": None}
    try:
        head = dst.open("rb").read(8)
    except OSError as e:
        return {"ok": False, "error": "门禁1 无法读取产物头: %s" % e}
    if head != b"CNWTEPRG":
        return {"ok": False,
                "error": "门禁1 失败：头 8 字节 = %r ≠ b'CNWTEPRG'，不是合法已编译易模块"
                         % head}
    r["header"] = "CNWTEPRG"

    if not tool:
        return {"ok": False,
                "error": "门禁3 无法执行：" + etool.NOT_FOUND +
                         " 按红线3，未做反向抽取验证的 .ec 不准交付"}
    audit = work / "audit"
    p = etool.run(tool, "e2t", dst, audit, "-nosidecar")
    has_txt = (audit / "代码").is_dir() or (audit / "class").is_dir()
    if not p.ok or not has_txt:
        return {"ok": False,
                "error": "门禁3 失败：e2t 反向抽取未产出文本（rc=%s）。坏模块不准交付。\n%s"
                         % (p.rc, (p.stderr or p.stdout)[-300:])}
    pm = ec_guard.parse_error_block(p.stdout, p.stderr, rc=p.rc)
    if pm:
        return {"ok": False,
                "error": "门禁3 失败：反向抽取没能完整还原代码（产物方法体损坏）。\n%s" % pm}
    r["reverseExtract"] = str(audit)

    if guard:
        fails = ec_guard.check_text_dir(audit, guard)
        if fails:
            return {"ok": False,
                    "error": "门禁3 失败：反向抽取关键语句缺失（丢代码，不准交付）：\n  - %s"
                             % "\n  - ".join(fails)}
        r["guard"] = "PASS (%s)" % os.path.basename(guard)
    return r


def doctor(args) -> int:
    mkcage = find_tool("mkcage.py", "elang-debug" + os.sep + "scripts")
    elaunch = find_tool("elaunch.py", "elang-debug" + os.sep + "scripts")
    fne = Path(args.fne) if args.fne else find_tool(
        "elang_addin.fne", "elang-debug" + os.sep + "assets")
    tool = find_etool(args.etool)
    elang = find_elang_dir(args.home)
    checks = {
        "mkcage": str(mkcage) if mkcage else None,
        "elaunch": str(elaunch) if elaunch else None,
        "elang_addin_fne": str(fne) if fne else None,
        "qaq_etool": tool,
        "elang_dir": str(elang) if elang else None,
        "static_linker_note": "静态编译要求 IDE 已配置过静态链接器（首次先在 IDE 手动静态编译一次）",
    }
    # QAQ-ETool 是加壳（mkcage）、文本目录输入与 .ec 门禁的必需品
    ok_all = all([mkcage, elaunch, fne, elang, tool])
    result = {"ok": ok_all, **checks}
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        for k, v in checks.items():
            out("  %s = %s" % (k, v))
        out("  自检: %s" % ("通过" if ok_all else "未通过"))
    return EXIT_OK if ok_all else EXIT_DOCTOR


# ecompile 对外的承诺是「绝不改原工程」。t2e 读的是用户的文本目录，为防转换工具
# 就地改写或新建 `项目.etprj`（把临时路径写进别人的工程），这里快照 + 还原
# （连 mtime 一起还原，免得版本控制每次都报这个文件被改）。
ETPRJ_NAMES = ("项目.etprj", "project.etprj")


def snapshot_etprj(src: Path) -> dict:
    """记录 `项目.etprj` 的原状；**不存在的也记一条 None**，用于事后判定「是被新建的」。"""
    snap = {}
    for n in ETPRJ_NAMES:
        p = src / n
        try:
            if p.is_file():
                st = p.stat()
                snap[p] = (p.read_bytes(), st.st_atime, st.st_mtime)
            else:
                snap[p] = None
        except OSError:
            snap[p] = None
    return snap


def restore_etprj(snap: dict) -> list:
    """还原被就地改写的 `项目.etprj`；返回实际被还原的文件名列表。"""
    fixed = []
    for p, orig in snap.items():
        try:
            if orig is None:
                if p.exists():                 # 原本没有 ⇒ 是转换时新建的，删掉才算还原
                    p.unlink()
                    fixed.append(p.name + "（新建已删）")
                continue
            data, at, mt = orig
            if p.read_bytes() != data:
                p.write_bytes(data)
                os.utime(p, (at, mt))
                fixed.append(p.name)
        except OSError:
            pass
    return fixed


def t2e_if_dir(src: Path, tool: str | None, work: Path) -> tuple[Path, list]:
    """文本目录 → .e。返回 (产物路径, 被还原的输入文件名列表)。"""
    if src.is_file():
        return src, []
    if not (src / "代码").is_dir() and not (src / "class").is_dir():
        raise SystemExit("输入既不是 .e 也不是文本目录（应含 代码/ 或 class/）: %s" % src)
    if not tool:
        raise SystemExit("文本目录输入需要 QAQ-ETool。" + etool.NOT_FOUND)
    dst = work / (src.stem + "_base.e")
    snap = snapshot_etprj(src)
    r = etool.run(tool, "t2e", src, dst)
    restored = restore_etprj(snap)
    if not r.ok or not dst.is_file():
        raise SystemExit("t2e 失败: %s\n  %s" % (src, "\n  ".join((r.fatal + r.errors)[:5])
                                                  or r.tail(300)))
    # ★ 红线2 门禁：QAQ-ETool 默认宽松组包——绑定不了的语句会写成「未验证代码」，
    #   照常 SUCC 并落盘，只以 [错误] + 退出码 2 提示。不查就是带着坏代码往下走，立即失败。
    pm = ec_guard.parse_error_block(r.stdout, r.stderr, rc=r.rc)
    if pm:
        raise SystemExit("t2e 未能完整转换（红线2 门禁）: %s" % pm)
    return dst, restored


def main() -> int:
    ap = argparse.ArgumentParser(
        epilog=RED_LINES, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", nargs="?", help="输入 .e 或文本目录")
    ap.add_argument("dst", nargs="?", help="输出 .exe / .dll（--check 时可省略）")
    ap.add_argument("--check", action="store_true",
                    help="语法兜底检查：不要产物，只判源码能否通过编译器的语法/语义静态检查")
    ap.add_argument("--direct", action="store_true",
                    help="直接编译输入 .e，跳过 mkcage 的 e2t→t2e 回写。"
                         "适用于已有临时 FNE 投放能力且不能承受源码转换的工程。")
    ap.add_argument("--guard", help="交付门禁的关键语句清单（ec_guard 格式）。"
                                    "传给 mkcage 做 _ai.e 回读，也用于最终 .ec 的反向抽取验证")
    ap.add_argument("--ecom-validate", action=argparse.BooleanOptionalAction, default=True,
                    help="编译 .ec 后自动跑交付门禁（头 CNWTEPRG + 反向抽取 + --guard 关键语句），"
                         "不过就删掉候选并返回失败。默认开启，--no-ecom-validate 可关")
    ap.add_argument("--fast", action="store_true",
                    help="与 --check 同用：改走官方预编译 FN_PRE_COMPILE（毫秒级）。"
                         "⚠️ 实测它对确定有问题的源码也一律返回成功，**不能单独当通过判据**")
    ap.add_argument("--visible", action="store_true",
                    help="显式显示易语言窗口（等价 --headless off）")
    ap.add_argument("--headless", choices=["park", "shide", "hide", "off"],
                    help="显式指定窗口策略。编译默认 off，避免隐藏守护链干扰 IDE 编译；"
                         "hide=隐藏+移出屏幕；shide=同上但主窗口走 SW_HIDE 启动；"
                         "park=只移出屏幕、保持可见；off=显示窗口")
    ap.add_argument("--kind", choices=KINDS, default="static_console",
                    help="静态编译种类（默认 static_console）")
    ap.add_argument("--fne", help="宿主库 elang_addin.fne（默认自动定位套装内副本）")
    ap.add_argument("--home", help="易语言安装目录（默认 ELANG_HOME → 注册表 → 文件关联）")
    ap.add_argument("--etool", help="QAQ-ETool.exe 路径或其所在目录（默认 QAQ_ETOOL → "
                                    "同级技能 qaq-etool-cli/assets → ~/.agents/skills → PATH）")
    ap.add_argument("--timeout", type=float, default=300.0, help="编译等待秒数（默认 300）")
    ap.add_argument("--workdir", help="工作目录（默认临时目录，保留加壳副本与日志）")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--doctor", action="store_true", help="环境自检")
    args = ap.parse_args()

    if args.doctor:
        return doctor(args)

    result: dict[str, object] = {"ok": False, "kind": args.kind,
                                 "check": True if args.check else None,
                                 "fast": True if (args.check and args.fast) else None}
    mkcage = find_tool("mkcage.py", "elang-debug" + os.sep + "scripts")
    elaunch = find_tool("elaunch.py", "elang-debug" + os.sep + "scripts")
    fne = Path(args.fne) if args.fne else find_tool(
        "elang_addin.fne", "elang-debug" + os.sep + "assets")
    elang = find_elang_dir(args.home)

    missing = []
    if not mkcage and not args.direct:
        missing.append("mkcage.py（elang-debug 技能 scripts/）")
    if not elaunch:
        missing.append("elaunch.py（elang-debug 技能 scripts/）")
    if not fne:
        missing.append("elang_addin.fne（elang-debug 技能 assets/）")
    if not elang:
        missing.append("易语言安装目录（--home / ELANG_HOME / 注册表）")
    if missing:
        result["error"] = "缺少依赖: " + "; ".join(missing)
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV

    if not args.src or (not args.dst and not args.check):
        out("用法: ecompile.py <输入.e|文本目录> <输出.exe|dll> [--kind …] | "
            "<输入> --check | --doctor")
        return EXIT_ENV

    src = Path(args.src).resolve()
    if not src.exists():
        result["error"] = "输入不存在: %s" % src
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV

    try:
        tool = find_etool(args.etool)
    except SystemExit as e:
        result["error"] = str(e)
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV
    if src.is_dir() and not tool:
        result["error"] = "文本目录输入需要 QAQ-ETool。" + etool.NOT_FOUND
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV

    dst = Path(args.dst).resolve() if args.dst else None
    if dst is not None:
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists():
            dst.unlink()                    # 旧产物必须清掉，否则会误判成功

    work = Path(args.workdir).resolve() if args.workdir \
        else Path(tempfile.mkdtemp(prefix="ecompile_"))
    work.mkdir(parents=True, exist_ok=True)

    try:
        # 1) 文本目录 → .e（t2e_if_dir 内部对 `项目.etprj` 快照还原）
        base_e, restored = t2e_if_dir(src, tool, work)
        if restored:
            result["inputRestored"] = restored
        # 2) 默认加壳声明宿主库；--direct 时由 elaunch 临时投放 FNE 后直接编译原 .e。
        if args.direct:
            ai_e = base_e
            result["direct"] = True
        else:
            ai_e = work / (base_e.stem + "_ai.e")
            mc_cmd = [sys.executable, str(mkcage), str(base_e), str(ai_e),
                      "--fne", str(fne), "--json"]
            if args.guard:
                mc_cmd += ["--guard", args.guard]
            r = subprocess.run(mc_cmd, capture_output=True)
            d = json.loads((r.stdout or b"{}").decode("utf-8", "replace"))
            if not d.get("ok"):
                result["error"] = "加壳失败: %s" % (d.get("error") or r.stderr[:300])
                if args.json:
                    print(json.dumps(result, ensure_ascii=False))
                out(str(result["error"]))
                return EXIT_ENV
            if d.get("guard"):
                result["cageGuard"] = d["guard"]
        # 3) 无头编译 / 语法兜底检查
        cmd = [sys.executable, str(elaunch), str(ai_e),
               "--timeout", str(args.timeout), "--fne", str(fne),
               "--home", str(elang), "--json"]
        if args.visible:
            cmd += ["--headless", "off"]
        elif args.headless:
            cmd += ["--headless", args.headless]
        if args.check and args.fast:
            # 快速模式：官方 FN_PRE_COMPILE（毫秒级）。**不充分**，见 docstring。
            # 必须显式给种类 —— elaunch 的 --check 默认走的是权威真编译路径。
            cmd += ["--check", "--compile", "precompile"]
        elif args.check:
            # 权威模式：真编译，用「编译器来要产物路径 ⇒ 静态检查已过」当判据，
            # 保存框由宿主库自动取消（等于用户在看到保存框时点"取消"）。
            cmd += ["--check", "--compile", args.kind]
        else:
            cmd += ["--compile", args.kind, "--expect-out", str(dst)]
        r = subprocess.run(cmd, capture_output=True)
        try:
            d = json.loads((r.stdout or b"{}").decode("utf-8", "replace"))
        except ValueError:
            d = {}
        if not d:
            result["error"] = "elaunch 未返回 JSON（stderr 尾部: %s）" % \
                r.stderr.decode("utf-8", "replace")[-400:]
            if args.json:
                print(json.dumps(result, ensure_ascii=False))
            out(str(result["error"]))
            return EXIT_COMPILE
        result["status"] = d.get("status")
        result["cleanupWarning"] = d.get("cleanupWarning")
        result["logFile"] = d.get("logFile")
        result["logUtf8"] = d.get("logUtf8")
        result["errors"] = d.get("errors")
        result["errorTail"] = d.get("errorTail")
        result["precompileOk"] = d.get("precompileOk")
        result["eExitCode"] = d.get("eExitCode")
        result["headless"] = d.get("headless")
        if args.check:
            result["syntaxOk"] = bool(d.get("syntaxOk"))
            result["ok"] = bool(d.get("ok"))
            if not result["ok"]:
                errs = d.get("errors") or []
                if errs:
                    result["error"] = "语法检查未通过：%s" % errs[0]
                elif d.get("status") == "timeout":
                    result["error"] = ("语法检查超时：既没等到保存框也没等到报错，"
                                       "结论未知（**不能当通过**）")
                else:
                    result["error"] = "语法检查未能完成（status=%s）" % d.get("status")
                    if d.get("error"):      # elaunch 前置条件失败时的原因（别吞掉）
                        result["error"] += "：%s" % d["error"]
        elif d.get("ok") and dst.is_file() and dst.stat().st_size > 0:
            result["ok"] = True
            result["output"] = str(dst)
            result["outputSize"] = dst.stat().st_size
            # 易模块专项闸门（2026-09-23 新增）：IDE 会强制「内部模块名 = 文件名」。
            # 名字一旦与宿主工程里原有的不同，挂在旧名字下的类/公开命令全部失联，
            # 编译期 IDE 直接 0xc0000005 崩溃（连报错都没有）。这种事故只看产物
            # 是看不出来的，所以在这里主动暴露。
            if dst.suffix.lower() == ".ec":
                inner = ec_internal_name(dst)
                if inner:
                    result["moduleName"] = inner
                    want = dst.stem
                    if inner != want:
                        result["nameMismatch"] = (
                            "产物内部模块名 %r ≠ 文件名 %r。易语言按**文件名**重写模块名："
                            "被宿主引用后，工程里原挂在 %r 下的类/公开命令会全部失联，"
                            "编译期 IDE 会直接崩溃（0xc0000005，无任何报错）。"
                            "请把产物重命名为 %s.ec，或让工程名与目标模块名一致后重编译。"
                            % (inner, want, inner, inner))
                # ★ 交付门禁（--ecom-validate 默认开启）：编译成功 ≠ 可以交付。
                #   门禁1 头 = CNWTEPRG；门禁3 反向抽取无 [错误] 且退出码 0 +
                #   --guard 关键语句全命中。不过 ⇒ 删掉候选，绝不让坏模块流到部署位。
                if args.ecom_validate:
                    gate = validate_ec(dst, tool, args.guard, work)
                    result["gate"] = {k: v for k, v in gate.items() if k != "ok"}
                    if not gate["ok"]:
                        result["ok"] = False
                        result["error"] = gate["error"]
                        result["candidateDeleted"] = True
                        try:
                            dst.unlink()
                        except OSError:
                            result["candidateDeleted"] = False
        else:
            result["error"] = "编译未产出（status=%s）" % d.get("status")
    finally:
        # 保留判据 = 「确实产生过值得回看的日志」**且**本次不成功：
        #   * 成功（ok）           → 日志无回看价值，删
        #   * 早早失败（没跑到 elaunch，没有 logFile）→ 原因已经在 result["error"] 里，
        #     目录里没有额外信息，删（否则会留下一个「JSON 里查不到路径」的孤儿目录）
        #   * 失败且跑到了 elaunch → 保留，并把 workdir 写进结果
        # 顺带修掉一个老毛病：只要带 --check 就一律保留工作目录，导致 (a) 成功也留、
        # (b) 真编译失败时 JSON 里的 logFile/logUtf8 指向已被删掉的路径（悬空）。
        keep = bool(result.get("logFile")) and not result.get("ok")
        if not args.workdir:
            if keep:
                result["workdir"] = str(work)
            else:
                shutil.rmtree(work, ignore_errors=True)
            # 每次调用都顺手清历史堆积（保留最新 KEEP_WORKDIRS 个）。
            # 刚保留的 work 是最新的，必然在保留集里。
            prune_workdirs()

    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    elif not args.quiet:
        if args.check and result.get("ok"):
            out("语法检查通过：编译器静态检查全过（已取消产物生成）")
        elif result.get("ok"):
            out("编译成功: %s (%d B)" % (result["output"], result["outputSize"]))
            if result.get("nameMismatch"):
                out("  ⚠ %s" % result["nameMismatch"])
        else:
            out(str(result.get("error")))
            for e in (result.get("errors") or []):
                out("  %s" % e)
            if result.get("logUtf8"):
                out("  完整面板日志（UTF-8 视图）: %s" % result["logUtf8"])
    return EXIT_OK if result.get("ok") else EXIT_COMPILE


if __name__ == "__main__":
    sys.exit(main())
