#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""elaunch.py —— 启动易语言 IDE、打开「加壳后」的 .e、触发一次“调试运行”，
并**流式**收集调试框日志，最后打出一份机器可读的结果 JSON。

它把 team-lead 定的**产品约束**落进代码：**不用面板尾行判断结果**（尾行分不出
“正常跑完”和“被中断”），而是用——
  * `crashed`  ← 易语言 IDE 子进程**退出码 ≠ 0**（如 `0xC0000005`）
  * `stopped`  ← **我们主动发过** `FN_END_RUN` 且观测到 `FN_END_RUN enabled 1→0`
  * `timeout`  ← 到 `--timeout` 仍未见到结束跃迁
  * `ok`       ← 自然结束（观测到 `1→0` 且不是我们发的）

用法
====
    python elaunch.py <加壳后的_ai.e>
      --log <path>        调试日志落盘路径（默认 <ai.e 同目录>/<名>.debug.log）
      --timeout <秒>      整体超时（默认 30）
      --stop-after <秒>   运行满这么久就主动发 FN_END_RUN（默认：不主动停）
      --json              把结果 JSON 打到 stdout
      （以下为纪律/调试补充项）
      --fne <path>        要投放的支持库 .fne（默认 ../assets/elang_addin.fne）
      --home <dir>        易语言安装目录（默认 $ELANG_HOME → 注册表 → 报错）
      --tag <name>        证据命名前缀（默认 <名>_<时分秒>）
      -q, --quiet

退出码：0=调试会话正常收尾（ok/stopped）；1=crashed/timeout；3=启动器自身失败。

纪律（team-lead 要求，勿退化）
==============================
  * 只用**精确 PID** 结束 e.exe（绝不 `taskkill /F /IM`）
  * `finally` 里清理投放的 `.fne`，并对易语言安装目录做 before/after `diff` 自证
  * 易语言安装目录**默认只读**（只写/删一个 `lib\elang_addin.fne`）
  * 日志里的中文同时给 `.utf8.txt` 视图（面板文本是 GBK）
  * `--tag` 唯一命名证据
  * 启动器自身的 stdout/stderr **tee** 到 `<TAG>_harness.txt`
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_FNE = os.path.normpath(os.path.join(HERE, os.pardir, "assets", "elang_addin.fne"))

REG_PATH = r"Software\FlySky\E\Install"
REG_VALUE = "Path"

# 支持库在 lib\ 下的文件名（= 技能加壳用的 Key）
LIB_FNAME = "elang_addin.fne"


# --------------------------------------------------------------------------- #
# 路径解析（**不出现任何本机绝对路径**）
# --------------------------------------------------------------------------- #
def _install_root(d: str):
    """把候选目录规范化成「含 e.exe 的安装根」。

    易语言注册表 `HKCU\\Software\\FlySky\\E\\Install\\Path` 实测存的是**lib 目录**
    （如 `…\\e\\lib\\`），不是安装根；用户设 `ELANG_HOME` 时也可能指到 lib。
    这里统一兜底：本身含 `e.exe` 就用它；否则若它是 `lib` 且其父目录含 `e.exe`，
    就用父目录。
    """
    d = os.path.abspath(d)
    if os.path.isfile(os.path.join(d, "e.exe")):
        return d
    stripped = d.rstrip("\\/")
    if os.path.basename(stripped).lower() == "lib":
        parent = os.path.dirname(stripped)
        if os.path.isfile(os.path.join(parent, "e.exe")):
            return parent
    return d


def resolve_home(explicit: str = None) -> str:
    """按 ① 参数 → ② 环境变量 ELANG_HOME → ③ 注册表 → ④ 报错 的顺序定位易语言目录。"""
    tried = []

    def consider(cand):
        if not cand:
            return None
        r = _install_root(cand)
        tried.append(r)
        return r if os.path.isfile(os.path.join(r, "e.exe")) else None

    r = consider(explicit)
    if r:
        return r

    r = consider(os.environ.get("ELANG_HOME"))
    if r:
        return r

    if sys.platform == "win32":
        try:
            import winreg  # noqa: WPS433 (仅 Windows 有)
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_PATH) as k:
                val, _ = winreg.QueryValueEx(k, REG_VALUE)
            r = consider(val)
            if r:
                return r
        except Exception:                                          # noqa: BLE001
            pass

    raise SystemExit(
        "找不到易语言安装目录（应含 e.exe 与 lib\\）。请用 --home 或设环境变量 "
        "ELANG_HOME=<易语言安装目录>。已尝试: %s" % (tried or "无"))


