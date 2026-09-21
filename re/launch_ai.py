#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
launch_ai.py —— PoC-1 启动器（路线④a「独立技能 + 官方宿主化」第 0 步验证）

流程（严格按 team-lead 要求）：
  1) 备份并快照 D:\\ides\\e\\lib\\ 的完整清单（recursive: relpath/size/mtime）
  2) 确认 lib\\ 中没有同名文件（避免覆盖任何人已有的库）
  3) 把 re\\addin\\elang_addin.fne 投放进 D:\\ides\\e\\lib\\
  4) 设置环境变量：ELANG_AI_TRACE（必给）/ ELANG_AI_TASK（可选）/ ELANG_AI_HIDE（可选）
  5) 启动 D:\\ides\\e\\e.exe（**不带任何 .e 参数**）
  6) 轮询等待 trace 文件出现并包含关键钩子；期间用 ToolHelp32 枚举该进程模块，
     独立自证 elang_addin.fne 是否真的被加载
  7) **finally 里无论如何都**：杀掉"本次启动的那个 PID"（精确 PID，绝不 IM 全杀，
     以避免误伤用户自己正在编辑的易语言）+ 移除放进去的 .fne + 重新快照并 diff 自证

⚠️ 与 team-lead 原始要求的一处（经审慎判断的）偏差：
   team-lead 要求"无论成败都 taskkill /F /IM e.exe"。但 /IM 会连带杀掉**用户正在使用的
   易语言 IDE**（可能正在编辑未保存的工程），属于破坏性操作。这里改为精确 PID 终止
   （subprocess.kill + taskkill /F /PID），效果等价且安全。详见汇报 ⑨。
