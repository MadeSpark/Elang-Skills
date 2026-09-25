#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""build_mapkit.py —— 编译 mapkit.c 成位置无关 blob（mapkit_text.bin），并**强制过验证门**。

★ 为什么需要这个脚本（2026-09-24 实测结论）

  同一份 mapkit.c，不同优化级别下 GCC 会生成**行为不同**的机器码：
    -O1 / -O3 版本在「调用 MbKit 入口点」时把 `entry` 错用成了 `base`
    ⇒ 跳向 2×base（实测 0xD8580000）⇒ 0xC0000005。
    根因定位：`-O1 -fno-omit-frame-pointer` 即可修复
    ⇒ GCC 的**帧指针省略**（-fomit-frame-pointer）在这段代码上 miscompile。
  也正因如此，出厂那个 1952 B blob 属于「碰巧编错」的一版，从未被验证过。

  所以本脚本不再「手工敲一条 gcc 命令」，而是：
    1) 固定一组**已实测通过**的编译参数；
    2) 编译后立刻用原生探针跑**两条独立路径**
         o    ：直接调用链接进来的 mk_map 符号
         prol ：52 字节前导 + blob 拷到 池+0x800（= 易语言侧的真实调用路径）
    3) 两条都通过，才把 blob **原子替换**到 mapkit_text.bin；
       任何一步失败 → 旧 blob 原样保留，退出码非 0。

用法：
    python build_mapkit.py           构建 + 验证 + 落盘
    python build_mapkit.py --dry     构建 + 验证，但不落盘
