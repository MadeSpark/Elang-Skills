#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""无头编译易语言 .e -> 原生 exe/dll（走本套装自己的宿主支持库链路，零第三方依赖）。

    python ecompile.py <输入.e|文本目录> <输出.exe|输出.dll> [--kind 静态编译种类]
                       [--fne <elang_addin.fne>] [--home <易语言目录>] [--e2txt <e2txt.exe>]
                       [--timeout 300] [--workdir <目录>] [--json] [-q]
    python ecompile.py --doctor [--json]

流程（全部复用套装既有技能脚本）：

  1. 输入是文本目录（含 代码/）时先 t2e 出 .e（e2txt）。
  2. mkcage 加壳：把本套装宿主库 elang_addin 登记进工程库表（绝不改原 .e）。
  3. elaunch --compile：临时投放 .fne 到 lib\\ → 启动独立 IDE → 宿主库进程内触发
     静态编译功能号（官方 FNST_RUN 家族，见 elang-debug 技能《接口与调用约定清单》）→
     自动填「请输入欲编译到的可执行文件名」保存框 → 监视期望产物出现 → 按 PID 收尾，
     并对易语言安装目录做 before/after diff 自证还原。

静态编译种类（--kind，缺省 static_console）：

  static         FN_COMPILE_STATIC(0x05020007)   按 IDE 当前工程类型静态编译
  static_exe     FN_STATIC_COMPILE_WINDOWS_EXE   静态编译为 Windows 程序
  static_console FN_STATIC_COMPILE_WINDOWS_CONOLE_EXE  静态编译为 Windows 控制台程序
  static_dll     FN_STATIC_COMPILE_WINDOWS_DLL   静态编译为 Windows 动态链接库
  exe/console/dll                                    对应的非静态编译

结果 JSON（--json；stdout 只放 JSON）：{"ok":true,"status":"ok","output":"…","outputSize":N,…}
退出码：0 成功；2 环境/输入错误；3 编译失败或超时；4 自检未通过。

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

EXIT_OK, EXIT_ENV, EXIT_COMPILE, EXIT_DOCTOR = 0, 2, 3, 4
SKILL_SCRIPTS = Path(__file__).resolve().parent
SKILL_ROOT = SKILL_SCRIPTS.parent

KINDS = ("static", "static_exe", "static_console", "static_dll", "exe", "console", "dll")


def out(msg: str) -> None:
    print(msg, file=sys.stderr)


def find_tool(name: str, sub: str) -> Path | None:
    """定位同套装技能里的脚本/资产：本技能根 → ~/.agents/skills → PATH 不适用。"""
    cands = [SKILL_ROOT / sub / name,
             SKILL_ROOT.parent / sub / name,
             Path.home() / ".agents" / "skills" / sub / name]
    for c in cands:
        if c.is_file():
            return c
    return None


