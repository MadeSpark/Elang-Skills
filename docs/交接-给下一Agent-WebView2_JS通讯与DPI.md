# 交接：MbKit WebView2、JS 通讯与 DPI

更新时间：2026-10-13

## 目标

修复实际易语言宿主中 AILI·LIFE 的 WebView2 页面问题：

1. `wkeJsBindFunction` / `jsArg*` 缺失导致页面调用 `e_notify()` 不能进入易语言。
2. `wkeRunJsByFrame` 缺失或不完整导致易语言回调中的 JS 注入不可靠。
3. 中文文件路径通过 WebView2 `Navigate()` 时页面停在 `about:blank`。
4. 宿主进程被 AppCompat 设置为 `DPIUNAWARE`，WebUI 页面模糊；不能修改主窗口的进程级 DPI 行为。
5. 固化宿主 SetWindowsHookEx/UnhookWindowsHookEx 代理的生命周期修复。

## 真实链路

- 宿主 exe：`D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\小栗子虚拟机.exe`
- 宿主源码：`D:\DevelopmentFolder\插件开发目录\E\插件\框架-栗子兼容计划\小栗子虚拟机_AI工作区\栗子虚拟机[3.1.x].e`
- AILI 插件源码：`D:\DevelopmentFolder\插件开发目录\E\插件\AILI·LIFE\Skill测试\AILI·LIFE_识图版_决策编号_注释版_卡死修复 - 複製.e`
- 运行插件：`D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\main\plugin\AILI·LIFE.dll`
- 运行内核文件：`D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\MbKit144.dll`
- MbKit C/C++ 工程：`C:\Users\MadeSpark\Desktop\测试\src\mbkit`
- 模块文本工程：`C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink_源码文本`
- 模块源码/产物：`C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink.e`、`miniblink.ec`
- 临时 AILI 文本工程：`C:\Users\MadeSpark\AppData\Local\Temp\opencode\aili_plugin_src2`

当前宿主 PID：`49712`，窗口标题 `Main`；适配器 PID：`32324`，不要终止适配器。PID `103228`、`104248` 是零线程旧记录，PID `126968` 也是零线程旧记录。

## 已完成且有证据的改动

### JS 桥接

新增文件：

- `src/mbkit/mbk_jsbridge.inl`

修改：

- `src/mbkit/mbk_webview.cpp`
- `src/mbkit/gen_webview.py`

实现内容：

- 注册 `jsBindFunction` / `wkeJsBindFunction`。
- 用 WebView2 `AddHostObjectToScript` 暴露同步 JS 函数。
- 实现 `jsArg`、`jsArgCount`、`jsArgType`、`jsTypeOf`、`jsToInt`、`jsToBoolean`、`jsToDouble`、`jsToFloat`。
- 实现 `wkeRunJsByFrame`，转到现有同步 `ExecuteScript` 链路。
- JS 参数支持字符串、布尔、数值、空值；返回值支持 `jsValue` 字符串/数值/布尔/NULL。
- 在原生 COM 回调返回后再投递文档就绪回调，避免回调内同步 ExecuteScript 造成 COM 重入限制。
- 使用单调 view ID，避免窗口销毁后旧回调命中复用地址。

独立真实 WebView2 回归程序：`src/mbkit/test_jsbridge.c`。

三轮结果均为：

```text
cycle=1 roundtrip=PASS calls=1 ready=1 errors=0
cycle=2 roundtrip=PASS calls=2 ready=2 errors=0
cycle=3 roundtrip=PASS calls=3 ready=3 errors=0
```

### 中文本地文件 URI

`mbkwv_wkeLoadFileW` 原来把中文 Windows 路径直接拼成 `file:///` URI。现在先转 UTF-8，并对非 URI 安全字符做 `%XX` 编码，再交给 WebView2 `Navigate()`。这是必要修复，AILI 实际缓存路径包含中文：

`D:\DevelopmentFolder\插件开发目录\E\插件\框架-爱丽\main\data\plugin\AILI·LIFE\WebUI缓存\表情包管理.html`

### 宿主 hook 修复

正式宿主 exe 当前哈希：

