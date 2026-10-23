# vendor/webview2 — WebView2 后端的第三方头文件与 Loader

MbKit 的 **edgewebview（WebView2）内核**（`src/mbkit/mbk_webview.cpp`）只依赖这里三份文件，
不引入 NuGet / 不要求 SDK 安装：

| 文件 | 大小 | 来源 | 用途 |
|---|---|---|---|
| `WebView2.h` | 2,439,514 | Microsoft.Web.WebView2 SDK（`build/native/include/WebView2.h`，API 版本对应 Runtime 154.x） | 全部 COM 接口定义：`ICoreWebView2{,2,3,4}`、`ICoreWebView2Environment{,2,3,4}`、`ICoreWebView2Controller`、各类 `*EventArgs`、`ICoreWebView2Settings{,2}`… |
| `WebView2EnvironmentOptions.h` | 17,120 | 同上 | `ICoreWebView2EnvironmentOptions` 的实现类（本工程当前**未使用**，传 `NULL` 让 Runtime 用默认 options；保留以便将来加参数） |
| `WebView2Loader.dll` | 116,184 | 同上（`build/native/x86/WebView2Loader.dll`） | 运行时加载器：`CreateCoreWebView2EnvironmentWithOptions` 等入口。**x86 版**（MbKit 是 32 位） |

`WebView2Loader.dll` 在 MbKit 里**按需加载**（`loader_load()`，见 `mbk_webview.cpp`）：
先试 `MbKit144.dll` 同目录，再试 `WebView2Loader.dll` 同目录，最后交系统搜索。
加载不到 ⇒ 探测失败 ⇒ 回退 `node.dll`，不影响原内核链路。

## 编译/链接方式

```
mbk_webview.cpp  →  g++ -m32 -std=c++11 -I vendor/webview2 -c
链接              →  g++ -m32 ... -lole32 -loleaut32 -lshlwapi -lshell32
（build.sh 里 WVLIBS 已就位；loader 用 LoadLibrary 动态取，不进导入表）
```

## 两条必须记住的坑（都实测踩过）

### 1. WebView2 返回的是 `LPWSTR`，**不是 `BSTR`**

`get_Uri / get_Source / get_DocumentTitle / get_Message / get_Url / get_UserAgent …`
签名一律是 `HRESULT get_XXX(LPWSTR* value)`（见 `WebView2.h`），内存由 **CoTaskMemAlloc** 分配：

* 长度用 `wcslen()`，**不要 `SysStringLen()`**——后者读指针前 4 字节，
  实测读出 `117459558`（≈224MB）→ `std::wstring::assign` 越界 → `0xC0000005`（崩溃栈
  `NavStartEv::Invoke → _M_replace → _M_mutate → operator new → memcpy`）。
* 释放用 `CoTaskMemFree()`，**不要 `SysFreeString()`**——两个堆不通用，串了会静默破坏堆。

`ExecuteScript` 的回调参数 `LPCWSTR resultAsJson` 是**只读借用**，不许释放。
（唯一例外：`IErrorInfo` / `BSTR` 相关的老接口——那类才是 BSTR。）

### 2. 浏览器子进程继承宿主的完整性级别（IL），低 IL 写不了默认 profile

WebView2 的 `EBWebView` profile 默认在 `%LOCALAPPDATA%\MbKitWebView2`。Windows
**No-Write-Up** 规则禁止「低完整性级别进程」写「中完整性级别目录」：

* 宿主 exe 从带 `Low Mandatory Level` 标签的目录（某些工作区）启动 → 进程 IL = Low →
  浏览器子进程同为 Low → 写 profile 失败 → `CreateCoreWebView2Controller`
  返回 `0x800700AA`（或更早直接崩溃）。
* 同一个 exe 换到普通（Medium）目录 → 一切正常。

因此 `profile_dir()` 的策略是「**谁可写就用谁**」：先建默认目录并用探针文件实测可写，
不可写就退回**宿主 exe 所在目录** `\MbKitWebView2`（它与宿主同标签）。
`MBKIT_WV2_PROFILE` 环境变量可显式覆盖（调试/多实例隔离用）。

> 判据命令（PowerShell 读进程 TokenIntegrityLevel）：`S-1-16-4096`=Low、`S-1-16-8192`=Medium。

## 内核选择（`WKE设置node路径` 第三个参数「强制使用node内核」）

```
强制使用node内核 = 真          → 一定走 node.dll（mbkwv_set_force → bind_all 跳过 edge）
否则 WebView2 Runtime 探测通过  → edgewebview 内核（g_hNode = MbKit.dll 自身，走 gen_thunk）
否则                            → node.dll（原路）
探测到但 controller 启动失败    → 诊断写入 mbkwv_diag()（WKE取WebView2诊断() 可读）
```

## Runtime 探测做了什么

`rt_detect()`：查注册表（`EdgeWebView\WebView2`）+ 常见安装路径找 `msedgewebview2.exe`
的版本目录，**不加载任何 DLL、不启动任何进程**，失败返回 0（`MbKit_HasWebViewEnv()`）。
真正拉起浏览器要等到 `wkeSetHandle`/`载入` 时的 `ensure_env()` + `ensure_ctl()`。