def find_e2txt(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    env = os.environ.get("E2TXT")
    if env and Path(env).is_file():
        return env
    found = shutil.which("e2txt")
    if found:
        return found
    env_dir = os.environ.get("E2TXT_DIR")
    if env_dir and (Path(env_dir) / "e2txt.exe").is_file():
        return str(Path(env_dir) / "e2txt.exe")
    return None


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


def doctor(args) -> int:
    mkcage = find_tool("mkcage.py", "elang-debug" + os.sep + "scripts")
    elaunch = find_tool("elaunch.py", "elang-debug" + os.sep + "scripts")
    fne = Path(args.fne) if args.fne else find_tool(
        "elang_addin.fne", "elang-debug" + os.sep + "assets")
    e2txt = find_e2txt(args.e2txt)
    elang = find_elang_dir(args.home)
    checks = {
        "mkcage": str(mkcage) if mkcage else None,
        "elaunch": str(elaunch) if elaunch else None,
        "elang_addin_fne": str(fne) if fne else None,
        "e2txt": e2txt,
        "elang_dir": str(elang) if elang else None,
        "static_linker_note": "静态编译要求 IDE 已配置过静态链接器（首次先在 IDE 手动静态编译一次）",
    }
    ok_all = all([mkcage, elaunch, fne, elang])  # e2txt 仅文本目录输入时必需
    result = {"ok": ok_all, **checks}
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    else:
        for k, v in checks.items():
            out("  %s = %s" % (k, v))
        out("  自检: %s" % ("通过" if ok_all else "未通过"))
    return EXIT_OK if ok_all else EXIT_DOCTOR


def t2e_if_dir(src: Path, e2txt: str | None, work: Path) -> Path:
    if src.is_file():
        return src
    if not (src / "代码").is_dir() and not (src / "class").is_dir():
        raise SystemExit("输入既不是 .e 也不是文本目录（应含 代码/ 或 class/）: %s" % src)
    if not e2txt:
        raise SystemExit("文本目录输入需要 e2txt（--e2txt / E2TXT / PATH）")
    dst = work / (src.stem + "_base.e")
    r = subprocess.run([e2txt, "-mode", "t2e", "-src", str(src), "-dst", str(dst),
                        "-enc", "UTF-8", "-level", "1", "-ns", "2"], capture_output=True)
    if "SUCC" not in r.stdout.decode("gbk", "replace") or not dst.is_file():
        err = r.stderr.decode("gbk", "replace")
        if "AI调试宿主" in err or "elang_addin" in err:
            raise SystemExit("输入 .e 已经加壳过（库表里已有本套装宿主库），请改用原始 .e；"
                             "e2t 无法回读声明了未安装支持库的工程"
                             "（会报「加载支持库失败」且不产出文本目录）")
        raise SystemExit("t2e 失败（见上方 stderr 的 [错误] 行）: %s" % src)
    return dst


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("src", nargs="?", help="输入 .e 或文本目录")
    ap.add_argument("dst", nargs="?", help="输出 .exe / .dll")
    ap.add_argument("--kind", choices=KINDS, default="static_console",
                    help="静态编译种类（默认 static_console）")
    ap.add_argument("--fne", help="宿主库 elang_addin.fne（默认自动定位套装内副本）")
    ap.add_argument("--home", help="易语言安装目录（默认 ELANG_HOME → 注册表 → 文件关联）")
    ap.add_argument("--e2txt", help="e2txt 可执行文件（输入为文本目录时需要）")
    ap.add_argument("--timeout", type=float, default=300.0, help="编译等待秒数（默认 300）")
    ap.add_argument("--workdir", help="工作目录（默认临时目录，保留加壳副本与日志）")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--doctor", action="store_true", help="环境自检")
    args = ap.parse_args()

    if args.doctor:
        return doctor(args)

    result: dict[str, object] = {"ok": False, "kind": args.kind}
    mkcage = find_tool("mkcage.py", "elang-debug" + os.sep + "scripts")
    elaunch = find_tool("elaunch.py", "elang-debug" + os.sep + "scripts")
    fne = Path(args.fne) if args.fne else find_tool(
        "elang_addin.fne", "elang-debug" + os.sep + "assets")
    elang = find_elang_dir(args.home)

    missing = []
    if not mkcage:
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

    if not args.src or not args.dst:
        out("用法: ecompile.py <输入.e|文本目录> <输出.exe|dll> [--kind …] | --doctor")
        return EXIT_ENV

    src = Path(args.src).resolve()
    if not src.exists():
        result["error"] = "输入不存在: %s" % src
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV

    e2txt = find_e2txt(args.e2txt)
    if src.is_dir() and not e2txt:
        result["error"] = "文本目录输入需要 e2txt（--e2txt / E2TXT / PATH）"
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV

    dst = Path(args.dst).resolve()
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()                        # 旧产物必须清掉，否则会误判成功

    work = Path(args.workdir).resolve() if args.workdir \
        else Path(tempfile.mkdtemp(prefix="ecompile_"))
    work.mkdir(parents=True, exist_ok=True)

    try:
        # 1) 文本目录 → .e
        base_e = t2e_if_dir(src, e2txt, work)
        # 2) 加壳（绝不改原 .e）
        ai_e = work / (base_e.stem + "_ai.e")
        r = subprocess.run([sys.executable, str(mkcage), str(base_e), str(ai_e),
                            "--fne", str(fne), "--json"], capture_output=True)
        d = json.loads((r.stdout or b"{}").decode("utf-8", "replace"))
        if not d.get("ok"):
            result["error"] = "加壳失败: %s" % (d.get("error") or r.stderr[:300])
            if args.json:
                print(json.dumps(result, ensure_ascii=False))
            out(str(result["error"]))
            return EXIT_ENV
        # 3) 无头编译
        r = subprocess.run([sys.executable, str(elaunch), str(ai_e),
                            "--compile", args.kind, "--expect-out", str(dst),
                            "--timeout", str(args.timeout), "--fne", str(fne),
                            "--home", str(elang), "--json"], capture_output=True)
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
        if d.get("ok") and dst.is_file() and dst.stat().st_size > 0:
            result["ok"] = True
            result["output"] = str(dst)
            result["outputSize"] = dst.stat().st_size
        else:
            result["error"] = "编译未产出（status=%s）" % d.get("status")
    finally:
        if not args.workdir:
            shutil.rmtree(work, ignore_errors=True)
        else:
            result["workdir"] = str(work)

    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    elif not args.quiet:
        if result.get("ok"):
            out("编译成功: %s (%d B)" % (result["output"], result["outputSize"]))
        else:
            out(str(result.get("error")))
    return EXIT_OK if result.get("ok") else EXIT_COMPILE


if __name__ == "__main__":
    sys.exit(main())
