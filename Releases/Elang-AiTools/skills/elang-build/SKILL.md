---
name: elang-build
description: 把易语言（.e / .ec 工程）编译成原生 exe/dll，或验证其能否编译通过时使用——无需打开 IDE、无需人操作、不依赖任何第三方工具。走**本套装自己的宿主支持库 elang_addin 链路**：加壳登记 → 独立 IDE 实例 → 进程内触发官方静态编译功能号（FN_COMPILE_STATIC 等 FNST_RUN 家族）→ 自动填充「请输入欲编译到的可执行文件名」对话框 → 监视产物落盘 → 按 PID 收尾并自证还原。封装成 ecompile.py 一条命令（--doctor 自检）。含静态编译种类选择（程序/控制台/DLL）、成功判据（产物存在且 mtime 更新，别信旧产物）、失败路径（面板报错 fail-fast）、程序入口优先级（_启动窗口 会无视 _启动子程序）、控制台验证模式。触发词：编译、编译exe、出exe、静态编译、无头编译、命令行编译、发布、构建、ecompile、编译失败、编译错误、LNK、链接器、控制台验证、标准输出、_启动子程序、_启动窗口、程序入口。
compatibility: 需与同套装的 elang-debug 技能一并安装——ecompile.py 运行时要调用它的 mkcage.py / elaunch.py / elang_addin.fne。单独安装本技能会在启动阶段缺件报错，装之前先用 ecompile.py --doctor 自检。
---

# 易语言无头编译与构建验证

> 三技能分工：`elang-ai-coding` 管「写对代码」，`e2txt-cli` 管「.e ⇄ 文本」，`elang-debug` 管「调试运行 + 抓调试输出」，**本技能管「编译成原生产物」**。
>
> 路径占位符：`<SCRIPTS>` = 本技能目录下 `scripts`；`<PY>` = `python`（3.9+，脚本零第三方依赖）。
> ⚠️ **本技能依赖 `elang-debug` 技能，两者必须同装**：`ecompile.py` 运行时要调用同套装 `elang-debug` 里的 `mkcage.py` / `elaunch.py` / `elang_addin.fne`。定位顺序是「本技能根 → 同级技能目录 → `~/.agents/skills/`」；**单独把本技能的 zip 装进一台没有 `elang-debug` 的机器会缺件失败**。装完先自检：`<PY> "<SCRIPTS>/ecompile.py" --doctor`（缺什么会直接列出来）。

## 0. 原理一句话

易语言官方 SDK 暴露了整套编译功能号（`PublicIDEFunctions.h`，`FNST_RUN` 家族）：`FN_COMPILE_STATIC(0x05020007)`＝静态编译、`FN_STATIC_COMPILE_WINDOWS_EXE(0x05020011)`、`FN_STATIC_COMPILE_WINDOWS_CONOLE_EXE(0x05020012)`、`FN_STATIC_COMPILE_WINDOWS_DLL(0x05020013)` 等。本套装的宿主支持库 `elang_addin.fne`（`elang-debug` 技能自带，源码在 `src/addin/`）被加壳登记进工程后，会在 IDE 进程内收到通知并**代为触发这些功能号**——同一机制也驱动着 `elang-debug` 的「调试运行」。触发后 IDE 会弹「请输入欲编译到的可执行文件名：」保存对话框，宿主库在**同进程内**自动填入期望产物路径并确认（模态对话框会阻塞触发线程，因此填充在独立线程完成——这是实测踩出来的关键点）。

## 1. 一条命令：`ecompile.py`

```bash
<PY> "<SCRIPTS>/ecompile.py" 原始.e 输出.exe                     # 默认 static_console
<PY> "<SCRIPTS>/ecompile.py" 原始.e 输出.exe --kind static_exe
<PY> "<SCRIPTS>/ecompile.py" 工程文本目录 输出.exe               # 目录输入自动 t2e
<PY> "<SCRIPTS>/ecompile.py" --doctor [--json]                   # 环境自检
```

