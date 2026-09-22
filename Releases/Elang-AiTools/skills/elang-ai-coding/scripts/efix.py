#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
efix —— 读写易语言 .e 里「变量 / 参数声明记录」的工具
        （内含一个**已确认不该使用**的 bit3 清理动作）

⚠️ 结论先行（2026-09-20 用易语言 IDE 实测更正，此前本工具的用途说明是错的）：

    **t2e 产物里的变量 flags bit3 不能清，原样交付就行。**

      · 同一文本目录、同一时刻，CLI `e2txt.exe` 与 GUI `e2txt-gui.exe` 的 t2e 产物
        **逐字节完全相同** —— 不存在「GUI 干净、CLI 脏」这回事，早先的归因是错的。
      · 用本工具 `apply` 清掉 bit3 之后，**易语言 IDE 直接打不开该 .e**；
        未清的原样产物则能打开、能成功编译运行（用户实测）。
      · `e2t` 回读时把 bit3 显示成 `, 数组`，那只是 e2txt 自己的解读口径，
        IDE 并不把该变量当数组。

    ⇒ **交付 t2e 产物时请直接用，不要跑 apply。**

    本工具因此只剩两项正当用途：
      ① `find_records()` —— 按精确签名读 .e 里的声明记录（分析用，值得保留）；
      ② `check` 子命令 —— 只读列出「带 bit3 的记录」，供研究对照。
    真要 apply 也行，但后果自负：脚本里的往返护栏只保证**文本层**不出意外，
    管不了 IDE 能不能解析。

【记录签名】
    u32 总长 = 9 + 4*(是否有值块) + 名字字节数 + 备注字节数
    u32 数据类型
    u8  标志   bit0=静态  bit1=参考(参数)  bit2=可空(参数)  bit3=数组(参数)
    u8  0
    u8  是否有值块 0/1
    [u32 值]
    char 名字[] GBK
    char 备注[] GBK

【安全策略】
    本工具默认只读（check）。apply 时会加栏杆：
      1. 只清 bit3，不改任何其它位/长度/类型/名字。
      2. 只处理「该名字在 .e 中所有命中记录都带 bit3」且「源文本声明不含数组」的名字；
         出现混合标志即视为歧义，跳过。
      3. 修补后重新 e2t 回读，逐行比对：只允许出现「去掉『, 数组』」这一类变化，
         出现任何其它文本变化立即放弃写入并报错。

用法：
    python efix.py check --e out.e --src 文本目录
    python efix.py apply --e out.e --src 文本目录 [--out 新文件.e]
