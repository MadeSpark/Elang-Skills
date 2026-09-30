# e2txt 输出格式规格（e2t，`-level 2 -enc UTF-8`）

用途：QAQ-ETool 的文本生成器 / 解析器照此实现，目标是与 e2txt 产物**逐字节一致**。
依据：`tests/baseline/` 下两个夹具的 e2txt 实际产物（只读分析，未运行 e2txt）。与旧文档冲突处以产物为准，见 §10。

## 0. 记号与样本

| 记号 | 路径（相对 `QAQ-ETool/tests/baseline/`） | 生成命令（`report.json`） |
|---|---|---|
| `W` | `窗口属性无法解析/en/` | `-mode e2t -src …\测试源码\窗口属性无法解析.e -dst …\en -level 2 -enc UTF-8` |
| `Wc` | `窗口属性无法解析/cn/` | 同上 + `-ns 2` |
| `N` | `新增 文字文件/en/` | 同 W |
| `Nc` | `新增 文字文件/cn/` | 同 N + `-ns 2` |

引用写法：`W/class/CQ应用_兼容.class.e.txt:10` = 该文件第 10 行（行号按 CRLF 分行，BOM 不计）。
「未确认」= 样本中没有出现或无法判定；「推测」= 有迹象但样本不足以证实。

样本局限（生成器作者须知）：
- 模块（`.ec`）是编译产物，没有源码体，模块里所有子程序体都只有一行空语句（§5.6）。真正有语句的只有 W 的 4 个程序集和 N 的 1 个窗口程序集，共 877 行子程序体。
- 因此 `.如果/.否则/.如果结束`、`.判断循环首/尾`、`.循环判断首/尾`、`.变量循环首/尾`、`－ × ÷ ％ ＜ ≤ 或 非`、小数/日期字面量、长文本常量、屏蔽行**都未在样本中出现**，相关条目标「未确认」。

---

## 1. 编码与通用字节规则

| 文件 | 编码 | BOM | 行分隔 | 文件末尾 |
|---|---|---|---|---|
| `*.e.txt` | UTF-8 | 有 `EF BB BF` | CRLF | 见 §5.1 / §6（以 `\r\n` 或 `\r\n\r\n` 结尾，依文件类型） |
| `*.json`（含 `ec.init` 的 `{}`） | UTF-8 | 有 | CRLF | **无**结尾换行，最后一个字节是 `}` 或 `]` |
| `order.list.txt` | UTF-8 | 有 | CRLF | **无**结尾换行 |
| `project.etprj` | **GBK** | **无** | CRLF | **无**结尾换行 |
| `*.image.dat`、`icon.ico` | 二进制原样 | — | — | — |

- 全部 208 个 `*.e.txt`（W+N 的 en）：BOM 齐全，裸 LF 为 0，无 Tab 缩进。
- **只能按 `\r\n` 分行**。`W/module/精易模块/const.e.txt:24`（常量 `HtmlEntitiesTable`）的值里含字面 TAB（0x09）和**裸 CR（0x0D）**，该文件共 1647 个裸 CR，全部在常量值内。Python 的 `str.splitlines()` 或 `open()` 默认换行转换都会把它拆错，必须 `open(..., newline='')` + `split('\r\n')`。
- 行尾空白**原样保留**，不做 rstrip。256 行非空行带尾随空格，均来自备注原文，如 `W/module/叮叮当当可可爱爱/dll.e.txt:95` `…, , BOOL␠`、`W/module/精易模块/class/正则表达式类.class.e.txt:14` `.子程序 查看, , 公开,␠␠`（备注是一个空格）、`W/module/精易模块/dll.e.txt:505` 备注是一个全角空格 `　`。

---

## 2. 目录结构

### 2.1 en ⇄ cn 完整对照（逐项比对两棵树得出）

W：en 144 个文件 / cn 144 个；N：en 132 / cn 132。按下表映射后**一一对应、无多无少**。

| 英文布局（不传 `-ns`） | 中文布局（`-ns 2`） | 备注 |
|---|---|---|
| `class/` | `代码/` | |
| `class/<程序集名>.static.e.txt` | `代码/<程序集名>.static.e.txt` | 后缀不翻译 |
| `class/<类名>.class.e.txt` | `代码/<类名>.class.e.txt` | 后缀不翻译 |
| `class/<窗口程序集名>.form.e.txt` | `代码/<窗口程序集名>.form.e.txt` | 后缀不翻译 |
| `class/order.list.txt` | `代码/排序.list.txt` | |
| `class/dirs.desc.json` | `代码/目录.desc.json` | |
| `config/` | `配置/` | |
| `config/lib.config.json` | `配置/支持库.config.json` | |
| `config/system.config.json` | `配置/系统.config.json` | |
| `config/user.config.json` | `配置/用户.config.json` | |
| `config/ec.init.config.json` | `配置/模块.初始化.config.json` | |
| `config/epack.files.config.json` | `配置/易包.文件.config.json` | |
| `form/` | `窗口/` | |
| `form/<窗口名>.form.json` | `窗口/<窗口名>.form.json` | 后缀不翻译 |
| `resource/` | `资源/` | |
| `resource/resource.desc.json` | `资源/资源.desc.json` | |
| `resource/<资源名>.image.dat` | `资源/<资源名>.image.dat` | 后缀不翻译 |
| `module/` | `模块/` | |
| `module/<模块名>.desc.json` | `模块/<模块名>.desc.json` | |
| `module/<模块名>/class/…` | `模块/<模块名>/代码/…` | 文件名规则同 `class/` |
| `module/<模块名>/const.e.txt` | `模块/<模块名>/常量.e.txt` | |
| `module/<模块名>/dll.e.txt` | `模块/<模块名>/DLL.e.txt` | 注意是大写 `DLL` |
| `module/<模块名>/struct.e.txt` | `模块/<模块名>/自定义类型.e.txt` | |
| `module/<模块名>/global.e.txt` | `模块/<模块名>/全局变量.e.txt` | |
| `const.e.txt` | `常量.e.txt` | |
| `dll.e.txt` | `DLL.e.txt` | |
| `struct.e.txt` | `自定义类型.e.txt` | |
| `global.e.txt` | `全局变量.e.txt` | |
| `icon.ico` | `图标.ico` | |
| `project.etprj` | `项目.etprj` | 唯一内容有差异的文件，见 §4 |

模块内部**没有** `config/`、`form/`、`resource/`、`order.list.txt`、`dirs.desc.json`、`icon.ico`（W、N 所有模块目录）。

### 2.2 代码文件后缀的判定

