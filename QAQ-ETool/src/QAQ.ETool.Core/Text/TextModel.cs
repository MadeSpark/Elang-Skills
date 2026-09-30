namespace QAQ.ETool.Core.Text;

// =====================================================================================
// t2e 文本工程模型（契约文件）。
// 由 Importer 从 e2txt 兼容目录读出（只做词法级拆分，不做名称解析），交给 Model/Builder 组装 .e。
// 所有 Line 为所在文件的 1 基行号；File 为相对工程根的路径（用 '/' 分隔），用于报错。
// 字符串字段保持原文：类型名、数组维数（去掉外层半角引号后的内容）、常量值原文等由 Builder 解释。
// 空的可选字段一律为 null（而不是 ""），便于区分「省略」。名称为 "" 表示 IDE 表格里的无名空白行。
// =====================================================================================

public sealed class TextProject
{
    public string Root { get; set; } = "";
    public Layout Layout { get; set; } = Layout.English;

    /// <summary>工程文件 project.qtprj（GBK JSON）原样</summary>
    public JObject? ProjectInfo { get; set; }

    // ---------------- config/
    public List<TLib> Libs { get; } = new();
    /// <summary>system.config.json 原样（可能缺失）</summary>
    public JObject? SystemConfig { get; set; }
    /// <summary>user.config.json 原样（可能缺失）</summary>
    public JObject? UserConfig { get; set; }
    /// <summary>ec.init.config.json：(模块名, 初始化子程序名)，保持文件中顺序</summary>
    public List<(string Module, string Method)> EcInit { get; } = new();
    /// <summary>epack.files.config.json 的 FileNames（原样）</summary>
    public List<string> EpackFileNames { get; } = new();

    // ---------------- 主工程声明（顶层 const/dll/struct/global.e.txt）
    public TDeclSet Main { get; } = new();

    /// <summary>主工程程序集，按 Importer 决定的顺序（见 Importer 文档：按排序表展开）</summary>
    public List<TClass> Classes { get; } = new();

    /// <summary>窗口（form/*.form.json 与 class/&lt;分组&gt;/*.form.json）</summary>
    public List<TForm> Forms { get; } = new();

    /// <summary>
    /// 编辑过滤器分组树。Key = 分组相对 class/ 的路径（'/' 分隔，如 "界面/子分组"）；
    /// 值 = 该分组 order.list 里的名称顺序（程序集名、窗口名、子分组名混排）。
    /// 根目录（class/ 本身）的排序表单独放在 RootOrder。
    /// </summary>
    public Dictionary<string, List<string>> FolderOrders { get; } = new(StringComparer.Ordinal);
    /// <summary>class/order.list.txt 原样（空文件 → 空列表）</summary>
    public List<string> RootOrder { get; } = new();
    /// <summary>class/dirs.desc.json 的 ExpandedDirs</summary>
    public List<string> ExpandedDirs { get; } = new();

    // ---------------- resource/
    public List<TResource> Resources { get; } = new();

    /// <summary>icon.ico 原始字节（缺失为空数组）</summary>
    public byte[] Icon { get; set; } = Array.Empty<byte>();

    // ---------------- module/
    public List<TModule> Modules { get; } = new();

    /// <summary>读取阶段的错误（格式不符等），File:Line: 消息</summary>
    public List<string> Errors { get; } = new();
    public List<string> Warnings { get; } = new();
}

/// <summary>lib.config.json 一项</summary>
public sealed class TLib
{
    public string Key { get; set; } = "";       // 文件名（krnln / iext …）
    public string Guid { get; set; } = "";
    public string Name { get; set; } = "";
    public int Major { get; set; }
    public int Minor { get; set; }
    public int CmdCount { get; set; }
    public int MaxRefConstPos { get; set; }
    public int MaxRefObjectPos { get; set; }
}

/// <summary>一组「文件级声明」：const / dll / struct / global 四个文件</summary>
public sealed class TDeclSet
{
    public List<TConst> Consts { get; } = new();
    public List<TDll> Dlls { get; } = new();
    public List<TStruct> Structs { get; } = new();
    public List<TVar> Globals { get; } = new();
}

public sealed class TModule
{
    /// <summary>模块名（module/&lt;名&gt;/ 目录名，与 &lt;名&gt;.desc.json 对应）</summary>
    public string Name { get; set; } = "";
    /// <summary>desc.json 的 Source（原样）</summary>
    public string? Source { get; set; }
    /// <summary>desc.json 的 Rename.Struct：(Src 原名, Dst 引用处用名)</summary>
    public List<(string Src, string Dst)> StructRenames { get; } = new();
    public TDeclSet Decls { get; } = new();
    /// <summary>module/&lt;名&gt;/class/*.e.txt，按文件名序号排序（__HIDDEN_TEMP_MOD__ 等）</summary>
    public List<TClass> Classes { get; } = new();
}

public enum TClassKind { Static, Class, Form }

