#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""无头编译易语言 .e -> 原生 exe/dll（封装 e-packager semantic 编译，零第三方依赖）。

    python ecompile.py <输入.e> <输出.exe|输出.dll> [--subsystem console|windows|auto]
                       [--target exe|dll] [--packager <e-packager.exe>] [--e-dir <易语言目录>]
                       [--workdir <目录>] [--keep] [--json] [-q]
    python ecompile.py --doctor [--json]

封装了实测验证过的完整环境配方（缺一即失败）：

  1. 解包/编译期支持库解析：.e 旁边必须有 lib\\（脚本自动做目录联接或拷贝 krnln），
     否则命令名退化为 _Lib0CmdXXX，编译报 unknown_call。
  2. --e-dir 指向易语言目录：提供 MFC 静态库（VC98linker）与静态支持库。
  3. --x86-decoder 指向 e-packager 自身路径。
  4. 环境变量 LIB = <易语言目录>\\linker\\VC6linker\\Lib（提供 libcimt.lib 等 VC6 库）。

退出码：0 成功；2 输入/环境错误；3 编译失败（diagnostics 里有原因）；4 自检未通过。
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
SKILL_DIR = Path(__file__).resolve().parent.parent


def out(msg: str) -> None:
    print(msg, file=sys.stderr)


def find_packager(explicit: str | None) -> Path | None:
    cands: list[Path] = []
    if explicit:
        cands.append(Path(explicit))
    env = os.environ.get("E_PACKAGER")
    if env:
        cands.append(Path(env))
    for base in (SKILL_DIR, SKILL_DIR.parent, SKILL_DIR.parent.parent):
        cands.append(base / "e2txt-cli" / "assets" / "e-packager.exe")
    cands.append(Path.home() / ".agents" / "skills" / "e2txt-cli" / "assets" / "e-packager.exe")
    for c in cands:
        if c.is_file():
            return c
    found = shutil.which("e-packager")
    return Path(found) if found else None


def find_elang_dir(explicit: str | None) -> Path | None:
    cands: list[Path] = []
    if explicit:
        cands.append(Path(explicit))
    env = os.environ.get("ELANG_DIR")
    if env:
        cands.append(Path(env))
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            r"Software\FlySky\E\Install") as k:
            val = winreg.QueryValueEx(k, "Path")[0]
            if val:
                p = Path(str(val))
                if p.suffix.lower() == ".exe":
                    cands.append(p)
                else:
                    # 该值可能指向安装目录，也可能指向其 lib 子目录
                    cands.append(p / "e.exe")
                    cands.append(p.parent / "e.exe")
    except OSError:
        pass
    # 文件关联（.e 双击打开用的 IDE）兜底
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


def autolinker_state(elang: Path) -> str:
    fne = elang / "lib" / "AutoLinker.fne"
    if not fne.is_file():
        return "not_installed"
    return "installed"


def make_sandbox(e_file: Path, elang: Path, workdir: Path | None,
                 keep: bool) -> tuple[Path, Path]:
    box = Path(workdir) if workdir else Path(tempfile.mkdtemp(prefix="ecompile_"))
    box.mkdir(parents=True, exist_ok=True)
    dst = box / e_file.name
    shutil.copy2(e_file, dst)
    lib_dst = box / "lib"
    lib_src = elang / "lib"
    if lib_src.is_dir() and not lib_dst.exists():
        try:
            os.umask(0)
            subprocess.run(["cmd", "/c", "mklink", "/J", str(lib_dst), str(lib_src)],
                           capture_output=True, check=True)
        except (OSError, subprocess.CalledProcessError):
            shutil.copy2(lib_src / "krnln.fne", lib_dst)  # 兜底：至少解析核心库
    return box, dst


def run_compile(packager: Path, e_file: Path, out_file: Path, elang: Path,
                subsystem: str | None, box: Path, quiet: bool) -> tuple[int, str]:
    cmd = [str(packager), "compile", str(e_file), str(out_file),
           "--e-dir", str(elang),
           "--x86-decoder", str(packager),
           "--diagnostics", "text"]
    if subsystem and subsystem != "auto":
        cmd += ["--subsystem", subsystem]
    env = dict(os.environ)
    vc6 = elang / "linker" / "VC6linker" / "Lib"
    if vc6.is_dir():
        env["LIB"] = str(vc6) + os.pathsep + env.get("LIB", "")
    proc = subprocess.run(cmd, capture_output=True, cwd=str(box), env=env)
    text = proc.stdout.decode("gbk", "replace") + proc.stderr.decode("gbk", "replace")
    return proc.returncode, text