后缀对应 e2txt 日志里的三种写出类型（`窗口属性无法解析/logs/e2t.en.stdout.txt:11-104`）：
- `【程序集】` → `.static.e.txt`（例：`主程序集`、`__HIDDEN_TEMP_MOD__`）
- `【类】` → `.class.e.txt`（例：`CQ应用_兼容`、`corn`）
- `【窗口程序集】` → `.form.e.txt`（例：`窗口程序集_选择账号`）

窗口程序集与窗口的关联：`form/<窗口名>.form.json` 的 `"Class"` 字段 = 窗口程序集名（`W/form/设置窗口.form.json:2`）。窗口文件名用窗口 `Name`（`设置窗口`），代码文件名用程序集名（`窗口程序集_设置窗口`）。

### 2.3 各文件何时生成（`-level 2`）

| 文件 | 顶层 | 模块目录内 |
|---|---|---|
| `const/dll/struct/global.e.txt` | **总是生成**，为空时是 16 字节 | **为空就不生成**（`W/module/miniblink/` 只有 const+struct；`W/module/小栗子sdk/` 同样没有 dll、global） |
| `icon.ico` | 总是生成；无图标时 0 字节（W、N 均为 0 字节） | 不生成 |
| `class/dirs.desc.json` | 总是（两个样本内容相同，31 字节） | 不生成 |
| `class/order.list.txt` | 总是 | 不生成 |
| `config/*.json` 五个 | 总是（`ec.init` 为 `{}`、`epack.files` 为空数组时仍生成） | 不生成 |
| `form/*.form.json` | 每个窗口一个 | — |
| `resource/*` | 两个样本都有 19 个资源。**无资源时是否生成 `resource/` 与空 `resource.desc.json`：未确认** | — |
| `module/*` | 有模块引用时。无模块时是否生成空 `module/`：未确认 | — |

**空 `*.e.txt` 的确切字节（16 字节）**，`W/const.e.txt`、`W/dll.e.txt`、`W/struct.e.txt`、`N/global.e.txt` 等 md5 全部为 `a14fe73b…`：

```
EF BB BF  2E E7 89 88 E6 9C AC 20 32  0D 0A  0D 0A
BOM       .  版        本       ␠  2   CRLF   CRLF
```

即 `".版本 2" CRLF` + 一个空行 `CRLF`。这正是所有 `*.e.txt` 的通用文件头（§5.1），只是后面没有内容。

---

## 3. JSON 文件

### 3.1 通用序列化规则（已逐文件验证）

对 W、N 中**全部** JSON 文件和 `project.etprj` 做了反序列化再序列化，以下 Python 调用的输出与原文件**逐字符一致**（无一例外）：

```python
body = json.dumps(obj, ensure_ascii=False, indent=4, sort_keys=True).replace('\n', '\r\n')
data = b'\xef\xbb\xbf' + body.encode('utf-8')   # *.json
data = body.encode('gbk')                        # project.etprj（无 BOM）
# 结尾不追加换行
```

由此得到的规则：
- 缩进 4 个空格；`key: value` 冒号后一个空格；数组/对象每项一行。
- 对象的键**按码点升序**排序（全部文件检查过都是有序的）。例：`"E2TXT-EFile"` 排在 `"Encoding"` 前（`'2' < 'n'`）；`"FMTID_MUSIC"` 排在 `"FMTID_Misc"` 前（大写在前）。
- 数组保持原顺序（支持库顺序、控件顺序、`Rename` 列表顺序）。
- 空数组写成 `[]`（`W/class/dirs.desc.json:2`），空对象写成 `{}`（`W/config/ec.init.config.json` 全文就是 BOM+`{}`，5 字节）。
- 非 ASCII 直接输出 UTF-8，不转 `\uXXXX`。样本中出现过的转义只有 `\\`（21 处，路径）、`\r`、`\n`（`W/config/user.config.json:10`）。其它控制字符（如 `\t`）的转义方式：未确认，按 JSON 标准推测为 `\t`。
- 布尔 `true/false`，数字都是十进制整数；样本无浮点。
- 二进制字节串一律是**大写十六进制、无分隔**的字符串（`ExtensionData`、`Cursor`、`UnknownBefore*`）。
- 支持库 GUID **不做规整**，原样照抄 `.e` 里的字符串（可能小写、大写、带花括号，见 §3.4）。

### 3.2 `class/dirs.desc.json`

两个样本完全相同（31 字节）：

```json
{
    "ExpandedDirs": []
}
```

含义推测为 IDE 程序集树的展开状态；非空时的元素格式：未确认。

### 3.3 `class/order.list.txt`（非 JSON）

- UTF-8 BOM，每行一个程序集名，CRLF 分隔，**最后一行后面没有 CRLF**（`W/class/order.list.txt` 以 `…系统` 字节结尾）。
- 内容是 e2t **写出所有程序集的顺序**，包含**模块里的程序集**，与日志 `e2t.en.stdout.txt:11-104` 的写出顺序一致：
  - W：`主程序集`、`corn`、`文本代码`、`应用信息`、`小栗子文本代码解析类`、`__HIDDEN_TEMP_MOD__`、`窗口程序集_选择账号`、`窗口程序集_设置窗口`、`CQ应用_兼容`、…（`W/class/order.list.txt:1-9`）。主工程和模块的程序集交错排列，不是「主工程在前」。
  - N：第 1 行是 `窗口程序集_启动窗口`，接着是 miniblink 的 `MB浏览框`、`wke文件系统`、`__HIDDEN_TEMP_MOD__`，再往后是精易模块的类（`N/class/order.list.txt:1-5`）。
- **按名字去重，保留首次出现**：每个模块都有一个 `__HIDDEN_TEMP_MOD__`，但列表里只出现一次。W 共 4+3+13+5+69=94 个程序集 → 91 行；N 共 1+3+13+69=86 → 84 行。

### 3.4 `config/lib.config.json`

顶层是**数组**，顺序同 `.e` 里的支持库表（krnln 在首位）。元素键（已排序）：`CmdCount, Guid, Key, MaxRefConstPos, MaxRefObjectPos, Name, Version{Major, Minor}`。

`W/config/lib.config.json`（截断）：

```json
[
    {
        "CmdCount": 633,
        "Guid": "d09f2340818511d396f6aaf844c7e325",
        "Key": "krnln",
        "MaxRefConstPos": 22,
        "MaxRefObjectPos": 49,
        "Name": "系统核心支持库",
        "Version": {
            "Major": 5,
            "Minor": 7
        }
    },
    {
        "CmdCount": 5,
        "Guid": "A512548E76954B6E92C21055517615B0",
        "Key": "spec",
        ...
    },
    ...
    {
        "CmdCount": 0,
        "Guid": "{E5E6A8E356A843bd94609DDD678BD6D8}",
        "Key": "CodeStyleEdit",
        ...
    }
]
```

