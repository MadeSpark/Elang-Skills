using System.Text;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Sections;
using QAQ.ETool.Core.Model;

namespace QAQ.ETool.Core.Text;

public sealed class ExportOptions
{
    public Layout Layout { get; init; } = Layout.English;
    /// <summary>e2txt 的 -level（1 省略空产物，2 完整）</summary>
    public int Level { get; init; } = 2;
    /// <summary>写进 project.etprj 的源文件路径（原样记录）</summary>
    public string? SourcePath { get; init; }
}

public sealed class ExportResult
{
    public int FileCount { get; set; }
    public long TotalBytes { get; set; }
    public List<string> Failures { get; } = new();
    public List<string> Warnings { get; } = new();
}

/// <summary>
/// e2t：EProject → e2txt 兼容文本目录。
/// 版式依据 docs/e2txt-format-spec.md。
/// </summary>
public sealed class Exporter
{
    private readonly EProject _p;
    private readonly ExportOptions _o;
    private readonly NameResolver _n;
    private readonly Layout _l;
    private readonly ExportResult _r = new();
    private string _root = "";

    public Exporter(EProject p, ExportOptions o)
    {
        _p = p;
        _o = o;
        _l = o.Layout;
        _n = new NameResolver(p);
    }

    public ExportResult Export(string dstDir)
    {
        _root = dstDir;
        Directory.CreateDirectory(dstDir);

        WriteClasses();
        WriteDecls(-1, dstDir);
        for (int mi = 0; mi < _p.Modules.Count; mi++)
            WriteDecls(mi, ModuleDir(mi));
        WriteModuleDescs();
        WriteConfigs();
        WriteForms();
        WriteResources();
        WriteBytes(Path.Combine(dstDir, _l.Icon), _p.Code.IconData ?? Array.Empty<byte>(), always: true);
        WriteProject();

        _r.Failures.AddRange(_n.Failures);
        if (_n.ModulePrivateTypes.Count > 0)
            _r.Warnings.Add($"{_n.ModulePrivateTypes.Count} 个模块私有数据类型未随 .e 保存（仅有 ID，无名称），已按 <?未知本地类型:0x…?> 原样保留");
        return _r;
    }