`A03F5CFC70703D91EBD41E371390EB892753A0464E3D0507006ADD69C0089442`

这是已经部署的 hook 候选版本，大小 9981952 字节。宿主源码当前已恢复为原始源码，哈希：

`64464D34713F26145F27489941564D9ADB45338836A83F1D5A0124F050E17D2B`

临时 hook 源码在：

`C:\Users\MadeSpark\AppData\Local\Temp\opencode\host_legacy_probe.e`

正式源码备份在：

`C:\Users\MadeSpark\AppData\Local\Temp\opencode\栗子虚拟机[3.1.x].before_hook.e`

两者当前都曾验证过 hook 生命周期修复。正式源码若要重新编译，必须把临时 hook 源码中的改动重新合入，然后用新输出文件名编译，避免易语言“另存为”旧文件的阻塞。

修复重点：代理不再按 `lpfn/idHook/tid` 复用历史 hook 句柄；每次真实调用 `SetWindowsHookExA/W`；新增 `UnhookWindowsHookEx` 代理，成功后从台账删除句柄。

## 当前产物状态

源码 MbKit：

- 文件：`src/mbkit/MbKit.dll`
- 大小：701952 字节
- SHA256：`F4AB3619EE030A5587A4B57736EC9A1CDB21BA49198504706A51931C298BE32A`

模块：

- `out_mbkit/miniblink.ec`
- 大小：919250 字节
- 最近一次 ecompile ecom 成功。
- `miniblink_源码文本/resource/_常量28010234.image.dat` 已从最新 MbKit.dll 更新。

插件候选：

- `C:\Users\MadeSpark\AppData\Local\Temp\opencode\aili_new_bridge.dll`
- 大小：19243008 字节
- SHA256：`771DC0514A5FC71A0BC57392C4D22B4E09889B2821D3D85A8A1E47336B1A65CC`
- 当前已复制到生产 `main/plugin/AILI·LIFE.dll`，目标哈希相同。
- 由 `aili_plugin_src2` t2e 后用 static_dll 成功编译。

当前生产 `MbKit144.dll` 的 Get-FileHash 结果仍是旧版本：

- 旧 SHA256：`214B30794FC911017FD9117F6BF16223BCFADA65F5C2F36945A680CAD6ABDCFE`
- 旧大小：652800 字节

虽然曾尝试复制新 DLL，但复制/恢复期间存在文件锁和插件释放逻辑覆盖；交接后的第一项必须确认 `MbKit144.dll` 是否真实为新 DLL。不能只看 `src/mbkit/MbKit.dll`。

## DPI 结论

曾实现过 `mbk_dpi.inl`，创建 per-monitor-aware 独立 WebView 承载窗口以绕开宿主的进程级 `DPIUNAWARE`。真实宿主测试中该方案导致 WebView 子窗口消失/位置不稳定，已撤回并删除该文件。

目前源码没有 DPI surface。不要直接恢复旧实现。下一 agent 应优先采用可观测、可回滚的方案：

1. 先确认 WebView2 controller 实际父窗口、Bounds、当前 DPI awareness context。
2. 检查 `ICoreWebView2Controller3` 的 `RasterizationScale` / `BoundsMode` 是否适合当前 WebView2 SDK。
3. 任何 DPI 改动必须只作用于 WebView controller，不能调用进程级 `SetProcessDpiAwareness`，不能修改现有 HKCU AppCompatFlags：
   `~ GDIDPISCALING DPIUNAWARE`。
4. 每次先做独立窗口回归，再在真实宿主中截图和探测 `Chrome_WidgetWin_*`。

## 最近真实宿主证据

用新版插件启动 PID `49712` 后：

- 宿主主窗口可响应。
- 页面区域出现了灰色 WebView 区域，窗口标题栏/侧栏仍正常。
- 当前 `probe_windows.py` 没有列出 `Chrome_WidgetWin_*`，说明父子窗口关系或 WebView 创建仍有问题，不能宣称实际宿主验收通过。
- 强制把 Main 置顶截图时，宿主 WebView 区域仍为灰色；其背后桌面出现大字号粉色网页文字，提示当前窗口/页面可能存在渲染层或窗口层级错位。
- 此次没有完成真实 `e_notify -> 易语言 -> JS` 交互验收。