GUID 大小写和花括号都是原样（`W/config/lib.config.json:4,16,40`）。`CmdCount`/`MaxRef*` 随工程变化（N 的 krnln `CmdCount` 是 53，`MaxRefConstPos` 是 0，`N/config/lib.config.json:3,6`），不是库的固定属性。

### 3.5 `config/system.config.json`

```json
{
    "Lang": 1,
    "ProjectType": 2,
    "ProjectVersion": {
        "Major": 1,
        "Minor": 7
    },
    "Type": 1,
    "Version": {
        "Major": 5,
        "Minor": 6
    }
}
```

W 的 `ProjectType` 为 2（W 是 DLL 插件工程），N 为 0（普通窗口程序）。推测 0=Windows 窗口程序、2=Windows DLL，1 大概率是控制台，未确认。

### 3.6 `config/user.config.json`

键（固定 17 个，已排序）：`Address, Author, BuilderVersion{Major,Minor}, CompilePlugins, Copyright, Description, Email, ExportPublicClassMethod, FaxNumber, Homepage, Name, ReleaseVersion{Major,Minor}, TelephoneNumber, WriteVersion, ZipCode`。

`W/config/user.config.json:10`：`"Description": "小栗子应用\r\n@备注:\r\n我的第一个小栗子应用"`，多行文本的换行转义成 `\r\n`。N 中所有文本字段都是 `""`。

### 3.7 `config/ec.init.config.json`、`config/epack.files.config.json`

- `ec.init`：两个样本都是 `{}`（BOM + 2 字节，无换行）。非空格式：未确认。
- `epack.files`：`{\r\n    "FileNames": []\r\n}`（28 字节）。非空格式：未确认。

### 3.8 `form/<窗口名>.form.json`

顶层键：`Class, Comment, Controls, Id, Menus, Name, UnknownAfterId, UnknownBeforeClass`。
控件键：`Children, Comment, Cursor, DataType, Disable, ExtensionData, Height, Left, Name, Tag, Top, UnknownBeforeEvents, UnknownBeforeExtensionData, UnknownBeforeLeft, UnknownBeforeName, UnknownBeforeParent, UnknownBeforeVisible, Visible, Width`。

`W/form/设置窗口.form.json`（截断）：

```json
{
    "Class": "窗口程序集_设置窗口",
    "Comment": "",
    "Controls": [
        {
            "Children": [],
            "Comment": "",
            "Cursor": "00000000",
            "DataType": "窗口",
            "Disable": false,
            "ExtensionData": "060000000100000001000000000000FF0100…0E0000006D696E69626C696E6B5F7465737400000000",
            "Height": 547,
            "Left": 50,
            "Name": "",
            "Tag": "",
            "Top": 50,
            "UnknownBeforeEvents": 0,
            "UnknownBeforeExtensionData": "0000000000000000000000000000000000000000",
            "UnknownBeforeLeft": 99427280,
            "UnknownBeforeName": "0000000000000000000000000000000000000000",
            "UnknownBeforeParent": 0,
            "UnknownBeforeVisible": 0,
            "Visible": true,
            "Width": 801
        },
        {
            "Children": [],
            "DataType": "按钮",
            "ExtensionData": "",
            "Name": "按钮1",
            ...
        }
    ],
    "Id": 1375976139,
    "Menus": [],
    "Name": "设置窗口",
    "UnknownAfterId": 42751680,
    "UnknownBeforeClass": 10
}
```

- `Controls[0]` 是窗口本身：`DataType: "窗口"`，`Name: ""`（`W/form/设置窗口.form.json:9,14`）。
- `DataType` 是控件类型中文名，支持库控件同样如此（`"超级列表框"`，`W/form/选择账号.form.json:93`）。
- `ExtensionData` 是控件私有数据的大写 hex，可以为 `""`（`W/form/设置窗口.form.json:32`）。其中的文本是 GBK 字节，例如 `D1A1D4F1D5CBBAC5` = GBK「选择账号」（`W/form/选择账号.form.json:11`）。
- `Cursor` 是 8 位 hex（4 字节）；`UnknownBeforeExtensionData` / `UnknownBeforeName` 是 40 位 hex（20 字节）。
- `Unknown*`、`Id` 是不透明整数，原样透传（例：`UnknownBeforeClass` 在 W 是 10/25，在 N 是 44959852）。
- 样本中的控件都是平铺、`Children: []`。容器控件是否嵌套到 `Children`：未确认。`Menus` 非空格式、事件绑定的表示方式：未确认。

### 3.9 `resource/resource.desc.json` 与 `*.image.dat`

顶层对象：键 = 资源名（排序），值 = `{Comment, IsPublic}`。

```json
{
    "FMTID_AudioSummaryInformation": {
        "Comment": "{64440490-4C8B-11D1-8B70-080036B11A03}",
        "IsPublic": false
    },
    ...
    "PSGUID_MEDIAFILESUMMARYINFORMATION": {
        "Comment": "{64440492-4C8B-11D1-8B70-080036B11A03}    媒体文件摘要信息。",
        "IsPublic": false
    },
    ...
}
```

（`W/resource/resource.desc.json:2-5, 70-73`）。每个资源对应 `resource/<资源名>.image.dat`，内容为资源原始字节（样本全是 16 字节 GUID，尾 6 字节与 Comment 里 GUID 的小端二进制吻合）。`.image` 以外的资源类型后缀（如声音）：未确认。

### 3.10 `module/<模块名>.desc.json`

```json
{
    "Source": "D:\\DevelopmentFolder\\常用模塊\\miniblink.ec"
}
```

带重命名的例子（`W/module/精易模块.desc.json:1-15`）：

```json
{
    "Rename": {
        "Struct": [
            {
                "Dst": "配置节数据2",
                "Src": "配置节数据"
            },
            {
                "Dst": "配置项数据2",
                "Src": "配置项数据"
            }
        ]
    },
    "Source": "D:\\DevelopmentFolder\\常用模塊\\精易模块[v11.1.5].ec"
}
```

