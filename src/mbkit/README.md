# src/mbkit —— MbKit：miniblink 常驻宿主层

解决「宿主 exe 卸载使用 miniblink 的插件 DLL 后，miniblink 再也初始化不了」的问题。

* 方案与实测结论：`docs/方案-MbKit-miniblink热重载（2026-09-23）.md`
* 易语言接入（`.DLL命令` 声明 + 用法）：`易语言接入说明.md`

---

## 目录

| 文件 | 角色 | 上生产？ |
|---|---|---|
| `mbkit.c` / `mbkit_api.h` | **常驻层本体 + 公共接口** | ✅ 这是交付物 |
| `mbload.h` | 动态装载器（模拟易语言 `.DLL命令` 的调用方式） | ✅ 参考用 |
| `mb_biz.c` | 业务薄层（可热重载）——接入参考实现 | ✅ 参考用 |
| `mb_legacy.c` | 旧做法对照 DLL（**只为复现问题**） | ❌ 别用 |
| `host_reload.c` | 压测宿主（6 个模式） | ❌ 测试用 |
| `build.sh` / `run_tests.sh` | 编译 / 一键验证 | — |

产物（32 位）：`MbKit.dll`（29 个朴素导出）、`biz_v1.dll`、`biz_v2.dll`、
`legacy_v1.dll`、`legacy_v2.dll`、`host_reload.exe`。

---

## 编译

```bash
bash build.sh
```

* 必须 **32 位 gcc**（与 `e.exe` 同架构）。
* ⚠️ 必须把 gcc 所在目录**前置进 `PATH`**，否则 gcc 找不到自己的 `cc1`/`as`/`ld`，
  表现是 **exit 1 且没有任何错误输出**。
* 可用环境变量覆盖：`GCC` / `MINGW_BIN` / `PY`（跑导出名核对用）。
* 关键编译参数（改之前先看注释，都有实测原因）：
  * `-Wl,--kill-at` —— 去掉 stdcall 的 `@N` 修饰，导出名必须朴素（易语言 `.DLL命令` 依赖朴素名）。
  * `-Wl,--image-base,0x62xxxxxx` —— 给各测试 DLL 不同基址，让「重载后落到不同地址」
    这一最坏情况**确定性**发生（否则 Windows 会映回同一地址，野指针可能"碰巧能用"）。
  * `-fexec-charset=GBK` —— 源码中文转 GBK 字节，日志在中文 Windows 控制台/记事本里可直接看。
  * 链接时 `-l` 必须排在源文件**之后**。

## 验证

```bash
bash run_tests.sh            # 证据落到 re/mbkit_run/evidence/
```

| 模式 | 期望 |
|---|---|
| `wildcard` | 3 / 0（自证「卸载后回调地址失效」可被检测） |
| `ansi` | 3 / 0（GBK 中文往返 + JS） |
| `paintprobe` | 四配置 paints 全 0（信息性：绘屏回调根本不触发） |
| `mbkit 5` | **20 / 0，退出码 0**，每轮 `kernel-init-count=1` |
| `fault` | **8 / 0**，`suppressed ≥ 1` |
| `legacy 2` | **崩溃，退出码 139**，停在第一条窗口消息 ← 这是预期结果 |

⚠️ 对照组的 **139 = SIGSEGV 是预期结果**，不是脚本失败。

---

## 设计要点（改代码前必读）

1. **常驻层自我 PIN**：`GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_PIN)`。
   业务 DLL 怎么卸载都动不了它。代价：MbKit 自身不能热更新（刻意的）。
2. **内核由本层加载并 PIN**：路径解析顺序见
   `resolve_node_path()`，**包含 `<当前工作目录>\main\corn\node.dll`**，
   与用户现有 `.DLL命令` 相对路径的行为保持一致 —— **不改用户的依赖布局约定**。
3. **唯一进入内核的回调都定义在本模块内**（`on_paint` / `on_load_finish`），
   内核手里永远不会攥着别的模块的代码地址。
4. **转发前做三重存活校验**（`view_cb_alive`）：地址是否还属于已加载模块 / 模块句柄是否一致 /
   模块全路径是否一致。挡下时 `G.suppressed++`（`MbKit_SuppressedCount()` 可读）。
5. **本层不在窗口上留任何过程**：窗口交互交给内核自己（`wkeSetHandle`），
   避免复刻旧做法的「子类化残留」。
6. **热重载契约 = `MbKit_DetachAll()`**：注销回调 + 解绑窗口 + 销毁 view；
   **不** finalize 内核（重载才够轻）。

## 实测踩到的坑（别重复踩）

* `wkeGetVersionString()` 必须在 `wkeInitialize()` **之后**调用，否则直接崩。
* `wkeFinalize()` 之后 `wkeIsInitialize()` **仍返回 1** —— 标志位根本没清，
  别用 `wkeIsInitialize` 判断「能不能重来」。
* `wkeOnPaintUpdated` 在 view 绑定窗口后**从不触发**（四配置实测全 0），
  不要拿它当渲染入口、也不要拿"它没崩"当成功判据。
* `wkeSetHandle` 的第二个参数是 `HWND`（不是 offset）；offset 走 `wkeSetHandleOffset`。
* 32 位 gcc + `-shared` 时，MinGW 默认 `--enable-auto-image-base` 会把输出路径哈希成
  映像基址 —— 输出名写成相对名（别带目录）才能得到稳定基址。
