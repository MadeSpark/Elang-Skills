"""mkproj —— 把纯文本的易语言源码目录补全成一个能直接组包、也能用 QAQ-ETool 图形界面打开的完整项目。

图形界面 `qaq-etool-gui.exe <项目目录\\项目.etprj>`（或把 .etprj 拖到 exe 上）会把
**项目文件所在目录**预选为「组包（文本 → .e）」的源目录。一个合格的项目目录包含：

    <项目目录>/
    ├── 项目.etprj              项目描述（中文布局的标志文件；GUI 靠它定位）
    ├── 代码/                   ← 必须是这个目录名（NameStyle=2 中文命名风格）
    │   ├── xxx.static.e.txt    标准程序集
    │   ├── xxx.class.e.txt     类程序集
    │   ├── xxx.form.e.txt      窗口程序集
    │   └── 排序.list.txt        程序集顺序
    ├── 常量.e.txt              常量
    ├── 配置/
    │   ├── 支持库.config.json  用到的支持库（krnln 必写，spec 按需）
    │   ├── 用户.config.json
    │   └── 系统.config.json
    └── 代码.e                  生成好的 .e（t2e 产物）

用法：
    python mkproj.py <项目目录>                # 用内置示例初始化一个项目
    python mkproj.py <项目目录> --keep         # 已有 代码/ 时只补全配置与 etprj
    python mkproj.py <项目目录> --no-build     # 不调用 QAQ-ETool 生成 代码.e

`项目.etprj` 在 t2e **跑完之后**再写，并做「保留已有值 + 补齐缺失键」的合并，
不覆盖用户改过的字段。

依赖：命令行转换工具 QAQ-ETool，由同装的技能 qaq-etool-cli 提供。定位顺序（见 etool.py）：
环境变量 QAQ_ETOOL → 同级技能 qaq-etool-cli/assets → ~/.agents/skills/qaq-etool-cli/assets → PATH。
"""
import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import etool  # noqa: E402

BOM = b'\xef\xbb\xbf'

# ---------------------------------------------------------------- 内置示例

SAMPLE_STATIC = """\
.版本 2

.程序集 程序集1
.程序集变量 调用次数, 整数型

.子程序 _启动子程序, 整数型, , 本子程序在程序启动后最先执行
    .局部变量 计数, 整数型
    .局部变量 结果文本, 文本型
    .局部变量 数字表, 整数型, , "5"
    .局部变量 合计, 整数型

    ' 数组赋值
    数字表 ＝ { 1, 2, 3, 4, 5 }
    
    ' 计次循环：遍历数组
    .计次循环首 (取数组成员数 (数字表), 计数)
        合计 ＝ 合计 ＋ 数字表 [计数]
        结果文本 ＝ 结果文本 ＋ 到文本 (数字表 [计数]) ＋ “ ”
    .计次循环尾 ()
    调试输出 (“数组内容: ” ＋ 结果文本)
    调试输出 (“合计: ” ＋ 到文本 (合计))
    
    ' 条件分支
    .如果 (合计 ＞ 10)
        调试输出 (“合计大于 10”)
    .否则
        调试输出 (“合计不大于 10”)
    .如果结束
    
    ' 调用自定义子程序
    调试输出 (反转文本 (“Hello 易语言”))
    
    返回 (0)
    

.子程序 反转文本, 文本型, 公开, 把文本倒序后返回
    .参数 原文, 文本型, , 待反转的文本
    .局部变量 序号, 整数型
    .局部变量 结果文本, 文本型
    
    调用次数 ＝ 调用次数 ＋ 1
    .变量循环首 (取文本长度 (原文), 1, -1, 序号)
        结果文本 ＝ 结果文本 ＋ 取文本中间 (原文, 序号, 1)
    .变量循环尾 ()
    返回 (结果文本)

"""

SAMPLE_CONST = """\
.版本 2

.常量 应用名称, "“示例程序”"
.常量 最大重试次数, "3"

"""

SAMPLE_ORDER = """\
程序集1
"""

USER_CFG = {
    "Address": "", "Author": "", "BuilderVersion": {"Major": 0, "Minor": 0},
    "CompilePlugins": "", "Copyright": "", "Description": "", "Email": "",
    "ExportPublicClassMethod": False, "FaxNumber": "", "Homepage": "",
    "Name": "", "ReleaseVersion": {"Major": 1, "Minor": 0},
    "TelephoneNumber": "", "WriteVersion": True, "ZipCode": "",
}

SYS_CFG = {
    "Lang": 1, "ProjectType": 0,
    "ProjectVersion": {"Major": 1, "Minor": 7},
    "Type": 1, "Version": {"Major": 5, "Minor": 6},
}

