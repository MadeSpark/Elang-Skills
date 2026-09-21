# e-packager：解包 / 回包 / 接口导出 / 一致性验证 / 语义编译

> e-packager 是独立第三方工具（aiqinxuancai 出品，MIT），把二进制 `.e/.ec` 转成可读文本工作区、改完回包，并附带**接口导出、一致性验证、语义编译**等本技能 e2txt 之外的能力。本套装已在 `assets/e-packager.exe` 内置 Win32 版（v1.2.13），开箱即用。
> 也可自行取新版：<https://github.com/aiqinxuancai/e-packager/releases>（下载 `e-packager-<版本>-windows-win32.zip`；**别下** Source code 自动包）。

## 0. 何时用 e-packager，何时用 e2txt

| 需求 | 用谁 |
|---|---|
| `.e` ⇄ 文本目录常规互转（本套装其他技能的主链路） | **e2txt** |
| 支持库命令名直接解析进文本（不依赖外部库文档） | **e-packager**（要求能定位易语言目录，见 §3） |
| 带打开密码的工程解包/回包 | **e-packager**（`--password`） |
| 引用模块/支持库的公开接口批量导出（`ecom/`、`elib/`、`header/`） | **e-packager** |
| 向工程加图片/音频/任意二进制资源 | **e-packager**（`update --add-image/--add-audio`） |
| 验证「零修改往返」字节一致 | **e-packager**（`verify-roundtrip`） |
| 从 `.fne` 直接导出公开接口文本 | **e-packager**（`decrypt-fne`；Win32 版专属） |
| **把 `.e` 直接编译成原生 exe（无 IDE / 无 AutoLinker）** | **e-packager**（`compile`，semantic 模式） |

两者**写出的文本目录格式互不兼容**（目录名、配置形状都不同），不要混着操作同一个目录。

## 1. 命令总览

```text
e-packager unpack <in.e|in.ec> <out-dir> [--password 密码] [--main-only]
e-packager pack   <in-dir> <out.e|out.ec> [--password 密码]
e-packager update <in-dir> [--add-ecom 文件.ec]... [--add-elib 库名|文件.fne]...
                          [--add-image 文件|名称=文件]... [--add-audio ...]...
e-packager compare-bundle <in.e> <in-dir>
e-packager roundtrip <in.e> <work-dir> <out.e>
e-packager verify-roundtrip <in.e> <work-dir> <out.e>
e-packager decrypt-fne <in.fne> [out.txt]        # 导出支持库公开接口（Win32 版）
e-packager validate <in-dir> [--diagnostics text|json]
e-packager compile  <in.e|in-dir> <out.exe|out.dll> [选项]   # 语义编译，见 §5
e-packager compile-check <in.e> [--eide e.exe] ...          # 走 AutoLinker 无头编译（见 elang-build）
e-packager version
```

工作区结构：`src/`（源码 `.txt` + 窗口 `.xml` 只读）、`project/`（回包元数据，**别手改**）、`ecom/`、`elib/`、`header/`（依赖公开接口，派生只读）、`image/`+`audio/`（资源索引 `list.json`）、`info.json`、`AGENTS.md`（动手前先读）。`--main-only` 不生成 `ecom/elib/`——需要依赖上下文的任务别用。

## 2. 解包 / 回包 / 密码

```bash
"<ASSETS>/e-packager.exe" unpack 原工程.e 工作区\
"<ASSETS>/e-packager.exe" pack   工作区\ 新工程.e
```

- 密码工程：解包与回包都传 `--password`；回包默认**不保留**密码，需要继续加密就显式再传。别把密码写进日志/提交。
- `.ec` 回包产物实际是 `.e` 容器格式，别只看扩展名。
- 回包写到**新文件**，不覆盖原件。
- 改完先 `validate <工作区>`（声明/基础语法/可确定类型错误的静态检查），再回包。

## 3. ⚠️ 支持库定位（实测最大的坑）

e-packager 要解析工程引用的支持库命令名（krnln 等）。找不到库时**不报错**，但解包文本里命令名会**退化成 `_Lib0CmdXXX`**，后续 AI 解读与 `compile` 都会失败（`unknown_call:_Lib0CmdXXX`）。