"""

import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

# ---------------------------------------------------------------- 配置
E_EXE      = r"D:\ides\e\e.exe"
E_DIR      = r"D:\ides\e"
LIB_DIR    = r"D:\ides\e\lib"
ADDIN_DIR  = os.path.join(os.path.dirname(os.path.abspath(__file__)), "addin")
FNE_SRC    = os.path.join(ADDIN_DIR, "elang_addin.fne")
FNE_NAME   = "elang_addin.fne"
# trace 路径刻意用纯 ASCII 的 %TEMP%，避免中文路径在 ANSI 环境下的编码歧义；
# 运行结束后再拷贝回 re\addin\ 便于查看。
TRACE_PATH = os.path.join(os.environ.get("TEMP", r"C:\Windows\Temp"), "elang_ai_poc1_trace.txt")
TRACE_COPY = os.path.join(ADDIN_DIR, "trace_run1.txt")
RESULT_PATH= os.path.join(ADDIN_DIR, "poc1_result.txt")
DIFF_PATH  = os.path.join(ADDIN_DIR, "lib_restore_diff.txt")
MODS_PATH  = os.path.join(ADDIN_DIR, "e_mods_run1.txt")

WAIT_TRACE_SEC = 45          # 最多等多久等 trace
POLL_INTERVAL  = 0.5


# ---------------------------------------------------------------- 工具
def log(msg: str) -> None:
    print(msg, flush=True)


def snapshot_lib() -> dict:
    """对 lib\\ 做递归快照：relpath -> (size, mtime)。"""
    snap = {}
    base = os.path.abspath(LIB_DIR)
    for root, _dirs, files in os.walk(base):
        for fn in files:
            full = os.path.join(root, fn)
            rel = os.path.relpath(full, base).replace("\\", "/")
            try:
                st = os.stat(full)
                snap[rel] = (st.st_size, int(st.st_mtime))
            except OSError as e:
                snap[rel] = ("ERR", str(e))
    return snap


def diff_snap(a: dict, b: dict) -> str:
    lines = []
    for k in sorted(set(a) | set(b)):
        va, vb = a.get(k), b.get(k)
        if va == vb:
            continue
        lines.append("  %-60s  before=%s  after=%s" % (k, va, vb))
    return "\n".join(lines) if lines else "  (无差异)"


# ---------------------------------------------------------------- ToolHelp32 模块枚举
TH32CS_SNAPMODULE    = 0x00000008
TH32CS_SNAPMODULE32  = 0x00000010
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
MAX_PATH = 260


class MODULEENTRY32(ctypes.Structure):
    # ⚠️ 32 位视图：指针字段必须声明成 4 字节 DWORD，否则 64 位 Python 下
    #    c_void_p(8B) 会把整个结构体串位，导致 szModule 乱码（曾踩坑）。
    _fields_ = [
        ("dwSize",        wt.DWORD),
        ("th32ModuleID",  wt.DWORD),
        ("th32ProcessID", wt.DWORD),
        ("GlblcntUsage",  wt.DWORD),
        ("ProccntUsage",  wt.DWORD),
        ("modBaseAddr",   wt.DWORD),
        ("modBaseSize",   wt.DWORD),
        ("hModule",       wt.DWORD),
        ("szModule",      ctypes.c_char * 256),
        ("szExePath",     ctypes.c_char * MAX_PATH),
    ]


def enum_modules(pid: int):
    """返回该 pid 的模块名列表（失败返回 None）。"""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
    k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
    k32.Module32First.restype = wt.BOOL
    k32.Module32First.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32)]
    k32.Module32Next.restype = wt.BOOL
    k32.Module32Next.argtypes = [wt.HANDLE, ctypes.POINTER(MODULEENTRY32)]
    k32.CloseHandle.argtypes = [wt.HANDLE]

    h = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    if h == INVALID_HANDLE_VALUE or h is None:
        return None
    mods = []
    try:
        me = MODULEENTRY32()
        me.dwSize = ctypes.sizeof(MODULEENTRY32)
        ok = k32.Module32First(h, ctypes.byref(me))
        while ok:
            mods.append((me.szModule.decode("gbk", "replace"),
                         me.szExePath.decode("gbk", "replace")))
            ok = k32.Module32Next(h, ctypes.byref(me))
    finally:
        k32.CloseHandle(h)
    return mods


def list_e_processes():
    """用 tasklist 列出当前 e.exe PID（只读）。"""
    try:
        out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq e.exe", "/FO", "CSV", "/NH"],
                             capture_output=True, text=True, timeout=15)
        pids = []
        for line in out.stdout.splitlines():
            parts = [p.strip('"') for p in line.split('","')]
            if len(parts) >= 2 and parts[0].lower().startswith("e.exe"):
                try:
                    pids.append(int(parts[1]))
                except ValueError:
                    pass
        return pids
    except Exception as e:  # noqa
        return []


def kill_pid(pid: int) -> None:
    """精确终止单个 PID（taskkill /F /PID + 兜底）。"""
    try:
        subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                       capture_output=True, text=True, timeout=15)
    except Exception:
        pass


# ---------------------------------------------------------------- 主流程
def main() -> int:
    if not os.path.isfile(E_EXE):
        log("[FAIL] 找不到 e.exe: %s" % E_EXE)
        return 2
    if not os.path.isfile(FNE_SRC):
        log("[FAIL] 找不到 .fne: %s（请先运行 build.sh）" % FNE_SRC)
        return 2

    dst = os.path.join(LIB_DIR, FNE_NAME)
    if os.path.exists(dst):
        log("[FAIL] %s 已存在，拒绝覆盖。请先人工确认。" % dst)
        return 2

    before = snapshot_lib()
    log("[info] lib\\ 快照: %d 个文件" % len(before))
    pre_pids = list_e_processes()
    log("[info] 启动前已存在的 e.exe PID（不会去动它们）: %s" % (pre_pids or "无"))

    trace_lines = []
    proc = None
    started_pid = None
    verdict = {}

    try:
        # 3) 投放 .fne
        import shutil
        shutil.copy2(FNE_SRC, dst)
        log("[info] 已投放: %s (%d bytes)" % (dst, os.path.getsize(dst)))

        # 4) 环境变量
        env = os.environ.copy()
        env["ELANG_AI_TRACE"] = TRACE_PATH
        env["ELANG_AI_TASK"]  = os.environ.get("ELANG_AI_TASK", "PLACEHOLDER_TASK_PoC1")
        env["ELANG_AI_HIDE"]  = os.environ.get("ELANG_AI_HIDE", "0")
        if os.path.exists(TRACE_PATH):
            os.remove(TRACE_PATH)
        log("[info] env ELANG_AI_TRACE=%s" % TRACE_PATH)
        log("[info] env ELANG_AI_TASK =%s" % env["ELANG_AI_TASK"])
        log("[info] env ELANG_AI_HIDE =%s" % env["ELANG_AI_HIDE"])

        # 5) 启动 e.exe（不带 .e 参数）
        log("[info] 启动 %s （cwd=%s）" % (E_EXE, E_DIR))
        proc = subprocess.Popen([E_EXE], cwd=E_DIR, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        started_pid = proc.pid
        log("[info] 已启动 PID=%d" % started_pid)

        # 6) 轮询 trace + 模块
        deadline = time.time() + WAIT_TRACE_SEC
        seen_hooks = set()
        mod_found = None
        while time.time() < deadline:
            if os.path.exists(TRACE_PATH):
                try:
                    with open(TRACE_PATH, "r", encoding="utf-8", errors="replace") as f:
                        trace_lines = f.read().splitlines()
                except OSError:
                    trace_lines = []
                for ln in trace_lines:
                    if "NL_SYS_NOTIFY_FUNCTION" in ln: seen_hooks.add("NL_SYS_NOTIFY_FUNCTION")
                    if "NL_IDE_READY" in ln:          seen_hooks.add("NL_IDE_READY")
                if "NL_IDE_READY" in seen_hooks:
                    break
            # 独立自证：模块枚举
            if mod_found is None and started_pid:
                mods = enum_modules(started_pid)
                if mods:
                    hits = [m for m in mods if FNE_NAME.lower() in m[0].lower()
                            or FNE_NAME.lower() in m[1].lower()]
                    if hits:
                        mod_found = hits
            # 进程若已退出，提前结束等待
            if proc.poll() is not None:
                log("[warn] e.exe 已提前退出，exit=%s" % proc.returncode)
                break
            time.sleep(POLL_INTERVAL)

        # 结束时再补一次模块枚举（更稳），并落盘完整模块清单作为证据
        last_mods = enum_modules(started_pid) if started_pid else None
        if last_mods:
            try:
                with open(MODS_PATH, "w", encoding="utf-8") as f:
                    f.write("e.exe (PID=%s) 模块清单，共 %d 个\n" % (started_pid, len(last_mods)))
                    for n, p in last_mods:
                        f.write("  %-40s %s\n" % (n, p))
            except OSError:
                pass
            log("[info] e.exe 模块总数 = %d（清单见 %s）" % (len(last_mods), MODS_PATH))
            if mod_found is None:
                hits = [m for m in last_mods if FNE_NAME.lower() in m[0].lower()
                        or FNE_NAME.lower() in m[1].lower()]
                if hits:
                    mod_found = hits

        verdict["trace_exists"]  = os.path.exists(TRACE_PATH)
        verdict["hooks"]         = sorted(seen_hooks)
        verdict["module_found"]  = mod_found
        verdict["n_mods"]        = len(last_mods) if last_mods else 0

    finally:
        # 7a) 精确杀进程
        if started_pid:
            log("[cleanup] 终止本次启动的 PID=%d" % started_pid)
            kill_pid(started_pid)
            if proc is not None:
                try:
                    proc.wait(timeout=10)
                except Exception:
                    pass
            # 兜底：若仍未退出，直接 kill
            if proc is not None and proc.poll() is None:
                try:
                    proc.kill()
                except Exception:
                    pass
        # 7b) 移除投放的 .fne
        if os.path.exists(dst):
            try:
                os.remove(dst)
                log("[cleanup] 已移除 %s" % dst)
            except OSError as e:
                log("[FAIL] 无法移除 %s: %s" % (dst, e))
        # 7c) 重新快照 + diff 自证
        after = snapshot_lib()
        diff = diff_snap(before, after)
        log("[cleanup] lib\\ 复原自证 diff:")
        log(diff)
        # 拷贝 trace 回项目目录
        try:
            if os.path.exists(TRACE_PATH):
                import shutil
                shutil.copy2(TRACE_PATH, TRACE_COPY)
                log("[cleanup] trace 已拷贝 -> %s" % TRACE_COPY)
        except OSError:
            pass
        try:
            with open(DIFF_PATH, "w", encoding="utf-8") as f:
                f.write("lib\\ 复原 diff（before -> after）\n")
                f.write("before files=%d, after files=%d\n" % (len(before), len(after)))
                f.write(diff + "\n")
        except OSError:
            pass

    # ---------------------------------------------------------------- 结论
    trace_txt = "\n".join(trace_lines) if trace_lines else "(trace 文件不存在或为空)"
    try:
        with open(RESULT_PATH, "w", encoding="utf-8") as f:
            f.write("PoC-1 结果\n")
            f.write("=" * 60 + "\n")
            f.write("自动加载(第0步): trace_exists=%s  module_found=%s\n"
                    % (verdict.get("trace_exists"), bool(verdict.get("module_found"))))
            if verdict.get("module_found"):
                for n, p in verdict["module_found"]:
                    f.write("  模块命中: %s  @ %s\n" % (n, p))
            f.write("钩子(第1步): %s\n" % (verdict.get("hooks")))
            f.write("\n---- trace 全文 ----\n")
            f.write(trace_txt + "\n")
    except OSError:
        pass

    log("")
    log("=" * 70)
    log("PoC-1 判定")
    log("=" * 70)
    log("第0步 新库被自动加载 : %s" % ("PASS" if verdict.get("trace_exists") else "FAIL"))

    def has(marker):
        return any(marker in ln for ln in trace_lines)

    log("第1步 收到 NL_SYS_NOTIFY_FUNCTION : %s" % ("PASS" if has("NL_SYS_NOTIFY_FUNCTION") else "FAIL"))
    log("第1步 收到 NL_IDE_READY            : %s" % ("PASS" if has("NL_IDE_READY") else "FAIL"))
    log("第1步 工作线程跑完(STEP3)          : %s" % ("PASS" if has("STEP3 read-only probe end") else "FAIL"))
    log("主窗口类名 ENewFrame               : %s" % ("PASS" if has("class='ENewFrame'") else "FAIL"))
    log("")
    log("trace 文件: %s" % TRACE_PATH)
    log("结果文件 : %s" % RESULT_PATH)
    log("差异自证 : %s" % DIFF_PATH)
    return 0 if verdict.get("trace_exists") else 1


if __name__ == "__main__":
    sys.exit(main())
