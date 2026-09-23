# 改造 miniblink 模块：以 MbKit 替代直接加载 node.dll（2026-09-23）

> 目标：让 miniblink（基于 node.dll 的易模块）在宿主进程里**可以卸载后重新加载**，
> 不再需要「结束宿主 exe 再启动」。
> 交付：`miniblink_MbKit.e`（改造后源码）、`miniblink_MbKit.ec`（编译产物）、`MbKit.dll`（常驻层）。

---

## 0. 结论先行

**改动极小：易语言源码只改了一处 —— 252 条 `.DLL命令` 的库名，从
`"main\corn\node.dll"` 改成 `"main\corn\MbKit.dll"。逻辑代码一行未动。**

之所以能这么小，是因为 MbKit 导出了**与内核同名同调用约定（cdecl）的 252 个透明转发桩**，
业务侧看到的调用面完全不变；变的只是「谁持有内核」和「回调登记在哪」。
（`main\corn` 的路径约定保持不变，MbKit.dll 就放在那个目录里。）

---

## 1. 为什么原来必须重启宿主进程

原模块的三处设计叠加，使卸载必然踩空：

| # | 原实现 | 后果 |
|---|---|---|
| 1 | `.DLL命令` 用**相对路径** `"main\corn\node.dll"` | 易语言的 DLL 命令是运行时解析且**只增不减引用**，node.dll 进程级常驻，业务 DLL 卸载动不了它 |
| 2 | 回调是**本模块内**的子程序（经 `_stdcall_to_cdecl` 生成的 thunk） | 模块一卸，内核手里就是**指已释放代码页的野指针**，还在继续调 |
| 3 | `_子类化` + `SetPropA(hwnd,"wkeEThis",…)`，恢复动作挂在 `如果真 (IsWindow(窗口)≠0)` 分支里 | **先关窗口再卸 DLL** 时该分支整段跳过，窗口过程链里留着指向已卸模块的 `_SubProc` |

实测（`re/mb/mb_probe.c` 直接驱动真实 node.dll 走完整生命周期）：

```
[1] 首次 LoadLibrary            module base = 79b90000, wke 1.02 / blink 98096
[2] 首次 init + 建 WebView      loadingCompleted=1        ✅
[3] 销毁后不 finalize 再建       loadingCompleted=1        ✅
[4] finalize + init 后重建       loadingCompleted=1        ✅
[5] FreeLibrary 真卸载了         GetModuleHandle(node.dll)=NULL ✅
[6] 重新加载后完整可用            loadingCompleted=1        ✅
```

⇒ **内核本身完全支持重复初始化与卸载重载**，问题不在 miniblink，在「谁持有内核 + 回调登记在哪」。

顺带两个会误导排查的硬事实：
- `wkeFinalize()` 之后 `wkeIsInitialize()` **仍返回 1**（标志没清）——别拿它判断能否重来。
- `wkeGetVersionString()` 在 `wkeInitialize()` **之前**调用必崩。

---

## 2. 方案：把「内核」和「回调」搬到一个常驻层

```
宿主 exe
  └── 业务插件 DLL
        └── miniblink（易模块，编进插件）
              │  .DLL命令 全部指向 "main\corn\MbKit.dll"（名字、参数、返回类型一字不改）
              ▼
        ┌──────────────────── MbKit.dll ────────────────────┐
        │ 自 PIN（GetModuleHandleEx …_FLAG_PIN）常驻进程      │
        │ 进程内**唯一**持有 node.dll（懒加载，只加载一份）    │
        │ 252 个同签名透明转发桩：纯 `jmp [slot]`，不碰栈      │
        │ 22 个回调注册类被接管：                              │
        │   内核只见 MbKit 的 wrapper（永不失效）              │
        │   业务 thunk 只作**数据**存在登记表里                │
        │   wrapper 每次被调先校验「业务回调是否还有效」         │
        │   无效 → 丢弃 + 计数（不崩）                         │
        └───────────────────────────────────────────────────┘
