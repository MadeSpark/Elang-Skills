# 交接：miniblink 模块编译与交付阻塞（2026-09-26）

> 给接手 AI 读：先看 **确诊事实** 与 **交付门禁**；跳过任何已标为"红线/严禁"的历史失败方向。
> 最后用户指令（交付方向）：`倒是输出到 D:\DevelopmentFolder\常用模塊\miniblink.ec 啊`，并当面要求"把 `.ec` 必须由 `.e` 编译产出、不得改扩展名"的规则写进 skill。
> 用户最后现象（功能阻塞）：
> ```
> [内存加载] 池=0x0 映像基址=0x6C2C0000 状态=2 槽0=0x6C2CA880 槽259=0x6C2C4A90 / 宿主库载入失败
> 依旧不行
> ```

---

## 0. 确诊事实（必须遵守，违反必败）

### 0.1 合法性红线（已被用户当面点名）
1. `.ec` 是**易语言 IDE 编译完成的已编译易模块**；头 8 字节 ASCII 必须为 `CNWTEPRG`，不是这 8 字节的一律不是合法 `.ec`。
2. `e2txt -mode t2e` 只能产出 `.e` 源码；**严禁**把 `t2e` 的输出改扩展名或伪装为 `.ec` 部署；违者 IDE 会报 `本模块程序未经编译，不能载入`。
3. 任何候选 `.ec` 在反向抽取（`e2txt -mode e2t`）中若 `ParseMethodCode 错误 > 0` 或关键修复语句不存在，**不得交付**。

### 0.2 功能修复的根因（已 100% 确诊）
- 映射器本身**并未失败**：读 `XiaoLiZi_VM(pid=49704)` 进程 `0x50000D00` 处：
  - `映像基址 = 0x6C2C0000`（非零，说明 MbKit 已被某进程手工载入并记录进共享池）
  - `MapperStatus = 0`（映射器成功）
  - `MBK1 = 0x314B424D`（魔数正确，十进制 `827015757`，写在偏移 `0x50000D08`）
- "失败"只是**实例静态变量 `_内存状态 = 2` 的早返回分支没有检查共享魔数**：某并发首载/重载实例先把自己的 `_内存状态` 置 2，后续即便共享池已经就绪，它仍然永远早返回失败，表现为"和狗皮膏药一样，重启还一样"。

