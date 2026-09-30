using System.Text;
using OpenEpl.ELibInfo;
using OpenEpl.ELibInfo.Loader;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Sections;

namespace QAQ.ETool.Core.Model;

/// <summary>
/// 已加载的易语言工程：EProjectFile 文档 + 支持库元数据 + 名称表 + 模块归属。
/// e2t 与 t2e 共用。
/// </summary>
public sealed class EProject
{
    public EplDocument Doc { get; }
    public CodeSection Code { get; }
    public ResourceSection Resource { get; }
    public ESystemInfoSection? SystemInfo { get; }
    public ProjectConfigSection? UserInfo { get; }
    public ECDependenciesSection? ECDeps { get; }
    public LosableSection? Losable { get; }
    public Encoding Encoding { get; }

    /// <summary>支持库元数据，与 Code.Libraries 下标一一对应；加载失败为 null</summary>
    public ELibManifest?[] Libs { get; }

    /// <summary>模块（易模块引用），与 ECDeps.ECDependencies 下标一一对应</summary>
    public List<ModuleInfo> Modules { get; } = new();

    // ---- 索引 ----
    public Dictionary<int, ClassInfo> ClassById { get; } = new();
    public Dictionary<int, MethodInfo> MethodById { get; } = new();
    public Dictionary<int, int> MethodToClass { get; } = new();
    public Dictionary<int, StructInfo> StructById { get; } = new();
    public Dictionary<int, DllDeclareInfo> DllById { get; } = new();
    public Dictionary<int, ConstantInfo> ConstById { get; } = new();
    public Dictionary<int, FormInfo> FormById { get; } = new();
    public Dictionary<int, FormInfo> FormByClass { get; } = new();
    /// <summary>组件/菜单 ID → (所属窗口, 元素)</summary>
    public Dictionary<int, (FormInfo Form, FormElementInfo Elem)> ElementById { get; } = new();
    /// <summary>用户定义 ID → 名称（程序集/子程序/变量/参数/常量/类型/成员/窗口/组件）</summary>
    public Dictionary<int, string> Names { get; } = new();
    /// <summary>被模块重命名（Rename）的类型：ID → 引用处使用的新名称</summary>
    public Dictionary<int, string> RenamedTypes { get; } = new();
    /// <summary>ID → 所属模块下标；不在字典里 = 主工程</summary>
    public Dictionary<int, int> ModuleOf { get; } = new();

    public EProject(EplDocument doc)
    {
        Doc = doc;
        Code = doc.Get(CodeSection.Key);
        Resource = doc.GetOrNull(ResourceSection.Key) ?? new ResourceSection { Forms = new(), Constants = new() };
        SystemInfo = doc.GetOrNull(ESystemInfoSection.Key);
        UserInfo = doc.GetOrNull(ProjectConfigSection.Key);
        ECDeps = doc.GetOrNull(ECDependenciesSection.Key);
        Losable = doc.GetOrNull(LosableSection.Key);
        Encoding = doc.DetermineEncoding();
        Libs = LoadLibs(Code.Libraries);
        BuildIndex();
        BuildModules();
        BuildRenames();
    }