- `--kind`：`static_console`（缺省，静态编译为控制台程序——**验证逻辑首选**）/ `static_exe` / `static_dll` / `static`（按工程当前类型）/ `exe` `console` `dll`（对应非静态编译）。
- `--json` 契约：stdout 只放 JSON `{"ok":true,"status":"ok","output":"…","outputSize":N}`；失败 `status` ∈ `failed`（触发被拒 / 面板报错 / IDE 崩）/ `timeout`，附 `error`。退出码 0/2/3/4。
- 流程与红线（脚本内建）：输入**文本目录**先 t2e → `mkcage` 加壳（**绝不改原 .e**，产物放工作目录）→ 临时投放 `.fne` 到 lib\ → 独立 IDE 实例编译 → 监视期望产物（启动前先清旧产物，**绝不以旧产物判成功**）→ 精确 PID 收尾 → lib 还原 + 安装目录 diff 自证。
- ⚠️ **输入必须是原始 .e**。已加壳的 .e（库表已登记本套装宿主库）**无法再走 e2t**：该库没装在 IDE 的 `lib\` 里时，`e2t` 会报「加载支持库失败」且**不产出**文本目录。脚本在这一步会明确报错，换原始 .e 重跑即可。
- ⚠️ **前提**：静态编译要求本机 IDE 已配置过「静态编译链接器」。没配置过时，先在 IDE 里手动做一次静态编译完成初始配置（脚本 `--doctor` 会提示）。

## 2. 成功判据与排错

| 现象 | 判定 | 处置 |
|---|---|---|
| `status=ok` 且产物存在 | 成功 | 运行产物做行为验证（§3） |
| `status=failed` + 面板有 `错误(NNNN):` | 源码编译错误 | 读 logFile（UTF-8 视图）第一组根因 → 修源码 → 重跑 |
| `status=failed` + eExitCode=0xC0000005 | IDE 在报错路径上被第三方插件带崩（已知风险） | 同上修源码；崩溃不影响「未产出」判定 |
| `status=timeout` | 90~300 秒无产物 | 查 logFile；确认 IDE 能手动打开该工程、静态链接器已配置 |
| `error: 已经加壳过` | 输入是加壳产物 | 换原始 .e |

## 3. 编译期验证工作流（AI 排错闭环）

```
1. 写/改代码（elang-ai-coding 的格式铁律 + echeck/rtcheck）
2. ecompile.py 编译（--kind static_console 出控制台产物便于验证）
3. 失败 → 读 logFile 第一组根因 → 修 → 重跑（别叠补丁）
4. 成功 → 运行产物做行为验证：
     控制台验证：_启动子程序 里 标准输出 (1, "…")，产物直接跑收 stdout
                 （实测真编译器下 标准输出 工作正常；仅第一个参数须显式给 1/2）
     调试输出验证：elang-debug 技能（加壳 → 调试运行 → 实时抓 输出调试文本）
5. 涉及 UI/线程/网络/DLL 的改动：编译通过 ≠ 行为正确，补真实场景测试
```

**程序入口提醒**：工程里存在 `_启动窗口` 时它优先作为入口、`_启动子程序` 不会执行；编译验证前先确认入口归属（详见《程序入口与编译验证》）。

## 4. 红线（与全套装一致）

1. 绝不覆盖、不「修补」用户的原 `.e`；加壳副本与产物都放脚本自己的工作目录。
2. 绝不 `taskkill /F /IM e.exe`——只按本次启动的精确 PID 收尾（脚本内建）。
3. `D:\<易语言目录>\` 视为只读：只允许临时投放/移除 `lib\elang_addin.fne` 并 diff 自证（脚本内建）。
4. 技能/脚本不写任何本机绝对路径；外部依赖一律「怎么找」。
5. 机器码相关工程按《置入代码与机器码》核对（静态编译后端下 `置入代码` 已实测真实执行）；INT3 等调试字节不进交付物。

## 5. 文件地图

| 文件 | 作用 |
|---|---|
| `<SCRIPTS>/ecompile.py` | 无头编译编排器：t2e(目录输入) → mkcage 加壳 → elaunch --compile → 判定（`--doctor` 自检） |
| `elang-debug` 技能 `scripts/mkcage.py` `scripts/elaunch.py` `assets/elang_addin.fne` | 被编排的既有链路（加壳 / 启动+投放+收尾 / 宿主库本体） |
| `elang-debug` 技能 `references/接口与调用约定清单.md` | 功能号与调用约定的实测底册 |