    // ================================================================ 文件写出
    private void WriteBytes(string path, byte[] data, bool always = true)
    {
        if (!always && data.Length == 0) return;
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, data);
        _r.FileCount++;
        _r.TotalBytes += data.Length;
    }

    private void WriteText(string path, List<string> lines) =>
        WriteBytes(path, TextFiles.Utf8Bom(string.Join(TextFiles.CRLF, lines) + TextFiles.CRLF));

    private void WriteJson(string path, object value) => WriteBytes(path, EJson.ToUtf8Bom(value));

    private string ModuleDir(int mi) => Path.Combine(_root, _l.Module, SafeName(_p.Modules[mi].Name));

    private static string SafeName(string name)
    {
        var bad = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder(name.Length);
        foreach (var c in name) sb.Append(bad.Contains(c) ? '_' : c);
        return sb.ToString();
    }

    // ================================================================ 声明字段
    /// <summary>字段以 ", " 连接，尾部空字段省略（spec §5.3）</summary>
    private static string Fields(params string?[] xs)
    {
        int n = xs.Length;
        while (n > 0 && string.IsNullOrEmpty(xs[n - 1])) n--;
        return string.Join(", ", xs.Take(n).Select(x => x ?? ""));
    }

    private static string Dims(int[]? ub)
    {
        if (ub == null || ub.Length == 0) return "";
        if (ub.Length == 1 && ub[0] == 0) return "\"0\"";
        return "\"" + string.Join(",", ub.Select(x => x == 0 ? "" : x.ToString())) + "\"";
    }

    private static string Join(params (string Word, bool On)[] xs) =>
        string.Join(" ", xs.Where(x => x.On).Select(x => x.Word));

    private string Type(int id, ISet<int>? used) => _n.DataType(id, used);

    // ================================================================ 程序集
    private static string ClassSuffix(int id) => EplSystemId.GetType(id) switch
    {
        EplSystemId.Type_FormClass => ".form.e.txt",
        EplSystemId.Type_Class => ".class.e.txt",
        _ => ".static.e.txt",
    };

    // ---------------- 编辑过滤器（IDE 程序集分组）→ class/<分组>/ 子目录
    private Dictionary<int, CodeFolderInfo> _folders = new();
    /// <summary>程序集/窗口 ID → 所在分组 key</summary>
    private readonly Dictionary<int, int> _itemFolder = new();

    private void BuildFolders()
    {
        var fs = _p.Doc.GetOrNull(FolderSection.Key);
        if (fs == null) return;
        _folders = fs.Folders.ToDictionary(x => x.Key);
        foreach (var f in fs.Folders)
            foreach (var c in f.Children)
                if (!_folders.ContainsKey(c)) _itemFolder[c] = f.Key;
    }

    private string FolderPath(int key)
    {
        var parts = new List<string>();
        var guard = 0;
        while (key != 0 && _folders.TryGetValue(key, out var f) && guard++ < 64)
        {
            parts.Insert(0, SafeName(f.Name));
            key = f.ParentKey;
        }
        return Path.Combine(parts.ToArray());
    }

    private string ItemDir(int id)
    {
        var classDir = Path.Combine(_root, _l.Class);
        return _itemFolder.TryGetValue(id, out var fk) ? Path.Combine(classDir, FolderPath(fk)) : classDir;
    }

    private string ItemName(int id) =>
        _folders.TryGetValue(id, out var f) ? f.Name : _n.User(id);

    private void WriteClasses()
    {
        BuildFolders();
        var order = new List<string>();
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var publicity = _p.Doc.GetOrNull(ClassPublicitySection.Key)?.ClassPublicities
            .ToDictionary(x => x.Class, x => x) ?? new();

        // 根目录排序表：顶层分组在前，然后是不在任何分组里的程序集
        foreach (var f in _folders.Values.Where(f => f.ParentKey == 0))
            if (seen.Add(f.Name)) order.Add(f.Name);

        foreach (var cls in _p.Code.Classes)
        {
            var name = _n.User(cls.Id);
            if (!_itemFolder.ContainsKey(cls.Id) && seen.Add(name)) order.Add(name);

            int mi = _p.ModuleIndexOf(cls.Id);
            var dir = mi < 0 ? ItemDir(cls.Id) : Path.Combine(ModuleDir(mi), _l.Class);
            // 模块导出的程序集必然公开（公开性段不一定记录）；主工程按公开性段。
            // 窗口程序集：e2txt 依据窗口记录的 UnknownBeforeClass（非 0 且最高字节为 0 → 公开），
            // 三夹具 9 个窗口全部吻合；该字段经 form.json 原样往返，不影响 t2e。
            bool isPublic = mi >= 0 || (publicity.TryGetValue(cls.Id, out var pub) && pub.Public)
                            || (_p.FormByClass.TryGetValue(cls.Id, out var fm)
                                && fm.UnknownBeforeClass != 0 && (fm.UnknownBeforeClass & unchecked((int)0xFF000000)) == 0);
            var lines = RenderClass(cls, mi, isPublic);
            WriteText(Path.Combine(dir, SafeName(name) + ClassSuffix(cls.Id)), lines);
        }

        var classDir = Path.Combine(_root, _l.Class);
        WriteBytes(Path.Combine(classDir, _l.OrderList), TextFiles.Utf8Bom(string.Join(TextFiles.CRLF, order)));

        // 每个分组一份排序表（成员 = 分组 Children 顺序，含子分组、程序集、窗口）
        foreach (var f in _folders.Values)
        {
            var names = f.Children.Select(ItemName).ToList();
            WriteBytes(Path.Combine(classDir, FolderPath(f.Key), _l.OrderList), TextFiles.Utf8Bom(string.Join(TextFiles.CRLF, names)));
        }

        // 展开的分组：按 GBK 字节序排序（e2txt 行为）
        var expanded = _folders.Values.Where(f => f.Expand).Select(f => FolderPath(f.Key).Replace('\\', '/')).ToList();
        expanded.Sort((a, b) => CompareBytes(TextFiles.Gbk.GetBytes(a), TextFiles.Gbk.GetBytes(b)));
        WriteJson(Path.Combine(classDir, _l.DirsDesc), new JObject { ["ExpandedDirs"] = expanded.Cast<object?>().ToList() });
    }

    private static int CompareBytes(byte[] a, byte[] b)
    {
        for (int i = 0; i < Math.Min(a.Length, b.Length); i++)
            if (a[i] != b[i]) return a[i].CompareTo(b[i]);
        return a.Length.CompareTo(b.Length);
    }

    private List<string> RenderClass(ClassInfo cls, int moduleIndex, bool isPublic)
    {
        var body = new List<string>();
        var used = new OrderedIntSet();   // 声明与代码共用，按首次出现顺序
        var constLibs = new OrderedIntSet();
        var rc = new CodeRenderer(_p, _n) { UsedLibs = used, ConstLibs = constLibs };
        rc.CurrentForm = _p.FormByClass.TryGetValue(cls.Id, out var f) ? f : null;

        var baseName = cls.BaseClass == 0 || cls.BaseClass == -1 ? "" : Type(cls.BaseClass, used);
        body.Add(".程序集 " + Fields(_n.User(cls.Id), baseName, isPublic ? "公开" : "", cls.Comment));
        foreach (var v in cls.Variables)
            body.Add(Decl(".程序集变量 ", v.Name, () => Fields(v.Name, Type(v.DataType, used), VarAttr(v.Flags), Dims(v.UBound), v.Comment)));
        if (cls.Variables.Count > 0) body.Add("");

        foreach (var mid in cls.Methods)
        {
            if (!_p.MethodById.TryGetValue(mid, out var m))
            {
                _r.Warnings.Add($"程序集 {cls.Name} 引用了不存在的子程序 0x{mid:X8}");
                continue;
            }
            // 模块里留下的子程序都是导出的公开子程序（.e 不一定保留公开标志）
            body.Add(".子程序 " + Fields(_n.User(m.Id), Type(m.ReturnDataType, used), m.Public || moduleIndex >= 0 ? "公开" : "", m.Comment));
            foreach (var a in m.Parameters)
                body.Add("    .参数 " + Fields(a.Name, Type(a.DataType, used),
                    Join(("参考", a.ByRef), ("可空", a.OptionalParameter), ("数组", a.ArrayParameter)), a.Comment));
            foreach (var v in m.Variables)
                body.Add(Decl("    .局部变量 ", v.Name, () => Fields(v.Name, Type(v.DataType, used), VarAttr(v.Flags), Dims(v.UBound), v.Comment)));
            if (m.Parameters.Count > 0 || m.Variables.Count > 0) body.Add("");

            var stmts = new List<string>();
            var data = m.CodeData.ExpressionData;
            if (data != null && data.Length > 0)
            {
                try
                {
                    var block = CodeDataParser.ParseStatementBlock(data, m.CodeData.Encoding ?? _p.Encoding);
                    rc.WriteBlock(block, stmts, 1);
                }
                catch (Exception ex)
                {
                    _n.Failures.Add($"子程序 {m.Name} 代码解析失败: {ex.Message}");
                    stmts.Add("    ' <?代码解析失败?>");
                }
            }
            if (stmts.Count == 0) stmts.Add("    ");
            body.AddRange(stmts);
            body.Add("");
        }

        // .支持库 行：按首次使用顺序（经 # 常量引用的库排最前）。
        // 已知与 e2txt 有 5 个文件行序不同（大项目），规律未明；t2e 不读这些行，不影响回写。
        var head = new List<string> { ".版本 2" };
        var libOrder = new OrderedIntSet();
        libOrder.UnionWith(constLibs);
        libOrder.UnionWith(used);
        foreach (var lib in libOrder.Where(x => x > 0))
            head.Add(".支持库 " + _p.Code.Libraries[lib].FileName);
        head.Add("");
        head.AddRange(body);
        return head;
    }

    /// <summary>名称为空的声明 = IDE 表格里的空白行，e2txt 写成空行</summary>
    private static string Decl(string keyword, string? name, Func<string> fields) =>
        string.IsNullOrEmpty(name) ? "" : keyword + fields();

    /// <summary>变量第 3 槽：静态(0x1) / 数组(0x8)（spec §5.3）</summary>
    private static string VarAttr(int flags) => Join(("静态", (flags & 0x1) != 0), ("数组", (flags & 0x8) != 0));

    // ================================================================ 常量 / 全局变量 / 数据类型 / DLL
    private void WriteDecls(int mi, string dir)
    {
        bool inModule = mi >= 0;
        bool Mine(int id) => _p.ModuleIndexOf(id) == mi;

        // 常量（图片/声音资源另写 resource/）
        var consts = new List<string> { ".版本 2", "" };
        // 长文本常量不进 const.e.txt，写到 resource/<名>.txt（e2txt 行为）
        foreach (var c in _p.Resource.Constants.Where(c => Mine(c.Id) && ((uint)c.Id >> 28) == 1 && !(inModule == false && IsLongText(c))))
            consts.Add(Decl(".常量 ", c.Name, () => Fields(c.Name, ConstValue(c), c.Public && !inModule ? "公开" : "", c.Comment)));

        var globals = new List<string> { ".版本 2", "" };
        foreach (var g in _p.Code.GlobalVariables.Where(g => Mine(g.Id)))
            globals.Add(Decl(".全局变量 ", g.Name, () => Fields(g.Name, Type(g.DataType, null), g.Public && !inModule ? "公开" : "", Dims(g.UBound), g.Comment)));

        var structs = new List<string> { ".版本 2", "" };
        foreach (var s in _p.Code.Structs.Where(s => Mine(s.Id)))
        {
            structs.Add(".数据类型 " + Fields(s.Name, s.Public || inModule ? "公开" : "", s.Comment));
            foreach (var m in s.Members)
                structs.Add("    .成员 " + Fields(m.Name, Type(m.DataType, null), m.ByRef ? "传址" : "", Dims(m.UBound), m.Comment));
            structs.Add("");
        }

        var dlls = new List<string> { ".版本 2", "" };
        foreach (var d in _p.Code.DllDeclares.Where(d => Mine(d.Id)))
        {
            dlls.Add(".DLL命令 " + Fields(d.Name, Type(d.ReturnDataType, null),
                Quote(d.LibraryName), Quote(d.EntryPoint), d.Public || inModule ? "公开" : "", d.Comment));
            foreach (var a in d.Parameters)
                dlls.Add("    .参数 " + Fields(a.Name, Type(a.DataType, null), Join(("传址", a.ByRef), ("数组", a.ArrayParameter)), a.Comment));
            dlls.Add("");
        }

        // 顶层空文件照样写（-level 2）；模块目录内为空就不写（spec §2.3）
        void Emit(string file, List<string> lines)
        {
            bool empty = lines.Count <= 2;
            if (empty && (inModule || _o.Level < 2)) return;
            WriteText(Path.Combine(dir, file), lines);
        }
        Emit(_l.Const, consts);
        Emit(_l.Dll, dlls);
        Emit(_l.Struct, structs);
        Emit(_l.Global, globals);
    }

    /// <summary>空串不加引号（e2txt：`.DLL命令 x, 整数型, , "GetLastError"`）</summary>
    private static string Quote(string? s) => string.IsNullOrEmpty(s) ? "" : "\"" + s + "\"";

    private static bool IsLongText(ConstantInfo c) => c.LongText && c.Value is string;

    private static string ConstValue(ConstantInfo c) => c.Value switch
    {
        null => "",
        // 多行文本常量：e2txt 把 CRLF 写成裸 CR，保证一条常量占一行；t2e 时 CR → CRLF 还原
        string s => "\"“" + s.Replace("\r\n", "\r") + "”\"",
        bool b => "\"" + (b ? "真" : "假") + "\"",
        DateTime d => "\"" + CodeRenderer.DateLit(d) + "\"",
        double v => "\"" + CodeRenderer.Number(v) + "\"",
        _ => "\"" + c.Value + "\"",
    };

    // ================================================================ 模块描述
    private void WriteModuleDescs()
    {
        foreach (var m in _p.Modules)
        {
            var o = new JObject { ["Source"] = m.Dep.Path };
            if (m.StructRenames.Count > 0)
            {
                o["Rename"] = new JObject
                {
                    ["Struct"] = m.StructRenames.Select(x => (object?)new JObject { ["Src"] = x.Src, ["Dst"] = x.Dst }).ToList(),
                };
            }
            WriteJson(Path.Combine(_root, _l.Module, SafeName(m.Name) + ".desc.json"), o);
        }
    }

    // ================================================================ 配置
    private void WriteConfigs()
    {
        var dir = Path.Combine(_root, _l.Config);

        var libs = _p.Code.Libraries.Select(x => (object?)new JObject
        {
            ["CmdCount"] = x.MinRequiredCmd,
            ["Guid"] = x.GuidString,
            ["Key"] = x.FileName,
            ["MaxRefConstPos"] = (int)x.MinRequiredConstant,
            ["MaxRefObjectPos"] = (int)x.MinRequiredDataType,
            ["Name"] = x.Name,
            ["Version"] = new JObject { ["Major"] = x.Version.Major, ["Minor"] = x.Version.Minor },
        }).ToList();
        WriteJson(Path.Combine(dir, _l.LibConfig), libs);

        var si = _p.SystemInfo;
        WriteJson(Path.Combine(dir, _l.SystemConfig), new JObject
        {
            ["Lang"] = si?.Language ?? 1,
            ["ProjectType"] = si?.ProjectType ?? 0,
            ["ProjectVersion"] = new JObject { ["Major"] = si?.EProjectFormatVersion.Major ?? 1, ["Minor"] = si?.EProjectFormatVersion.Minor ?? 7 },
            ["Type"] = si?.FileType ?? 1,
            ["Version"] = new JObject { ["Major"] = si?.ESystemVersion.Major ?? 5, ["Minor"] = si?.ESystemVersion.Minor ?? 6 },
        });

        var u = _p.UserInfo;
        var ver = u?.Version ?? new Version(1, 0, 0, 0);
        WriteJson(Path.Combine(dir, _l.UserConfig), new JObject
        {
            ["Address"] = u?.Address ?? "",
            ["Author"] = u?.Author ?? "",
            ["BuilderVersion"] = new JObject { ["Major"] = ver.Build, ["Minor"] = ver.Revision },
            ["CompilePlugins"] = (u?.CompilePlugins ?? "").TrimEnd('\0'),
            ["Copyright"] = u?.Copyright ?? "",
            ["Description"] = u?.Description ?? "",
            ["Email"] = u?.Email ?? "",
            ["ExportPublicClassMethod"] = u?.ExportPublicClassMethod ?? false,
            ["FaxNumber"] = u?.FaxNumber ?? "",
            ["Homepage"] = u?.Homepage ?? "",
            ["Name"] = u?.Name ?? "",
            ["ReleaseVersion"] = new JObject { ["Major"] = ver.Major, ["Minor"] = ver.Minor },
            ["TelephoneNumber"] = u?.TelephoneNumber ?? "",
            ["WriteVersion"] = u?.WriteVersion ?? true,
            ["ZipCode"] = u?.ZipCode ?? "",
        });

        var init = _p.Doc.GetOrNull(InitECSection.Key);
        var initObj = new JObject();
        if (init != null)
            for (int i = 0; i < init.ECName.Length && i < init.InitMethod.Length; i++)
                initObj[init.ECName[i]] = _n.User(init.InitMethod[i]);
        WriteJson(Path.Combine(dir, _l.EcInitConfig), initObj);

        var ep = _p.Doc.GetOrNull(EPackageInfoSection.Key);
        // 易包信息与子程序一一对应，绝大多数为空；全空时 e2txt 写 []
        var files = new List<object?>();
        if (ep != null && ep.FileNames.Any(x => !string.IsNullOrEmpty(x)))
            foreach (var x in ep.FileNames) files.Add(x ?? "");
        WriteJson(Path.Combine(dir, _l.EpackFilesConfig), new JObject { ["FileNames"] = files });
    }

    // ================================================================ 窗口（不透明带出，t2e 原样带回）
    private void WriteForms()
    {
        foreach (var f in _p.Resource.Forms)
        {
            var ctrlList = f.Elements.OfType<FormControlInfo>().ToList();
            var indexOf = new Dictionary<int, int>();
            for (int i = 0; i < ctrlList.Count; i++) indexOf[ctrlList[i].Id] = i;
            var parentOf = new Dictionary<int, int>();
            foreach (var c in ctrlList)
                foreach (var ch in c.Children ?? Array.Empty<int>()) parentOf[ch] = c.Id;

            var controls = ctrlList.Select(c => (object?)ControlJson(c, indexOf, parentOf)).ToList();
            var menus = f.Elements.OfType<FormMenuInfo>().Select(m => (object?)MenuJson(m)).ToList();
            var o = new JObject
            {
                ["Class"] = f.Class == 0 ? "" : _n.User(f.Class),
                ["Comment"] = f.Comment ?? "",
                ["Controls"] = controls,
                ["Id"] = f.Id,
                ["Menus"] = menus,
                ["Name"] = f.Name ?? "",
                ["UnknownAfterId"] = f.MemoryAddress,
                ["UnknownBeforeClass"] = f.UnknownBeforeClass,
            };
            // 窗口在分组里 → class/<分组>/；否则 form/
            var dir = _itemFolder.ContainsKey(f.Id) ? ItemDir(f.Id) : Path.Combine(_root, _l.Form);
            WriteJson(Path.Combine(dir, SafeName(f.Name ?? $"0x{f.Id:X8}") + ".form.json"), o);
        }
    }

    private static string Hex(IEnumerable<byte>? b) => b == null ? "" : Convert.ToHexString(b.ToArray());

    private string ElemTypeName(int dataType)
    {
        if (EplSystemId.IsLibDataType(dataType))
        {
            EplSystemId.DecomposeLibDataTypeId(dataType, out var lib, out var type);
            return _n.LibTypeName(lib, type);
        }
        return _n.DataType(dataType);
    }

    /// <summary>
    /// 控件 JSON。Children = 子控件在本窗口 Controls 数组里的 **1 基下标**（e2txt 约定，Parent 由此推出）。
    /// </summary>
    private JObject ControlJson(FormControlInfo c, Dictionary<int, int> indexOf, Dictionary<int, int> parentOf)
    {
        var o = new JObject
        {
            ["Children"] = (c.Children ?? Array.Empty<int>()).Select(x => (object?)(indexOf.TryGetValue(x, out var i) ? i + 1 : 0)).ToList(),
            ["Comment"] = c.Comment ?? "",
            ["Cursor"] = Hex(c.Cursor),
            ["DataType"] = ElemTypeName(c.DataType),
            ["Disable"] = c.Disable,
            ["ExtensionData"] = Hex(c.ExtensionData),
            ["Height"] = c.Height,
            ["Left"] = c.Left,
            ["Name"] = c.Name ?? "",
            ["Tag"] = c.Tag ?? "",
            ["Top"] = c.Top,
            ["UnknownBeforeEvents"] = c.TabIndex,
            ["UnknownBeforeExtensionData"] = Hex(c.UnknownBeforeExtensionData),
            ["UnknownBeforeLeft"] = c.CWndAddress,
            ["UnknownBeforeName"] = Hex(c.UnknownBeforeName),
            ["UnknownBeforeParent"] = c.UnknownBeforeParent,
            ["UnknownBeforeVisible"] = c.UnknownBeforeVisible,
            ["Visible"] = c.Visible,
            ["Width"] = c.Width,
        };
        // e2txt 不写、但无损回写需要的字段：只在非默认值时补充（默认值时与 e2txt 逐字节一致）
        int derivedParent = parentOf.TryGetValue(c.Id, out var dp) ? dp : 0;
        if (c.Parent != derivedParent) o["Parent"] = indexOf.TryGetValue(c.Parent, out var pi) ? pi + 1 : 0;
        if (c.TabStop) o["TabStop"] = true;
        if (c.Locked) o["Locked"] = true;
        if (c.Events is { Length: > 0 })
            o["Events"] = c.Events.Select(x => (object?)new JObject { ["Event"] = x.Key, ["Method"] = _n.User(x.Value) }).ToList();
        return o;
    }

    private JObject MenuJson(FormMenuInfo m)
    {
        var o = new JObject
        {
            ["DataType"] = ElemTypeName(m.DataType),
            ["Disable"] = m.Disable,
            ["HotKey"] = m.HotKey,
            ["Level"] = m.Level,
            ["Name"] = m.Name ?? "",
            ["Selected"] = m.Selected,
            ["Text"] = m.Text ?? "",
            ["UnknownAfterClickEvent"] = Hex(m.UnknownAfterClickEvent),
            ["UnknownBeforeName"] = Hex(m.UnknownBeforeName),
            ["Visible"] = m.Visible,
        };
        // 仅 EC 有效的点击事件映射；非零时补充（e2txt 不写）
        if (m.ClickEvent != 0) o["ClickEvent"] = _n.User(m.ClickEvent);
        return o;
    }

    // ================================================================ 资源（图片/声音）
    /// <summary>
    /// 图片/声音资源 + 长文本常量 → resource/。
    /// 命名：有名用名；无名用 `图片资源_`/`声音资源_` + ID 小端 hex。
    /// 扩展名：PNG→.png，长文本→.txt（GBK 原始字节），其余图片 .image.dat、声音 .sound.dat。
    /// resource.desc.json 只记有备注或公开的项（e2txt 行为）。
    /// </summary>
    private void WriteResources()
    {
        var dir = Path.Combine(_root, _l.Resource);
        var desc = new JObject();
        foreach (var c in _p.Resource.Constants)
        {
            if (_p.ModuleIndexOf(c.Id) >= 0) continue;
            uint kind = (uint)c.Id >> 28;
            byte[] data;
            string ext;
            if (kind == 2 || kind == 3)
            {
                data = c.Value as byte[] ?? Array.Empty<byte>();
                ext = kind == 3 ? ".sound.dat" : ResourceExt(data);
            }
            else if (kind == 1 && IsLongText(c))
            {
                data = _p.Encoding.GetBytes((string)c.Value);
                ext = ".txt";
            }
            else continue;

            var name = string.IsNullOrEmpty(c.Name)
                ? (kind == 3 ? "声音资源_" : "图片资源_") + Convert.ToHexString(BitConverter.GetBytes(c.Id))
                : c.Name;
            WriteBytes(Path.Combine(dir, SafeName(name) + ext), data);
            if (!string.IsNullOrEmpty(c.Comment) || c.Public)
                desc[name] = new JObject { ["Comment"] = c.Comment ?? "", ["IsPublic"] = c.Public };
        }
        WriteJson(Path.Combine(dir, _l.ResourceDesc), desc);
    }

    private static string ResourceExt(byte[] d)
    {
        if (d.Length >= 8 && d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47) return ".png";
        return ".image.dat";
    }

    // ================================================================ project.etprj（GBK 无 BOM）
    private void WriteProject()
    {
        var src = _o.SourcePath == null ? "" : RelativeSource(_o.SourcePath);
        var o = new JObject
        {
            ["AsyncFile"] = false,
            ["E2TXT-EFile"] = "",
            ["E2TXT-InSourceDir"] = false,
            ["E2TXT-IsCreateE"] = false,
            ["Encoding"] = "UTF-8",
            ["Level"] = _o.Level,
            ["NameStyle"] = _l.NameStyle,
            ["Password"] = "",
            ["ResetNames"] = new List<object?>(),
            ["Source"] = src,
        };
        WriteBytes(Path.Combine(_root, _l.Project), TextFiles.Gbk.GetBytes(EJson.Serialize(o)));
    }

    private string RelativeSource(string src)
    {
        try
        {
            var rel = Path.GetRelativePath(Path.GetFullPath(_root), Path.GetFullPath(src));
            return rel.Replace('\\', '/');
        }
        catch
        {
            return src.Replace('\\', '/');
        }
    }
}