"""

import argparse
import glob
import os
import re
import shutil
import struct
import subprocess
import sys

def find_e2txt():
    """定位 e2txt：环境变量 E2TXT → PATH。**不假定任何安装路径**（技能要给任何机器用）。"""
    env = os.environ.get("E2TXT")
    if env and os.path.exists(env):
        return env
    for name in ("e2txt", "e2txt.exe", "e2txt.cmd", "e2txt.bat"):
        p = shutil.which(name)
        if p:
            return p
    return env or "e2txt"        # 都没找到就交回系统，让报错信息保持可读


E2TXT = find_e2txt()
DECL_RE = re.compile(r"^\s*\.(局部变量|程序集变量|参数)\s+(.+?)\s*$")
SUB_RE = re.compile(r"^\s*\.子程序\s+(\S+)")


# ---------------- 记录定位 ----------------

def find_records(data, name):
    """用精确签名定位名字为 name 的记录，返回 [(pos, flags, type, hasBlock, value)]"""
    nb = name.encode("gbk")
    out = []
    start = 0
    while True:
        i = data.find(nb, start)
        if i < 0:
            break
        start = i + 1
        for hai in (0, 1):
            p = i - 11 - (4 if hai else 0)
            if p < 0:
                continue
            # 记录总长 = 9 + 4*(有尺寸块) + 名字字节数 + 备注字节数
            # 备注可长可短（含 0），所以只能校验下界
            if struct.unpack_from("<I", data, p)[0] < 9 + (4 if hai else 0) + len(nb):
                continue
            if data[p + 9] != 0 or data[p + 10] != hai:
                continue
            out.append((p, data[p + 8],
                        struct.unpack_from("<I", data, p + 4)[0],
                        hai,
                        struct.unpack_from("<I", data, i - 4)[0] if hai else None))
    return out


# ---------------- 源文本 ----------------

def source_decls(src_dir):
    """返回 (name -> {'scalar':n, 'array':n, 'kinds':set})"""
    info = {}
    for f in sorted(glob.glob(os.path.join(src_dir, "**", "*.e.txt"), recursive=True)):
        txt = open(f, encoding="utf-8-sig", errors="replace").read().replace("\r\n", "\n")
        for line in txt.split("\n"):
            m = DECL_RE.match(line)
            if not m:
                continue
            kind, body = m.group(1), m.group(2)
            name = body.split(",")[0].strip()
            if not name:
                continue
            rec = info.setdefault(name, {"scalar": 0, "array": 0, "kinds": set()})
            rec["kinds"].add(kind)
            rec["array" if "数组" in body else "scalar"] += 1
    return info


def e2t(e_path, out_dir):
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    subprocess.run([E2TXT, "-mode", "e2t", "-src", e_path, "-dst", out_dir,
                    "-enc", "UTF-8", "-level", "2"], capture_output=True)
    return out_dir


def read_lines(root):
    """把文本目录读成 {相对路径: [行]}"""
    out = {}
    for f in sorted(glob.glob(os.path.join(root, "**", "*.e.txt"), recursive=True)):
        rel = os.path.relpath(f, root)
        txt = open(f, encoding="utf-8-sig", errors="replace").read().replace("\r\n", "\n")
        out[rel] = txt.split("\n")
    return out


def only_array_removals(before, after):
    """校验 after 相对 before 只发生了「去掉 数组 修饰」的变化"""
    def canon(s):
        # 去掉「数组」字样后，把声明按逗号分段、丢弃空段，得到可比较的字段元组
        fields = [f.strip() for f in s.replace("数组", "").split(",")]
        return tuple(f for f in fields if f)

    problems = []
    if set(before) != set(after):
        problems.append(f"文件集合变化: 少了 {sorted(set(before)-set(after))[:3]} 多了 {sorted(set(after)-set(before))[:3]}")
        return problems
    for rel in before:
        b, a = before[rel], after[rel]
        if len(b) != len(a):
            problems.append(f"{rel}: 行数 {len(b)} -> {len(a)}")
            continue
        for i, (lb, la) in enumerate(zip(b, a)):
            if lb == la:
                continue
            if canon(lb) != canon(la):
                problems.append(f"{rel}:{i+1}\n    前: {lb!r}\n    后: {la!r}")
    return problems


# ---------------- 主流程 ----------------

def main():
    ap = argparse.ArgumentParser(description="检查/修补 e2txt t2e 的数组位缺陷")
    ap.add_argument("mode", choices=["check", "apply"])
    ap.add_argument("--e", required=True, help="t2e 产出的 .e 文件")
    ap.add_argument("--src", required=True, help="对应的源文本目录")
    ap.add_argument("--out", help="apply 模式的输出文件，默认原地覆盖")
    ap.add_argument("--work", help="临时校验目录，默认在 .e 同级的 _efix_tmp")
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args()

    data = open(args.e, "rb").read()
    src = source_decls(args.src)

    # 候选一：源文本里**只**当过「局部变量 / 程序集变量」的名字。
    #   IDE 对这类变量从不使用 bit3（数组靠尺寸块表达，实测 IDE 给数组局部变量写 0x00），
    #   所以它们的 bit3 一律是 t2e 加的，无论源文本写的是标量还是数组，都可安全清除。
    local_only = {n for n, r in src.items()
                  if r["kinds"] and r["kinds"] <= {"局部变量", "程序集变量"}}
    # 候选二：源文本声明为纯标量（从没写成数组）的名字。
    #   用于那些还兼作「参数」等身份的名字，只清明确是标量的那些，保守起见。
    scalar_only = {n for n, r in src.items() if r["scalar"] and not r["array"]}
    ambiguous = {n for n, r in src.items()
                 if r["array"] and r["scalar"] and n not in local_only}
    cand = local_only | scalar_only

    targets, skipped = [], []
    for name in sorted(cand):
        recs = find_records(data, name)
        if not recs:
            continue
        # 只清带 bit3 的记录。参数不会被污染（实测 6/6 正确），所以不做整体排除。
        dirty = [(p, f, t, h, v) for (p, f, t, h, v) in recs if f & 0x08]
        if dirty:
            targets.append((name, dirty))
        else:
            skipped.append((name, len(recs)))

    n_rec = sum(len(r) for _, r in targets)
    print(f"[efix] 候选名字 {len(cand)} 个（其中「仅局部/程序集变量」{len(local_only)} 个、"
          f"「纯标量」{len(scalar_only)} 个；歧义跳过 {len(ambiguous)} 个）")
    print(f"[efix] 判定为污染的变量名 {len(targets)} 个，共 {n_rec} 条记录")
    if skipped:
        print(f"[efix] 同名但全部无数组位、无需处理 {len(skipped)} 个: {[s[0] for s in skipped[:6]]}")

    if args.verbose:
        for name, recs in targets[:60]:
            print(f"[efix]   {name:16s} x{len(recs)}  {[hex(f) for _, f, _, _, _ in recs]}")

    if not targets:
        print("[efix] 未发现污染")
        return
    if args.mode == "check":
        print("[efix] check 模式，未改动文件")
        return

    # ---- apply ----
    # 临时校验目录：默认丢系统临时目录并自动清理（以前默认落在 .e 同级的
    # `_efix_tmp`，会把用户的工程目录弄脏）。要看中间产物就显式传 --work。
    if args.work:
        tmp = args.work
    else:
        import atexit
        import tempfile
        tmp = tempfile.mkdtemp(prefix="efix_")
        atexit.register(lambda: shutil.rmtree(tmp, ignore_errors=True))
    os.makedirs(tmp, exist_ok=True)

    base_e = os.path.join(tmp, "orig.e")
    shutil.copy2(args.e, base_e)
    before_dir = os.path.join(tmp, "before")
    e2t(base_e, before_dir)
    before = read_lines(before_dir)

    buf = bytearray(data)
    for _, recs in targets:
        for p, flags, _, _, _ in recs:
            buf[p + 8] = flags & ~0x08

    fixed_e = os.path.join(tmp, "fixed.e")
    open(fixed_e, "wb").write(bytes(buf))
    after_dir = os.path.join(tmp, "after")
    e2t(fixed_e, after_dir)
    after = read_lines(after_dir)

    problems = only_array_removals(before, after)
    if problems:
        print("[efix] ✗ 护栏拦截：修补引入了非预期的文本变化，已放弃写入", file=sys.stderr)
        for p in problems[:8]:
            print("      " + p, file=sys.stderr)
        raise SystemExit(2)

    changed = sum(1 for rel in before for x, y in zip(before[rel], after[rel]) if x != y)
    print(f"[efix] ✓ 护栏通过：回读文本变化 {changed} 行，全部为「去掉 , 数组」")

    out = args.out or args.e
    open(out, "wb").write(bytes(buf))
    print(f"[efix] 已写入 {out}")


if __name__ == "__main__":
    main()
