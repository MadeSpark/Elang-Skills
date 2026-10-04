namespace QAQ.ETool.Core.Text;

/// <summary>
/// 文本目录布局：英文（默认）/ 中文（-ns 2）。对照表见 docs/e2txt-format-spec.md §2.1
/// </summary>
public sealed class Layout
{
    public string Class { get; private init; } = "";
    public string Config { get; private init; } = "";
    public string Form { get; private init; } = "";
    public string Resource { get; private init; } = "";
    public string Module { get; private init; } = "";
    public string OrderList { get; private init; } = "";
    public string DirsDesc { get; private init; } = "";
    /// <summary>打开标签页描述（编辑信息段2 的文本化，见 docs/e2txt-format-spec.md §3.12）</summary>
    public string OpenTabs { get; private init; } = "";
    public string LibConfig { get; private init; } = "";
    public string SystemConfig { get; private init; } = "";
    public string UserConfig { get; private init; } = "";
    public string EcInitConfig { get; private init; } = "";
    public string EpackFilesConfig { get; private init; } = "";
    public string ResourceDesc { get; private init; } = "";
    public string Const { get; private init; } = "";
    public string Dll { get; private init; } = "";
    public string Struct { get; private init; } = "";
    public string Global { get; private init; } = "";
    public string Icon { get; private init; } = "";
    /// <summary>工程文件（扩展名 .qtprj，供 qaq-etool-gui.exe 关联打开）</summary>
    public string Project { get; private init; } = "";
    /// <summary>工程文件中的 NameStyle 值</summary>
    public int NameStyle { get; private init; }

    public static readonly Layout English = new()
    {
        Class = "class", Config = "config", Form = "form", Resource = "resource", Module = "module",
        OrderList = "order.list.txt", DirsDesc = "dirs.desc.json", OpenTabs = "tabs.desc.json",
        LibConfig = "lib.config.json", SystemConfig = "system.config.json", UserConfig = "user.config.json",
        EcInitConfig = "ec.init.config.json", EpackFilesConfig = "epack.files.config.json",
        ResourceDesc = "resource.desc.json",
        Const = "const.e.txt", Dll = "dll.e.txt", Struct = "struct.e.txt", Global = "global.e.txt",
        Icon = "icon.ico", Project = "project.qtprj", NameStyle = 1,
    };

    public static readonly Layout Chinese = new()
    {
        Class = "代码", Config = "配置", Form = "窗口", Resource = "资源", Module = "模块",
        OrderList = "排序.list.txt", DirsDesc = "目录.desc.json", OpenTabs = "标签页.desc.json",
        LibConfig = "支持库.config.json", SystemConfig = "系统.config.json", UserConfig = "用户.config.json",
        EcInitConfig = "模块.初始化.config.json", EpackFilesConfig = "易包.文件.config.json",
        ResourceDesc = "资源.desc.json",
        Const = "常量.e.txt", Dll = "DLL.e.txt", Struct = "自定义类型.e.txt", Global = "全局变量.e.txt",
        Icon = "图标.ico", Project = "项目.qtprj", NameStyle = 2,
    };

    public static Layout FromNs(string? ns) => ns == "2" ? Chinese : English;

    /// <summary>目录里有任一布局的工程文件（项目.qtprj / project.qtprj）即视为文本工程目录</summary>
    public static bool IsTextProjectDir(string dir) =>
        File.Exists(Path.Combine(dir, Chinese.Project)) || File.Exists(Path.Combine(dir, English.Project));

    /// <summary>t2e：探测文本目录用的是哪种布局</summary>
    public static Layout Detect(string dir)
    {
        if (Directory.Exists(Path.Combine(dir, Chinese.Class)) || File.Exists(Path.Combine(dir, Chinese.Project)))
            return Chinese;
        return English;
    }
}