独立 test_jsbridge 通过不等于真实 AILI 页面通过。

## 下一步建议

1. 结束测试宿主 PID `49712`，只结束该 PID；不要使用 `taskkill /T`，不要结束 `XiaoLiZi_Adapter.exe`。
2. 确认生产 `MbKit144.dll` 的存在、大小、SHA256；若需替换，先备份，确认没有宿主进程锁定，再复制最新 `src/mbkit/MbKit.dll`。
3. 检查实际进程加载的模块。PowerShell `Get-Process.Modules` 可能因权限不显示，可使用现有 `.workbuddy` 取证脚本或新增 PSAPI 查询，目标是确认 MbKit 映像大小和路径。
4. 在不改 DPI 的情况下，用当前 URI 编码 DLL + 当前插件重新启动。先确认 `Chrome_WidgetWin_0/1` 存在，再判断页面内容。
5. 若仍灰屏，检查 `mbk_webview.cpp` 的 `CtlDone::Invoke`、`wkeSetHandle`、`apply_state`、`wkeResize` 和 `Navigate` 返回/诊断日志；不要先恢复 DPI surface。
6. 真实页面通过后，再单独设计 DPI controller 方案，并保留前后截图和窗口矩形证据。
7. 需要重新编译宿主时，把 hook 修复重新合入正式 `.e`；当前正式宿主源码本身是原始版本，生产 exe 才是 hook 候选。

## 构建命令

MbKit：

```powershell
$env:PATH='C:\msys64\usr\bin;C:\msys64\mingw32\bin;'+$env:PATH
& 'C:\msys64\usr\bin\bash.exe' ./build.sh
```

模块：

```powershell
$env:PYTHONIOENCODING='utf-8'
& 'C:\Users\MadeSpark\.config\opencode\skills\qaq-etool-cli\assets\QAQ-ETool.exe' -mode t2e -src 'C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink_源码文本' -dst 'C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink.e'
python 'C:\Users\MadeSpark\.config\opencode\skills\elang-build\scripts\ecompile.py' --kind ecom --no-require-versioninfo 'C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink.e' 'C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink.ec'
```

插件：

```powershell
& 'C:\Users\MadeSpark\.config\opencode\skills\qaq-etool-cli\assets\QAQ-ETool.exe' -mode t2e -src 'C:\Users\MadeSpark\AppData\Local\Temp\opencode\aili_plugin_src2' -dst 'C:\Users\MadeSpark\AppData\Local\Temp\opencode\aili_bridge.e'
python 'C:\Users\MadeSpark\.config\opencode\skills\elang-build\scripts\ecompile.py' --kind static_dll --no-require-versioninfo 'C:\Users\MadeSpark\AppData\Local\Temp\opencode\aili_bridge.e' 'C:\Users\MadeSpark\AppData\Local\Temp\opencode\aili_new_bridge.dll'
```

QAQ 交付标准：t2e/ecompile 必须退出码 0；t2e 必须有 `SUCC:`；不得有 `[错误]`；`.ec` 只能由 ecompile 生成，不能把 t2e 输出改名为 `.ec`。

## 注意事项

- `miniblink.desc.json` 当前应指向 `C:\Users\MadeSpark\Desktop\测试\out_mbkit\miniblink.ec`，不能指向临时 `legacy_probe.ec`。
- `.e/.ec` 读取和编辑必须经 QAQ e2t/t2e；文本必须 UTF-8 BOM + CRLF。
- 所有删除操作先 `Remove-Item -WhatIf`。
- 不要依据宿主日志单独判定卡死；日志线程可能挂。优先使用窗口响应、线程等待链、异常和模块映像证据。
- 不要删除原始备份：`AILI-LIFE.before_bridge.dll`、`MbKit144.before_bridge.dll`、`栗子虚拟机[3.1.x].before_hook.e`。
- 当前仓库有大量既有未提交文件和构建产物，修改前先看 `git status`，不要清理或覆盖无关工作。