def md5f(p: str) -> str:
    h = hashlib.md5()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(65536), b""):
            h.update(c)
    return h.hexdigest()


def toplist(d: str):
    try:
        return set(os.listdir(d))
    except OSError:
        return set()


# --------------------------------------------------------------------------- #
# Tee：把启动器自身的输出同时写进 <TAG>_harness.txt
# --------------------------------------------------------------------------- #
class Tee:
    """进程内所有 print 的去向。

    * **stderr**：给人看的进度/诊断（这样 `--json` 时 **stdout 保持干净**，只放结果 JSON）。
    * **<TAG>_harness.txt**：完整留证（含 e.exe 自己的 stdout/stderr，另开句柄追加）。
    """

    def __init__(self, path: str):
        self.path = path
        self.fh = open(path, "wb")

    def write(self, s):
        try:
            sys.__stderr__.write(s)
            sys.__stderr__.flush()
        except Exception:                                          # noqa: BLE001
            pass
        if self.fh:
            self.fh.write(s.encode("utf-8", errors="replace"))
            self.fh.flush()

    def flush(self):
        try:
            sys.__stderr__.flush()
        except Exception:                                          # noqa: BLE001
            pass
        if self.fh:
            self.fh.flush()

    def close(self):
        if self.fh:
            try:
                self.fh.close()
            finally:
                self.fh = None


def read_text_best(p: str) -> str:
    if not os.path.isfile(p):
        return ""
    return open(p, "rb").read().decode("gbk", errors="replace")


def write_utf8_view(p: str) -> str:
    """把（可能是 GBK 的）文件转出 UTF-8 伴生视图；返回视图路径。"""
    u8 = os.path.splitext(p)[0] + ".utf8.txt"
    open(u8, "wb").write(open(p, "rb").read().decode("gbk", errors="replace").encode("utf-8"))
    return u8


def _os_remove(p: str) -> bool:
    try:
        os.remove(p)
        return True
    except BaseException:                                          # noqa: BLE001
        return False


def _win32_delete(p: str) -> bool:
    """Windows 上直接走 Win32 `DeleteFileW`（当 `os.remove` 因占用/拦截失败时的兜底）。"""
    try:
        import ctypes
        return bool(ctypes.windll.kernel32.DeleteFileW(ctypes.c_wchar_p(os.path.abspath(p))))
    except BaseException:                                          # noqa: BLE001
        return False


def safe_remove(p: str) -> bool:
    """尽力删除一个文件；**绝不抛**。

    收尾清理失败（文件被占用 / 被环境的「批量删除」保护拦下）**不能**把一次成功的
    调试运行变成失败 —— 所以这里吞掉一切异常（含 SystemExit），只回报最终是否已删除。
    先试 `os.remove`，仍在则退到 Win32 `DeleteFileW`。
    """
    if not os.path.exists(p):
        return True
    _os_remove(p)
    if os.path.exists(p) and sys.platform == "win32":
        _win32_delete(p)
    return not os.path.exists(p)


