#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""无头编译易语言 .e -> 原生 exe/dll（走本套装自己的宿主支持库链路，零第三方依赖）。

    python ecompile.py <输入.e|文本目录> <输出.exe|输出.dll> [--kind 静态编译种类]
                       [--fne <elang_addin.fne>] [--home <易语言目录>] [--etool <QAQ-ETool.exe>]
                       [--echeck <echeck.py>] [--timeout 300] [--workdir <目录>] [--json] [-q]
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

  0. **流程分支硬门禁（2026-10-24 起，无条件）**：先用 echeck.py --branch-only 检查
     输入源码；任何流程分支错误都拒绝编译（不解决就不允许编译）。输入是 .e 时先
     用 QAQ-ETool 拆到临时目录；拆包不完整也拒绝编译。echeck.py 由同装的
     elang-ai-coding 技能提供，找不到照样 fail closed。
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
个别大型 DLL 工程若在无头编译中崩（eExitCode=0xC0000005），怀疑与窗口守护链对 IDE
窗口的并发改写有关，可改 `--headless park`（只移出屏幕不隐藏）或 `--visible` 重试。

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
  真编译 {"ok":true,"status":"ok","output":"…","outputSize":N,"branchGate":{…},…}
  --check {"ok":true,"status":"syntax-ok","errors":null,"branchGate":{…},…}
          {"ok":false,"status":"syntax-error","errors":["错误(10022): …",…],"errorTail":[…],…}
  流程分支门禁失败 {"ok":false,"branchGate":{"ok":false,"branch":{"errors":N,"warnings":0,
                    "passed":false,"messages":[…]}},"error":"流程分支门禁未过…"}
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
  5. 流程分支问题一律为错误；echeck --branch-only 不过 ⇒ 禁止启动编译器（无旁路开关）。
