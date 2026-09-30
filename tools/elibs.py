#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
elibs —— 支持库依赖检查 / 补齐（不依赖易语言 IDE）

问题：文本源码里用了 `调试输出 (…)`（特殊功能支持库 spec 的命令），但工程的
`配置/支持库.config.json` 里没有 spec，t2e 就报「未定义的子程序或命令 调试输出」，
IDE 编译报「找不到命令」。`.支持库 spec` 这种文本行对 t2e 不生效，库表只认 json。

本脚本用「导出支持库文档.exe」的产物（commands.jsonl + 库/<key>.md）：
  1. 查某个命令 / 数据类型 / 常量属于哪个支持库（--which）；
  2. 扫描文本目录，找出「用到了、但所属库没进库表」的名字；
  3. --fix 把缺的库按本机 .fne 的真实 GUID / 名称 / 版本追加进库表。

文档自动定位：--docs → 环境变量 ELANG_LIBDOCS → ./支持库文档 → <文本目录上级>/支持库文档
→ 缓存 %LOCALAPPDATA%/elang-ai-coding/支持库文档；都没有就调用技能自带的
assets/导出支持库文档.exe 生成到缓存（新装了支持库后加 --refresh 重新导出）。

用法：
    python elibs.py --which 调试输出 [快速文本对象 …]
    python elibs.py <文本目录>                 # 只报告，缺库时退出码 1
    python elibs.py <文本目录> --fix           # 补齐唯一可判定的库
    python elibs.py <文本目录> --add spec      # 手动指定要加的库（多候选时用）