LIB_BASE = {
    "krnln": {
        "CmdCount": 193,
        "Guid": "d09f2340818511d396f6aaf844c7e325",
        "Key": "krnln",
        "MaxRefConstPos": 0,
        "MaxRefObjectPos": 11,
        "Name": "系统核心支持库",
        "Version": {"Major": 5, "Minor": 7},
    },
    "spec": {
        "CmdCount": 5,
        "Guid": "A512548E76954B6E92C21055517615B0",
        "Key": "spec",
        "MaxRefConstPos": 0,
        "MaxRefObjectPos": 0,
        "Name": "特殊功能支持库",
        "Version": {"Major": 3, "Minor": 1},
    },
}


def etprj_dict():
    """项目描述的基本键值（与 QAQ-ETool e2t 写出的 项目.etprj 同一套含义）。

    `Source` = 源 .e 相对项目目录的路径；我们的 .e 就在项目目录内，所以是 `代码.e`。
    QAQ-ETool 组包时只把 项目.etprj 当「中文布局」的标志，不读其中的值，
    这些键是给人看、给别的工具留的元信息。
    """
    return {
        "AsyncFile": False,
        "Encoding": "UTF-8",
        "Level": 1,
        "NameStyle": 2,
        "Password": "",
        "ResetNames": [],
        "Source": "代码.e",
    }


def w(rel, text, root, crlf=True):
    p = os.path.join(root, rel.replace('/', os.sep))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    body = text.replace('\r\n', '\n')
    if crlf:
        body = body.replace('\n', '\r\n')
    data = BOM + body.encode('utf-8')
    with open(p, 'wb') as f:
        f.write(data)
    print(f'  {rel:40s} {len(data):6d}B')


def w_gbk(rel, text, root):
    """写 etprj 专用：**GBK 无 BOM + CRLF**。

    文本工程的约定：`项目.etprj` 写成 ANSI(GBK) 无 BOM，而 `*.e.txt` / `*.config.json`
    是 UTF-8 **带 BOM**（QAQ-ETool e2t 的产物就是这样；读取时两种编码它都接受）。
    按 GBK 读的工具遇到 UTF-8+BOM 会把 `EF BB BF` 塞到 `{` 前面，JSON 解析直接失败。
    """
    p = os.path.join(root, rel.replace('/', os.sep))
    os.makedirs(os.path.dirname(p), exist_ok=True)
    data = text.replace('\r\n', '\n').replace('\n', '\r\n').encode('gbk', 'replace')
    with open(p, 'wb') as f:
        f.write(data)
    print(f'  {rel:40s} {len(data):6d}B  (GBK)')


def read_etprj(p):
    """按 GBK 优先读 etprj（兼容历史上被写成 UTF-8 的文件）。"""
    raw = open(p, 'rb').read()
    for enc in ('gbk', 'utf-8-sig'):
        try:
            return json.loads(raw.decode(enc))
        except Exception:
            continue
    return {}


def write_etprj(root):
    """写 etprj：保留已有的全部键值（含本脚本不认识的），只补齐缺失的基本键。

    格式：**GBK、无 BOM、CRLF**（见 w_gbk 的说明）。
    """
    p = os.path.join(root, '项目.etprj')
    cur = read_etprj(p) if os.path.exists(p) else {}
    merged = dict(etprj_dict())
    merged.update(cur if isinstance(cur, dict) else {})
    merged = dict(sorted(merged.items()))
    w_gbk('项目.etprj', json.dumps(merged, ensure_ascii=False, indent=4) + '\n', root)
    return len(merged)


def scan_libs(src_dir):
    """从 代码/*.e.txt 里收集 `.支持库 xxx` 声明。"""
    keys = []
    code_dir = os.path.join(src_dir, '代码')
    for dp, _dn, fn in os.walk(code_dir):
        for f in fn:
            if not f.endswith('.e.txt'):
                continue
            txt = open(os.path.join(dp, f), 'rb').read().decode('utf-8', 'replace')
            for ln in txt.splitlines():
                s = ln.strip()
                if s.startswith('.支持库'):
                    k = s[len('.支持库'):].strip()
                    if k and k not in keys:
                        keys.append(k)
    return keys


def build_libs(root, sample):
    """库表 = 已有 配置/支持库.config.json（保留）＋ krnln ＋ 源码实际用到的库。

    「用到哪些库」由 elibs 按本机支持库文档扫描得出（命令 / 数据类型 / 常量），
    GUID / 名称 / 版本取自本机 .fne；不再靠 `.支持库 xxx` 文本行（t2e 不认它）。
    兼容旧写法：`.支持库 xxx` 行里的 key 也会加进来。
    没有支持库文档（非 Windows）时退回内置的 krnln / spec 两项。
    """
    p = os.path.join(root, '配置', '支持库.config.json')
    libs = []
    if os.path.isfile(p):
        try:
            libs = json.load(open(p, encoding='utf-8-sig'))
        except ValueError:
            libs = []
    have = lambda k: any(l.get('Key', '').lower() == k.lower() for l in libs)
    if not have('krnln'):
        libs.insert(0, LIB_BASE['krnln'])

    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    idx = None
    try:
        import elibs
        docs = elibs.find_docs(None, root)
        idx = elibs.LibIndex(docs) if docs else None
    except SystemExit as e:
        print(f'  （支持库文档不可用：{e}）')

    want = scan_libs(root)
    if idx:
        _p, _c, _h, missing = elibs.analyze(root, idx)
        auto, ambiguous = elibs.pick_libs(missing)
        want += sorted(auto)
        for name, (cands, where) in sorted(ambiguous.items()):
            print(f'  !! 「{name}」可能属于 {"/".join(sorted(cands))}，请用 elibs.py --add 指定'
                  f'（{where[0][0]}:{where[0][1]}）')
    elif sample:
        want.append('spec')          # 内置示例用了 调试输出
    for k in want:
        if have(k):
            continue
        if idx and k in idx.meta:
            libs.append(idx.entry(k))
        elif k in LIB_BASE:
            libs.append(LIB_BASE[k])
        else:
            print(f'  !! 本机没有支持库「{k}」，未加入库表')
    return libs


