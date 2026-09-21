# AutoLinker 与无头编译

> AutoLinker（aiqinxuancai 出品，MIT）是装进易语言 IDE 的支持库插件：给 IDE 加 MCP 服务、内置 AI、**无头命令行编译**与项目级编译配置。本文覆盖它的无头编译用法、项目配置、MCP 边界，以及**本套装实测的版本兼容闸门**（重要，先看 §1）。
> 改编自 aiqinxuancai/e-language-skill（MIT）。

## 1. ⚠️ 版本兼容闸门（本套装实测，装前必读）

实测结论：**AutoLinker 能否被 IDE 加载，取决于 IDE 构建是否够新**，与「把 `.fne` 放进 lib」无关。旧版 IDE 打开引用 AutoLinker 的工程会弹：

> 「★ 不能载入支持库"AutoLinker 1.0版"，其文件名为"AutoLinker.fne"，数字签名为"…"，请确认易语言中是否安装了此支持库。」

用**受控变体实验**（自研最小支持库逐项开关 LIB_INFO 字段）+ 30 个历史版本逐一取证，钉死旧 IDE 的三道闸，AutoLinker 全系版本（5.0.7 ~ 5.1.18）**三道全踩**：

| 闸门 | 旧 IDE 的行为 | AutoLinker 的声明 |
|---|---|---|
| 要求的系统版本 `m_nRqSys*Ver` | 低于其要求即拒载 | 3.7 |
| 要求的核心库版本 `m_nRqSysKrnlLib*Ver` | 同上 | 5.3 |
| `m_dwState` 带 `LBS_LIB_INFO2`（0x80，授权信息结构） | 直接拒载 | 置位（LicenseToUserName 非空） |

任一闸门命中即拒载——**和授权字段内容无关**（实验变体把授权用户填成任何值都被拒）。

**给使用者的判定步骤**：

1. 下载 AutoLinker（<https://github.com/aiqinxuancai/AutoLinker/releases>，取 `AutoLinker-<版本>.zip` 里的 `AutoLinker.fne`，别下 Source code 包）。
2. 放进易语言目录的 `lib\`，在 IDE「工具 → 支持库配置」里启用，重启 IDE。
3. 打开任意工程看有没有上面的「不能载入」弹窗：**有** → 你的 IDE 构建太旧，升级到 AutoLinker 支持的新版易语言（官方口径为 5.9x 最新版）再用，否则装了也白装；**没有** → 可用。
4. 也可先跑 `elang-build` 技能的 `python scripts/ecompile.py --doctor` 做环境自检。

另注：易语言**不会**自动加载新丢进 `lib\` 的库（加载集合来自持久化登记表，注册表/文件里均无明文清单，实测无法脚本化登记）。所以「支持库配置里启用」这一步要人做一次；做不了时的替代是给目标工程加壳登记库表（见 `elang-debug` 技能的加壳机制），但版本闸门照样会拦。

## 2. 无头命令行编译（AutoLinker 装好且能加载后）

直接给 `e.exe` 传参，对任意磁盘 `.e` 启动**独立 IDE 实例**（默认隐藏窗口）→ 编译 → 写结果 JSON → 退出。不需要 MCP 会话：

```text
e.exe <input.e> --autolinker-headless-compile
  --autolinker-output <输出路径>          # EXE 用 .exe / DLL 用 .dll / 模块用 .ec
  --autolinker-target auto|win_exe|win_console_exe|win_dll|ecom
  --autolinker-result <结果.json>          # 强烈建议显式指定
  --autolinker-startup-timeout <秒>        # 建议 120
  [--autolinker-static]                    # 仅 EXE/DLL；模块目标不要传