```

关键点：

- **转发桩不碰栈**。桩体只有 `jmp *slot`（外加首次的懒加载检查），
  所以 cdecl/stdcall、参数个数、返回值、栈平衡全部由内核与调用方自行负责，
  MbKit 无需知道任何一个 API 的签名。
- **`wkeOnXxx(view, 0, 0)` 原样转发**。模块里有大量「传 0 表示关闭该事件」的用法，
  绝不能替它注册代理，否则语义被悄悄改掉。
- **存活判据必须两路**（这是最容易做错的一点）：

  | 业务回调形态 | 判据 |
  |---|---|
  | 模块内的真实函数 | 直接查该地址所属模块是否仍加载 |
  | `VirtualAlloc` 出来的 thunk（本模块 `_stdcall_to_cdecl` 就是这种） | thunk **不属于任何模块、永远可读**，查不到 ⇒ 改查「注册那一刻调用点的返回地址」所属模块 |

  判错的表现是「**所有回调都被丢弃、页面事件全哑**」，不是崩溃 —— 所以必须实测验证。

---

## 3. 改造内容（精确到行）

| 文件 | 改动 |
|---|---|
| `dll.e.txt` | 252 处 `"main\corn\node.dll"` → `"main\corn\MbKit.dll"`（仅此一项，BOM/CRLF 原样保留） |
| 其余全部文件 | **零改动**（`class/*`、`const`、`global`、`struct`、`project.etprj` 一字未动） |

生成的 `miniblink_MbKit.e` 里 `MbKit.dll` 引用 **252**、`node.dll` 残留 **0**。

---

## 4. 部署（3 步）

1. 把 `MbKit.dll` 放到**依赖目录** `main\corn\`（与 `node.dll` 同目录）。
   → MbKit 找内核的顺序是「自己所在目录 → 当前目录\main\corn → exe 目录上溯」，
   同目录是首选，所以放一起最稳。
2. 用 `miniblink_MbKit.e` **重新编译**出 `miniblink.ec`，替换项目里原来那份。
   （也可直接用交付的 `miniblink_MbKit.ec`。）
3. 完事。业务侧代码、调用方式、`main\corn` 路径约定**都不需要改**。

> ⚠️ 旧 `.ec` 仍指向 node.dll，**必须替换**，否则还是老行为。

卸载/重载的流程：业务插件关闭窗口 → 卸载插件 DLL → 重新加载 → 新的
`wkeInitialize`/`wkeCreateWebView` 照常工作（内核已常驻，重复初始化安全）。
**不要求**业务显式调用 `MbKit_DetachAll()`；漏调也只会被静默挡下。

---

## 5. 证据（三层，均可复跑）

### 5.1 C 侧端到端（`re/mb/kit/`，模拟易语言调用方式）

业务模块刻意用易语言的方式调用：按**名字**取地址、按 **cdecl** 调用、回调走
**`VirtualAlloc` thunk**（复刻 `_stdcall_to_cdecl` 的机器码）。

```
bash re/mb/kit/build.sh && MBKIT_SILENT=1 re/mb/kit/nk_test.exe 3 re/mb/kit/nk.log
```

实测结果：

| 步骤 | 结果 |
|---|---|
| 转发桩 + 懒加载 | Attach 成功、页面载入完成 |
| 回调拦截 | title 回调命中 7 次（**说明没有误杀 thunk 形态的回调**） |
| 热重载 3 轮（v1/v2 交替、image-base 不同） | 3/3 全部载入成功 |
| **故障注入：忘记解绑直接卸载，之后持续触发 2.5s** | **进程存活，挡下 27 次野回调** |
| 换新模块继续工作 | 载入完成（内核未被污染） |

### 5.2 易语言侧「编译即检查」

```
python Releases/Elang-AiTools/skills/elang-build/scripts/ecompile.py miniblink_MbKit.e --check --json
→ {"status":"syntax-ok","syntaxOk":true,"eExitCode":0}
```

易语言编译第一阶段是整个工程的静态检查，只有全过才会弹保存框 —— 通过即说明
该 .e 能被编译器完整读取、无语法/语义错误。

### 5.3 真编译出 `.ec`

```
python …/ecompile.py miniblink_MbKit.e miniblink_MbKit.ec --kind ecom --json
→ {"status":"ok","outputSize":204625}
```

产物文件头 `43 4E 57 54 45 50 52 47` = `CNWTEPRG`（易语言工程文件），
转回文本后：`MbKit.dll` 引用 **252**、`node.dll` **0**、`.DLL命令` 总数 **329**（与原版一致）。

> 本轮给 `elang-build` 补了 `--kind ecom`（对应官方 `FN_COMPILE_WINDOWS_ECOM`）——
> 此前只能编 exe/dll，**易模块工程编译不出东西**。宿主库、`elaunch.py`、`ecompile.py` 三处已同步。

---

## 6. 注意事项与已知边界

1. **MbKit 自身不能热更新**（它被刻意 PIN 住）。换来的是「业务层随便重载」。
   换 MbKit 本身仍需重启宿主 —— 这是取舍，不是缺陷。
2. **模块里有 4 条注定取不到的命令**，与改造无关（原模块同样取不到）：
   `JsBindGetter`/`JsBindSetter`（内核实际导出名是小写 `jsBindGetter`/`jsBindSetter`）、
   `wkeNetSetURL`/`wkeGetWebView`（内核未导出）。
   MbKit 对这类缺失项填「安全桩」（调用返回 0、不崩）并记账，可用
   `MbKit_MissingList()` / `MbKit_MissingCount()` 查。**关键 API 缺失才会拒绝启动。**
3. **性能**：每次 API 调用多一条 `test/jne`（槽位已填充后直接 `jmp`），可忽略。
4. **未验证项**：只在 C 侧等价实现了调用方式与 thunk 机制；**真机在易语言业务工程里的
   交互效果（网页渲染、JS 互调、鼠标事件）需要你在实际项目里确认一次**。
5. 诊断接口：`MbKit_CbCount/CbHits/CbBlocked/CbLive`（回调登记与挡下计数）、
   `MbKit_SuppressedCount`（合并计数）、`MbKit_KernelPath`（实际用的内核路径）、
   `MbKit_MissingList`。排障时先看 `CbBlocked` 与 `MissingList`。

---

## 7. 复现与再生成

```bash
# 常驻层（含 252 转发桩 + 22 回调接管）
python src/mbkit/gen_fwd.py                 # 由 fwd_list.json 生成 gen_fwd.{S,def,h}
bash   src/mbkit/build.sh                   # -> src/mbkit/MbKit.dll

# 端到端验证
bash   re/mb/kit/build.sh
MBKIT_SILENT=1 re/mb/kit/nk_test.exe 3 re/mb/kit/nk.log

# 源码改造（在 re/mb/text_kit/ 上）：库名替换 + t2e
#   re/mb/text/      = miniblink.e 的 e2t 产物（原样）
#   re/mb/text_kit/  = 改库名后的文本工程
#   re/mb/out/       = t2e 产物 miniblink_MbKit.e + 编译产物 .ec
```

`src/mbkit/` 文件职责：

| 文件 | 作用 |
|---|---|
| `mbkit_fwd.c` | 懒加载 node.dll、填槽位、路径解析、缺失项安全桩 |
| `mbkit_cb.c` | 22 个回调注册类的接管、登记表、两路存活判据、解绑 |
| `mbkit.c` | 常驻层与高层接口（view 管理、自 PIN、事件转发） |
| `gen_fwd.py` | 生成器：由清单产出转发桩/导出表/名字表 |
| `fwd_list.json` | 输入清单（从 `dll.e.txt` 解析而来，252 条） |