- `Source`：`.e` 里记录的模块绝对路径，原样照抄（反斜杠转义为 `\\`）。
- `Rename`：只在多个模块出现同名成员时出现。两个样本里「精易模块」和「叮叮当当可可爱爱」都定义了 `配置节数据`/`配置项数据`；**哪个模块带 `Rename` 取决于工程**：W 里是精易模块，N 里是叮叮当当可可爱爱（`N/module/叮叮当当可可爱爱.desc.json:2-13`，`N/module/精易模块.desc.json` 只有 `Source`）。推测是模块加载顺序中后出现的那个。
- `Rename` 对文本的影响：**声明名不变，引用名改成 `Dst`**。
  - `N/module/叮叮当当可可爱爱/struct.e.txt:3` 仍是 `.数据类型 配置节数据, 公开`，但 `:5` 变成 `.成员 项, 配置项数据2, , "1"`。
  - `W/module/精易模块/struct.e.txt:381,383` 同理；`W/module/精易模块/class/类_内存配置项.class.e.txt:36` 的参数类型也是 `配置项数据2`。
- `Rename` 下除 `Struct` 以外的键（类、DLL、常量等）：未确认。

### 3.11 模块目录名

目录名**不是** `.ec` 文件名：`Source` 为 `cornsdk_免打包版.ec` 的模块目录叫 `小栗子sdk`（`W/module/小栗子sdk.desc.json:2`），`精易模块[v11.1.5].ec` 的目录叫 `精易模块`。推测来自 `.e` 中模块引用记录的名称字段，具体来源未确认。

---

## 4. `project.etprj`

GBK、无 BOM、CRLF、无结尾换行，序列化方式同 §3.1。W、N 的 en/cn 都是**这 10 个键**：

`W/project.etprj:1-12`：

```json
{
    "AsyncFile": false,
    "E2TXT-EFile": "",
    "E2TXT-InSourceDir": false,
    "E2TXT-IsCreateE": false,
    "Encoding": "UTF-8",
    "Level": 2,
    "NameStyle": 1,
    "Password": "",
    "ResetNames": [],
    "Source": "../baseline/窗口属性无法解析/en/测试源码/窗口属性无法解析.e"
}
```

- `Encoding` / `Level` 对应 `-enc` / `-level`。
- `NameStyle`：**en（不传 `-ns`）写 `1`，cn（`-ns 2`）写 `2`**。这是 en 与 cn 全部产物里唯二的差异之一（另一处是 `Source`）。
- `Source` 相对路径的算法看起来有缺陷：实际源文件是 `QAQ-ETool\测试源码\窗口属性无法解析.e`，输出目录是 `QAQ-ETool\tests\baseline\窗口属性无法解析\en`，正确相对路径应是 `../../../../测试源码/窗口属性无法解析.e`，e2txt 却写了 `../baseline/窗口属性无法解析/en/测试源码/窗口属性无法解析.e`。看起来像「去掉公共前缀后，输出一个 `../` + 目标目录去掉首段后的剩余部分 + 源文件剩余部分」。两个夹具的目录结构相同，所以无法验证这个猜测。**算法未确认**；要逐字节复现，最稳的办法是拿同样的 src/dst 关系做更多基准。分隔符是 `/`。
- 旧文档（`易语言文本格式规范.md` §10.2）列的是 GUI 写的 14 键版本（含 `Dest`、`TXT2E-*`）。命令行 e2t 写的只有上面 10 键，且 `E2TXT-InSourceDir`、`E2TXT-IsCreateE` 都是 `false`。

---

## 5. 代码文本 `*.e.txt`（程序集文件）

### 5.1 整体版式（所有 `class/*.e.txt`，统计自 208 个文件）

每一行都以 CRLF 结尾。空行（0 字符）**只**出现在下列四个位置，没有例外，也从不出现两个连续空行：

```
.版本 2                              ← 固定第 1 行（208/208）
[.支持库 <Key>]                       ← 0 或多行，顶格，见 §5.2
                                     ← 空行（文件头结束，180 处）
.程序集 <字段…>
[.程序集变量 <字段…>]*                ← 顶格
[空行]                               ← 仅当有程序集变量时（3 处）；否则 .子程序 紧贴 .程序集（177 处）
.子程序 <字段…>                       ← 顶格
[    .参数 <字段…>]*                  ← 4 空格
[    .局部变量 <字段…>]*              ← 4 空格
[空行]                               ← 仅当有 .参数/.局部变量 时（5983 处，0 例外）
    <语句行>*                         ← 缩进 = 4 × (块深度+1)
                                     ← 空行：每个子程序后一个（7237+180 处）
.子程序 …
…
                                     ← 最后一个子程序后的空行 → 文件以 "\r\n\r\n" 结尾
```

实例 `N/class/窗口程序集_启动窗口.form.e.txt` 全文（`␠` = 空格）：

```
.版本 2
                                         ← :2
.程序集 窗口程序集_启动窗口                ← :3
.子程序 __启动窗口_创建完毕                ← :4，紧贴 .程序集
␠␠␠␠                                     ← :5，空语句
                                         ← :6，子程序间空行
.子程序 __启动窗口_尺寸被改变
    图片框1.宽度 ＝ _启动窗口.宽度
    图片框1.高度 ＝ _启动窗口.高度
␠␠␠␠                                     ← :10
                                         ← :11，文件结尾
```

带程序集变量和声明的例子：`W/class/CQ应用_兼容.class.e.txt:1-20`、`W/class/窗口程序集_选择账号.form.e.txt:1-16`。

注意：
- 子程序体里的「空行」其实是**空语句**，写成当前缩进的空格（最常见 4 个；也见到 8/12/16 个，如 `W/class/主程序集.static.e.txt:147` 是 8 个、`:181` 是 16 个）。样本中只有长度为 4/8/12/16 的纯空格行。
- **子程序末尾那行 4 空格不是 e2txt 加的格式，而是 `.e` 里真实存在的空语句**。反例：`W/class/窗口程序集_选择账号.form.e.txt:24` 的 `.计次循环尾 ()` 后直接是 `:25` 子程序分隔空行，`:44-45` 的 `_退出_被单击` 也没有 4 空格行；`W/class/主程序集.static.e.txt:23` 同样以 `返回 (0)` 结束。生成器应当「有几条语句写几行」，不要补；IDE 保存的程序通常自带这条空语句。
- 块结束后是否跟一行空语句同样取决于数据：`W/class/主程序集.static.e.txt:141-142` 有，`W/class/CQ应用_兼容.class.e.txt:118-119` 的 `.判断结束` 后直接是语句。
- 没有子程序的程序集、没有 `.程序集` 行的代码文件：样本中不存在，未确认。

### 5.2 `.支持库` 行

旧文档说「任何文本文件里都不会出现 `.支持库` 行」——**这是错的**。样本中共 6 处，全部在第 2 行、顶格，后面跟文件头空行：