```

- 别名：`--autolinker-compile`、`--autolinker-output-path`、`--autolinker-static-compile`、`--autolinker-result-path` 等；窗口/生命周期开关 `--autolinker-show-window/--autolinker-hide-window/--autolinker-no-exit/--autolinker-exit`（自动化保持默认：隐藏 + 编译后退出）。
- 外层命令超时要覆盖 IDE 启动 + 编译时间（`startup-timeout 120` 时至少给 180 秒）。超时先查结果 JSON 与 IDE 进程，别急着重编。
- 并行任务：显式 `--autolinker-result` 各用各的；共享的 `AutoLinker/Log/headless_compile_last.json` **不能**用来区分并行结果。

**成功判定（四条全过才算成功）**：`e.exe` 退出码 0；结果 JSON `ok=true`；`compile_result.artifact_verified=true`（或 `output_file_exists` 且 `output_file_modified_after_compile` 均真）；输出文件存在且路径一致。**不要**只看控制台「编译成功」字样或旧产物。排错读 `error`、`compile_dialogs`、`ide_message_boxes`、错误位置字段。

**退出码**：0 成功；1 编译失败/产物未生成；2 请求错误（缺输出路径、目标非法）；3 IDE/工程信息等待超时；4 内部工具调用/解析失败；6 启动期有阻塞弹窗（运行时未就绪 → 查 `lib/AutoLinker.fne` 是否安装启用、工程依赖能否加载）。

**用控制台 EXE 验证逻辑**：`--autolinker-target win_console_exe [--autolinker-static]` + `_启动子程序` 里 `标准输出` 打印（见《程序入口与编译验证》）。入口没执行 → 先查 `_启动窗口` 是否还在抢入口。

## 3. 项目级按配置编译（IDE 菜单驱动）

「工具 → AutoLinker 设置 → 编译配置」维护多套输出方案，存工程旁 `<文件名>.autolinker.json`（UTF-8；字段 `active_configuration` + `configurations[]`，每项含 `name/target/static_compile/output_path/pre_build[]/post_build[]`）。`target` 取值同 §2；输出路径变量 `{ProgramDir}/{ProjectDir}/{ProjectName}/{SourcePath}`（编译前后动作还可用 `{OutputPath}`）；编译前后动作每行一条 PowerShell 命令，任一前置失败即停。

执行：主菜单「编译 → 按配置编译」→ AutoLinker 快照当前内存工程 → 另起隐藏 IDE 实例异步编译。菜单「已提交」≠ 产物已出——等 IDE 输出「异步编译完成」并检查文件。多套配置用**不同输出路径**，避免互覆。

## 4. AutoLinker MCP（连接已打开的 IDE）

- IDE 内置本地 Streamable HTTP MCP 网关 `http://127.0.0.1:19207/mcp`（19208+ 是实例后端，别直连）。操作的是**IDE 内存工程**，不是磁盘文件。
- 强制顺序：`initialize` →（多 IDE 时 `list_instances`/`select_instance`）→ `get_current_eide_info` → `refresh_workspace_mirror` → `list_files/search_code/read_files/read_code_item` → 每个待改文件**紧邻写入前** `read_real_file` 拿 `real_source`+`code_hash` → `edit_file/multi_edit_file/write_file/diff_file`（都要 `expected_base_hash`，冲突 = 真实页已变，重读重改，**不许绕过 CAS**）→ 需要验证时 `compile_with_output_path`。
- 能力边界：**不能**创建/修改原生窗体与控件布局（`src/*.xml` 只读）；新增事件子程序 ≠ 建立了事件绑定；`add_new_file` 只能建普通程序集/类；`ecom/elib/header` 只读。
- 编译目标匹配工程；模块工程 `target=auto` 出的是控制台测试程序，发布 `.ec` 才用 `ecom` 且关静态编译。
- 编译磁盘上的**其他** `.e` 用 §2 的无头编译，别把 MCP 的 `compile_with_output_path` 当磁盘编译器用。

## 5. AutoLinker 安装与目录速记

- 安装：Release 的 `AutoLinker.fne` → 易语言目录 `lib\` → 支持库配置启用 → 重启 IDE。主要支持新版易语言（见 §1 闸门）；设置页基于 WebView2（缺运行时会受影响）。
- 运行目录（`e.exe` 所在目录下）：`AutoLinker\AIConfig.json`（AI 配置）、`AutoLinker\Log\`（日志与无头编译结果）、`AutoLinker\Skills\`（全局技能）、`%TEMP%\AutoLinker\`（临时镜像）。工程级：`.e` 旁同名 `{文件名}.AGENTS.md` 与 `<工程目录>\.agents\skills\`（同名优先于全局）。
- API Key 只在「工具 → AutoLinker 设置 → AI 接口」配，先连通性测试；协议/BaseURL/模型必须同服务商；**别把 Key 写进工程规范、源码或日志**。
- 核心库函数重写（静态链接覆盖）：只适用于明确 32 位静态链接；自有 `.lib` 通常要关 `/GL` 并接受 `/FORCE` 告警。

---

*改编自 [aiqinxuancai/e-language-skill](https://github.com/aiqinxuancai/e-language-skill)（MIT License, © 2026 aiqinxuancai）三份 AutoLinker 参考文档，§1 为本套装实测新增。*
