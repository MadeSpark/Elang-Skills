# EModFix — 易语言 .e 模块完整性检查/修复工具

针对「AI 往返改坏 .e 后模块方法丢失」问题的专用工具。核心原理：

- 易模块记录段（0x0C007319，`ECDependenciesSection`）为每个模块声明 `DefinedIds`（起始 Id + 数量）。
- IDE/编译器按**方法表下标连续窗口**取模块方法：前一个模块窗口结束后，下一个模块从当前游标开始连续取「声明数量」个方法。
- 若某模块实际方法数少于声明数，后续所有模块窗口整体后移，把方法表末尾的主工程方法吞成模块方法（典型症状：表尾新建按钮事件点击无效）。
- 修复 = 把缺失方法从供体 .e 补回方法表该模块 Id 区间内的正确位置，并在易包信息段1（0x0D007319）`FileNames` 并行数组同一下标插 null。**不改 `AllocatedIdNum`，不动其它实体，不移动非缺失方法。**

## 依赖

- .NET 8 运行时（框架依赖，非 self-contained）
- `lib/EProjectFile.dll`（QIQI.EProjectFile，csproj 已用 HintPath 引用）

## 用法

### 检查

```bash
dotnet bin/EModFix.dll check <file.e> [--json]
```

- 对每个模块记录、每个 DefinedIds 区间，按实体类型族统计「声明数量 vs 实际存在数量」：
  - 方法（0x04）
  - 程序集族（0x09 / 0x49 归并——49 范围常含一个同号 09 `__HIDDEN_TEMP_MOD__`，高字节不同不算缺失）
  - 常量/资源族（0x18 / 0x28 / 0x38 归并）
  - 结构（0x41）、DLL（0x43）、全局变量（0x00）
- 列出全部缺失 Id（含名称）。
- 按累计窗口模型列出会被错误归入模块窗口的主工程方法（名字 + Id + 下标 + 被哪个模块吞入）。
- 退出码：有缺口 2，无缺口 0，出错 1。
- `--json` 输出结构化 JSON（UTF-8，含 `HasGap`、`Ranges[].MissingIds`、`Windows[].SwallowedMain`）。

### 修复

```bash
dotnet bin/EModFix.dll restore <target.e> <donor.e> <out.e>
```

只恢复 target 中「声明存在但实际缺失、且 donor 中存在」的**模块方法**（0x04 类型）：

1. 插入位置 = 该模块已有方法块末尾（按 Id 数值 / 供体相对顺序），紧接模块方法块。
2. 同步恢复所属程序集的 `ClassInfo.Methods` 列表。
3. 同步 0x0D `EPackageInfoSection.FileNames`（与方法表下标一一对应，同下标插 null；不补会导致 IDE 报「[易包信息段1]读入失败」）。
4. 0x0A / 0x0B / 0x05 / 0x09 / 0x0E 等段经审计**不按方法下标对齐**，无需同步。
5. 不改 `AllocatedIdNum`、不改其它实体、不移动非缺失方法。
6. 缺失实体在 donor 中不存在 → 报错退出、不写输出。
7. 写输出前用 EProjectFile 重新加载做完整性复检（等价再跑一次 check 必须无缺口），失败不落盘。

## 构建与发布

```bash
cd scripts/emodfix
dotnet publish -c Release -o bin
```

产物为框架依赖的 `bin/EModFix.dll`（+ `EModFix.exe` 启动器），需目标机器装有 .NET 8 运行时。

## 已知限制

- restore 目前只处理**方法**（0x04）类型的缺失；若出现结构/DLL/常量等其它类型的实体缺失，check 会如实报告，restore 会报「无需修复」并退出（不写输出）。
- 供体与目标的方法 `Id` 必须完全一致；工具不做任何 Id 重映射。
- `check` 的名称解析来自当前文件自身实体表；缺失方法名显示 `?` 属预期（实体已丢失，名称无处可查），restore 阶段会从供体取回名称。
