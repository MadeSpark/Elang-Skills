# 实测：换 node.dll 能否解决「强制 Node 内核卡死」（2026-10-16）

## 结论

**能。** 把 `main\corn\node.dll` 从 **2021-03-29** 换成 **2023-09-23（x86）** 之后，
强制 Node 内核**跑通了**，宿主不再卡死。

| 观察项 | 2021-03-29（原现役） | 2023-09-23（x86） |
|---|---|---|
| `wkeCreateWebView` 直接调用 | 不卡 | 不卡 |
| **经 MbKit 真实路径** | **卡死在 `表情包管理.绑定()`** | **跑通** |
| 插件日志进度 | 止于 `node内核设置成功` | 继续到 `开始载入表情包管理页面` → `页面文档已就绪` |
| JS ↔ 易语言 双向通信 | 无 | **通**（`[Console] 状态更新: 读取成功，共 25 张`） |
| 窗口 `responsive` | **415 / 417 卡死** | **419 / 419 全部正常** |
| 画面 | 灰屏 | WebUI 完整渲染（25 条结果 + 缩略图） |

截图：`out_mbkit/ab_test/evidence/node2023_final.png`

---

## 一个反直觉但很关键的发现

我写了个最小复现器 `out_mbkit/ab_test/nodetest.c`（源码 + 已编译 `nodetest.exe`），
只做 MbKit 在强制 node 下做的那几件事：

```
LoadLibrary(node.dll)
  → GetProcAddress(wkeInitialize / wkeCreateWebView / wkeSetHandle / wkeResize / ...)
  → wkeInitialize()
  → CreateWindow(host)
  → wkeCreateWebView()      ← 疑点
  → wkeSetHandle / wkeResize / wkeLoadHTMLW
  → 抽 20 秒消息
```

**两个版本都「不卡」，都返回了有效 view**：

```
[step1] LoadLibrary -> 7a150000 err=0                     ← 2021 版
[step3] wkeInitialize returned; IsInitialize now=1
[step4] wkeCreateWebView returned 03a0ef80
RESULT: OK (no hang; view=03a0ef80)
```
```
[step1] LoadLibrary -> 788b0000 err=0                     ← 2023 版
[step4] wkeCreateWebView returned 017e9aa8
RESULT: OK (no hang; view=017e9aa8)
```

**所以卡死不是某个 dll 的孤立 bug。** 光看 miniblink 的导出表、光单独调它的 API，
都复现不出来 —— 这与之前排查时「找不到根因」是吻合的。
它是 **「旧版 miniblink × MbKit 的调用方式」这个组合**才触发的；
新版把触发条件消掉了。

⇒ 这同时说明：**换 dll 是有效的解法，但不是「因为旧 dll 有 bug」这种简单解释**，
所以不能推广成「以后遇到问题换 dll 就行」。

---

## 静态对比（换之前先确认 ABI 兼容）

`out_mbkit/ab_test/node_dll_compare.py` 的输出（纯静态读 PE，不加载 dll）：

| | 2021-03-29 x86 | 2023-09-23 x86 | 2023-09-23 x64 | 2023-11-15 x86 |
|---|---|---|---|---|
| 大小 | 33,600,512 | 34,361,280 | 41,440,192 | 35,473,920 |
| 架构 | x86 | x86 | x64（**不可用**） | x86 |
| 导出总数 | 9,611 | 18,262 | 17,959 | 18,059 |
| `wke*` | 306 | 312 | 312 | 312 |

**21 个 MbKit 关键导出跨版本全部存在**（`wkeInitialize`/`wkeCreateWebView`/`wkeSetHandle`/
`wkeLoadHtmlWithBaseUrl`/`wkeOnPaintUpdated`/… 逐个核对 YES）。
2023 版相对 2021 版是**严格的超集**：`仅旧版有的 wke* = 0`，
新增 `wkeDownloadByPath`、`wkeOnCaretChanged`、`wkeOnLoadUrlFinish` 等 6 个。
⇒ ABI 层面可直接替换，不需要改 MbKit 的绑定表。

---

## 本机可用的 node.dll 清单

| 大小 | SHA256(前16) | 日期 | 说明 |
|---|---|---|---|
| 34,361,280 | `1B3BA529441555E5` | 2023-09-23 | **x86，本次实测可用**（已部署） |
| 33,600,512 | `B0B10619E699E117` | 2021-03-29 | x86，原现役，**会卡死**（已备份） |
| 35,473,920 | `08EEB6E4F5E80EFF` | 2023-11-15 | x86，**未测**（在 `D:\Ides\e\lib\ExuiKrnln\miniblink\`） |
| 41,440,192 | — | 2023-09-23 | **x64，不可用**（MbKit 是 32 位） |

路径：
- 实测可用：`D:\Ides\炫彩IDE\data\XC-Module\易炫彩\hyiyle-易炫彩Miniblink支持库\x86\node.dll`
- 未测备选：`D:\Ides\e\lib\ExuiKrnln\miniblink\node.dll`
- 原版备份：`out_mbkit\ab_test\backups\node.2021-03-29.dll`

---

## 当前已做的改动

| 项 | 状态 |
|---|---|
| `框架-爱丽\main\corn\node.dll` | **已替换**为 2023-09-23 x86（34,361,280 / `1B3BA529441555E5`） |
| 原 2021 版 | 已备份到 `out_mbkit/ab_test/backups/node.2021-03-29.dll` |
| 生产插件 | **已恢复为 edge 版**（`94b16dcf597f9e6e`），保持已验证基线 |
| Node 强制版插件 | 留档在 `out_mbkit/ab_test/plugin_node.dll`，要试 node 直接覆盖生产插件即可 |

**为什么插件仍留 edge 版**：node 这次只验证了「能起来、能渲染、JS 通、不卡死」，
**没有**做「重载 5 次不崩溃」验收。在跑完那一步之前，生产默认保持 edge。

## 想切到 Node 内核时

1. 换插件（同名覆盖，不要改文件名）：
   ```powershell
   Copy-Item out_mbkit\ab_test\plugin_node.dll `
             'D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\main\plugin\AILI·LIFE.dll' -Force
   ```
   该插件里 `启动Web表情包管理` 的调用是 `WKE设置node路径 (…, , 真)`（第三参 = 真）。
   想改回 edge：把第三参改成 `假` 或缺省，重编插件。
2. 确认 `main\corn\node.dll` 是 2023 版（`1B3BA529…`）。
3. **重启宿主**（内核常驻，换内核只能重启宿主 exe —— 设计行为）。

## 还没验证的

1. **node 模式下的「重载 5 次不崩溃」**：未测。这是 node 能否作为默认内核的**唯一门槛**。
   工具已就绪：`out_mbkit/ab_test/accept.py` + `prebuild_variants.py`。
2. **2023-11-15 那份 x86** 未测，可能更好（若能解决重载崩溃，就换它）。
3. 2021 vs 2023 之间**具体是哪一处改动**消掉了触发条件 —— 未定位（不影响结论，但影响「以后怎么选 dll」）。