"""
import os
import re
import sys
import json
import time
import shutil
import hashlib
import tempfile
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)
sys.stdout.reconfigure(encoding="utf-8")

MINGW = r"C:\msys64\mingw32\bin"
GCC   = os.path.join(MINGW, "gcc.exe")
OBJC  = os.path.join(MINGW, "objcopy.exe")
OBJD  = os.path.join(MINGW, "objdump.exe")

ENV = dict(os.environ)
ENV["PATH"] = MINGW + os.pathsep + ENV.get("PATH", "")

# ★★ 固化编译参数（已由 ubmatrix 实测通过；-fno-omit-frame-pointer 是修掉 miscompile 的关键）
FLAGS = [
    "-m32",
    "-O2",
    "-fno-tree-loop-distribute-patterns",   # 阻止 gcc 把拷贝循环变成 memcpy 调用（会引入外部重定位）
    "-fno-omit-frame-pointer",              # ★ 修掉 entry/base 错用（miscompile 的触发开关）
    "-fno-strict-aliasing",
    "-fno-stack-protector",
]

SRC       = os.path.join(HERE, "mapkit.c")
BLOB      = os.path.join(HERE, "mapkit_text.bin")
PROBE_SRC = os.path.join(HERE, "probe_prol.c")
MBKIT_DLL = os.path.abspath(os.path.join(HERE, "..", "..", "src", "mbkit", "MbKit.dll"))

# ★ 中间产物必须放在**纯 ASCII 路径**：本工程目录含中文（…\测试\…），
#   而 mingw 的汇编器对「含中文的输出绝对路径」会直接 Fatal error: can't create …。
#   输入一律用相对路径（cwd 已是 HERE），输出一律用 ASCII 临时目录。
BUILD     = os.path.join(tempfile.gettempdir(), "mbkit_build")
os.makedirs(BUILD, exist_ok=True)
CAND_OBJ  = os.path.join(BUILD, "cand.o")
CAND_BIN  = os.path.join(BUILD, "cand.bin")
PROBE_OBJ = os.path.join(BUILD, "probe.o")
GATE_EXE  = os.path.join(BUILD, "gate.exe")


def run(cmd, timeout=30):
    try:
        p = subprocess.run(cmd, env=ENV, capture_output=True, timeout=timeout, cwd=HERE)
        return (p.returncode,
                (p.stdout or b"").decode("utf-8", "replace"),
                (p.stderr or b"").decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return 124, "", "(hard timeout)"


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


# ---------------------------------------------------------------- 前导一致性
PROL_RE = re.compile(r"unsigned\s+char\s+PROL\s*\[\s*\]\s*=\s*\{(.*?)\};", re.S)


def extract_prol_from_probe():
    t = open(PROBE_SRC, encoding="utf-8", errors="replace").read()
    m = PROL_RE.search(t)
    if not m:
        return None
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)   # 去注释
    return [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{1,2})", body)]


def expected_prol():
    """从 memgen.mapper_code() 推出探针应有的 PROL：3 字节易语言帧 + 49 字节前导。"""
    sys.path.insert(0, HERE)
    import memgen
    return [0x55, 0x8B, 0xEC] + memgen.mapper_code()


# ---------------------------------------------------------------- 编译
def compile_blob():
    rc, o, e = run([GCC] + FLAGS + ["-c", "mapkit.c", "-o", CAND_OBJ])
    if rc != 0:
        return "编译失败：\n%s\n%s" % (o, e)

    rc, dis, _ = run([OBJD, "-r", CAND_OBJ])
    nrel = dis.count("R_386")
    if nrel:
        return "候选 .o 存在 %d 处重定位，不能整体搬走：\n%s" % (nrel, dis[:800])

    rc, o, e = run([OBJC, "-O", "binary", "-j", ".text", CAND_OBJ, CAND_BIN])
    if rc != 0 or not os.path.exists(CAND_BIN):
        return "objcopy 失败：%s %s" % (o, e)
    return None


def build_gate_exe():
    rc, o, e = run([GCC, "-m32", "-O0", "-c", "probe_prol.c", "-o", PROBE_OBJ])
    if rc != 0:
        return "探针编译失败：\n%s\n%s" % (o, e)
    rc, o, e = run([GCC, "-m32", "-o", GATE_EXE, PROBE_OBJ, CAND_OBJ, "-lkernel32"], timeout=40)
    if rc != 0:
        return "门程序链接失败：\n%s\n%s" % (o, e)
    return None


def run_gate(mode, timeout=15):
    # 注意：blob 用 ASCII 绝对路径；dll 不传，让探针用相对路径
    #       `../../src/mbkit/MbKit.dll`（cwd=HERE）——避免中文路径过 fopen(ANSI)。
    rc, out, err = run([GATE_EXE, mode, os.path.abspath(CAND_BIN)], timeout=timeout)
    if rc == 124:
        return False, "超时（rc=124，blob 跑飞）"
    if "code = 0xC" in out:
        note = ""
        for ln in out.splitlines():
            if "code = 0x" in ln:
                note = ln.strip(); break
        return False, "崩溃：%s" % note
    # ★ 判据只认 [8] RESULT 行里**真实取回**的 base/status，
    #   绝不能 grep 常量 0x6C2C0000 —— 那串曾出现在说明文本里，会假阳性。
    res = [ln.strip() for ln in out.splitlines() if ln.startswith("[8] RESULT")]
    if not res:
        return False, "没有 RESULT 行（rc=%d）\n%s" % (rc, out[-400:])
    if " PASS" not in res[0]:
        return False, res[0]
    return True, res[0]


# ---------------------------------------------------------------- 主流程
def main():
    dry = "--dry" in sys.argv

    print("=" * 76)
    print("build_mapkit.py")
    print("固化参数： gcc %s" % " ".join(FLAGS))
    print("=" * 76)

    if not os.path.isfile(MBKIT_DLL):
        print("[✗] 找不到宿主库：%s" % MBKIT_DLL); return 1

    # 0) 前导一致性（防「前导 与 blob 版本不匹配」这种最隐蔽的坑）
    want = expected_prol()
    got  = extract_prol_from_probe()
    if got is None:
        print("[✗] 无法从 probe_prol.c 提取 PROL[]"); return 1
    if got != want:
        print("[✗] 前导不一致！memgen.mapper_code() 与 probe_prol.c 的 PROL[] 不相等")
        print("    want(%d) = %s" % (len(want), " ".join("%02X" % x for x in want)))
        print("    got (%d) = %s" % (len(got), " ".join("%02X" % x for x in got)))
        return 1
    print("[✓] 前导一致：PROL[] = %d B，与 memgen.mapper_code() 逐字节相同" % len(got))

    # 1) 编译候选 blob
    err = compile_blob()
    if err:
        print("[✗] " + err); return 1
    print("[✓] 候选 blob：%d B，sha1=%s，重定位 0 处" % (os.path.getsize(CAND_BIN), sha1(CAND_BIN)[:12]))

    # 2) 门程序
    err = build_gate_exe()
    if err:
        print("[✗] " + err); return 1

    # 3) 门 A：o 模式（直接调链接进来的 mk_map 符号）
    ok_a, note_a = run_gate("o")
    print("%s 门 A（o 模式，直调符号）：    %s" % ("[✓]" if ok_a else "[✗]", note_a))
    if not ok_a:
        print("    ⇒ 候选不可用；mapkit_text.bin 保持原样不动。"); return 1

    # 4) 门 B：prol 模式（52 B 前导 + blob @ 池+0x800 = 易语言侧真实路径）
    ok_b, note_b = run_gate("prol")
    print("%s 门 B（prol 模式，前导+blob）：%s" % ("[✓]" if ok_b else "[✗]", note_b))
    if not ok_b:
        print("    ⇒ 候选不可用；mapkit_text.bin 保持原样不动。"); return 1

    # 5) 落盘（原子替换）
    old = None
    if os.path.exists(BLOB):
        old = {"size": os.path.getsize(BLOB), "sha1": sha1(BLOB)}
    if dry:
        print("[i] --dry：不落盘。旧 blob = %s" % (old,))
        return 0

    tmp = BLOB + ".new"
    shutil.copy2(CAND_BIN, tmp)
    os.replace(tmp, BLOB)

    meta = {
        "built_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "gcc": (run([GCC, "--version"])[1].splitlines() or [""])[0].strip(),
        "flags": FLAGS,
        "blob_size": os.path.getsize(BLOB),
        "blob_sha1": sha1(BLOB),
        "gate_o": note_a,
        "gate_prol": note_b,
        "prev_blob": old,
        "why": "GCC -fomit-frame-pointer 在 mapkit.c 上 miscompile（entry 被当 base）；"
               "本脚本固化 -fno-omit-frame-pointer 并用双路径原生探针强制验证后才落盘。",
    }
    with open(os.path.join(HERE, "mapkit_build.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, ensure_ascii=False, indent=2)

    print("-" * 76)
    print("[✓] 已落盘：mapkit_text.bin  %d B  sha1=%s"
          % (os.path.getsize(BLOB), sha1(BLOB)[:12]))
    if old:
        print("    旧 blob：%d B  sha1=%s   （已替换；回滚见 re/_rollback/）"
              % (old["size"], old["sha1"][:12]))
    print("[✓] 构建记录：mapkit_build.json")
    return 0


if __name__ == "__main__":
    sys.exit(main())