"""

import argparse
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
EXPORTER = os.path.join(os.path.dirname(HERE), "assets", "导出支持库文档.exe")
CACHE = os.path.join(os.environ.get("LOCALAPPDATA") or os.path.expanduser("~"),
                     "elang-ai-coding", "支持库文档")
LIBCFG_NAMES = (os.path.join("配置", "支持库.config.json"), os.path.join("config", "lib.config.json"))

# 声明里「类型」位置可能出现的基本类型，不属于任何支持库
BASIC_TYPES = {"字节型", "短整数型", "整数型", "长整数型", "小数型", "双精度小数型", "逻辑型",
               "日期时间型", "文本型", "字节集", "子程序指针", "通用型", ""}


# ---------------------------------------------------------------- 文档
def _is_docs(d):
    return bool(d) and os.path.isfile(os.path.join(d, "commands.jsonl"))


def export_docs(out_dir):
    """调用技能自带的 32 位导出器，把本机支持库文档写到 out_dir。"""
    if not os.path.isfile(EXPORTER):
        raise SystemExit(f"找不到导出器：{EXPORTER}")
    os.makedirs(out_dir, exist_ok=True)
    p = subprocess.run([EXPORTER, "-q", "-o", out_dir], capture_output=True, timeout=300)
    if not _is_docs(out_dir):
        raise SystemExit(f"导出支持库文档失败（退出码 {p.returncode}）：{out_dir}")
    return out_dir


def find_docs(explicit=None, src=None, refresh=False):
    """按优先级定位支持库文档目录；找不到就自动导出到缓存。"""
    if explicit:
        d = os.path.dirname(explicit) if explicit.endswith(".jsonl") else explicit
        if not _is_docs(d):
            raise SystemExit(f"--docs 指向的目录里没有 commands.jsonl：{d}")
        return d
    if refresh:
        return export_docs(CACHE)
    cands = [os.environ.get("ELANG_LIBDOCS"), os.path.join(os.getcwd(), "支持库文档")]
    if src:
        cands.append(os.path.join(os.path.dirname(os.path.abspath(src)), "支持库文档"))
    cands.append(CACHE)
    for d in cands:
        if _is_docs(d):
            return d
    if os.name != "nt":
        return None
    return export_docs(CACHE)


class LibIndex:
    """commands.jsonl + 库/*.md → 名字到库的索引与库元数据。"""

    def __init__(self, docs):
        self.docs = docs
        self.cmds = {}      # 命令名 → [(参数flags, 库key)]
        self.types = {}     # 数据类型名 → {库key}
        self.consts = {}    # 常量名 → {库key}
        self.meta = {}      # 库key → {Key, Name, Guid, Major, Minor}
        with open(os.path.join(docs, "commands.jsonl"), "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    d = json.loads(line)
                except ValueError:
                    continue
                flags = [a.get("flags", "") for a in d.get("args", [])]
                self.cmds.setdefault(d.get("name", ""), []).append((flags, d.get("lib", "")))
        for md in glob.glob(os.path.join(docs, "库", "*.md")):
            self._load_md(md)

    def _load_md(self, path):
        with open(path, "r", encoding="utf-8-sig") as f:
            lines = f.read().splitlines()
        info, section = {"Name": ""}, None
        for ln in lines:
            if ln.startswith("# ") and not info["Name"]:
                info["Name"] = ln[2:].strip()
                continue
            m = re.match(r"^- \*\*(库标识|版本|GUID)\*\*:\s*(.*)$", ln)
            if m:
                v = m.group(2).strip().strip("`")
                if m.group(1) == "库标识":
                    info["Key"] = v
                elif m.group(1) == "GUID":
                    info["Guid"] = v
                else:
                    mm = re.match(r"(\d+)\.(\d+)", v)
                    if mm:
                        info["Major"], info["Minor"] = int(mm.group(1)), int(mm.group(2))
                continue
            if ln.startswith("## "):
                section = ln[3:].strip()
                continue
            if section in ("数据类型", "常量"):
                m = re.match(r"^\|\s*`([^`]+)`", ln)
                if m and info.get("Key"):
                    tgt = self.types if section == "数据类型" else self.consts
                    tgt.setdefault(m.group(1), set()).add(info["Key"])
        if info.get("Key") and info.get("Guid") and "Major" in info:
            self.meta[info["Key"]] = info

    def libs_of(self, name):
        """名字可能属于的库（命令 ∪ 数据类型 ∪ 常量）。"""
        s = {lib for _f, lib in self.cmds.get(name, [])}
        s |= self.types.get(name, set())
        s |= self.consts.get(name, set())
        return s

    def entry(self, key):
        """生成一条库表项。CmdCount / MaxRef* 是「最少需要」的计数，
        IDE 保存时会按实际引用重算；新加的库写 0 永远满足（IDE 自己也会写 0）。"""
        m = self.meta[key]
        return {"CmdCount": 0, "Guid": m["Guid"], "Key": key, "MaxRefConstPos": 0,
                "MaxRefObjectPos": 0, "Name": m["Name"],
                "Version": {"Major": m["Major"], "Minor": m["Minor"]}}


# ---------------------------------------------------------------- 工程库表
def find_libcfg(src):
    """文本目录 → (库表路径, 库表列表)。找不到返回 (None, None)。"""
    root = os.path.abspath(src)
    if os.path.isfile(root):
        root = os.path.dirname(root)
    for _ in range(4):          # 允许传 代码/ 子目录或单个文件
        for rel in LIBCFG_NAMES:
            p = os.path.join(root, rel)
            if os.path.isfile(p):
                with open(p, "r", encoding="utf-8-sig") as f:
                    return p, json.load(f)
        parent = os.path.dirname(root)
        if parent == root:
            break
        root = parent
    return None, None


def write_libcfg(path, libs):
    """与 t2e/e2t 同格式：UTF-8 带 BOM、CRLF、4 空格缩进。"""
    body = json.dumps(libs, ensure_ascii=False, indent=4).replace("\n", "\r\n") + "\r\n"
    with open(path, "wb") as f:
        f.write(b"\xef\xbb\xbf" + body.encode("utf-8"))


# ---------------------------------------------------------------- 扫描源码
DECL_TYPE_RE = re.compile(r"^\s*\.(局部变量|程序集变量|全局变量|参数|成员|子程序|DLL命令)\s+[^,，]*[,，]\s*([^,，\s]*)")
USER_TYPE_RE = re.compile(r"^\s*\.(数据类型|程序集)\s+([^\s,，]+)")
USER_CONST_RE = re.compile(r"^\s*\.常量\s+([^\s,，]+)")
CONST_REF_RE = re.compile(r"#([\u4e00-\u9fffA-Za-z_][\u4e00-\u9fffA-Za-z0-9_]*)")


def text_files(src):
    src = os.path.abspath(src)
    if os.path.isfile(src):
        return [src]
    return sorted(glob.glob(os.path.join(src, "**", "*.e.txt"), recursive=True))


def scan_usage(files, base=None):
    """返回 用到的名字 → [(文件, 行号, 种类)]（已排除本工程自定义的子程序/DLL/类型/常量）。"""
    import echeck  # 复用 echeck 的调用解析（跳过字符串、处理嵌套）
    texts, defined = {}, set()
    for f in files:
        with open(f, "r", encoding="utf-8-sig") as fh:
            texts[f] = fh.read().replace("\r\n", "\n").split("\n")
    subs = {}
    for lines in texts.values():
        echeck.collect_subs(lines, subs)
        echeck.collect_dllcmds(lines, subs)
        for raw in lines:
            m = USER_TYPE_RE.match(raw) or USER_CONST_RE.match(raw)
            if m:
                defined.add(m.group(m.lastindex))
    defined |= set(subs)

    used = {}

    def hit(name, f, ln, kind):
        if name and name not in defined:
            used.setdefault(name, []).append((os.path.relpath(f, base) if base else f, ln, kind))

    for f, lines in texts.items():
        for ln, raw in enumerate(lines, 1):
            line = echeck.strip_comment(raw)
            m = DECL_TYPE_RE.match(line)
            if m:
                t = m.group(2).strip()
                if t not in BASIC_TYPES:
                    hit(t, f, ln, "类型")
                continue
            if not line.strip() or echeck.DECL_RE.match(line):
                continue
            masked = echeck.mask_quoted(line)
            for c in CONST_REF_RE.findall(masked):
                hit(c, f, ln, "常量")
            # 流程行（`.如果 (…)`、`.计次循环首 (…)`）里的调用照常扫；
            # 关键字本身带前导「.」，会被下面的 "." 过滤掉
            work = [line]
            while work:
                for name, args in echeck.parse_calls(work.pop()):
                    if "." not in name:
                        hit(name, f, ln, "命令")
                    work.extend(a for a in args if "(" in a)
    return used


def analyze(src, idx):
    """→ (库表路径, 已引用 key 集合, 缺失 {名字: (候选库集合, 出处列表)})"""
    cfg_path, cfg = find_libcfg(src)
    have = {l.get("Key", "") for l in (cfg or [])}
    have_l = {k.lower() for k in have}
    missing = {}
    base = src if os.path.isdir(src) else os.path.dirname(src)
    for name, where in scan_usage(text_files(src), base).items():
        libs = idx.libs_of(name)
        if not libs:
            continue            # 不是支持库里的名字（子程序/模块/写错名，交给 echeck）
        if any(l.lower() in have_l for l in libs):
            continue
        missing[name] = (libs, where)
    return cfg_path, cfg, have, missing


def pick_libs(missing):
    """每个缺失名字只有一个候选库时自动选；多候选的留给用户 --add。"""
    auto, ambiguous = set(), {}
    for name, (libs, _w) in missing.items():
        if len(libs) == 1:
            auto |= libs
    for name, (libs, w) in missing.items():
        if len(libs) > 1 and not (libs & auto):
            ambiguous[name] = (libs, w)
    return auto, ambiguous


# ---------------------------------------------------------------- CLI
def main():
    ap = argparse.ArgumentParser(description="易语言支持库依赖检查 / 补齐")
    ap.add_argument("src", nargs="?", help="文本目录（或单个 .e.txt）")
    ap.add_argument("--which", nargs="+", metavar="名字", help="查命令/数据类型/常量属于哪个支持库")
    ap.add_argument("--fix", action="store_true", help="把能唯一判定的缺失库追加进库表")
    ap.add_argument("--add", nargs="+", metavar="库key", default=[], help="手动追加指定库")
    ap.add_argument("--docs", help="支持库文档目录（含 commands.jsonl），缺省自动定位/导出")
    ap.add_argument("--refresh", action="store_true", help="重新导出本机支持库文档到缓存")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    if not a.src and not a.which:
        ap.error("需要 <文本目录> 或 --which")

    docs = find_docs(a.docs, a.src, a.refresh)
    if not docs:
        raise SystemExit("没有支持库文档（非 Windows 无法自动导出），用 --docs 指定")
    idx = LibIndex(docs)

    if a.which:
        res = {n: sorted(idx.libs_of(n.lstrip("#"))) for n in a.which}
        if a.json:
            print(json.dumps(res, ensure_ascii=False, indent=2))
        else:
            for n, libs in res.items():
                desc = "、".join(f"{k}（{idx.meta.get(k, {}).get('Name', '?')}）" for k in libs)
                print(f"{n}: {desc or '不在本机任何支持库里（可能是易模块/子程序，或名字写错）'}")
        if not a.src:
            return 0

    cfg_path, cfg, have, missing = analyze(a.src, idx)
    if cfg_path is None:
        raise SystemExit("没找到 配置/支持库.config.json 或 config/lib.config.json")
    auto, ambiguous = pick_libs(missing)

    added = []
    if a.fix or a.add:
        want = list(a.add) + (sorted(auto) if a.fix else [])
        for key in want:
            real = next((k for k in idx.meta if k.lower() == key.lower()), None)
            if real is None:
                raise SystemExit(f"本机没有支持库「{key}」（见 {docs}/索引.md）")
            if real.lower() in {k.lower() for k in have} or real in added:
                continue
            cfg.append(idx.entry(real))
            added.append(real)
        if added:
            write_libcfg(cfg_path, cfg)
        cfg_path, cfg, have, missing = analyze(a.src, idx)
        auto, ambiguous = pick_libs(missing)

    if a.json:
        print(json.dumps({
            "libcfg": cfg_path, "have": sorted(have), "added": added,
            "missing": {n: {"libs": sorted(l), "at": [f"{f}:{ln}" for f, ln, _k in w[:5]]}
                        for n, (l, w) in missing.items()},
            "docs": docs}, ensure_ascii=False, indent=2))
    else:
        print(f"库表: {cfg_path}（已引用 {len(have)} 个：{', '.join(sorted(have))}）")
        for k in added:
            print(f"  + 已追加 {k}（{idx.meta[k]['Name']} {idx.meta[k]['Major']}.{idx.meta[k]['Minor']}）")
        for name, (libs, where) in sorted(missing.items()):
            f, ln, kind = where[0]
            more = f" 等 {len(where)} 处" if len(where) > 1 else ""
            cand = "、".join(f"{k}（{idx.meta.get(k, {}).get('Name', '?')}）" for k in sorted(libs))
            print(f"  ✗ {kind}「{name}」属于 {cand}，库表未引用 —— {f}:{ln}{more}")
        if missing:
            hint = "--fix" if auto else "--add <库key>"
            if ambiguous:
                hint = ("--fix 后再 " if auto else "") + "--add <库key>（多候选的名字要自己挑）"
            print(f"结论: ✗ 缺 {len(missing)} 个名字的支持库，运行 elibs.py <文本目录> {hint}")
        else:
            print("结论: ✓ 用到的支持库都已在库表里")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