    public static EProject Load(string path)
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        var doc = new EplDocument();
        using (var fs = File.OpenRead(path)) doc.Load(fs);
        return new EProject(doc);
    }

    // ------------------------------------------------------------------
    /// <summary>按引用信息加载支持库元数据（失败项为 null）。t2e 的 Builder 也用它。</summary>
    public static ELibManifest?[] LoadLibs(LibraryRefInfo[] libs)
    {
        var loader = LibLoader.Instance;
        return libs.Select(x =>
        {
            try
            {
                Guid.TryParseExact(x.GuidString.Replace("-", "").Replace("{", "").Replace("}", ""), "N", out var guid);
                return loader.Load(guid, x.FileName, x.Version);
            }
            catch (Exception)
            {
                return null;
            }
        }).ToArray();
    }

    private void Add(int id, string? name)
    {
        if (name != null) Names[id] = name;
    }

    private void BuildIndex()
    {
        foreach (var c in Code.Classes)
        {
            ClassById[c.Id] = c;
            Add(c.Id, c.Name);
            foreach (var m in c.Methods) MethodToClass[m] = c.Id;
            foreach (var v in c.Variables) Add(v.Id, v.Name);
        }
        foreach (var m in Code.Methods)
        {
            MethodById[m.Id] = m;
            Add(m.Id, m.Name);
            foreach (var v in m.Parameters) Add(v.Id, v.Name);
            foreach (var v in m.Variables) Add(v.Id, v.Name);
        }
        foreach (var s in Code.Structs)
        {
            StructById[s.Id] = s;
            Add(s.Id, s.Name);
            foreach (var v in s.Members) Add(v.Id, v.Name);
        }
        foreach (var d in Code.DllDeclares)
        {
            DllById[d.Id] = d;
            Add(d.Id, d.Name);
            foreach (var v in d.Parameters) Add(v.Id, v.Name);
        }
        foreach (var g in Code.GlobalVariables) Add(g.Id, g.Name);
        foreach (var c in Resource.Constants)
        {
            ConstById[c.Id] = c;
            Add(c.Id, c.Name);
        }
        foreach (var f in Resource.Forms)
        {
            FormById[f.Id] = f;
            if (f.Class != 0) FormByClass[f.Class] = f;
            Add(f.Id, f.Name);
            foreach (var e in f.Elements)
            {
                ElementById[e.Id] = (f, e);
                if (!string.IsNullOrEmpty(e.Name)) Add(e.Id, e.Name);
            }
        }
        // 已删除但仍被引用的定义（可丢失程序段）：仅在名称缺失时补
        if (Losable?.RemovedDefinedItems != null)
            foreach (var x in Losable.RemovedDefinedItems)
                if (!Names.ContainsKey(x.Id) && !string.IsNullOrEmpty(x.Name)) Names[x.Id] = x.Name;
    }

    private void BuildModules()
    {
        if (ECDeps == null) return;
        for (int i = 0; i < ECDeps.ECDependencies.Count; i++)
        {
            var d = ECDeps.ECDependencies[i];
            Modules.Add(new ModuleInfo(i, d));
        }
        bool InModule(int id, out int mi)
        {
            for (int i = 0; i < Modules.Count; i++)
                foreach (var r in Modules[i].Dep.DefinedIds)
                    if (unchecked((uint)id - (uint)r.Start) < (uint)r.Count) { mi = i; return true; }
            mi = -1;
            return false;
        }
        void Mark(int id, int mi) { if (mi >= 0) ModuleOf[id] = mi; }

        foreach (var m in Code.Methods)
            if (InModule(m.Id, out var mi))
            {
                Mark(m.Id, mi);
                foreach (var v in m.Parameters) Mark(v.Id, mi);
                foreach (var v in m.Variables) Mark(v.Id, mi);
            }
        foreach (var c in Code.Classes)
        {
            if (!InModule(c.Id, out var mi))
            {
                // 普通程序集（0x09）不在 DefinedIds 区段里：按它的子程序归属判定
                var owners = c.Methods.Select(x => ModuleOf.TryGetValue(x, out var o) ? o : -1).ToList();
                mi = owners.Count > 0 && owners.All(o => o == owners[0]) ? owners[0] : -1;
            }
            if (mi >= 0)
            {
                Mark(c.Id, mi);
                foreach (var v in c.Variables) Mark(v.Id, mi);
            }
        }
        foreach (var s in Code.Structs)
            if (InModule(s.Id, out var mi)) { Mark(s.Id, mi); foreach (var v in s.Members) Mark(v.Id, mi); }
        foreach (var d in Code.DllDeclares)
            if (InModule(d.Id, out var mi)) { Mark(d.Id, mi); foreach (var v in d.Parameters) Mark(v.Id, mi); }
        foreach (var g in Code.GlobalVariables)
            if (InModule(g.Id, out var mi)) Mark(g.Id, mi);
        foreach (var c in Resource.Constants)
            if (InModule(c.Id, out var mi)) Mark(c.Id, mi);
    }

    /// <summary>
    /// 多个模块定义同名数据类型时，e2txt 给后出现者在引用处改名（原名 + 序号），
    /// 并在 module/&lt;名&gt;.desc.json 里记 Rename.Struct（spec §3.10）。
    /// </summary>
    private void BuildRenames()
    {
        var used = new Dictionary<string, int>(StringComparer.Ordinal);
        // 主工程的类型先占名
        foreach (var s in Code.Structs.Where(s => !ModuleOf.ContainsKey(s.Id)))
            used.TryAdd(s.Name, 1);
        for (int mi = 0; mi < Modules.Count; mi++)
        {
            foreach (var s in Code.Structs.Where(s => ModuleOf.TryGetValue(s.Id, out var o) && o == mi))
            {
                if (used.TryGetValue(s.Name, out var n))
                {
                    string dst;
                    do { n++; dst = s.Name + n; } while (used.ContainsKey(dst));
                    used[s.Name] = n;
                    used[dst] = 1;
                    RenamedTypes[s.Id] = dst;
                    Modules[mi].StructRenames.Add((s.Name, dst));
                }
                else used[s.Name] = 1;
            }
        }
    }

    // ------------------------------------------------------------------
    public int ModuleIndexOf(int id) => ModuleOf.TryGetValue(id, out var m) ? m : -1;

    public LibraryRefInfo LibRef(int lib) => Code.Libraries[lib];

    public ELibDataTypeInfo? LibType(int lib, int type)
    {
        var l = lib >= 0 && lib < Libs.Length ? Libs[lib] : null;
        if (l == null || type < 0 || type >= l.DataTypes.Length) return null;
        return l.DataTypes[type];
    }
}

public sealed class ModuleInfo
{
    public int Index { get; }
    public ECDependencyInfo Dep { get; }
    public List<(string Src, string Dst)> StructRenames { get; } = new();
    public ModuleInfo(int index, ECDependencyInfo dep) { Index = index; Dep = dep; }
    public string Name => Dep.Name;
}

/// <summary>
/// 支持库元数据加载器入口。缓存放在 %LOCALAPPDATA%\QAQ-ETool\libcache（可用 QAQ_ETOOL_LIBCACHE 覆盖），
/// 导出器 qaq-libinfo.exe 与主程序同目录。
/// </summary>
public static class LibLoader
{
    private static IELibInfoLoader? _instance;
    public static string? LibDir { get; private set; }

    public static IELibInfoLoader Instance
    {
        get
        {
            if (_instance != null) return _instance;
            var cache = Environment.GetEnvironmentVariable("QAQ_ETOOL_LIBCACHE")
                        ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "QAQ-ETool", "libcache");
            LibDir = QaqLibLoader.FindLibDir();
            var exporter = Path.Combine(AppContext.BaseDirectory, "qaq-libinfo.exe");
            _instance = new QaqLibLoader(cache, LibDir, exporter);
            return _instance;
        }
    }
}