### 0.3 文本层修复（已写入真源，尚未进入最终 ecom）
在真源文件 [程序集_内存映射.static.e.txt](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/mod_src/class/程序集_内存映射.static.e.txt#L63-L83) 中，
以及 e-roundtrip 副本 [format-fix/e-roundtrip/代码/程序集_内存映射.static.e.txt](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/format-fix/e-roundtrip/代码/程序集_内存映射.static.e.txt#L73-L83) 中，**已正确写入**：
```
' 并发首载时，本实例可能在先行实例写完 MBK1 前暂时失败。
' 共享池尚未就绪才保留失败；已就绪时清掉本 DLL 的旧失败标记并继续附着。
.如果真 (_内存状态 ＝ 2 且 _内存读整数 (1342180616, 0) ≠ 827015757)
    返回 (假)
.如果真结束

.如果真 (_内存状态 ＝ 2)
    _内存状态 ＝ 0
.如果真结束

_内存状态 ＝ 2
```
同时 [_内存自检文本](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/format-fix/e-roundtrip/代码/程序集_内存映射.static.e.txt#L433-L445) 已把单行超长 `返回(...)` 拆成多条赋值，避免 `ParseMethodCode` 丢弃整段。
生成器 [gen_module.py](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/gen_module.py) 也已同步该修复版本，防止后续重生成时回滚。

### 0.4 已确认不存在 / 不能走的路径
- `format-fix/ai-roundtrip/代码/程序集_内存映射.static.e.txt`：不存在 → 佐证 `_ai.e` 引导工程不是可回读真源，不能当作用户源码。
- `--direct` 模式（ecompile.py 里跳过 mkcage 直接编译原 `.e`）：**不能用**。因为 FNE 只被"已由当前工程声明过的库"加载；原 `.e` 没有声明 `elang_addin`，IDE 启动后 FNE 根本不进 WorkerProc，编译任务永不触发。
- 把 `t2e` 产物改名部署、`ParseMethodCode>0` 仍交付、不做反向抽取验证：**全部红线，严禁。**

---

## 1. 当前交付阻塞：两条编译路径都失败，原因已拆开

目标：产出一个能交付的 `miniblink.ec` → `D:\DevelopmentFolder\常用模塊\miniblink.ec`
必须通过 3 项门禁（门禁写在第 3 节）。

当前有两条编译路径，**各自的失败点已定位**：

### 路径 A：mkcage + elaunch（"加壳链"，历史默认路径）→ **丢代码**

流程：文本目录 `→ t2e → 改库表(登记 elang_addin 声明) → t2e → _ai.e → elaunch 编译 ecom`

**失败现象：**
- `candidate/miniblink-fix.ec` 确实成功产出（421,695 B，头 `CNWTEPRG`，合法 ecom）
- 但**反向抽取 `e2txt -mode e2t` 后**，在 [fix-audit/class/程序集_内存映射.static.e.txt](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/candidate/fix-audit/class/) 中：
  - ❌ **没有**状态恢复分支（即 0.3 节那两段 `.如果真 (_内存状态 ＝ 2 ...)` 完全丢失）
  - ❌ `_内存自检文本` 函数体**为空**（只剩局部变量声明，无拼接文本语句）
  - ✅ 只有旧版里的"等 50 次计次循环"读 MBK1 仍然存在（那是旧版等待逻辑，不是修复）
- 换言之：**mkcage 链的两次 t2e 过程又静默丢了修复代码**，与之前触发用户 `错误(10022): 未命名子程序…` 的那次失败（422,096 B 坏模块）根因同源，只是这次没刚好撞到"未命名子程序"的严重后果、但修复逻辑同样丢了。

**已确认触发丢代码的三类文本层问题（在 module 真源里都已修，但 `mkcage` 的第一次加壳 `e2t` 反向抽取用户工程时还会出）：**
1. `.如果真` 嵌套 `.否则` → `t2e` 不支持，直接丢段
2. 单行超长 `返回 (超多字段拼接 ...)` → 超出行缓冲后被截断丢
3. 块间空行不写 4 空格（写了 0 空格或 2 空格）→ 块结构解析错位

**这条路径能救吗？能。但必须在 mkcage.py 里加门禁：**
- 每次 `t2e` 跑完，强制 `grep ParseMethodCode [错误]`，**>0 立即失败**，不准继续编译；
- 并且每次 `t2e → e2t → e-roundtrip` 双次往返后，要 `diff` 关键语句是否还在（至少检查：`_内存状态 ＝ 2 且 _内存读整数 (1342180616, 0) ≠ 827015757`、`_内存状态 ＝ 0`、`_内存自检文本` 函数体内行数 ≥ 8 行）。
- 这三条门禁必须写进 `tools/mkcage.py` 和 `tools/ecompile.py` 的 mkcage 分支，**不能只停在口头/文档**，否则下一个 AI 又会复现。

### 路径 B：引导工程 + `FN_OPEN_FILE2`（`--open-target`，想避开 mkcage 损失链）→ **FNE 被卸载**

代码已落地三处：
- [elaunch.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/elaunch.py)：新增 `--open-target <path>` CLI 和 `ELANG_AI_OPEN_TARGET` 环境变量；
- [elang_addin.cpp](file:///C:/Users/MadeSpark/Desktop/测试/src/addin/elang_addin.cpp)：WorkerProc STEP4 之后、DoCompileTask 之前新增 `OPEN_TARGET` 块，读 env → `IdeRunFuncOnMainThread(FN_OPEN_FILE2, target, 0)` → `Sleep(2000)` → 再跑编译；
- [ecompile.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/ecompile.py)：新增 `--open-target` 与 `--direct/--no-direct` 参数分流。

**首跑失败日志（关键 6 行，来自 [miniblink_ai_000803_trace.txt](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/format-fix/ecom-work/miniblink_ai_000803_trace.txt#L235-L241)）：**
```
STEP4 skipped (ELANG_AI_RUN != 1)
COMPILE open target 'C:\Users\MadeSpark\Desktop\????\re\mbkit_mem\format-fix\miniblink.e' ...   ← ① 中文路径乱码!!!!
COMPILE main-thread invoke 0x03010008 ...
NOTIFY nMsg=17 (0x11) p1=0 p2=0                                       ← ② NL_UNLOAD_FROM_IDE!!!!
HOOK NL_UNLOAD_FROM_IDE
COMPILE main-thread invoke return handled=1
COMPILE open target invoked=1 handled=1
```
（之后就没有任何 COMPILE-CHECK / DO-COMPILE / ECOM-SAVE 记录，任务断了）

**失败根因（两个叠加）：**
1. **致命：`FN_OPEN_FILE2` 会让 IDE 关闭当前引导工程 → 引导工程声明的 FNE 被 IDE 立即卸载（`NL_UNLOAD_FROM_IDE`）**。此时 Worker 线程还拿着已经卸载的 DLL 里的函数指针，后续编译任务要么不跑、要么直接崩溃。这是架构问题，不是 sleep 不够。
2. **次要：中文路径 GBK/UTF-8 编码不一致** → 日志里目标路径变成 `\????\`，IDE 实际打开了什么不可知；即便 #1 修了，路径乱码也会让"打开正确目标工程"失败。

**这条路径要继续的正确方向（不是 Sleep 更久）：**
- 架构上必须保证 FNE 在目标工程打开后**仍然驻留**：两条可行子方向任选：
  - **子方向 B1（推荐）**：不做"引导工程→切换目标工程"，而是用 `mkcage` 的库表篡改思路，**但只篡改库表，不走两次 `t2e`**：即对用户提供的真 `.e` 做"二进制级库表 patch"，在工程库表 section 里插入一条 `elang_addin` 的声明（e 工程文件里库表是明文结构，用 Python 直接 patch 字节即可，无需 t2e），然后直接 `elaunch` 编译这个被 patch 过的用户 `.e`。这样用户源码既不经过 `t2e` 回写，又声明了 FNE 所以 WorkerProc 会跑。这个方向最干净。
  - **子方向 B2（备选）**：把 `elang_addin` 真正安装成 IDE 常显插件（不是工程临时声明），写进易语言安装目录的插件配置里；这样无论打开哪个工程，FNE 都常驻。但有两个历史坑：
    - `Hard Constraints`：`elang_addin.fne 严禁常驻 lib 目录，必须随用随放`（项目记忆里写的，怕污染用户安装导致 eide 卡死），所以这条是用临时 CopyFile/RemoveFile 包装"编译前投进去，编译后不论成败都删掉"的围栏，失败时要 try/finally 清理，别留脏文件。
    - 还要补掉 #2 的中文路径乱码：`GetEnvironmentStringsW` / `GetCommandLineW` 用 Unicode 版本读 env，不要用 A 版，否则含中文的路径永远乱码。

---

## 2. 当前部署状态（避免误判自己重复成功）

`D:\DevelopmentFolder\常用模塊\miniblink.ec`：
- 大小 421,911 B，SHA `721B894B...5AB74`（已在上一轮回滚）
- **不含本次修复逻辑**：反向抽取验证过，`_内存状态=2` 恢复分支不存在。
- 所以用户说"依旧不行"是正确的。**不要把这个回滚版当成交付成功**。

坏模块取证保留：`C:\Users\MadeSpark\Desktop\测试\re\_rollback\miniblink-10022-rollback-20260926\miniblink.ec.bad-422096`
- 是触发用户 `错误(10022): 未命名子程序…` 的元凶。大小 422,096 B，严禁覆盖回常用模块。

---

## 3. 交付门禁（任何候选 `.ec` 必须三项全过，否则不准覆盖常用模块）

### 门禁 1：合法性（ecom 头 + 官方编译）
```
head -c 8 candidate.ec  == b"CNWTEPRG"
```
不是 → 直接报废，不是合法已编译模块。

### 门禁 2：反向抽取含关键修复语句（最重要）
对候选 `.ec` 执行 `e2txt -mode e2t -src candidate.ec -dst audit_dir`，然后：
- 必须命中 `程序集_内存映射.static.e.txt` 中这两行（且上下文是 `.如果真` 块，不是注释）：
  ```
  .如果真 (_内存状态 ＝ 2 且 _内存读整数 (1342180616, 0) ≠ 827015757)
  ...
  .如果真 (_内存状态 ＝ 2)
      _内存状态 ＝ 0
  ```
- `_内存自检文本` 函数体内必须有 ≥ 6 条 `文本 ＝ ...` 赋值语句（不能只是 `局部变量声明` 后就直接结束）。
- 任一不满足 → 丢代码了，不准交付。

### 门禁 3：用户侧 10022 验收
- 方法：在 IDE 新建一个"普通 Windows 易程序"空工程 → 菜单"模块引用 → 导入模块 → 选 candidate.ec"。
- 通过标准：IDE 不崩溃、不报 `错误(10022) 子程序“未命名子程序”…`、不报 `本模块程序未经编译，不能载入`。
- 任一出现 → 不准交付。

### 门禁 4（功能验收，交付后）：重载不锁死
- 部署到常用模块后，让用户在"小栗子虚拟机"宿主内做至少两轮：
  1. 载入 MbKit/插件一次 → `WKE宿主自检()` 显示 `池=0x50000000 状态=1`
  2. 不重启宿主，再重载一次 / 并发载入第二个 MbKit 插件 → 仍显示 `池=0x50000000 状态=1`
  3. 不能再出现 `池=0x0 映像基址=非0 状态=2 / 宿主库载入失败`（这是修复的核心用例）

---

## 4. 下一步清单（按优先级，先 #1 再 #2）

### 优先级 1：选一条编译路径打通（推荐 A1，产出最快）
**推荐走 A1 = 路径 A（mkcage）加硬门禁 + 重跑：**
1. 改 [tools/mkcage.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/mkcage.py)：
   - 每次调用 t2e 后立即读 t2e.log 统计 `\[错误\].*ParseMethodCode` 条数；
   - `> 0` 立刻 `sys.exit(非0)`，并把报错的上下文（前后 5 行）打印到 stderr；
   - 同时加关键语句门禁：对产出的 `_ai.e` 立即 `e2t` 回读一次，确认三条关键语句存在（"_内存状态 ＝ 2 且 _内存读整数 (1342180616, 0) ≠ 827015757"、"_内存状态 ＝ 0"、"_内存自检文本 子程序下有 ≥ 8 行正文"）。任一缺失立即失败。
2. 改 [tools/ecompile.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/ecompile.py) 的 ecom 分支：**编译成功后必须自动跑门禁 2（反向抽取验证关键语句）**，不通过就把候选 `.ec` 删掉并返回失败 JSON，让调用方不要以为成功了。
3. 用门禁加固后的工具链，把 [format-fix/e-roundtrip](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/format-fix/e-roundtrip/)（文本真源，已含修复语句）重新编译成 ecom。这一轮只要 ecompile 不报失败，就一定满足门禁 1+2。
4. 手动门禁 3：新建空易工程导入 candidate.ec → 不爆 10022 → OK。

### 优先级 2（若优先级 1 仍丢代码，就走路径 B1，根本绕开 t2e）
**走 B1 = 直接 patch 库表进用户 `.e`，不经过两次 t2e：**
1. 先把 `.e` 文件结构摸清：用干净的 miniblink.e 对比 mkcage 产出的 `_ai.e`，定位 `库表 section` 在 `.e` 二进制中的偏移与条目格式（它应该是：库数量 N + N 条记录，每条是库 GUID/名称/版本号的固定长度 + 变长字符串表，或直接明文"支持库1\x00支持库2\x00"样式，取决于版本）。
2. 写一个一次性脚本 `patch_libs_to_e.py <input.e> <output.e>`，把"elang_addin"那条库记录**直接字节级插入**，完全不经过 t2e 解析代码。
3. 然后直接把 `output.e` 喂给 elaunch 编译 ecom（注意：此时 output.e 已经声明了 FNE，所以 `--direct` 会正确加载 FNE，之前 `--direct` 失败是因为原 .e 没声明 FNE，不是 `--direct` 逻辑坏了）。
4. 门禁同样按第 3 节全部走一遍。

### 优先级 3（无论走哪条路，交付前做）
1. 三项门禁全过后，先备份当前 `D:\DevelopmentFolder\常用模塊\miniblink.ec`（命名如 `miniblink.ec.bak-YYYYMMDD`，放同目录或 `re/_rollback/` 下），再 Copy-Item 覆盖目标路径；
2. 把新文件 SHA、大小、反向抽取命中关键字的日志一起写到 `deployed-audit.log` 存证；
3. 通知用户去"小栗子虚拟机"做重载验证（门禁 4），等用户说`状态=1`不再锁死，才算真交付完成。

### 优先级 4：把硬规则写进 skill（用户明确要求）
改 `tools/ecompile.py` 的 --help 和 SKILL.md 的 e2txt-cli / elang-build 两条：
- 明确：`.ec 必须由 .e 编译产出，t2e 不改扩展名，ParseMethodCode>0 不交付，反向抽取关键语句缺失不交付`；
- 把第 3 节 4 条门禁作为 ecompile 的"--ecom-validate"默认开关，开了就自动跑 1+2 门禁。
- 这样就满足用户要求："给我把这一点写进 skill 里，免得别的 ai 也犯这种低级错误"。

---

## 5. 工作区关键文件索引（接手时从这里读）

| 分类 | 路径 | 作用 |
|---|---|---|
| 真源-模块文本 | [mod_src/class/程序集_内存映射.static.e.txt](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/mod_src/class/) | 修复写入的最原始文本真源，任何改动从这里起 |
| 真源-模块生成器 | [gen_module.py](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/gen_module.py) | 从真源+资源生成完整工程，任何同步修改要两边写 |
| 候选 ecom（最新，含合法头但丢修复） | [candidate/miniblink-fix.ec](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/candidate/) | 421,695 B；反向抽取缺修复语句，**未过门禁，不得覆盖部署** |
| open-target 失败 trace | [format-fix/ecom-work/miniblink_ai_000803_trace.txt](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/format-fix/ecom-work/) | 看 L235-L241 就知道为什么路径 B 会失败 |
| FNE 真源 | [src/addin/elang_addin.cpp](file:///C:/Users/MadeSpark/Desktop/测试/src/addin/elang_addin.cpp) | OPEN_TARGET 段写在 DoCompileTask 前，要做 Unicode + 常驻改造从这里入 |
| 工具链 - 编译入口 | [tools/ecompile.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/ecompile.py) | 加门禁、--ecom-validate 默认开、mkcage 失败即退出 |
| 工具链 - 加壳/转文本 | [tools/mkcage.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/mkcage.py) | 当前"丢代码不报错"的缺口就在这里；必加 t2e 错误统计 |
| 工具链 - IDE 启动器 | [tools/elaunch.py](file:///C:/Users/MadeSpark/Desktop/测试/tools/elaunch.py) | --open-target 参数和 env 在这里传 |
| 反向抽取-旧常用模块（回滚版） | [format-fix/ec-audit/class/](file:///C:/Users/MadeSpark/Desktop/测试/re/mbkit_mem/format-fix/ec-audit/class/) | 对照看"旧版缺什么"，最直观：_内存自检文本函数体是空的 |
| 交付目标路径 | `D:\DevelopmentFolder\常用模塊\miniblink.ec` | 用户唯一认可的交付位置，必须门禁全过才写 |
| 坏模块留证 | `re/_rollback/miniblink-10022-rollback-20260926/miniblink.ec.bad-422096` | 触发 10022 的 422,096 B 版，留作对比，别删 |

---

## 6. 绝对不能再犯的低级错误清单（本项目连续踩过 3 次）

> 写这里不是为了骂人，是为了"下一位 AI 不要重复踩同样的坑再被用户骂"。

1. **❌ 把 t2e 产出的 .e 改名 .ec 部署** → 用户原话：`卧槽，你脑子呢？ec 不是 e，ec 模块是要那 e 编译成 ec 才能用的！` → 已写进 0.1 红线。
2. **❌ t2e 出现 ParseMethodCode [错误] 但继续往下编译** → 丢了控制语句后最终 ecom 有坏子程序，但编译表面"成功"，用户导入直接 10022。→ 必须 >0 立即 fail。
3. **❌ ecom 编译成功但不反向抽取验证就部署** → 表面"合法 CNWTEPRG"，但修复语句全丢，用户侧功能仍然 `状态=2 永久失败`，白忙活一轮。→ 门禁 2 必须自动执行。
4. **❌ open-target 失败后以为是 Sleep 不够就改 2s→5s→10s** → 根因是工程切换导致 FNE 被卸载，sleep 没用。→ 走 B1（patch 库表绕开切换）或 B2（常驻装插件 + Unicode 读 env + finally 清理）。
5. **❌ 直接在常用模块目录写候选 .ec，没先备份旧版** → 10022 坏版本一留就是半天，用户被迫自己找备份。→ 交付前必须先备份目标路径上的旧 .ec。

---

## 7. 待清理后台任务（接手前先确认状态）

- 本次会话启动时已有 3 个 background job（job-ca4cf777、42cd4e2b、865b9b85）在跑，是前序会话遗留的编译/工具任务，接手时先查 output.log 是否结束；如果卡住 > 10 分钟可以 Kill，它们不影响本次交接的工作区。
- PID 清理：若 `e.exe` / `ecom` 编译进程残留，会导致下次 elaunch 打开同一个引导工程时被锁定，表现为编译超时 → 先 `taskkill /F /IM e.exe` 清干净再跑新任务。

---

**交接最后一句话给接手 AI：**
> 功能修复的文本层已经 100% 正确（0.3 节），现在的 100% 阻塞是**编译链把修复代码静默弄丢了 + 不做门禁直接交付**。优先给 mkcage/ecompile 加上"t2e 错误立即失败 + 反向抽取验证关键语句"两个硬门禁（第 4 节优先级 1），再重跑一次编译即可稳定产出正确 `.ec`。别再绕弯子，先加门禁，再重跑。