# --------------------------------------------------------------------------- #
# 主流程
# --------------------------------------------------------------------------- #
def elaunch(ai_e: str, log: str = None, timeout: float = 30.0, stop_after: float = None,
            fne: str = None, home: str = None, tag: str = None, quiet: bool = False) -> dict:
    ai_e = os.path.abspath(ai_e)
    if not os.path.isfile(ai_e):
        raise SystemExit("找不到加壳后的 .e: %s" % ai_e)

    e_dir = resolve_home(home)
    e_exe = os.path.join(e_dir, "e.exe")
    if not os.path.isfile(e_exe):
        raise SystemExit("在 %s 下找不到 e.exe" % e_dir)
    lib_dir = os.path.join(e_dir, "lib")
    dst_fne = os.path.join(lib_dir, LIB_FNAME)

    fne = os.path.abspath(fne or DEFAULT_FNE)
    if not os.path.isfile(fne):
        raise SystemExit("找不到支持库 .fne: %s（用 --fne 指定）" % fne)

    if log is None:
        log = os.path.splitext(ai_e)[0] + ".debug.log"
    log = os.path.abspath(log)
    log_dir = os.path.dirname(log)
    os.makedirs(log_dir, exist_ok=True)
    if not tag:
        tag = "%s_%s" % (os.path.splitext(os.path.basename(ai_e))[0], time.strftime("%H%M%S"))
    harness = os.path.join(log_dir, "%s_harness.txt" % tag)
    trace = os.path.join(log_dir, "%s_trace.txt" % tag)

    # 只清**同名 tag** 旧证据，不碰别次运行
    for f in (log, trace, harness):
        if os.path.exists(f):
            safe_remove(f)

    tee = Tee(harness)
    old_out, old_err = sys.stdout, sys.stderr
    sys.stdout = sys.stderr = tee
    pid = None
    proc = None
    ho = None
    before_dir = toplist(e_dir)
    cleanup_warn = []
    start = {"runStart": None, "runEnd": None, "weSent": False}
    e_self_exit = None
    e_exit_after_grace = None
    run_seconds = 0.0
    t0 = time.time()
    try:
        print("== elaunch ==")
        print("   ai.e    = %s" % ai_e)
        print("   ELANG   = %s" % e_dir)
        print("   fne     = %s" % fne)
        print("   log     = %s" % log)
        print("   tag     = %s" % tag)
        print("   timeout = %ss   stop-after = %s" % (timeout, stop_after))

        # 投放支持库（只写这一个文件；结束删掉）
        shutil.copy2(fne, dst_fne)
        print("   已投放 lib\\%s (%d B)" % (LIB_FNAME, os.path.getsize(dst_fne)))

        env = os.environ.copy()
        env.update({
            "ELANG_AI_TRACE": trace,
            "ELANG_AI_TASK": ai_e,
            "ELANG_AI_RUN": "1",
            "ELANG_AI_RUN_DELAY": "2500",
            "ELANG_AI_CAPTURE": log,
            "ELANG_AI_HIDE": "0",
        })
        env["ELANG_AI_STOP"] = str(int(stop_after)) if stop_after else "0"

        # e.exe 的 stdout/stderr 也并进 harness（追加）
        ho = open(harness, "ab")
        proc = subprocess.Popen([e_exe, ai_e], cwd=e_dir, env=env,
                                stdout=ho, stderr=ho)
        pid = proc.pid
        print("   e.exe pid = %d" % pid)

        deadline = t0 + float(timeout)
        while True:
            rc = proc.poll()
            if rc is not None:
                e_self_exit = rc
                break
            tr = read_text_best(trace)
            if start["runStart"] is None and "FN_END_RUN enabled: 0 -> 1" in tr:
                start["runStart"] = time.time()
            if "发送 FN_END_RUN" in tr:
                start["weSent"] = True
            if start["runEnd"] is None and "FN_END_RUN enabled: 1 -> 0" in tr:
                start["runEnd"] = time.time()
            if start["runEnd"] is not None:
                # 给 IDE 一点余量把退出码/尾行刷出来，再判是否崩
                for _ in range(15):
                    if proc.poll() is not None:
                        e_exit_after_grace = proc.returncode
                        break
                    time.sleep(0.05)
                break
            if time.time() > deadline:
                break
            time.sleep(0.2)

        if start["runStart"] and start["runEnd"]:
            run_seconds = round(start["runEnd"] - start["runStart"], 2)
        elif start["runStart"]:
            run_seconds = round(time.time() - start["runStart"], 2)
        print("   观测: runStart=%s runEnd=%s weSent=%s e_self_exit=%s"
              % (bool(start["runStart"]), bool(start["runEnd"]),
                 start["weSent"], e_self_exit))
    finally:
        # 精确 PID 结束；绝不 /IM（收尾失败也不改判结果）
        try:
            if pid:
                subprocess.run(["taskkill", "/F", "/PID", str(pid)], capture_output=True)
                if proc:
                    try:
                        proc.wait(timeout=10)
                    except Exception:                              # noqa: BLE001
                        pass
        except BaseException:                                      # noqa: BLE001
            pass
        # 清理投放的 .fne + diff 自证（尽力而为，失败只记警告）
        if os.path.exists(dst_fne) and not safe_remove(dst_fne):
            cleanup_warn.append("未能移除 lib\\%s" % LIB_FNAME)
        try:
            newf = sorted(toplist(e_dir) - before_dir)
        except BaseException:                                      # noqa: BLE001
            newf = []
        print("   install-dir 新文件: %s" % (newf or "（无）"))
        for n in newf:
            p = os.path.join(e_dir, n)
            if os.path.isfile(p):
                if safe_remove(p):
                    print("   已清理: %s" % n)
                else:
                    print("   无法清理 %s（请手动处理）" % n)
                    cleanup_warn.append("未能清理 %s" % n)
        try:
            ho.close()
        except Exception:                                          # noqa: BLE001
            pass
        sys.stdout, sys.stderr = old_out, old_err
    # ---------- 判定 ----------
    log_txt = read_text_best(log)
    log_bytes = os.path.getsize(log) if os.path.isfile(log) else 0
    panel_seen = "PANEL-DELTA" in log_txt or "PANEL-CHANGED" in log_txt

    # 被调试程序退出码（面板里 `调试程序[pid]退出,ExitCode:N`）
    target_exit = None
    ms = re.findall(r"ExitCode:\s*(-?\d+)", log_txt)
    if ms:
        target_exit = int(ms[-1])

    e_code = e_self_exit if e_self_exit is not None else e_exit_after_grace
    if e_code is None:
        e_exit = 0                      # 我们主动关的，没崩
    else:
        e_exit = e_code & 0xFFFFFFFF

    if e_self_exit is not None and (e_self_exit & 0xFFFFFFFF) != 0:
        status = "crashed"
    elif start["runEnd"] and start["weSent"]:
        status = "stopped"
    elif start["runEnd"]:
        status = "ok"
    else:
        status = "timeout"

    ok = status in ("ok", "stopped")
    log_utf8 = write_utf8_view(log) if os.path.isfile(log) else None
    if os.path.isfile(trace):
        write_utf8_view(trace)

    result = {
        "ok": ok,
        "status": status,
        "targetExitCode": target_exit,
        "eExitCode": e_exit,
        "runSeconds": run_seconds,
        "logFile": log,
        "panelSeen": panel_seen,
        "logBytes": log_bytes,
        "eExe": e_exe,
        "libDeployed": dst_fne,
        "tag": tag,
        "harnessFile": harness,
        "traceFile": trace,
        "logUtf8": log_utf8,
        "stopAfter": stop_after,
        "timeout": timeout,
        "cleanupWarning": cleanup_warn or None,
    }
    # 结果摘要**始终**写进 harness（stderr），这样 `<TAG>_harness.txt` 里也有
    # 「异常 → 退出码」这条物证（即使面板随崩溃一起没了）。stdout 仍只放 JSON。
    print("== 结果 ==")
    print("   status=%s targetExitCode=%s eExitCode=0x%08X runSeconds=%s"
          % (status, target_exit, e_exit, run_seconds))
    print("   panelSeen=%s logBytes=%d cleanupWarning=%s"
          % (panel_seen, log_bytes, cleanup_warn or "无"))
    return result


