# 诊断：换成 miniblink_MbKit 后编译闪退（2026-09-23）

> 结论一句话：**问题不在 MbKit，也不在模块代码，在「文件名」。**
> 易语言把**文件名**当作模块名，你把模块命名为 `miniblink_MbKit.ec`，
> 模块名就从 `miniblink` 变成了 `miniblink_MbKit`，宿主工程里挂在这名字下的类/命令全部失联，
> IDE 在编译期直接崩（0xc0000005，**不报任何错**）。

---

## 1. 现象

- 把 AILI·LIFE 工程里的 `miniblink` 模块换成 `miniblink_MbKit` → **编译直接闪退**
- 换回原来的 `miniblink` → 一切正常
- 用户判断「代码结构有问题」

## 2. 崩溃的硬证据（Windows 事件日志 + WER）

`22:15 ~ 22:25` 之间 **e.exe 崩了 6 次**，异常签名完全一致：

```
失敗的應用程式名稱：e.exe，版本 5.9.0.0
錯誤模組名稱：      ntdll.dll
例外狀況代碼：      0xc0000005      ← 访问冲突
錯誤位移：          0x0008394d
```

WER 报告里的 `LoadedModule`（192 个）**没有 `node.dll`，也没有 `MbKit.dll`**——
说明崩溃发生在「读模块 / 解析代码」阶段，**根本还没走到加载 DLL**。

> 这条排除了一大票猜想：不是 MbKit.dll 缺依赖（它只依赖 KERNEL32/msvcrt/USER32）、
> 不是导出表格式（269 个具名导出，无异常修饰）、不是找不到库文件
> （实测把 MbKit.dll 移走，`--check` 仍然 `syntax-ok`）。

## 3. 根因：易语言的「内部模块名 = 文件名」铁律

`.e` / `.ec` 文件头（字段位于 `@268`）记录着一个**内部模块名**：

```
[uint32 长度][GBK 名字]
```

**IDE 会强制把它同步成文件名。** 三重实证：

| 证据 | 内容 |
|---|---|
| ① 同一份 `.ec`，我产出 vs IDE 保存后 | `09 00 00 00 "miniblink"` → `0f 00 00 00 "miniblink_MbKit"`（仅打开再保存就被改写） |
| ② 同一份 `.e`，我产出 vs IDE 保存后 | 我产出里 `miniblink_MbKit` 出现 **0 次**；IDE 保存后出现 **2 次**（内部名 + 模块来源路径） |
| ③ 你的工程 `AILI_LIFE_orig.e` | 内部名读出来正好是 **`AILI·LIFE`** = 文件名 |

而宿主工程里，**类 / 公开命令是挂在模块名下的**。你的工程里写着：

```
.程序集 MB浏览框, , 公开, miniblink          ← 第 4 字段 = 属主模块名
```

**⇒ 模块名一变，`MB浏览框` 就找不到归属，IDE 内部引用断裂 → 0xc0000005。**

## 4. 另外两个被顺带查清的问题

### 4.1 模块代码结构本身是好的（不必再怀疑）

同参数（e2t level2/ns1）对比「你的权威 `miniblink.ec`」与「我的 `.ec`」回读文本：

| 文件 | 权威 205,017 | 我的 204,631 | 差 |
|---|---|---|---|
| `const.e.txt` | 4,960 | 4,960 | **0** |
| `global.e.txt` | 559 | 559 | **0** |
| `struct.e.txt` | 5,430 | 5,430 | **0** |
| `dll.e.txt` | 55,367 | 55,619 | **+252**（正是我改的 252 处库名） |
| 支持库清单 | 只有 `krnln` | 只有 `krnln` | **0** |

公开子程序 **253 个，两边逐个一致，0 差异**。
两个 `.ec` 用 e2t 回读都是 **0 错误**。

### 4.2 你的工程「原样往返」也能过编译（基线）

```
原始 AILI·LIFE.e        → ecompile --check → ok=true, syntax-ok
e2t→t2e 往返后          → ecompile --check → ok=true, syntax-ok
```

所以工程本身、以及 e2t/t2e 往返链路都没问题——**唯一的问题就是模块名**。

## 5. 修复

**核心：让文件名与宿主工程里原来的模块名一致，也就是叫 `miniblink.ec`。**

本次交付：

```
out_mbkit/miniblink.ec      ← 文件名 = 内部模块名 = "miniblink"（关键）
main/corn/MbKit.dll         ← 与 node.dll 同目录
```

> ⚠️ **不要**把它改名成 `miniblink_MbKit.ec`——一改名就复现这次事故。
> 上一版交付的 `miniblink_MbKit.ec` / `miniblink_MbKit.e` 已归档到
> `re/_archive/名字陷阱_勿用/`，**不要再用那两个名字**。

### 用法（二选一）

**A. 在 IDE 里替换模块（推荐，不动你的文件）**

1. 先把 `main\corn\MbKit.dll` 放进 `main\corn\`（和 `node.dll` 一起）
2. 在工程里**删除**原来的 `miniblink` 模块
3. 插入 `out_mbkit\miniblink.ec`（**文件名保持 `miniblink.ec`**）
4. 编译

**B. 直接替换模块文件**

1. 备份 `D:\DevelopmentFolder\插件开发目录\E\插件\框架-栗子兼容计划\小栗子虚拟机\miniblink.ec`
2. 把 `out_mbkit\miniblink.ec` 覆盖进去
3. 工程零改动，直接编译

## 6. 工具侧的两处修复（防同类事故）

| 工具 | 问题 | 修复 |
|---|---|---|
| `elang-debug/scripts/mkcage.py` | e2t 判据是「stderr 里出现 `[错误]` 就算失败」，而引用**本机未装支持库**的工程必然刷这类噪音（`解析【类型】名称 失败！[支持库] 20994`）→ 这类工程**一律加壳失败**，整条无头编译链路对它们不可用 | 判据改为 `rc` + `SUCC:` + 文本目录实产；噪音只提示不判死 |
| `elang-build/scripts/ecompile.py` | 易模块编译产物如果「文件名 ≠ 内部模块名」，用到宿主里必崩，但**没有任何迹象** | 新增 `nameMismatch` 检查：`.ec` 产物编译成功后自动读内部模块名比对，不一致即在 JSON/文本输出里告警 |

> 顺带：正是修了 mkcage 之后，你的 16.5 MB 工程才第一次跑通 `--check`（此前一直卡在"加壳失败"）。

## 7. 复现命令

```bash
# 1) 读任意 .e/.ec 的内部模块名（验证铁律）
python - <<'PY'
from pathlib import Path
import sys; sys.dont_write_bytecode = True
import importlib.util
spec = importlib.util.spec_from_file_location("ec", "tools/ecompile.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
for p in ["out_mbkit/miniblink.ec", "AILI·LIFE.e"]:
    print(p, "->", m.ec_internal_name(Path(p)))
PY

# 2) 编译宿主工程（基线）
python tools/ecompile.py <工程>.e --check

# 3) 崩溃取证
wevtutil qe Application /q:"*[System[Provider[@Name='Application Error']]]" /c:20 /rd:true /f:text
#   WER 报告（含 LoadedModule 列表）：
#   C:\ProgramData\Microsoft\Windows\WER\ReportArchive\AppCrash_e.exe_*\Report.wer
```