def main():
    ap = argparse.ArgumentParser(description='补全易语言文本项目（转换工具：QAQ-ETool）')
    ap.add_argument('root', help='项目目录')
    ap.add_argument('--keep', action='store_true', help='保留已有 代码/ 内容，只补全配置与 etprj')
    ap.add_argument('--no-build', action='store_true',
                    help='不调用 QAQ-ETool 生成 代码.e')
    ap.add_argument('--fix', action='store_true',
                    help='（不推荐）生成 代码.e 后跑 efix 清变量数组位。实测清过的 .e 在 IDE 里打不开，'
                         '只有确认目标环境需要时才加')
    a = ap.parse_args()
    root = os.path.abspath(a.root)

    print(f'项目目录: {root}')
    os.makedirs(root, exist_ok=True)

    code_dir = os.path.join(root, '代码')
    has_code = os.path.isdir(code_dir) and any(
        f.endswith('.e.txt') for _d, _n, fs in os.walk(code_dir) for f in fs)

    if not has_code or not a.keep:
        print('写入示例源码:')
        w('代码/程序集1.static.e.txt', SAMPLE_STATIC, root)
        w('代码/排序.list.txt', SAMPLE_ORDER, root)
        w('常量.e.txt', SAMPLE_CONST, root)
    else:
        print('保留已有 代码/ 内容')

    print('写入配置:')
    libs = build_libs(root, sample=not has_code or not a.keep)
    w('配置/支持库.config.json', json.dumps(libs, ensure_ascii=False, indent=4) + '\n', root)
    w('配置/用户.config.json', json.dumps(USER_CFG, ensure_ascii=False, indent=4) + '\n', root)
    w('配置/系统.config.json', json.dumps(SYS_CFG, ensure_ascii=False, indent=4) + '\n', root)
    print(f'  支持库: {[l["Key"] for l in libs]}')

    tool = None if a.no_build else etool.find()
    if tool:
        out_e = os.path.join(root, '代码.e')
        print(f'\n生成 .e -> {out_e}（QAQ-ETool: {tool}）')
        r = etool.run(tool, 't2e', root, out_e)
        errs = r.fatal + r.errors
        # 交付红线：有 [错误] 或退出码 2（绑定失败的语句被写成「未验证代码」）都算失败，
        # 即使 .e 已经产出 —— 那样的 .e 在 IDE 里跑不对。
        good = r.ok and r.rc == 0 and not r.errors
        print(f'  {"成功" if good else "失败"}，[错误] {len(r.errors)} 条（rc={r.rc}）'
              + (f'（{r.info()}）' if r.info() else ''))
        for e in errs[:8]:
            print('   ', e)
        if not good:
            if r.ok:
                print('  !! .e 虽已产出，但含 [错误] / 未验证代码，不可交付；按上面的行号改源码后重跑')
            return 1

        # 实测结论（用户用易语言 IDE 验证过，2026-09-20）：t2e 产物里变量记录的
        # flags bit3 **不能清** —— 清掉之后 IDE 直接打不开该 .e；原样交付则能打开
        # 并正常编译运行。所以默认原样交付，只有显式 --fix 才动它。
        if a.fix:
            efix = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'efix.py')
            if os.path.exists(efix):
                print('修补数组位（efix）:')
                p = subprocess.run([sys.executable, efix, 'apply',
                                    '--e', out_e, '--src', root],
                                   capture_output=True)
                for ln in p.stdout.decode('utf-8', 'replace').splitlines():
                    print('   ', ln)
                if p.returncode != 0:
                    print('    !! efix 未成功，请手工检查')
                    for ln in p.stderr.decode('utf-8', 'replace').splitlines()[:6]:
                        print('   ', ln)
            else:
                print(f'   （找不到 {efix}，跳过修补）')
    elif a.no_build:
        print('跳过生成 代码.e')
    else:
        print('!! ' + etool.NOT_FOUND)
        return 2

    n = write_etprj(root)
    print(f'项目.etprj: {n} 个键')
    return 0


if __name__ == '__main__':
    sys.exit(main())