它按「**易语言安装目录（及其上级）的 `lib\`**」或「注册表 `E.Document\Shell\Open\Command` 文件关联」找库。实测有效的做法（按优先级）：

1. **让 `.e` 旁边就有 `lib\`**：把工程要用的 `*.fne` 放到 `.e` 同目录的 `lib\` 子目录（至少 `krnln.fne`）。这是对自动化最稳的布局，与 `compile` 联用时必做。
2. 若本机文件关联齐全，自动定位即可用。
3. `compile`/`compile-check` 子命令支持 `--e-dir <易语言目录>` 显式指定。

**验收**：解包后 `grep -c "_Lib0Cmd"` 为 0 才算解析成功；大于 0 就按上面三步修环境再重解。

## 4. 接口导出与一致性验证（实测通过）

```bash
# 从 .fne 直接导出公开接口（单文件文本，含每个命令的参数/备注）
e-packager decrypt-fne krnln.fne krnln.txt
# 实测核心库：688 条命令 / 51 个数据类型 / 335 个常量，中文备注完整

# 零修改往返字节一致性（引入新工具/怀疑格式兼容性时跑）
e-packager verify-roundtrip 原工程.e 工作目录 输出.e
# 实测：小工程 14 个成分逐一比对通过
```

`decrypt-fne` 导出的文本可当**支持库命令查询**用（等价于「导出支持库文档」的产物，但不用装任何开发工具就能从任意 `.fne` 现场提取）。

## 5. 语义编译：无 IDE 直接出原生 exe（实测通过）

```bash
e-packager compile <in.e> <out.exe> \
    --e-dir "<易语言目录>" \
    --x86-decoder "<e-packager.exe 自身路径>" \
    [--subsystem console|windows] [--diagnostics text|json]
```

e-packager 自带**语义编译后端**（把易语言源码翻译成 C++ → 调 MSVC 编译 → 链接易语言静态库），不依赖 IDE、不依赖 AutoLinker。实测（x86、静态链接 krnln）：

- ✅ 普通工程（控制流/子程序/文件读写）编译出原生 exe 并正确运行；
- ✅ `置入代码 ({ 144, 144 })` / `({ 204 })` 嵌入的机器码字节**真实执行**（NOP 正常、INT3 触发断点异常）；
- ⚠️ **实测缺陷 1**：`标准输出` 在语义编译产物里会崩溃（写文件等命令正常）→ 控制台验证改用「写结果文件 → 读回」，见《程序入口与编译验证》§2；
- ⚠️ **实测缺陷 2**：支持库没定位到时（见 §3），编译在 `unknown_call:_Lib0CmdXXX` 失败——先把 lib 布局修好；
- ⚠️ **实测缺陷 3**：链接阶段要找 VC6 时代的 MFC/运行库（`NAFXCW.LIB`、`libcimt.lib`）。前者在易语言目录 `VC98linker\MFC\Lib`（传 `--e-dir` 自动用）；后者在易语言目录 `linker\VC6linker\Lib`——**把它加进 `LIB` 环境变量**再跑编译。
- 编译期若弹「是否自动下载 krnln (x86)」交互确认，说明它没找到静态库目录：按 §3/`--e-dir` 修环境，别在自动化里依赖交互输入。

产物自带 `.pdb` 与生成的 `.cpp/.obj` 中间文件，可用于排错与反汇编复核。

## 6. 标准工作流（AI 视角）

1. 备份原件，记 `version` 与输入 md5。
2. `unpack`（要依赖上下文就不用 `--main-only`），读工作区 `AGENTS.md`/`info.json`。
3. **检查 `_Lib0Cmd` 是否为 0**（§3），不通过先修环境。
4. 编辑 `src/*.txt`（声明与控制流遵守《易语言文本格式规范》）；`ecom/elib/header/project` 只读。
5. `validate` → `pack` 到**新文件** → 需要时 `compile` 出 exe 实测。
6. 怀疑工具/格式兼容性时 `verify-roundtrip`；校验失败保留全部现场，别覆盖原件。

## 7. 限制与风险

- 成功解包/回包**不代表**业务代码能编译能跑；`ecom/elib/header` 是快照，依赖更新后用 `update` 刷新。
- 自动化里别未经允许 `/update` 自升级工具。
- 窗口 `src/*.xml` 是设计器信息，没有明确 schema 时保持只读（布局/控件变更回 IDE 做）。
- 密码、本机路径可能进派生元数据——提交前检查。

---

*改编自 [aiqinxuancai/e-language-skill](https://github.com/aiqinxuancai/e-language-skill)（MIT License, © 2026 aiqinxuancai）`references/e-packager.md`，命令/实测按 v1.2.13 真机验证修订。*