def main():
    ap = argparse.ArgumentParser(
        description="启动易语言 IDE 调试运行加壳后的 .e，流式收调试日志并输出结果 JSON",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ai_e", help="加壳后的 .e")
    ap.add_argument("--log", help="调试日志落盘路径（默认 <ai.e 同目录>/<名>.debug.log）")
    ap.add_argument("--timeout", type=float, default=30.0, help="整体超时秒数（默认 30）")
    ap.add_argument("--stop-after", type=float, default=None, dest="stop_after",
                    help="运行满这么多秒就主动发 FN_END_RUN（默认不主动停）")
    ap.add_argument("--json", action="store_true", dest="as_json", help="结果 JSON 打到 stdout")
    ap.add_argument("--fne", help="要投放的 .fne（默认 ../assets/elang_addin.fne）")
    ap.add_argument("--home", help="易语言安装目录（默认 $ELANG_HOME → 注册表）")
    ap.add_argument("--tag", help="证据命名前缀（默认 <名>_<时分秒>）")
    ap.add_argument("-q", "--quiet", action="store_true", help="不打印过程")
    a = ap.parse_args()

    try:
        result = elaunch(a.ai_e, a.log, a.timeout, a.stop_after, a.fne, a.home, a.tag,
                         quiet=(a.quiet or a.as_json))
    except SystemExit as e:
        if a.as_json:
            print(json.dumps({"ok": False, "status": "error", "error": str(e)},
                             ensure_ascii=False))
        else:
            print("!! %s" % e, file=sys.stderr)
        return 3
    if a.as_json:
        # JSON 期间 stdout 已恢复
        sys.__stdout__.write(json.dumps(result, ensure_ascii=False) + "\n")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