"""


def out(msg: str) -> None:
    print(msg, file=sys.stderr)


def _harden_stdout() -> None:
    """让「打印结果」永远不会成为失败原因。

    本机控制台是 GBK（cp936）。IDE 面板/弹窗原文里只要出现一个 GBK 编不出的字符
    （实测：解码失败留下的 U+FFFD），`print(json.dumps(...))` 就会抛
    UnicodeEncodeError —— 于是**编译明明成功了，调用方却只看到一段 traceback**，
    连 JSON 都拿不到（这正是 2026-10-09 那次对照实验踩到的坑）。
    统一改成「编不出的字符替换掉」，宁可少一个字符，也不能丢结论。
    """
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")          # type: ignore[union-attr]
        except Exception:                                 # noqa: BLE001
            pass


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


def find_echeck(explicit: str | None) -> Path | None:
    """定位 elang-ai-coding 的 echeck.py。

    ecompile 的流程分支门禁是硬依赖：找不到就不允许编译（fail closed）。
    顺序：--echeck → 同级技能 elang-ai-coding → ~/.agents → ~/.dsh。
    """
    if explicit:
        p = Path(explicit)
        return p if p.is_file() else None
    cands = [
        SKILL_ROOT.parent / "elang-ai-coding" / "scripts" / "echeck.py",
        # 开发工作区布局：tools/ 里放的是 Python 真源；不能优先命中 ~/.agents 的旧副本。
        SKILL_ROOT / "tools" / "echeck.py",
        Path.home() / ".agents" / "skills" / "elang-ai-coding" / "scripts" / "echeck.py",
        Path.home() / ".dsh" / "skills" / "elang-ai-coding" / "scripts" / "echeck.py",
    ]
    for p in cands:
        if p.is_file():
            return p
    return None


def branch_gate(src: Path, tool: str | None, work: Path,
                echeck: Path | None) -> dict:
    """编译前强制跑 echeck --branch-only；任何流程分支错误都拒绝编译。

    这是用户明确要求的硬门禁：流程分支问题一律是错误，不解决就不允许编译。
    输入是 .e 时先用 QAQ-ETool 拆到临时文本目录；拆包不完整（rc≠0 或有
    [错误]）也按门禁失败处理，绝不在结构未知的源码上启动编译器。
    """
    res: dict[str, object] = {
        "ok": False,
        "echeck": str(echeck) if echeck else None,
        "rule": "流程分支问题一律为错误；不解决不允许编译",
    }
    if not echeck or not echeck.is_file():
        res["error"] = "缺少流程分支门禁 echeck.py（elang-ai-coding 技能 scripts/）"
        return res

    text_dir = src
    if src.is_file():
        if not tool:
            res["error"] = "输入 .e 需要 QAQ-ETool 拆包后才能跑流程分支门禁。" + etool.NOT_FOUND
            return res
        unpacked = work / "branch_gate_text"
        shutil.rmtree(unpacked, ignore_errors=True)
        r = etool.run(tool, "e2t", str(src), str(unpacked), "-level", "2")
        if not r.clean:
            res["error"] = ("流程分支门禁无法完整拆包（rc=%s）：%s"
                            % (r.rc, r.info() or r.tail()))
            return res
        text_dir = unpacked

    so_path = work / "branch_gate.stdout.txt"
    se_path = work / "branch_gate.stderr.txt"
    env = os.environ.copy()
    env["PYTHONUTF8"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    cmd = [sys.executable, str(echeck), str(text_dir), "--branch-only", "--json"]
    try:
        with open(so_path, "w", encoding="utf-8") as so, \
                open(se_path, "w", encoding="utf-8") as se:
            p = subprocess.run(cmd, stdout=so, stderr=se, env=env)
    except OSError as exc:
        res["error"] = "流程分支门禁无法执行: %s" % exc
        return res
    raw = so_path.read_text(encoding="utf-8", errors="replace")
    try:
        d = json.loads(raw)
    except ValueError:
        tail = se_path.read_text(encoding="utf-8", errors="replace")[-500:]
        res["error"] = ("流程分支门禁未返回 JSON（rc=%s）。stdout=%r stderr=%s"
                        % (p.returncode, raw[-300:], tail))
        return res
    branch = d.get("branch") or {}
    res["branch"] = branch
    res["exitCode"] = p.returncode
    if not branch:
        res["error"] = "流程分支门禁输出缺少 branch 字段"
        return res
    res["ok"] = bool(branch.get("passed")) and p.returncode == 0
    if not res["ok"]:
        msgs = branch.get("messages") or d.get("errors") or []
        if not msgs:
            msgs = ["流程分支错误 %s 条" % branch.get("errors", "?")]
        res["error"] = ("流程分支门禁未过（不解决不允许编译）：\n  - "
                        + "\n  - ".join(str(m) for m in msgs[:20]))
        if len(msgs) > 20:
            res["error"] += "\n  …（另有 %d 条）" % (len(msgs) - 20)
    return res


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
    echeck = find_echeck(args.echeck)
    elang = find_elang_dir(args.home)
    checks = {
        "mkcage": str(mkcage) if mkcage else None,
        "elaunch": str(elaunch) if elaunch else None,
        "elang_addin_fne": str(fne) if fne else None,
        "qaq_etool": tool,
        "echeck": str(echeck) if echeck else None,
        "elang_dir": str(elang) if elang else None,
        "static_linker_note": "静态编译要求 IDE 已配置过静态链接器（首次先在 IDE 手动静态编译一次）",
    }
    # QAQ-ETool 是加壳（mkcage）、文本目录输入与 .ec 门禁的必需品；
    # echeck 是编译前流程分支硬门禁的必需品，缺了必须 fail closed。
    ok_all = all([mkcage, elaunch, fne, elang, tool, echeck])
    result = {"ok": ok_all, **checks}
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        for k, v in checks.items():
            out("  %s = %s" % (k, v))
        out("  自检: %s" % ("通过" if ok_all else "未通过"))
    return EXIT_OK if ok_all else EXIT_DOCTOR


# ecompile 对外的承诺是「绝不改原工程」。t2e 读的是用户的文本目录，为防转换工具
# 就地改写或新建项目文件 `项目.qtprj`（把临时路径写进别人的工程），这里快照 + 还原
# （连 mtime 一起还原，免得版本控制每次都报这个文件被改）。
PRJ_NAMES = ("项目.qtprj", "project.qtprj")


def snapshot_prj(src: Path) -> dict:
    """记录项目文件（`项目.qtprj` 等）的原状；**不存在的也记一条 None**，用于事后判定「是被新建的」。"""
    snap = {}
    for n in PRJ_NAMES:
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


def restore_prj(snap: dict) -> list:
    """还原被就地改写的项目文件；返回实际被还原的文件名列表。"""
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
    snap = snapshot_prj(src)
    r = etool.run(tool, "t2e", src, dst)
    restored = restore_prj(snap)
    if not r.ok or not dst.is_file():
        raise SystemExit("t2e 失败: %s\n  %s" % (src, "\n  ".join((r.fatal + r.errors)[:5])
                                                  or r.tail(300)))
    # ★ 红线2 门禁：QAQ-ETool 默认宽松组包——绑定不了的语句会写成「未验证代码」，
    #   照常 SUCC 并落盘，只以 [错误] + 退出码 2 提示。不查就是带着坏代码往下走，立即失败。
    pm = ec_guard.parse_error_block(r.stdout, r.stderr, rc=r.rc)
    if pm:
        raise SystemExit("t2e 未能完整转换（红线2 门禁）: %s" % pm)
    return dst, restored


def _read_user_cfg(text_dir: Path) -> dict:
    """从文本工程里读「程序信息」（中文布局 配置/ 与英文布局 config/ 都认）。"""
    for rel in ("配置/用户.config.json", "config/user.config.json"):
        p = text_dir / rel
        if p.is_file():
            try:
                return json.loads(p.read_text(encoding="utf-8-sig"))
            except Exception:                             # noqa: BLE001
                return {}
    return {}


def probe_program_info(src: Path, tool, work: Path) -> dict:
    """读工程「程序信息」：WriteVersion / Name / ReleaseVersion。

    输入是文本目录就直接读；是 .e 就用 QAQ-ETool 拆一次包读回来（大工程约 0.5s，
    相对一次真编译可以忽略）。任何失败都返回 {}，绝不因为「探针」而影响编译。
    """
    if src.is_dir():
        return _read_user_cfg(src)
    if not tool:
        return {}
    probe = work / "probe_text"
    try:
        if probe.exists():
            shutil.rmtree(probe, ignore_errors=True)
        r = etool.run(tool, "e2t", str(src), str(probe), "-level", "2")
    except Exception as exc:                              # noqa: BLE001
        return {"_probeError": "e2t 探针异常: %s" % exc}
    if not r.ok:
        return {"_probeError": "e2t 探针失败: %s" % r.tail()}
    return _read_user_cfg(probe)


def version_resource_gate(dst: Path, args, src: Path, tool, work: Path) -> dict:
    """发布门禁：产物的 Windows 版本资源（RT_VERSION）是否齐全、版本是否对得上。

    背景（2026-10-09 事故）：易语言工程把「程序信息」编译成 RT_VERSION。源码只要在
    QAQ-ETool 的 e2t→t2e 往返里丢过这条信息，产物照样编译成功、照样能跑，但
    Windows「属性→详细信息」**所有字段全空**。只判「产物存在 + 大小 + mtime」的旧判据
    完全看不见，于是「软件信息全空」的成品被当成成功发了出去。
    """
    require = args.require_versioninfo
    info: dict = {}
    if require is None or args.expect_version:
        info = probe_program_info(src, tool, work)
    auto = require is None
    if require is None:
        require = bool(info.get("WriteVersion"))
    if not require and not args.expect_version:
        return {"ok": True, "required": False, "auto": auto,
                "skipped": "未要求（工程「程序信息」的 WriteVersion 不是 true）",
                "writeVersion": bool(info.get("WriteVersion")),
                "projectName": info.get("Name"),
                "releaseVersion": info.get("ReleaseVersion"),
                "probeError": info.get("_probeError")}
    try:
        import pever                                     # noqa: PLC0415
    except Exception as exc:                              # noqa: BLE001
        return {"ok": True, "required": bool(require), "auto": auto,
                "warning": "pever.py 不可用，跳过版本资源校验: %s" % exc}
    chk = pever.check_version_info(dst, args.expect_version, bool(require))
    return {"ok": bool(chk["ok"]), "required": bool(require), "auto": auto,
            "expectVersion": args.expect_version,
            "present": chk.get("present"), "fields": chk.get("fields"),
            "missing": chk.get("missing"), "mismatch": chk.get("mismatch"),
            "warnings": chk.get("warnings"),
            "resourceTypes": chk.get("resourceTypes"), "error": chk.get("error"),
            "writeVersion": bool(info.get("WriteVersion")),
            "projectName": info.get("Name")}


def _first_line(s: str) -> str:
    """取「程序信息」里 Description 的首段：易语言把它写成「描述\\r\\n@备注:\\r\\n备注」。"""
    if not s:
        return ""
    head = str(s).split("@备注:")[0]
    for line in head.replace("\r", "\n").split("\n"):
        if line.strip():
            return line.strip()
    return ""


def stamp_version_info(dst: Path, args, src: Path, tool, work: Path) -> dict:
    """编译成功后补写 Windows 版本资源（RT_VERSION）。

    为什么必须存在这一步（2026-10-09 查实，证据见 SKILL.md §2.2）
    ------------------------------------------------------------
    无头静态编译（elang_addin → FNST_RUN → FN_COMPILE_STATIC）对**免打包/易包**工程
    （产物里嵌了 3.6 MB 的 `E2EE_IDR_DLL`）**一律不写 RT_VERSION**：

    | 产物 | E2EE_IDR_DLL | IDE 编译 | 无头静态编译 |
    |------|--------------|----------|--------------|
    | 未开免打包的工程产物     | 无 | 1.0.0.0 | **1.0.0.0 ✔** |
    | 免打包工程产物 exe 旧版  | 有 | 1.0.0.0 | — |
    | 免打包工程产物 exe ×2   | 有 | —       | **空 ✘** |
    | 免打包工程产物 dll      | 有 | —       | **空 ✘** |

    而且和源码无关：同一份正式 `.e`（含作者另存的存档副本），无论是否经过
    QAQ-ETool 往返、无论 `--kind static/static_exe`、无论中英文布局，无头编译都不带版本
    资源；同一份源码用 IDE 编译就带。所以「改源码」这条路走不通，只能在产物上补写。

    这里走的是 **Windows 官方资源 API**（BeginUpdateResource / UpdateResource /
    EndUpdateResource，资源编辑器同款），产物仍是正常 PE。补写完由
    `version_resource_gate` 复核，过不了照样删候选。
    """
    try:
        import versionize                                 # noqa: PLC0415
    except Exception as exc:                              # noqa: BLE001
        return {"ok": False, "error": "versionize.py 不可用: %s" % exc}

    info = probe_program_info(src, tool, work)
    rel = info.get("ReleaseVersion") or {}
    ver = (args.expect_version
           or ("%s.%s" % (rel.get("Major", 1), rel.get("Minor", 0))))
    try:
        fv = ".".join(str(x) for x in versionize._ver_tuple(ver))
    except SystemExit as exc:
        return {"ok": False, "error": "版本号无法解析: %s" % exc}

    strings = {
        "Comments": args.stamp_comment or "",
        "CompanyName": args.stamp_company or (info.get("Author") or ""),
        "FileDescription": args.stamp_description or _first_line(info.get("Description")),
        "FileVersion": fv,
        "InternalName": dst.stem,
        "LegalCopyright": args.stamp_copyright or (info.get("Copyright") or "").strip(),
        "OriginalFilename": dst.name,
        "ProductName": args.stamp_product_name or (info.get("Name") or dst.stem),
        "ProductVersion": fv,
    }
    blob = versionize.build_version_resource(
        strings, 2052, 1200,
        versionize.VFT_DLL if dst.suffix.lower() == ".dll" else versionize.VFT_APP, fv, fv)
    res = versionize.write_version_resource(dst, blob, 2052)
    res.update({"fileVersion": fv, "strings": {k: v for k, v in strings.items() if v}})
    return res


def main() -> int:
    _harden_stdout()
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
                    help="显式指定窗口策略。默认 hide（真无头，编译全程不弹窗）；"
                         "hide=隐藏+移出屏幕；shide=同上但主窗口走 SW_HIDE 启动；"
                         "park=只移出屏幕、保持可见；off=显示窗口（排障用，等价 --visible）")
    ap.add_argument("--kind", choices=KINDS, default="static_console",
                    help="静态编译种类（默认 static_console）")
    ap.add_argument("--expect-version",
                    help="发布门禁：要求产物 FileVersion/ProductVersion 等于该版本"
                         "（写 3.1.3 即可，产物里是 3.1.3.0）。给了就强制校验版本资源")
    ap.add_argument("--require-versioninfo", action=argparse.BooleanOptionalAction,
                    default=None,
                    help="要求产物必须带 Windows 版本资源（RT_VERSION），否则判失败并删掉"
                         "候选产物。默认 auto：工程「程序信息」里 WriteVersion=true 就自动"
                         "开启（输入是文本目录直接读配置，是 .e 则拆包探针读回）。"
                         "显式 --require-versioninfo / --no-require-versioninfo 可覆盖")
    ap.add_argument("--stamp-version-info", action="store_true",
                    help="编译成功后，用 Windows 官方资源 API 给产物补写版本资源"
                         "（RT_VERSION），再走 --expect-version 门禁。"
                         "★ 无头静态编译对「免打包/易包」工程（产物里嵌了 E2EE_IDR_DLL）"
                         "一律不产出 RT_VERSION，源码怎么改都没用（2026-10-09 查实）；"
                         "这类工程要出带「软件信息」的正式版，就得加这个开关。"
                         "字段默认取自工程「程序信息」，可用 --stamp-* 覆盖")
    ap.add_argument("--stamp-product-name", help="覆盖补写的「产品名称」")
    ap.add_argument("--stamp-description", help="覆盖补写的「文件说明」")
    ap.add_argument("--stamp-company", help="覆盖补写的「公司名称」")
    ap.add_argument("--stamp-copyright", help="覆盖补写的「版权」")
    ap.add_argument("--stamp-comment", help="覆盖补写的「备注」")
    ap.add_argument("--fne", help="宿主库 elang_addin.fne（默认自动定位套装内副本）")
    ap.add_argument("--home", help="易语言安装目录（默认 ELANG_HOME → 注册表 → 文件关联）")
    ap.add_argument("--etool", help="QAQ-ETool.exe 路径或其所在目录（默认 QAQ_ETOOL → "
                                    "同级技能 qaq-etool-cli/assets → ~/.agents/skills → PATH）")
    ap.add_argument("--echeck", help="流程分支门禁 echeck.py（默认同级技能 elang-ai-coding/scripts "
                                    "→ ~/.agents/skills → ~/.dsh/skills；找不到就不允许编译）")
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
    echeck = find_echeck(args.echeck)

    missing = []
    if not mkcage and not args.direct:
        missing.append("mkcage.py（elang-debug 技能 scripts/）")
    if not elaunch:
        missing.append("elaunch.py（elang-debug 技能 scripts/）")
    if not fne:
        missing.append("elang_addin.fne（elang-debug 技能 assets/）")
    if not elang:
        missing.append("易语言安装目录（--home / ELANG_HOME / 注册表）")
    if not echeck:
        missing.append("echeck.py（elang-ai-coding 技能 scripts/，流程分支硬门禁）")
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
    work = Path(args.workdir).resolve() if args.workdir \
        else Path(tempfile.mkdtemp(prefix="ecompile_"))
    work.mkdir(parents=True, exist_ok=True)

    # ★ 编译前流程分支硬门禁（2026-10-24）：先跑 echeck --branch-only。
    #   只由流程分支决定是否放行；任何一条流程分支错误都拒绝启动编译器。
    #   门禁用独立临时目录，避免影响正式编译工作目录的保留/清理规则。
    gate_work = work / "branch_gate"
    shutil.rmtree(gate_work, ignore_errors=True)
    gate_work.mkdir(parents=True, exist_ok=True)
    try:
        gate = branch_gate(src, tool, gate_work, echeck)
    finally:
        shutil.rmtree(gate_work, ignore_errors=True)
    result["branchGate"] = gate
    if not gate.get("ok"):
        result["error"] = str(gate.get("error") or "流程分支门禁未过（不解决不允许编译）")
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        if not args.workdir:
            shutil.rmtree(work, ignore_errors=True)
        return EXIT_COMPILE

    # 旧产物必须在门禁通过后才能清掉：门禁失败时不能把用户已有的成品删掉。
    if dst is not None:
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists():
            dst.unlink()                    # 旧产物必须清掉，否则会误判成功
    # 易语言静态编译器会在**输出目录**里留下 0 字节的 RCX*.tmp 占位文件。发布区里冒出
    # 这些文件纯属污染（用户会问「哪来这么多 tmp」），所以记下编译前已有的，编译后只删
    # 本次新出现的，别的一概不碰。
    rcx_before: set = set()
    if dst is not None and dst.parent.is_dir():
        rcx_before = {p.name for p in dst.parent.glob("RCX*.tmp")}

    # ★ 修复（2026-10-09）：finally 里引用的 check_out_made / expect_out 从未被赋值，
    #   于是**每一次**调用都会在 finally 抛 NameError("name 'check_out_made' is not
    #   defined")——不仅失败被掩盖，连编译成功的 JSON 结果也一并被吞掉，调用方只能看到
    #   一段 traceback。--check 的占位产物本来就由 elaunch 自己创建并清理
    #   （elaunch.py: check_out_made 903-906 / safe_remove 1207-1208），ecompile 这侧
    #   无需再删；这里显式初始化，让该分支退化为空操作。
    check_out_made = False
    expect_out = None

    try:
        # 1) 文本目录 → .e（t2e_if_dir 内部对 `项目.qtprj` 快照还原）
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
        else:
            # ★ 修复（2026-10-09，用户实测点名）：此前这里什么都不传，落到 elaunch 的
            #   「编译模式默认 off」⇒ IDE 窗口全程可见，用户看得见它自动编译、自动填
            #   保存框再关闭 —— 与本技能「无头行为（默认）」的承诺直接矛盾。
            #   elaunch 的设计约定是「无头编译须由调用方显式声明」，那就在唯一调用方
            #   这里显式声明 hide；要看界面排障时 --visible / --headless off 仍然可用。
            cmd += ["--headless", "hide"]
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
        # 事件通道（2026-10-03 起）：编译/运行期的致命报错可能走 MessageBox
        # （如「无法找到启动子程序」），面板一个字都没有。elaunch 会 tail 宿主库
        # 事件文件并把弹窗原文带回 —— 这里原样透传给调用方（AI）。
        result["dialogBlocked"] = d.get("dialogBlocked")
        result["eventsFile"] = d.get("eventsFile")
        result["eventsTail"] = d.get("eventsTail")
        result["dialogsSeen"] = d.get("dialogsSeen")
        result["precompileOk"] = d.get("precompileOk")
        result["eExitCode"] = d.get("eExitCode")
        result["headless"] = d.get("headless")
        if args.check:
            result["syntaxOk"] = bool(d.get("syntaxOk"))
            result["ok"] = bool(d.get("ok"))
            if not result["ok"]:
                dlg = d.get("dialogBlocked") or {}
                errs = d.get("errors") or []
                if errs:
                    result["error"] = "语法检查未通过：%s" % errs[0]
                elif dlg.get("msg") or dlg.get("title"):
                    # 错误弹窗（面板没有错误行时，原文只在弹窗里）
                    result["error"] = ("语法检查未通过，IDE 弹窗报错：%s %s" % (
                        dlg.get("title") or "", dlg.get("msg") or "")).strip()
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
            # ★ 发布门禁（2026-10-09 新增）：编译成功 ≠ 发布就绪。
            #   产物「软件信息全空」在旧判据下完全不可见，必须单独验一遍版本资源。
            if dst.suffix.lower() in (".exe", ".dll", ".ocx", ".scr", ".cpl"):
                # 免打包/易包工程的无头编译不带 RT_VERSION（改源码无解），需要就先补写。
                if args.stamp_version_info:
                    st = stamp_version_info(dst, args, src, tool, work)
                    result["versionInfoStamped"] = st
                    if not st.get("ok"):
                        result["ok"] = False
                        result["error"] = "版本资源补写失败：%s" % st.get("error")
                        result["candidateDeleted"] = True
                        try:
                            dst.unlink()
                        except OSError:
                            result["candidateDeleted"] = False
                if result["ok"]:
                    vg = version_resource_gate(dst, args, src, tool, work)
                    result["versionInfo"] = vg
                    if not vg.get("ok"):
                        reasons = list(vg.get("missing") or []) + list(vg.get("mismatch") or [])
                        result["ok"] = False
                        result["error"] = (
                            "版本资源门禁未过（产物已删除，绝不发布「软件信息全空」的成品）："
                            + "；".join(reasons or [vg.get("error") or "未知原因"])
                            + "。若该工程是免打包/易包工程（产物内嵌 E2EE_IDR_DLL），"
                              "无头编译本来就不写版本资源，请加 --stamp-version-info 补写")
                        result["candidateDeleted"] = True
                        try:
                            dst.unlink()
                        except OSError:
                            result["candidateDeleted"] = False
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
            dlg = d.get("dialogBlocked") or {}
            if dlg.get("msg") or dlg.get("title"):
                result["error"] = ("编译被错误弹窗阻断（status=%s）：%s %s" % (
                    d.get("status"), dlg.get("title") or "",
                    dlg.get("msg") or "")).strip()
            else:
                result["error"] = "编译未产出（status=%s）" % d.get("status")
    finally:
        # 清掉本次编译在输出目录里新产生的 RCX*.tmp（0 字节占位，纯污染）。
        if dst is not None and dst.parent.is_dir():
            for p in dst.parent.glob("RCX*.tmp"):
                if p.name not in rcx_before:
                    try:
                        p.unlink()
                        result.setdefault("tempFilesRemoved", []).append(p.name)
                    except OSError:
                        pass
        # --check 的保存框只需要一个可填路径，绝不能把这个占位产物留在
        # 保留的失败现场里；真正需要回看的内容是面板/事件日志。
        if check_out_made and expect_out:
            try:
                Path(expect_out).unlink(missing_ok=True)
            except OSError:
                result.setdefault("cleanupWarning", []).append(
                    "未能清理 --check 占位产物: %s" % expect_out)
        # 保留判据 = 「确实产生过值得回看的日志」**且**本次不成功：
        #   * 成功（ok）           → 日志无回看价值，删
        #   * 早早失败（没跑到 elaunch，没有 logFile）→ 原因已经在 result["error"] 里，
        #     目录里没有额外信息，删（否则会留下一个「JSON 里查不到路径」的孤儿目录）
        #   * 失败且跑到了 elaunch → 保留，并把 workdir 写进结果
        # 顺带修掉一个老毛病：只要带 --check 就一律保留工作目录，导致 (a) 成功也留、
        # (b) 真编译失败时 JSON 里的 logFile/logUtf8 指向已被删掉的路径。
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
            dlg = result.get("dialogBlocked") or {}
            if dlg.get("msg"):
                out("  IDE 弹窗: %s %s" % (dlg.get("title") or "", dlg.get("msg")))
            if result.get("logUtf8"):
                out("  完整面板日志（UTF-8 视图）: %s" % result["logUtf8"])
    return EXIT_OK if result.get("ok") else EXIT_COMPILE


if __name__ == "__main__":
    sys.exit(main())