def main() -> int:
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("src", nargs="?", help="输入 .e（或 .ec）")
    ap.add_argument("dst", nargs="?", help="输出 .exe / .dll")
    ap.add_argument("--subsystem", choices=["auto", "console", "windows"], default="auto")
    ap.add_argument("--packager", help="e-packager.exe 路径（默认自动定位）")
    ap.add_argument("--e-dir", help="易语言安装目录（默认自动定位）")
    ap.add_argument("--workdir", help="沙箱目录（默认临时目录）")
    ap.add_argument("--keep", action="store_true", help="保留沙箱（生成的 .cpp/.obj 在里面）")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("-q", "--quiet", action="store_true")
    ap.add_argument("--doctor", action="store_true", help="环境自检")
    args = ap.parse_args()

    result: dict[str, object] = {"ok": False}

    if args.doctor:
        pkg = find_packager(args.packager)
        elang = find_elang_dir(args.e_dir)
        checks = {
            "e_packager": str(pkg) if pkg else None,
            "packager_version": None,
            "elang_dir": str(elang) if elang else None,
            "autolinker": autolinker_state(elang) if elang else "unknown",
        }
        if pkg:
            r = subprocess.run([str(pkg), "version"], capture_output=True)
            checks["packager_version"] = (r.stdout + r.stderr).decode("gbk", "replace").strip()[:40]
        result.update(checks)
        result["ok"] = bool(pkg and elang)
        result["hint"] = ("注意：AutoLinker 无头编译另受 IDE 版本闸门限制（旧构建会拒载，"
                          "见 references/AutoLinker与无头编译.md §1）；"
                          "本脚本走 e-packager semantic 编译，不受该闸门影响。")
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        else:
            for k, v in checks.items():
                out(f"  {k:18s} = {v}")
            out("  自检: " + ("通过" if result["ok"] else "未通过"))
        return EXIT_OK if result["ok"] else EXIT_DOCTOR

    if not args.src or not args.dst:
        out("用法: ecompile.py <输入.e> <输出.exe|dll> [--subsystem ...] | --doctor")
        return EXIT_ENV

    src = Path(args.src).resolve()
    if not src.is_file():
        result["error"] = f"输入不存在: {src}"
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(result["error"])
        return EXIT_ENV

    pkg = find_packager(args.packager)
    elang = find_elang_dir(args.e_dir)
    if not pkg:
        result["error"] = ("找不到 e-packager.exe（找过 --packager、E_PACKAGER、"
                           "e2txt-cli 技能 assets、PATH）")
    elif not elang:
        result["error"] = "找不到易语言目录（找过 --e-dir、ELANG_DIR、注册表、常见路径）"
    if "error" in result:
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(str(result["error"]))
        return EXIT_ENV

    dst = Path(args.dst).resolve()
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()  # 旧产物必须清掉，否则会误判成功

    box, box_e = make_sandbox(src, elang,
                              Path(args.workdir).resolve() if args.workdir else None,
                              args.keep)
    code, text = run_compile(pkg, box_e, dst, elang, args.subsystem, box, args.quiet)

    m = re.search(r"^compile: compiled:(.+?);compile_mode=(\w+)", text, re.M)
    result["compile_mode"] = m.group(2) if m else None
    result["sandbox"] = str(box) if args.keep else None
    if code == 0 and m and dst.is_file() and dst.stat().st_size > 0:
        result["ok"] = True
        result["output"] = str(dst)
        result["outputSize"] = dst.stat().st_size
        result["generatedCpp"] = str(box / (dst.stem + ".generated.cpp"))
        if not args.keep:
            for junk in box.glob(dst.stem + ".*"):
                try:
                    junk.unlink()
                except OSError:
                    pass
            if not args.workdir:
                shutil.rmtree(box, ignore_errors=True)
    else:
        result["error"] = "编译失败"
        result["exitCode"] = code
        result["diagnostics"] = text[-4000:]
        if not args.keep and not args.workdir:
            shutil.rmtree(box, ignore_errors=True)
        if args.json:
            print(json.dumps(result, ensure_ascii=False))
        out(text.strip()[-2000:] or "(无输出)")
        return EXIT_COMPILE
    if not args.keep and args.workdir:
        pass  # 用户指定了 workdir：保留现场
    if args.json:
        print(json.dumps(result, ensure_ascii=False))
    elif not args.quiet:
        out(f"编译成功: {dst} ({result['outputSize']} B, mode={result['compile_mode']})")
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