| 文件 | 行 | 触发它的引用 |
|---|---|---|
| `W/class/主程序集.static.e.txt:2` | `.支持库 spec` | `:6` 起调用 `调试输出`（spec 库命令） |
| `W/class/窗口程序集_选择账号.form.e.txt:2` | `.支持库 iext` | `:19,31` 调用 `账号列表.插入表项`/`.取标题`（超级列表框是 iext 控件） |
| `W/module/叮叮当当可可爱爱/class/超级列表框绘色_小栗子.class.e.txt:2`（N 同） | `.支持库 iext` | `:6` 参数类型 `超级列表框` |
| `W/module/叮叮当当可可爱爱/class/代码编辑框增强.class.e.txt:2`（N 同） | `.支持库 CodeStyleEdit` | `:6` 参数类型 `代码编辑框` |

对照组：`W/class/窗口程序集_设置窗口.form.e.txt` 只用到按钮、图片框（krnln），没有 `.支持库` 行；`W/global.e.txt:5` 引用的是模块类 `corn`，也没有。

规律：**该程序集（含其声明的参数/变量类型、调用的命令、访问的对象成员）引用了 krnln 以外的哪个支持库，就在文件头写一行 `.支持库 <Key>`**，`Key` 取 `lib.config.json` 的 `Key`。krnln 从不写。需要注意：
- 判定的是「本程序集是否引用」，不是「工程是否声明」。W 的工程声明了 spec/iext/CodeStyleEdit，但每个文件只写它自己用到的。
- 模块里子程序体为空，仍然因为参数类型而写出 `.支持库`，说明声明中的类型引用也算。
- 一个文件引用多个非核心库时的写法与排序：样本没有，**未确认**（推测每库一行，按 `lib.config.json` 顺序）。
- `const/dll/struct/global.e.txt` 从未出现 `.支持库` 行；它们引用支持库类型时是否会写：未确认。
- 窗口上放了支持库控件但代码没引用时是否写：`选择账号` 同时满足两个条件，无法区分，未确认。
- 旧文档 §4.6 的实验结论（t2e 忽略文本里的 `.支持库` 行，库表只从 `lib.config.json` 读）说的是 t2e 的输入侧，与这里的 e2t 输出侧不矛盾。

### 5.3 声明行的字段规则

通用规则（全部 208 个文件）：
- 字段以 `", "`（逗号 + 一个半角空格）连接。
- **尾部连续的空字段省略**；中间的空字段保留为空串，因此会出现 `, , `。
- 最后一个非空字段通常是备注，原样输出（含逗号、引号、尾随空格、全角空格、甚至看起来像另一条声明的文本，如 `W/module/精易模块/const.e.txt:63`）。解析时必须按固定槽位数 `split(', ', maxsplit=槽位数-1)`，并对带引号的值做引号感知。

| 声明 | 槽位（顺序） | 实例 |
|---|---|---|
| `.程序集` | 名称, 基类, 公开, 备注 | `.程序集 CQ应用_兼容`（`W/class/CQ应用_兼容.class.e.txt:3`）；`.程序集 窗口程序集_设置窗口, , 公开`（`W/class/窗口程序集_设置窗口.form.e.txt:3`）；`.程序集 MB浏览框, , 公开, miniblink`（`W/module/miniblink/class/MB浏览框.class.e.txt:3`）。样本里基类槽全为空，没见到 `<对象>` 之类的写法 |
| `.程序集变量` | 名称, 类型, (第3槽), 数组维数, 备注 | `.程序集变量 botQQ, 长整数型`（`W/class/CQ应用_兼容.class.e.txt:4`）；`.程序集变量 l, 超级列表框绘色_小栗子`（`W/class/窗口程序集_选择账号.form.e.txt:5`）。样本只有两个字段的形式，其余槽的写法未确认 |
| `.子程序` | 名称, 返回类型, 公开, 备注 | `.子程序 _启动子程序, 整数型`（`W/class/主程序集.static.e.txt:5`）；`.子程序 数据初始化, , 公开`（`W/class/CQ应用_兼容.class.e.txt:29`）；`.子程序 _初始化, , , 当基于本类的对象被创建后，此方法会被自动调用`（`:10`）；`.子程序 初始化, 文本型, 公开, 初始化插件，其他语言的用户可以把函数名改成[apprun]`（`W/class/主程序集.static.e.txt:25`） |
| `.参数`（子程序） | 名称, 类型, 属性, 备注 | 见下方「属性槽」 |
| `.局部变量` | 名称, 类型, 静态/数组, 数组维数, 备注 | `.局部变量 对比1, 文本型`（`W/class/CQ应用_兼容.class.e.txt:18`）；`.局部变量 list, 文本型, , "0"`（`:335`）；`.局部变量 _QQ, 文本型, 数组, "0"`（`:337`） |
| `.全局变量` | 名称, 类型, 公开, 数组维数, 备注 | `.全局变量 应用目录, 文本型`（`W/global.e.txt:3`）；`.全局变量 内存, 类_内存, , , 内存操作`（`W/module/精易模块/global.e.txt:4`） |
| `.常量` | 名称, 值, 公开, 备注 | §6.1 |
| `.数据类型` | 名称, 公开, 备注 | `.数据类型 WKE媒体载入信息, 公开, wkeMediaLoadInfo`（`W/module/miniblink/struct.e.txt:3`）；`.数据类型 配置节数据, 公开`（`W/module/叮叮当当可可爱爱/struct.e.txt:3`） |
| `.成员` | 名称, 类型, 传址, 数组维数, 备注 | `.成员 元素数量, 整数型`；`.成员 结构大小, 整数型, , , size`；`.成员 元素组, WKEPOST元素, , "10"`（`W/module/miniblink/struct.e.txt:25,4,24`）；`.成员 项目句柄, 长整数型, 传址`（`W/module/精易模块/struct.e.txt:153`） |
| `.DLL命令` | 名称, 返回类型, "库文件名", "入口名", 公开, 备注 | §6.2 |
| `.参数`（DLL） | 名称, 类型, 属性, 备注 | `.参数 phkResult, 整数型, 传址`（`W/module/叮叮当当可可爱爱/dll.e.txt:11`） |

