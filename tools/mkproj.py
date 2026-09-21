"""mkproj —— 把纯文本的易语言源码目录补全成一个「双击就能用 e2txt GUI 打开」的完整项目。

e2txt GUI 双击 .etprj 时执行的是 `e2txt-gui.exe -prj "<项目文件>"`，
它会把**项目文件所在目录**当作「TXT转E」的文本目录。所以一个合格的项目目录必须包含：

    <项目目录>/
    ├── 项目.etprj              项目描述（GUI 靠它定位）
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
    python mkproj.py <项目目录> --no-build     # 不调用 e2txt 生成 代码.e

坑（实测）：命令行 `e2txt -mode t2e -src <项目目录>` 会把该目录下的
`项目.etprj` **重写成只剩 4 个 TXT2E-* 键**（E2TXT-* 那批全丢，`Dest` 被写上
绝对路径）。所以 etprj 必须在 `t2e` **跑完之后**再写，且要做「保留已有值 + 补齐
缺失键」的合并，不能无脑覆盖用户改过的字段。

依赖：e2txt.exe 在 PATH 中，或设置环境变量 E2TXT。
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

def find_e2txt():
    """定位 e2txt：环境变量 E2TXT → PATH。**不假定任何安装路径**（技能要给任何机器用）。"""
    env = os.environ.get('E2TXT')
    if env and os.path.exists(env):
        return env
    for name in ('e2txt', 'e2txt.exe', 'e2txt.cmd', 'e2txt.bat'):
        p = shutil.which(name)
        if p:
            return p
    return env or 'e2txt'        # 都没找到就交回系统，让报错信息保持可读


E2TXT = find_e2txt()
BOM = b'\xef\xbb\xbf'

# ---------------------------------------------------------------- 内置示例

SAMPLE_STATIC = """\
.版本 2
.支持库 spec

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
    """GUI 写出来的 etprj 就是这套键值（对齐 `测试.代码/项目.etprj`，GUI 实测产物）。

    只改一处：`Source` —— E转TXT 的源 .e 相对项目目录的路径。参考工程里 .e 在
    上一级（`../测试.e`），我们的 .e 就在项目目录内，所以是 `代码.e`。

    注意别用命令行 t2e 写的那套值：它会把 `Dest` 写成绝对路径、`TXT2E-EFile`
    清空，那是 CLI 自己的口径，跟 GUI 不一致。
    """
    return {
        "AsyncFile": False,
        "Dest": "",
        "E2TXT-EFile": "",
        "E2TXT-InSourceDir": True,
        "E2TXT-IsCreateE": True,
        "Encoding": "UTF-8",
        "Level": 1,
        "NameStyle": 2,
        "Password": "",
        "ResetNames": [],
        "Source": "代码.e",
        "TXT2E-CreateLog": False,
        "TXT2E-EFile": "",
        "TXT2E-InSourceDir": False,
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

    实测（两个可执行文件都验过）：`项目.etprj` 无论 GUI 还是命令行都写成 ANSI(GBK)，
    而 `*.e.txt` / `*.config.json` 才是 UTF-8 **带 BOM**。e2txt 读 etprj 走 ANSI，
    所以写 UTF-8+BOM 会把 `EF BB BF` 塞到 `{` 前面，JSON 解析直接失败 ——
    双击能否打开项目全看这个文件，别搞错。
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
    """写 etprj：保留已有键的值，只补齐缺失的键（GUI 最少认这套 14 键）。

    格式必须与 GUI 一致：**GBK、无 BOM、CRLF**（见 w_gbk 的说明）。
    """
    p = os.path.join(root, '项目.etprj')
    cur = read_etprj(p) if os.path.exists(p) else {}
    merged = dict(etprj_dict())
    merged.update({k: v for k, v in cur.items() if k in merged})
    # CLI 的 t2e 会把 Dest 写成绝对路径（我们没要过），清掉
    if merged.get('Dest') and os.path.isabs(merged['Dest']):
        merged['Dest'] = ''
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


def drop_stray(dst_parent):
    """e2txt 的 t2e 会在**自身目录**里建一个与「-dst 父目录名」同名的空目录。

    实测：`-dst x\\y\\p1.e` 就会在 <e2txt目录>\\ 下多出一个 `y\\`。这里只删空目录。
    """
    stray = os.path.join(os.path.dirname(os.path.abspath(E2TXT)),
                         os.path.basename(os.path.normpath(dst_parent)))
    try:
        if os.path.isdir(stray) and not os.listdir(stray):
            os.rmdir(stray)
            return True
    except OSError:
        pass
    return False


def run_e2txt(args):
    p = subprocess.run([E2TXT] + args, capture_output=True)
    so = p.stdout.decode('gbk', 'replace')
    se = p.stderr.decode('gbk', 'replace')
    errs = [l for l in (so + '\n' + se).splitlines() if l.startswith('[错误]')]
    ok = 'SUCC:' in so
    return ok, errs, so


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('root', help='项目目录')
    ap.add_argument('--keep', action='store_true', help='保留已有 代码/ 内容，只补全配置与 etprj')
    ap.add_argument('--no-build', action='store_true', help='不调用 e2txt 生成 代码.e')
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
    keys = scan_libs(root) or ['krnln']
    if 'krnln' not in keys:
        keys.insert(0, 'krnln')
    libs = [LIB_BASE[k] for k in keys if k in LIB_BASE]
    w('配置/支持库.config.json', json.dumps(libs, ensure_ascii=False, indent=4) + '\n', root)
    w('配置/用户.config.json', json.dumps(USER_CFG, ensure_ascii=False, indent=4) + '\n', root)
    w('配置/系统.config.json', json.dumps(SYS_CFG, ensure_ascii=False, indent=4) + '\n', root)
    print(f'  支持库: {[l["Key"] for l in libs]}')

    if not a.no_build and os.path.exists(E2TXT):
        out_e = os.path.join(root, '代码.e')
        print(f'\n生成 .e -> {out_e}')
        ok, errs, _so = run_e2txt(['-mode', 't2e', '-src', root, '-dst', out_e,
                                   '-enc', 'UTF-8', '-level', '1', '-ns', '2'])
        print(f'  {"成功" if ok else "失败"}，[错误] {len(errs)} 条')
        for e in errs[:8]:
            print('   ', e)
        drop_stray(root)
        if not ok:
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
        print(f'!! 找不到 e2txt: {E2TXT}（可设环境变量 E2TXT）')
        return 2

    n = write_etprj(root)
    print(f'项目.etprj: {n} 个键')
    return 0


if __name__ == '__main__':
    sys.exit(main())