/// <summary>一个程序集文件（*.static.e.txt / *.class.e.txt / *.form.e.txt）</summary>
public sealed class TClass
{
    public string File { get; set; } = "";
    public int Line { get; set; }
    public TClassKind Kind { get; set; }
    /// <summary>所在分组路径（相对 class/，'/' 分隔；不在分组 = ""）。模块程序集恒为 ""。</summary>
    public string Folder { get; set; } = "";
    /// <summary>.支持库 行的 Key（仅记录，t2e 不依赖）</summary>
    public List<string> LibLines { get; } = new();

    public string Name { get; set; } = "";
    public string? Base { get; set; }
    public bool Public { get; set; }
    public string? Comment { get; set; }
    public List<TVar> Vars { get; } = new();
    public List<TMethod> Methods { get; } = new();
}

/// <summary>
/// 变量类声明：.程序集变量 / .局部变量 / .全局变量 / .成员（数据类型成员也用它）。
/// 槽位：名称, 类型, 第3槽, 数组维数, 备注
///   第3槽：程序集变量/局部变量 = 「静态」「数组」（空格连接）；全局变量 = 「公开」；成员 = 「传址」
/// </summary>
public sealed class TVar
{
    public int Line { get; set; }
    public string Name { get; set; } = "";
    public string? Type { get; set; }
    public bool Static { get; set; }
    public bool Array { get; set; }
    public bool Public { get; set; }
    public bool ByRef { get; set; }
    /// <summary>数组维数：去掉外层半角引号后的原文（"0"、"10"、"2,3"、","）；无 = null</summary>
    public string? Dims { get; set; }
    public string? Comment { get; set; }
}

/// <summary>.参数（子程序参数与 DLL 参数共用）。属性：参考/可空/数组（子程序），传址/数组（DLL，传址记入 ByRef）</summary>
public sealed class TParam
{
    public int Line { get; set; }
    public string Name { get; set; } = "";
    public string? Type { get; set; }
    public bool ByRef { get; set; }
    public bool Optional { get; set; }
    public bool Array { get; set; }
    public string? Comment { get; set; }
}

public sealed class TMethod
{
    public int Line { get; set; }
    public string Name { get; set; } = "";
    public string? ReturnType { get; set; }
    public bool Public { get; set; }
    public string? Comment { get; set; }
    public List<TParam> Params { get; } = new();
    public List<TVar> Locals { get; } = new();
    /// <summary>
    /// 子程序体原文行（含缩进，不含声明区与结尾的分隔空行）。
    /// 空子程序体在 e2txt 里写成一行 4 空格，这里照样收进来（= 1 条空语句），由 Builder 决定模块子程序置空。
    /// </summary>
    public List<string> Body { get; } = new();
    /// <summary>Body[0] 的文件行号</summary>
    public int BodyLine { get; set; }
}

/// <summary>.常量 名称, "值", 公开, 备注。RawValue = 去掉外层半角引号后的原文（“文本”/真/123/[日期]），无值 = null</summary>
public sealed class TConst
{
    public int Line { get; set; }
    public string Name { get; set; } = "";
    public string? RawValue { get; set; }
    public bool Public { get; set; }
    public string? Comment { get; set; }
}

public sealed class TStruct
{
    public int Line { get; set; }
    public string Name { get; set; } = "";
    public bool Public { get; set; }
    public string? Comment { get; set; }
    /// <summary>成员：TVar.ByRef = 传址</summary>
    public List<TVar> Members { get; } = new();
}

public sealed class TDll
{
    public int Line { get; set; }
    public string Name { get; set; } = "";
    public string? ReturnType { get; set; }
    /// <summary>去掉引号后的库文件名 / 入口名（可能为 null）</summary>
    public string? LibraryName { get; set; }
    public string? EntryPoint { get; set; }
    public bool Public { get; set; }
    public string? Comment { get; set; }
    public List<TParam> Params { get; } = new();
}

public sealed class TForm
{
    public string File { get; set; } = "";
    /// <summary>所在分组路径（相对 class/；在 form/ 目录下 = ""）</summary>
    public string Folder { get; set; } = "";
    /// <summary>*.form.json 原样（键见 Exporter.WriteForms / ControlJson / MenuJson）</summary>
    public JObject Json { get; set; } = new();
}

public enum TResourceKind { Image, Sound, LongText }

public sealed class TResource
{
    public string File { get; set; } = "";
    /// <summary>文件名去掉扩展名（.png / .image.dat / .sound.dat / .txt）后的部分</summary>
    public string Name { get; set; } = "";
    public TResourceKind Kind { get; set; }
    /// <summary>原始字节（长文本为 GBK 字节，Builder 负责解码）</summary>
    public byte[] Data { get; set; } = Array.Empty<byte>();
    /// <summary>resource.desc.json 里的 Comment / IsPublic（无记录 = null / false）</summary>
    public string? Comment { get; set; }
    public bool Public { get; set; }
}