**属性槽**是一个槽里用**单个半角空格**连接的多个关键字，固定顺序：
- 子程序参数：`参考` → `可空` → `数组`。见过的组合：`参考`（`W/class/CQ应用_兼容.class.e.txt:17`）、`可空`（`:32`）、`参考 可空`（`:30`）、`参考 数组`（`:147`）、`参考 可空 数组`（`:331`）、`可空 数组`（`W/module/精易模块/class/__HIDDEN_TEMP_MOD__.static.e.txt:3228`）、`数组`（`W/module/miniblink/class/__HIDDEN_TEMP_MOD__.static.e.txt:294`）。
- DLL 参数：`传址` → `数组`。见过 `传址`、`数组`（`W/module/精易模块/dll.e.txt:1421`）、`传址 数组`（`:460`）。
- 无属性但有备注：`.参数 群成员QQ, 长整数型, , 可以是自己，无权限修改别人时会失败`（`W/class/CQ应用_兼容.class.e.txt:140`）。

**数组维数**用**半角双引号**包住：`"0"`、`"1"`、`"10"`。多维（推测 `"2,3"`）样本中未出现，未确认。

**局部变量第 3 槽的 `数组`**：`W/class/CQ应用_兼容.class.e.txt:337-339` 是 `.局部变量 _QQ, 文本型, 数组, "0"`，而同一子程序 `:335` 是 `.局部变量 list, 文本型, , "0"`。两种都会出现，必须按 `.e` 里的标志位原样还原（旧文档 §7 说这是变量 flags 的 bit3；e2t 把它显示成 `数组`）。`静态`：样本中未出现，未确认。

**公开**：样本里模块导出的常量、全局变量都没写 `公开`（第 3 槽为空），而模块的程序集、子程序、数据类型、DLL 命令都写了 `公开`。

### 5.4 缩进与流程控制

- 只用空格，每层 4 个。顶格：`.版本/.支持库/.程序集/.程序集变量/.子程序`（以及 §6 文件里的 `.常量/.DLL命令/.数据类型/.全局变量`）。4 空格：`.参数/.局部变量/.成员` 与子程序体第一层。
- 块起止标记与所在语句同缩进，块体再加 4 空格。`.判断`、`.默认` 与 `.判断开始` 同缩进。

样本中实际出现的结构：

```
    .如果真 (是否为频道)                               ← W/class/CQ应用_兼容.class.e.txt:38
        fromGroupPro ＝ _fromgrouppro
    .如果真结束                                         ← :40，无括号

    .判断开始 (登录状态 ＝ “未登录”)                     ← W/class/窗口程序集_选择账号.form.e.txt:34
        进入设置.标题 ＝ “未登录”
    .判断 (登录状态 ＝ “登录完毕”)                       ← :36，可多个
        进入设置.标题 ＝ “进入设置”
    .默认                                               ← :38，无括号
        进入设置.标题 ＝ “请选择账号”
    .判断结束                                           ← :40，无括号

    .计次循环首 (取数组成员数 (框架昵称), i)             ← :18
        …
    .计次循环尾 ()                                      ← :24，带空括号
        .计次循环首 (次数, )                             ← W/class/CQ应用_兼容.class.e.txt:219，省略循环变量
```

- 没有 `.默认` 的判断：样本中每个 `.判断开始` 都带 `.默认`（13+3+2 处），没有 `.默认` 时的写法未确认（推测直接 `.判断结束`）。
- 空分支：`.默认` 下只有一条空语句，写成块体缩进的空格行（`W/class/主程序集.static.e.txt:146-148`）。
- 嵌套三层：`W/class/主程序集.static.e.txt:178-203`（`.如果真` > `.判断开始` > `.判断开始`，最深 16 空格）。
- 流程行尾注释：`.如果真 (数据.发送人QQ ≠ 数据.框架QQ) ' 过滤自己…`（`:178`）、`.判断 (…) ' 你申请…`（`:188`）。
- 未在样本中出现（写法取自旧文档 §5，**e2t 输出形式未确认**）：`.如果 (…)` / `.否则` / `.如果结束`；`.判断循环首 (…)` / `.判断循环尾 ()`；`.循环判断首 ()` / `.循环判断尾 (…)`；`.变量循环首 (起, 止, 步长, 变量)` / `.变量循环尾 ()`。

### 5.5 表达式与字面量

| 项 | 写法 | 例 |
|---|---|---|
| 命令/方法调用 | `名称 (参数, 参数)`，名称与 `(` 之间一个空格，逗号后一个空格 | `l.初始化 (账号列表, 取窗口句柄 ())`（`W/class/窗口程序集_选择账号.form.e.txt:16`） |
| 无参数 | `()` | `取窗口句柄 ()`（同上）、`销毁 ()`（`:45`） |
| 省略参数 | 留空，仍用 `", "` 分隔，末尾省略也保留逗号 | `账号列表.插入表项 (, , , , , )`（`:19`）；`设置窗口.CQ.数据初始化 (, bot)`（`W/class/主程序集.static.e.txt:157`）；`取数组下标 (QQ, )`（`W/class/CQ应用_兼容.class.e.txt:350`）；`正则.创建 (“…”, 发送内容, , , , )`（`:86`） |
| 成员访问 | `对象.成员`，链式 | `设置窗口.CQ.数据初始化`（`W/class/主程序集.static.e.txt:134`） |
| 数组下标 | 半角 `[ ]`，**前面有一个空格** | `框架QQ [i]`（`W/class/窗口程序集_选择账号.form.e.txt:21`）；`登录状态 [1]`（`W/class/主程序集.static.e.txt:133`） |
| 数组字面量 | `{ 元素, 元素 }`，花括号内侧各一个空格 | `{ #权限_设置位置共享, #权限_上报当前位置 }`（`W/class/主程序集.static.e.txt:71`） |
| 常量引用 | `#名称` | `#换行符`（`:49`）、`#权限_输出日志`（`:69`） |
| 子程序指针 | `&名称` | `&_AppStart`（`:57`）、`&子程序1`（`W/class/窗口程序集_设置窗口.form.e.txt:39`） |
| 文本字面量 | 全角 `“…”` | `“温馨提示”`（`W/class/窗口程序集_选择账号.form.e.txt:58`）；空文本 `“”`（`W/class/主程序集.static.e.txt:83`） |
| 逻辑 | `真` / `假` | `返回 (假)`（`W/class/CQ应用_兼容.class.e.txt:22`） |
| 整数 | 十进制；负号是**半角** `-` | `延时 (2000)`（`W/class/窗口程序集_设置窗口.form.e.txt:34`）；`≠ -1`（`W/class/CQ应用_兼容.class.e.txt:85`） |
| 小数、日期时间 | 样本中未出现，未确认 | |

**文本里的引号**：易语言文本字面量没有转义机制。样本里需要引号时用常量 `#左引号`/`#右引号`（`W/class/CQ应用_兼容.class.e.txt:93-94`）。反斜杠等字符原样出现（`“\[[^\]]*\]”`，`:86`；`“\”`，`:210`）。字面量本身含 `”` 时 e2t 怎么输出：未确认。

