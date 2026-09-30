#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""etool.py —— 定位并调用 QAQ-ETool（易语言 .e/.ec ⇄ 文本目录 转换工具）的共享实现。

mkproj / rtcheck / efix、mkcage、ecompile 等脚本（分属本套装的几个技能，各带一份本模块）
共用本模块，所以「去哪找转换工具、怎么判成败」只有一份。

定位顺序（找到即用）
====================
  1. 显式参数（如 ecompile 的 --etool）
  2. 环境变量 QAQ_ETOOL（可指向 QAQ-ETool.exe，也可指向它所在目录）
  3. 同级技能：<本脚本>/../../qaq-etool-cli/assets/QAQ-ETool.exe
     （所有技能装在同一个技能根目录下，这条在任何 AI 工具的技能目录里都成立）
  4. ~/.agents/skills/qaq-etool-cli/assets/QAQ-ETool.exe（安装器总会装这一份）
  5. PATH 中的 QAQ-ETool.exe
显式给出（1、2）却指向不存在的文件时直接报错，不静默换用别的位置。

命令行约定（QAQ-ETool 本体）
============================
  QAQ-ETool -mode e2t -src <文件.e|.ec> -dst <目录> [-ns 2] [-level 2] [-nosidecar]
  QAQ-ETool -mode t2e -src <目录> -dst <文件.e> [-strict] [-nosidecar]
  stdout：[信息] 摘要 + 成功行 `SUCC:<产物路径>`（UTF-8）
  stderr：`[警告] …` / `[错误] …` / 致命错误 `ERROR:<原因>`（UTF-8）
  退出码：0 成功；1 失败（无产物）；2 已产出但有无法解析 / 未验证的内容
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ENV = "QAQ_ETOOL"
EXE = "QAQ-ETool.exe"
SKILL = "qaq-etool-cli"
HERE = Path(__file__).resolve().parent


def _as_exe(p: str) -> Path:
    q = Path(p).expanduser()
    return q / EXE if q.is_dir() else q


def candidates(explicit: str | None = None):
    """按优先级产出 (来源说明, 路径)；只产出**存在**的文件，显式给错直接抛 SystemExit。"""
    for label, val in (("参数", explicit), ("环境变量 " + ENV, os.environ.get(ENV))):
        if val:
            p = _as_exe(val)
            if not p.is_file():
                raise SystemExit("%s 指向的 QAQ-ETool 不存在：%s" % (label, val))
            yield label, p
    for label, p in (
        ("同级技能 " + SKILL, HERE.parent.parent / SKILL / "assets" / EXE),
        ("~/.agents/skills/" + SKILL, Path.home() / ".agents" / "skills" / SKILL / "assets" / EXE),
    ):
        if p.is_file():
            yield label, p
    w = shutil.which(EXE) or shutil.which("QAQ-ETool")
    if w:
        yield "PATH", Path(w)


def find(explicit: str | None = None) -> str | None:
    """返回 QAQ-ETool.exe 的绝对路径；找不到返回 None（显式给错仍抛 SystemExit）。"""
    for _label, p in candidates(explicit):
        return str(p.resolve())
    return None


NOT_FOUND = ("找不到 QAQ-ETool.exe。请同时安装技能 %s（它的 assets/ 里带 QAQ-ETool.exe），"
             "或设环境变量 %s=<QAQ-ETool.exe 或其所在目录>。" % (SKILL, ENV))


def require(explicit: str | None = None) -> str:
    """同 find，但找不到时抛 SystemExit（带安装提示）。"""
    p = find(explicit)
    if not p:
        raise SystemExit(NOT_FOUND)
    return p


def decode(b: bytes) -> str:
    """QAQ-ETool 输出 UTF-8；个别异常（如 .NET 运行时崩溃）可能是系统代码页，按 GBK 兜底。"""
    try:
        return b.decode("utf-8")
    except UnicodeDecodeError:
        return b.decode("gbk", errors="replace")


class Result:
    """一次 QAQ-ETool 调用的结果。"""

    def __init__(self, argv, rc, so, se):
        self.argv, self.rc, self.stdout, self.stderr = argv, rc, so, se
        self.succ = next((ln[5:].strip() for ln in so.splitlines() if ln.startswith("SUCC:")), None)
        self.errors = [ln.rstrip() for ln in se.splitlines() if ln.startswith("[错误]")]
        self.warnings = [ln.rstrip() for ln in se.splitlines() if ln.startswith("[警告]")]
        self.fatal = [ln.rstrip() for ln in se.splitlines() if ln.startswith("ERROR:")]

    @property
    def ok(self) -> bool:
        """已产出：stdout 有 SUCC: 且退出码 ≠ 1（2 也算产出）。"""
        return self.succ is not None and self.rc in (0, 2)

    @property
    def clean(self) -> bool:
        """完全干净：已产出且退出码 0（没有无法解析 / 未验证的内容）。"""
        return self.ok and self.rc == 0

    def info(self) -> str:
        """stdout 里的 [信息] 摘要行（多行合一）。"""
        return "；".join(ln.strip() for ln in self.stdout.splitlines() if ln.startswith("[信息]"))

    def tail(self, n: int = 400) -> str:
        return "rc=%s\nSTDOUT:%s\nSTDERR:%s" % (self.rc, self.stdout[-n:], self.stderr[-n:])

    def warn_partial(self, what: str = "", stream=None) -> None:
        """退出码 2 时往 stderr 打摘要（不进 --json 的 stdout）。"""
        if self.rc != 2 or not self.ok:
            return
        stream = stream or sys.stderr
        lines = self.errors + self.warnings
        print("[警告] QAQ-ETool %s退出码 2：已产出，但有无法解析 / 未验证的内容（%s）"
              % (what + " " if what else "", self.info() or "%d 条提示" % len(lines)), file=stream)
        for ln in lines[:5]:
            print("    " + ln[:200], file=stream)
        if len(lines) > 5:
            print("    …（另有 %d 条）" % (len(lines) - 5), file=stream)


def run(tool: str, mode: str, src, dst, *extra: str, timeout: int = 900) -> Result:
    """调用 QAQ-ETool：run(tool, "e2t", "a.e", "out_dir", "-ns", "2")。"""
    argv = [tool, "-mode", mode, "-src", str(src), "-dst", str(dst), *extra]
    p = subprocess.run(argv, capture_output=True, timeout=timeout)
    return Result(argv, p.returncode, decode(p.stdout), decode(p.stderr))


if __name__ == "__main__":
    # 自检：python etool.py  → 打印找到的 QAQ-ETool 与来源
    try:
        found = list(candidates())
    except SystemExit as e:
        print("!! %s" % e)
        sys.exit(1)
    if not found:
        print("!! " + NOT_FOUND)
        sys.exit(1)
    for label, p in found:
        print("%-28s %s" % (label, p))