**运算符**（在代码中实际出现的，已排除字符串和注释）：

| 符号 | 语义 | 次数 | 例 |
|---|---|---|---|
| `＝` | 赋值，也是相等比较（按位置区分） | 160 | 赋值 `对比1 ＝ 编码_繁体到简体 (文本)`（`W/class/CQ应用_兼容.class.e.txt:20`）；比较 `.判断开始 (对比1 ＝ 文本)`（`:21`）；同一行两种都有：`是否为频道 ＝ _fromgrouppro ＞ 0`（`:37`） |
| `＋` | 加 / 文本连接 | 38 | `类型 ＋ “->” ＋ 日志`（`:53`） |
| `≠` | 不等于 | 8 | `:85` |
| `＞` | 大于 | 3 | `:37` |
| `≥` | 大于等于 | 1 | `:218` |
| `且` | 逻辑与，两侧各一个空格 | 4 | `… ＝ 假 且 取登录QQ () ＞ 0`（`:42`） |

运算符两侧各一个半角空格。`－ × ÷ ％ ＜ ≤ 或 非` 在代码中未出现（`÷ －` 只出现在备注文本里，如 `W/module/精易模块/class/__HIDDEN_TEMP_MOD__.static.e.txt:5309`），写法未确认，旧文档 §1 给出的全角形式可作参考。代码中没有任何半角 `= + < >` 运算符。

**注释**：
- 整行注释：缩进 + `' ` + 内容（`W/class/主程序集.static.e.txt:15`）。`'` 后固定一个空格；原文以空格开头时就会看到两个空格：`'  载入 (设置窗口, , 假)`（`:99`）。
- 行尾注释：语句 + `" ' "` + 内容（`:44` `Api.int (pluginkey, apidata) ' 请勿删除`）。
- 注释内容原样，可以含半角 `"`、`?`、`<` 等（`:47`）。

**屏蔽代码行**（IDE「屏蔽」功能）：样本中无法与普通注释区分，形如 `' info.置滑块处理函数 (到整数 (&_GetTicket))`（`:55`）的行可能是屏蔽代码也可能是注释。e2t 的屏蔽行写法未确认。

### 5.6 模块中的程序集

模块是编译后的 `.ec`，e2t 只能还原声明：每个子程序的体都只有一条空语句 `    `，声明块后仍然有空行（`W/module/叮叮当当可可爱爱/class/超级列表框绘色_小栗子.class.e.txt:5-10`）：

```
.子程序 初始化, 逻辑型, 公开
    .参数 listView, 超级列表框
    .参数 windowHwnd, 整数型, , 顶级窗口句柄

␠␠␠␠

.子程序 置表项颜色, 逻辑型, 公开
```

无声明的子程序：`.子程序 全部删除, , 公开` + `␠␠␠␠` + 空行（`:49-51`）。
模块里也没有写出 `.程序集变量`（样本中所有 `.程序集变量` 都在主工程），推测非公开成员不导出。

---

## 6. `const / dll / struct / global.e.txt`

四种文件都以 §2.3 的 16 字节文件头开头（`.版本 2` + 空行），不会出现 `.支持库`。区别在于块之间有没有空行：

| 文件 | 块结构 | 结尾 |
|---|---|---|
| `const.e.txt` | 每行一条 `.常量`，**之间无空行** | 最后一行后一个 CRLF（`…File\r\n`） |
| `global.e.txt` | 每行一条 `.全局变量`，之间无空行 | 同上（`W/global.e.txt` 以 `…文本型\r\n` 结尾） |
| `struct.e.txt` | `.数据类型` + 若干 4 空格 `.成员`，**每块后一个空行** | `\r\n\r\n` |
| `dll.e.txt` | `.DLL命令` + 若干 4 空格 `.参数`，**每块后一个空行** | `\r\n\r\n` |

### 6.1 常量

```
.版本 2

.常量 WKE代理_无, "0", , WKE_PROXY_NONE                    ← W/module/miniblink/const.e.txt:3
.常量 WkeCursorInfoPointer, "0"                            ← :37
.常量 插件_兼容性_管理员运行, "“RUNASADMIN”"               ← W/module/叮叮当当可可爱爱/const.e.txt:8
.常量 CP_gb2312, "936", , ANSI/OEM Simplified Chinese (PRC, Singapore); Chinese Simplified (GB2312)   ← :12
```

- 值统一包在**半角** `"…"` 里。
  - 数值常量：`"0"`、`"-1"`、`"-2147483648"`、`"1073741824"`。W 的模块里 1758 个数值常量全是十进制整数；小数、科学计数法未出现。
  - 文本常量：半角引号里再套全角引号：`"“RUNASADMIN”"`。文本内容原样保存，包括 TAB 和裸 CR（`W/module/精易模块/const.e.txt:24`）。
  - 逻辑、日期、长文本（资源型）常量：样本中未出现，未确认。
- 备注里的逗号不影响解析（第 4 槽以后全算备注）。
- 空的常量文件就是 16 字节文件头（`W/const.e.txt`）。

### 6.2 DLL 命令

```
.DLL命令 RegCreateKeyExA, 整数型, "advapi32.dll", "RegCreateKeyExA", 公开   ← W/module/叮叮当当可可爱爱/dll.e.txt:3
    .参数 hKey, 整数型
    …
    .参数 phkResult, 整数型, 传址                                             ← :11

.DLL命令 RegDeleteKeyA, 整数型, "advapi32.dll", "RegDeleteKeyA", 公开, _注册表_删除项删除现有项…   ← :22
    .参数 hKey, 整数型, , 一个已打开项的句柄，或者标准项名之一              ← :23
```

- 库文件名、入口名用半角引号。入口名 `"@名称"` 表示 cdecl（`W/module/精易模块/dll.e.txt:71` `.DLL命令 strrchr, 整数型, "ntdll.dll", "@strrchr", 公开`）。
- 无返回值时第 2 槽为空：`.DLL命令 CoInternetSetFeatureEnabled, , "urlmon.dll", …`（`:30`）。
- W 的 700 条 DLL 命令全部带 `公开`、全部有库名和入口名；不公开或缺入口名时的写法未确认。

### 6.3 数据类型

见 §5.3 表中实例。`W/module/miniblink/struct.e.txt` 全文的结构：`.数据类型` 行 → 4 空格 `.成员` 行 → 空行 → 下一个 `.数据类型`……最后一块后面也有空行，所以文件以 `\r\n\r\n` 结尾（`:37-38`）。

### 6.4 全局变量

```
.版本 2

.全局变量 应用目录, 文本型          ← W/global.e.txt:3
.全局变量 载入, 整数型
.全局变量 Api, corn                 ← 类型可以是模块里的类
.全局变量 text, 文本代码
.全局变量 AppID, 文本型             ← :7，后接 CRLF，文件结束
```

---

## 7. en 与 cn 的内容差异

按 §2.1 映射后逐字节比较：

- W：144 个文件中 143 个逐字节相同。
- N：132 个文件中 131 个逐字节相同。
- 唯一不同的是 `project.etprj` 的两行：`"NameStyle": 1` ↔ `2`，以及 `Source` 路径里的 `/en/` ↔ `/cn/`（§4）。

所以**代码文本、JSON、资源在两种布局下完全相同**，生成器只需要切换文件/目录名和 `NameStyle`。另外，同一个模块在 W、N 两个工程里的产物也基本相同，只在 `Rename` 相关的类型引用上不同（§3.10）。

---

## 8. 占位符 `<?…?>`

样本（W+N 全部文件）中只有两种：

| 占位符 | 次数 | 出现位置 | 例 |
|---|---|---|---|
| `<?未知本地类型?>` | 140 | 类型槽：`.参数`、`.局部变量`、DLL `.参数` | `.参数 Arr, <?未知本地类型?>, 参考 数组`（`W/module/叮叮当当可可爱爱/class/_Protobuffer.class.e.txt:33`）；`.参数 参数_字段信息, <?未知本地类型?>, 数组`（`…/CSQLite3.class.e.txt:341`）；`.参数 hMem, <?未知本地类型?>, 传址`（`W/module/精易模块/dll.e.txt:49`） |
| `<?未知名称:支持库不存在?>` | 4 | 表达式里的成员名 | `设置窗口.<?未知名称:支持库不存在?> ＝ “miniblink_test”`（`W/class/窗口程序集_设置窗口.form.e.txt:8`，另见 `:13,14,41`） |

- `<?未知本地类型?>` 全部出现在模块文件里。推测是模块内部未导出的自定义数据类型（例如 `CSQLite3` 引用的字段信息结构在 `叮叮当当可可爱爱/struct.e.txt` 里不存在），未确认。
- `<?未知名称:支持库不存在?>` 的 4 处与 `窗口属性无法解析/logs/e2t.en.stderr.txt` 的 4 条 `[错误] TxtCodeWriter::ParseExpression 解析【类型】名称 失败！[支持库] 20994 …` 一一对应。stderr 有错误时 e2t 仍然输出 `SUCC:`（`e2t.en.stdout.txt:117`）。
- 占位符里的 `<`、`?` 是半角。其它占位符形式：样本中没有。

---

## 9. 生成器伪代码（代码文件）

```python
CRLF = '\r\n'

def fields(*xs):
    xs = list(xs)
    while xs and xs[-1] == '':   # 只去掉尾部空槽；备注是 ' ' 时不算空
        xs.pop()
    return ', '.join(xs)

def write_class_file(asm):
    out = ['.版本 2']
    out += ['.支持库 ' + k for k in asm.used_nonkernel_libs]   # §5.2，多库顺序未确认
    out.append('')
    out.append('.程序集 ' + fields(asm.name, asm.base, '公开' if asm.public else '', asm.remark))
    for v in asm.vars:
        out.append('.程序集变量 ' + fields(v.name, v.type, v.slot3, dims(v), v.remark))
    if asm.vars:
        out.append('')
    for s in asm.subs:
        out.append('.子程序 ' + fields(s.name, s.ret, '公开' if s.public else '', s.remark))
        for p in s.params:
            attr = ' '.join(a for a, on in (('参考', p.ref), ('可空', p.nullable), ('数组', p.array)) if on)
            out.append('    .参数 ' + fields(p.name, p.type, attr, p.remark))
        for v in s.locals:
            out.append('    .局部变量 ' + fields(v.name, v.type, v.slot3, dims(v), v.remark))
        if s.params or s.locals:
            out.append('')
        out += s.body_lines          # 每条语句一行；空语句 = 缩进空格
        out.append('')               # 子程序分隔
    return b'\xef\xbb\xbf' + CRLF.join(out).encode('utf-8') + CRLF.encode()

def dims(v):
    return '"%s"' % v.dims if v.is_array_decl else ''
```

验证：用这套版式规则把 W、N 全部 208 个 `*.e.txt` 解析成「文件头 / 程序集 / 程序集变量 / 子程序（头、声明、语句）」再重新输出，208 个文件全部与原文件逐字节一致。这只验证了版式（空行、文件头、结尾）；行内字段仍按原文透传，§5.3–5.5 的字段规则是统计归纳得出的，没有经过同样的往返验证。

`const/global`：`['.版本 2', ''] + 各行`，用 CRLF 连接，最后补一个 CRLF。
`struct/dll`：`['.版本 2', '']` + 每块 `[头行, 成员行…, '']`，用 CRLF 连接，最后补一个 CRLF。
16 字节空文件是这两条规则在「无内容」时的自然结果。

---

## 10. 与旧文档的出入（以产物为准）

| 旧文档说法 | 实际产物 |
|---|---|
| 「任何文本文件里都不会出现 `.支持库` 行」（规范.md §4.5、SKILL.md） | 会出现，规律见 §5.2 |
| 中文布局对照表只有 10 项（规范.md §10.1） | §2.1 共 30 项，漏了 `DLL.e.txt`、`自定义类型.e.txt`、`全局变量.e.txt`、`图标.ico`、`目录.desc.json`、`资源.desc.json`、`模块.初始化`、`易包.文件`、`模块/`、`模块/<m>/代码/` 等（§2.1） |
| etprj 固定 14 键（规范.md §10.2） | 命令行 e2t 写 10 键，`E2TXT-*` 两个布尔都是 `false`，`NameStyle` en=1 / cn=2（§4） |
| `Guid` 是 32 位小写 hex | 原样照抄，可能大写或带花括号（§3.4） |
| 「块尾后必须插一个空行」（规范.md §5） | 那是给手写者的建议；e2t 输出只反映 `.e` 里有没有空语句（§5.1） |
| 「子程序末尾会补一行 4 空格」（规范.md §3） | 不会补，那是 `.e` 里的空语句；没有时就没有（§5.1） |
| `-level 1` 会省略空文件 | `-level 2` 下顶层空文件仍生成，但**模块目录内的空文件照样不生成**（§2.3） |
