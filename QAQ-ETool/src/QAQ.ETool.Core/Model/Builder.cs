using System.Collections.Immutable;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using OpenEpl.ELibInfo;
using QIQI.EProjectFile;
using QIQI.EProjectFile.EditorTabInfo;
using QIQI.EProjectFile.Sections;
using QIQI.EProjectFile.Statements;
using QAQ.ETool.Core.Text;
using QAQ.ETool.Core.Text.Ast;

namespace QAQ.ETool.Core.Model;

public sealed class ImportOptions
{
    /// <summary>代码绑定失败时回退为「未验证代码」（IDE 红字）继续生成，而不是整体失败</summary>
    public bool Lenient { get; init; }
    /// <summary>旁路文件（null = 按纯文本组包，与 e2txt 目录等价）</summary>
    public Sidecar? Sidecar { get; init; }
}

public sealed class ImportResult
{
    public EplDocument? Doc { get; set; }
    /// <summary>File:Line: 消息</summary>
    public List<string> Errors { get; } = new();
    public List<string> Warnings { get; } = new();
    /// <summary>因绑定失败而以未验证代码写入的语句数</summary>
    public int Unexamined { get; set; }
    /// <summary>旁路文件套用情况（未使用旁路 = null）</summary>
    public SidecarStats? Sidecar { get; set; }
}

public sealed class SidecarStats
{
    public int IdsKept, IdsNew, IdsLost, CodeKept, CodeRebound, SectionsKept, SectionsDropped, SectionsRebuilt;
    public override string ToString() =>
        $"沿用 ID {IdsKept}, 新 ID {IdsNew}, 已删除 {IdsLost}; 代码沿用原字节 {CodeKept}, 按文本重绑 {CodeRebound}; 段带回 {SectionsKept}, 舍弃 {SectionsDropped}, 按文本重建 {SectionsRebuilt}";
}

// ===================================================================== 符号表（Builder 建、Binder 用）
internal sealed class SScope
{
    public int Mod;                 // -1 = 主工程
    public string Name = "";
    /// <summary>数据类型「引用名」→ ID（自定义类型按改名后的名称；类；窗口）</summary>
    public readonly Dictionary<string, int> Types = new(StringComparer.Ordinal);
    public readonly Dictionary<string, SClass> Classes = new(StringComparer.Ordinal);
    /// <summary>普通程序集（0x09）里的子程序；同名时公开的优先</summary>
    public readonly Dictionary<string, SMethod> StaticMethods = new(StringComparer.Ordinal);
    /// <summary>窗口程序集（0x19）里的子程序：可在其它程序集里直接调用（大项目 `程序结束前操作 (真)` 实测）</summary>
    public readonly Dictionary<string, SMethod> FormMethods = new(StringComparer.Ordinal);
    public readonly Dictionary<string, DllDeclareInfo> Dlls = new(StringComparer.Ordinal);
    /// <summary>常量与图片/声音资源</summary>
    public readonly Dictionary<string, ConstantInfo> Consts = new(StringComparer.Ordinal);
    public readonly Dictionary<string, GlobalVariableInfo> Globals = new(StringComparer.Ordinal);
    public readonly List<SClass> ClassList = new();
}

internal sealed class SClass
{
    public TClass T = null!;
    public ClassInfo Info = null!;
    public int Mod;
    public SForm? Form;
    public SClass? Base;
    public readonly List<SMethod> Methods = new();
    public readonly Dictionary<string, SMethod> MethodByName = new(StringComparer.Ordinal);
    public readonly Dictionary<string, ClassVariableInfo> Vars = new(StringComparer.Ordinal);
    public int Kind => EplSystemId.GetType(Info.Id);
}

internal sealed class SMethod
{
    public TMethod T = null!;
    public MethodInfo Info = null!;
    public SClass Class = null!;
}

internal sealed class SForm
{
    public TForm T = null!;
    public FormInfo Info = null!;
    public string Name = "";
    public SClass? Class;
    public FormControlInfo? Self;
    /// <summary>组件/菜单（有名的）</summary>
    public readonly Dictionary<string, FormElementInfo> Elems = new(StringComparer.Ordinal);
}

/// <summary>
/// t2e：TextProject（Importer 读出的文本工程）→ EplDocument。
/// 三阶段：① 建实体、分配 ID、登记名称；② 解析数据类型/基类/常量值/窗口；③ 绑定代码体（Binder）。
/// 版式约定见 docs/e2txt-format-spec.md，与 Exporter 对称。
/// </summary>
public sealed class Builder
{
    private readonly TextProject _t;
    private readonly ImportOptions _o;
    private readonly ImportResult _r = new();

    internal Encoding Enc = Encoding.UTF8;
    internal LibraryRefInfo[] LibRefs = Array.Empty<LibraryRefInfo>();
    internal ELibManifest?[] Libs = Array.Empty<ELibManifest?>();
    internal int Krnln;             // krnln 在库表里的下标（通常为 0）
    internal bool Lenient => _o.Lenient;

    internal readonly List<SScope> Scopes = new();      // [0] 主工程，[i+1] 第 i 个模块
    internal SScope Main => Scopes[0];
    internal SScope ScopeOf(int mod) => Scopes[mod + 1];
    internal readonly List<SClass> Classes = new();     // Code.Classes 顺序
    internal readonly List<SForm> Forms = new();
    internal readonly Dictionary<string, SForm> FormByName = new(StringComparer.Ordinal);
    internal readonly Dictionary<int, SForm> FormById = new();
    internal readonly Dictionary<int, SClass> ClassById = new();
    internal readonly Dictionary<int, StructInfo> StructById = new();
    internal readonly Dictionary<int, Dictionary<string, StructMemberInfo>> StructMembers = new();

    private readonly List<StructInfo> _structs = new();
    private readonly List<DllDeclareInfo> _dlls = new();
    private readonly List<GlobalVariableInfo> _globals = new();
    private readonly List<ConstantInfo> _consts = new();
    private readonly List<ConstantInfo> _resConsts = new();
    private readonly List<MethodInfo> _methods = new();
    private readonly List<TModule> _modules = new();    // 按推断出的原始顺序
    private readonly List<ECDependencyInfo> _deps = new();

    // ---- 支持库索引
    internal readonly Dictionary<string, (int Lib, int Type)> LibTypes = new(StringComparer.Ordinal);
    internal readonly Dictionary<string, (int Lib, int Cmd)> LibGlobalCmds = new(StringComparer.Ordinal);
    internal readonly Dictionary<string, (int Lib, int Idx)> LibConsts = new(StringComparer.Ordinal);

    // ---- 旁路文件
    private readonly Sidecar? _sc;
    private readonly Dictionary<object, string> _keys;
    private readonly HashSet<string> _consumed = new(StringComparer.Ordinal);
    private readonly HashSet<int> _usedNum = new();
    private int _fresh;

    private Builder(TextProject t, ImportOptions o)
    {
        _t = t;
        _o = o;
        _sc = o.Sidecar;
        _keys = _sc != null ? Sidecar.KeysOf(t) : new Dictionary<object, string>(ReferenceEqualityComparer.Instance);
    }

    private Sidecar.Item? Sc(object t) =>
        _sc != null && _keys.TryGetValue(t, out var k) && _sc.Items.TryGetValue(k, out var it) ? it : null;

    /// <summary>实体 ID：旁路里有同路径、同类型且未被占用的原 ID 就沿用，否则分配新 ID</summary>
    private int Id(object t, int type)
    {
        if (_sc != null && _keys.TryGetValue(t, out var k) && _sc.Items.TryGetValue(k, out var it)
            && EplSystemId.GetType(it.Id) == type && _usedNum.Add(it.Id & EplSystemId.Mask_Num))
        {
            _consumed.Add(k);
            return Track(it.Id);
        }
        return New(type);
    }

    /// <summary>固定 ID（窗口、占位资源）：与旁路一致时记为已沿用</summary>
    private void Fixed(object t, int id)
    {
        _usedNum.Add(id & EplSystemId.Mask_Num);
        if (_sc != null && _keys.TryGetValue(t, out var k) && _sc.Items.TryGetValue(k, out var it) && it.Id == id) _consumed.Add(k);
    }

    /// <summary>标志：文本可表达的位（mask）以文本为准，其余位取旁路原值</summary>
    private int Fl(object t, int computed, int mask) =>
        Sc(t) is { Flags: { } raw } ? (raw & ~mask) | (computed & mask) : computed;

    private void VarX(object t, AbstractVariableInfo v, int mask, bool ubound)
    {
        v.Flags = Fl(t, v.Flags, mask);
        if (Sc(t) is not { } it) return;
        if (ubound && it.UBound != null && (v.UBound == null || v.UBound.Length == 0)) v.UBound = it.UBound;
        if (it.Tail != null) v.TrailingBytes = Convert.FromBase64String(it.Tail);
    }

    private int Mem(object t, int def = 0) => Sc(t)?.Mem ?? def;

    public static ImportResult Build(TextProject t, ImportOptions? o = null) => new Builder(t, o ?? new ImportOptions()).Run();

    internal void Error(string where, string msg) => _r.Errors.Add($"{where}: {msg}");
    internal void Warn(string where, string msg) => _r.Warnings.Add($"{where}: {msg}");
    internal void CountUnexamined() => _r.Unexamined++;

    private ImportResult Run()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
        _r.Errors.AddRange(_t.Errors);
        _r.Warnings.AddRange(_t.Warnings);

        var sys = BuildSystemInfo();
        Enc = sys.DetermineEncoding();
        LoadLibraries();
        OrderModules();

        ReserveIds();
        CreateEntities();
        CreateSubEntities();
        ResolveDeclarations();
        BuildForms();
        BindCode();

        _rebuiltTabs = BuildEditorInfo();

        var doc = new EplDocument();
        doc.Sections.Add(sys);
        doc.Sections.Add(BuildUserInfo());
        doc.Sections.Add(new ResourceSection
        {
            Forms = Forms.Select(f => f.Info).ToList(),
            Constants = _consts.Concat(_resConsts).ToList(),
        });
        doc.Sections.Add(BuildCode());
        doc.Sections.Add(new EPackageInfoSection { FileNames = EPackageNames() });
        doc.Sections.Add(BuildInitEC());
        doc.Sections.Add(new ECDependenciesSection { ECDependencies = _deps });
        doc.Sections.Add(BuildClassPublicity());
        doc.Sections.Add(new LosableSection { OutFile = "", RemovedDefinedItems = new List<RemovedDefinedItemInfo>() });
        doc.Sections.Add(new EventIndicesSection { Indices = BuildEventIndices() });
        var folders = BuildFolders();
        if (folders != null) doc.Sections.Add(folders);
        // 无旁路时保持 IDE 的写法（总带这些段）；有旁路时按原件取舍（见 ApplySidecar）
        if (_sc == null)
        {
            // 打开的标签页（编辑信息段2）：文本层有描述就按当前 ID 重建（IDE 位置惯例在段序列尾部）
            if (_rebuiltTabs != null) doc.Sections.Add(_rebuiltTabs);
        }
        else ApplySidecar(doc);
        _r.Doc = doc;
        return _r;
    }

    // ================================================================ 旁路：段 / 代码字节
    private readonly Dictionary<SMethod, (int W0, int W1, int U)> _bindLog = new(ReferenceEqualityComparer.Instance);

    private void ApplySidecar(EplDocument doc)
    {
        var sc = _sc!;
        var st = new SidecarStats
        {
            IdsKept = _consumed.Count,
            IdsNew = _fresh,
            IdsLost = sc.Items.Keys.Count(k => !_consumed.Contains(k)),
        };
        bool allKept = st.IdsLost == 0 && st.IdsNew == 0;

        // ---- 段：按原顺序排列，t2e 不生成的段原样带回
        var gen = doc.Sections.ToList();
        doc.Sections.Clear();
        foreach (var raw in sc.Sections)
        {
            var g = gen.FirstOrDefault(x => x.SectionKey == raw.Key);
            ISection? s = g;
            if (g != null) gen.Remove(g);
            // 文本不表达的段（如可丢失程序段）：旁路优先于生成的缺省段
            if (raw.Data != null && Sidecar.Passthrough(raw.Key))
            {
                if (raw.Key == EditorInfoSection.Key.SectionKey && !allKept && _rebuiltTabs != null)
                {
                    // 实体有增删：编辑信息段2 里的原 ID 已不可靠，改用文本层按名字重建的版本
                    s = _rebuiltTabs;
                    st.SectionsRebuilt++;
                }
                else if (Sidecar.IdSensitive(raw.Key) && !allKept)
                {
                    st.SectionsDropped++;
                    continue;
                }
                else
                {
                    var data = Convert.FromBase64String(raw.Data);
                    s = new GeneralSection(raw.Name, raw.Key, raw.Optional, data);
                    if (PredefinedSections.Keys.TryGetValue(raw.Key, out var key))
                    {
                        try { s = key.Parse(new QIQI.EProjectFile.Context.BlockParserContext(data, Enc, false)); }
                        catch (Exception) { /* 解析不了就按原字节写 */ }
                    }
                    st.SectionsKept++;
                }
            }
            if (s == null) continue;
            if (s.IsOptional != raw.Optional) doc.OptionalOverrides[s] = raw.Optional;
            doc.Sections.Add(s);
        }
        // 原件没有、这次新生成的段（如新建了文件夹）放在最后；空的可省略段不补
        doc.Sections.AddRange(gen.Where(x => !IsEmptySection(x)));
        // 文本层重建的编辑信息段：原件没有该段（或旁路里缺失）时补在最后
        if (_rebuiltTabs != null && doc.GetOrNull(EditorInfoSection.Key) == null)
            doc.Sections.Add(_rebuiltTabs);
        doc.EofNameXored = sc.EofNameXored;

        // ---- 程序段头
        var code = doc.Get(CodeSection.Key);
        code.AllocatedIdNum = Math.Max(code.AllocatedIdNum, sc.AllocatedIdNum);
        if (sc.Code != null)
        {
            code.Flag = sc.Code.Flag;
            code.DebugCommandParameters = sc.Code.DebugCommandParameters ?? "";
            if (sc.Code.UnknownBeforeIconData != null)
                code.UnknownBeforeIconData = ImmutableArray.Create(Convert.FromBase64String(sc.Code.UnknownBeforeIconData));
            if (sc.Code.MainMethod == 0 || _methods.Any(x => x.Id == sc.Code.MainMethod)) code.MainMethod = sc.Code.MainMethod;
        }

        // ---- 程序集公开性：公开位（主工程）以文本为准，其余位沿用原值
        if (doc.GetOrNull(ClassPublicitySection.Key) is { } cps && sc.ClassPublicity.Count > 0)
        {
            var orig = new Dictionary<int, int>();
            foreach (var x in sc.ClassPublicity) if (x.Length == 2) orig.TryAdd(x[0], x[1]);
            var computed = cps.ClassPublicities.ToDictionary(x => x.Class, x => x.Flags);
            var list = new List<ClassPublicityInfo>();
            foreach (var c in Classes)
            {
                int id = c.Info.Id;
                computed.TryGetValue(id, out var cf);
                int fl = !orig.TryGetValue(id, out var of) ? cf : c.Mod >= 0 ? of : (of & ~0x1) | (cf & 0x1);
                if (fl != 0) list.Add(new ClassPublicityInfo { Class = id, Flags = fl });
            }
            // 原表顺序优先
            var prank = new Dictionary<int, int>();
            foreach (var x in sc.ClassPublicity) if (x.Length == 2) prank.TryAdd(x[0], prank.Count);
            Reorder(list, x => x.Class, prank);
            cps.ClassPublicities = list;
        }

        // ---- 事件索引：原表里窗口/单元/子程序仍存在的条目原样保留，其后补上按命名约定新识别的
        if (doc.GetOrNull(EventIndicesSection.Key) is { } evs && sc.Events.Count > 0)
        {
            var units = new HashSet<int>(Forms.SelectMany(f => f.Info.Elements.Select(e => e.Id)));
            var forms = new HashSet<int>(Forms.Select(f => f.Info.Id));
            var methods = new HashSet<int>(_methods.Select(m => m.Id));
            var list = sc.Events.Where(x => x.Length == 4 && forms.Contains(x[0]) && units.Contains(x[1]) && methods.Contains(x[3]))
                .Select(x => new IndexedEventInfo { FormId = x[0], UnitId = x[1], EventId = x[2], MethodId = x[3] }).ToList();
            var seen = new HashSet<(int, int, int)>(list.Select(x => (x.UnitId, x.EventId, x.MethodId)));
            foreach (var x in evs.Indices) if (seen.Add((x.UnitId, x.EventId, x.MethodId))) list.Add(x);
            evs.Indices = list;
        }

        // ---- 列表顺序：沿用原件顺序（新增项跟在它在文本里的前一项后面）
        var rank = new Dictionary<int, int>();
        foreach (var it in sc.Items.Values) rank.TryAdd(it.Id, rank.Count);
        var res = doc.Get(ResourceSection.Key);
        Reorder(res.Constants, x => x.Id, rank);
        Reorder(res.Forms, x => x.Id, rank);
        foreach (var f in res.Forms) Reorder(f.Elements, x => x.Id, rank);
        var deps = doc.GetOrNull(ECDependenciesSection.Key)?.ECDependencies;
        if (deps != null)
        {
            var dkeys = sc.Deps.Keys.ToList();
            var idx = deps.Select(d => dkeys.IndexOf(d.Name)).ToList();
            var ordered = deps.Select((d, i) => (d, k: idx[i] < 0 ? int.MaxValue : idx[i], i)).OrderBy(t => t.k).ThenBy(t => t.i).Select(t => t.d).ToList();
            deps.Clear();
            deps.AddRange(ordered);
        }
        Reorder(code.Classes, x => x.Id, rank);
        // 子程序表与易包文件名表按下标对应，一起重排
        var epack = doc.GetOrNull(EPackageInfoSection.Key);
        var epackOf = new Dictionary<int, string>();
        if (epack?.FileNames is { } fns && fns.Length == code.Methods.Count)
            for (int i = 0; i < fns.Length; i++) epackOf[code.Methods[i].Id] = fns[i];
        var mrank = new Dictionary<int, int>();
        foreach (var id in sc.MethodOrder) mrank.TryAdd(id, mrank.Count);
        Reorder(code.Methods, x => x.Id, mrank);
        if (epack != null && epackOf.Count == code.Methods.Count) epack.FileNames = code.Methods.Select(x => epackOf[x.Id]).ToArray();
        Reorder(code.GlobalVariables, x => x.Id, rank);
        Reorder(code.Structs, x => x.Id, rank);
        Reorder(code.DllDeclares, x => x.Id, rank);

        // ---- 代码：原始字节按新工程渲染，与文本逐行相同才沿用
        ReuseCode(doc, st);
        _r.Sidecar = st;
    }

    private static void Reorder<T>(List<T>? list, Func<T, int> id, Dictionary<int, int> rank)
    {
        if (list == null || list.Count < 2) return;
        var keys = new double[list.Count];
        double last = -1;
        for (int i = 0; i < list.Count; i++)
        {
            if (rank.TryGetValue(id(list[i]), out var r)) last = r;
            else last += 1e-6;
            keys[i] = last;
        }
        var sorted = list.Select((x, i) => (x, k: keys[i], i)).OrderBy(t => t.k).ThenBy(t => t.i).Select(t => t.x).ToList();
        list.Clear();
        list.AddRange(sorted);
    }

    private static List<string> NormBody(IEnumerable<string> lines)
    {
        var r = lines.Select(l => l.TrimEnd()).ToList();
        while (r.Count > 0 && r[^1].Length == 0) r.RemoveAt(r.Count - 1);
        return r;
    }

    private void ReuseCode(EplDocument doc, SidecarStats st)
    {
        EProject proj;
        try { proj = new EProject(doc); }
        catch (Exception ex)
        {
            Warn(".qaq/sidecar.json", $"无法校验原始代码（{ex.Message}），全部按文本绑定");
            return;
        }
        var names = new QAQ.ETool.Core.Text.NameResolver(proj);
        var dropW = new List<(int, int)>();
        foreach (var c in Classes)
        {
            if (c.Mod >= 0) continue;
            var rc = new QAQ.ETool.Core.Text.CodeRenderer(proj, names)
            {
                UsedLibs = new QAQ.ETool.Core.Text.OrderedIntSet(),
                ConstLibs = new QAQ.ETool.Core.Text.OrderedIntSet(),
            };
            rc.CurrentForm = proj.FormByClass.TryGetValue(c.Info.Id, out var f) ? f : null;
            foreach (var m in c.Methods)
            {
                if (Sc(m.T) is not { Code: { } parts }) continue;
                MethodCodeData cd;
                var lines = new List<string>();
                try
                {
                    cd = Sidecar.DecodeCode(parts, Enc);
                    if (cd.ExpressionData is { Length: > 0 })
                        rc.WriteBlock(CodeDataParser.ParseStatementBlock(cd.ExpressionData, Enc), lines, 1);
                }
                catch (Exception) { st.CodeRebound++; continue; }
                // 逐行相同即沿用（原件里指向已删除项的占位 <?…?> 也会原样出现在文本里）
                if (!NormBody(lines).SequenceEqual(NormBody(m.T.Body)))
                {
                    st.CodeRebound++;
                    continue;
                }
                m.Info.CodeData = cd;
                st.CodeKept++;
                if (_bindLog.TryGetValue(m, out var log))
                {
                    dropW.Add((log.W0, log.W1));
                    _r.Unexamined -= log.U;
                }
            }
        }
        // 沿用了原始字节的子程序：它们绑定期间的警告已不适用
        if (dropW.Count > 0)
        {
            var drop = new HashSet<int>(dropW.SelectMany(x => Enumerable.Range(x.Item1, x.Item2 - x.Item1)));
            var keep = _r.Warnings.Where((_, i) => !drop.Contains(i)).ToList();
            _r.Warnings.Clear();
            _r.Warnings.AddRange(keep);
        }
    }

    // ================================================================ 打开的标签页（编辑信息段2）
    private EditorInfoSection? _rebuiltTabs;
    private const string TabsFile = "class/标签页.desc.json";

    /// <summary>
    /// class/标签页.desc.json → 编辑信息段2：把文本里的名字按当前符号表重新绑定成 ID。
    /// 实体已删除的标签页丢弃（警告），全部解析失败时不生成该段。
    /// </summary>
    private EditorInfoSection? BuildEditorInfo()
    {
        if (_t.OpenTabs.Count == 0) return null;
        var list = new List<IEditorTabInfo>();
        int real = 0;
        foreach (var t in _t.OpenTabs)
        {
            if (t == null) { list.Add(null!); continue; }   // 空占位：vendor 的 Tabs 契约允许 null 元素
            var info = TabInfo(t);
            if (info != null) { list.Add(info); real++; }
        }
        return real == 0 ? null : new EditorInfoSection { Tabs = list };
    }

    private IEditorTabInfo? TabInfo(TOpenTab t)
    {
        switch (t.Kind)
        {
            case TTabKind.Code:
            case TTabKind.ClassTable:
            {
                var cls = FindClass(t);
                if (cls == null) return null;
                short elem;
                if (t.Kind == TTabKind.Code)
                {
                    var sm = FindMethod(cls, t.Method, t.MethodIndex, $"程序集 {cls.T.Name} 的标签页");
                    if (sm == null) return null;
                    elem = (short)cls.Methods.IndexOf(sm);
                }
                else
                {
                    if (t.Table is not > 0) { Warn(TabsFile, $"程序集 {cls.T.Name} 的表格页缺少 Table"); return null; }
                    elem = (short)-t.Table.Value;
                }
                var tab = new ClassEditorTabInfo
                {
                    ClassId = cls.Info.Id,
                    ElemId = elem,
                    Offset = t.Offset,
                    ColumnInTable = (byte)t.Column,
                    SelectionStart = t.SelStart,
                    SelectionCurrent = t.SelCurrent,
                    SelectionEndpoints = CodeEndpoints(cls, t),
                };
                return tab;
            }
            case TTabKind.Form:
            {
                SForm? f = null;
                if (t.Form != null) FormByName.TryGetValue(t.Form, out f);
                if (f == null && t.FormId is int fid) FormById.TryGetValue(fid, out f);
                if (f == null) { Warn(TabsFile, $"标签页引用的窗口 {t.Form ?? $"0x{(t.FormId ?? 0):X8}"} 不存在，已丢弃"); return null; }
                var units = new List<int>();
                foreach (var u in t.Units)
                {
                    if (u == f.Name) { units.Add(f.Self?.Id ?? f.Info.Id); continue; }
                    if (f.Elems.TryGetValue(u, out var el)) { units.Add(el.Id); continue; }
                    Warn(TabsFile, $"窗口 {f.Name} 的标签页选中单元 {u} 不存在，已忽略");
                }
                return new FormDesignerTabInfo { FormId = f.Info.Id, UnitIds = units };
            }
            case TTabKind.Table:
            {
                var pt = t.TableKind switch
                {
                    "struct" => (PureTableEditorTabInfo?)new StructEditorTabInfo(),
                    "global" => new GlobalVariableEditorTabInfo(),
                    "dll" => new DllDeclareEditorTabInfo(),
                    "const" => new ConstantEditorTabInfo(),
                    "image" => new ImageResourceEditorTabInfo(),
                    "sound" => new SoundResourceEditorTabInfo(),
                    _ => null,
                };
                if (pt == null) { Warn(TabsFile, $"标签页的表格类型 {t.TableKind} 无法识别，已丢弃"); return null; }
                pt.Offset = t.Offset;
                pt.ColumnInTable = (byte)t.Column;
                pt.SelectionStart = t.SelStart;
                pt.SelectionCurrent = t.SelCurrent;
                if (t.Endpoints.Count > 0) pt.SelectionEndpoints = t.Endpoints.Select(e => e.Offset).ToList();
                return pt;
            }
            case TTabKind.Raw:
                try { return new GeneralEditorTabInfo((byte)t.RawTypeId, Convert.FromBase64String(t.RawData ?? "")); }
                catch (FormatException) { Warn(TabsFile, "标签页的原始数据不是合法 base64，已丢弃"); return null; }
            default:
                return null;
        }
    }

    /// <summary>程序集名 → SClass：先按 [模块名] 前缀定位作用域，再按名字查；都失败按原始 ID 兜底</summary>
    private SClass? FindClass(TOpenTab t)
    {
        string? name = t.Class;
        if (name != null)
        {
            SScope? scope = Main;
            if (name.StartsWith("[", StringComparison.Ordinal))
            {
                int close = name.IndexOf(']');
                if (close > 0)
                {
                    var mod = name.Substring(1, close - 1);
                    name = name.Substring(close + 1);
                    scope = Scopes.FirstOrDefault(s => s.Mod >= 0 && (SafeName(s.Name) == mod || s.Name == mod))
                            ?? Main;   // 模块名对不上：退回主工程再试
                }
            }
            if (name.Length > 0)
            {
                if (scope.Classes.TryGetValue(name, out var c)) return c;
                foreach (var s in Scopes)
                    if (s != scope && s.Classes.TryGetValue(name, out c)) return c;
            }
        }
        if (t.ClassId is int id && ClassById.TryGetValue(id, out var byId))
        {
            Warn(TabsFile, $"程序集 {t.Class} 按原始 ID 0x{id:X8} 兜底解析");
            return byId;
        }
        Warn(TabsFile, $"标签页引用的程序集 {t.Class ?? $"0x{(t.ClassId ?? 0):X8}"} 不存在，已丢弃");
        return null;
    }

    private SMethod? FindMethod(SClass cls, string? name, int? index, string where)
    {
        if (name != null && cls.MethodByName.TryGetValue(name, out var sm)) return sm;
        if (index is int ix && ix >= 0 && ix < cls.Methods.Count) return cls.Methods[ix];
        Warn(TabsFile, $"{where}找不到子程序 {name ?? $"下标 {index}"}，已丢弃");
        return null;
    }

    /// <summary>多行选区端点：Elem（子程序名）/ Table（表格号）/ MethodIndex 兜底；解析不出就丢弃该端点</summary>
    private List<ClassEditorEndpoint>? CodeEndpoints(SClass cls, TOpenTab t)
    {
        if (t.Endpoints.Count == 0) return null;
        var list = new List<ClassEditorEndpoint>();
        foreach (var e in t.Endpoints)
        {
            if (e.Table is int n)
            {
                list.Add(new ClassEditorEndpoint { ElemId = (short)-n, Offset = e.Offset });
                continue;
            }
            var sm = FindMethod(cls, e.Elem, e.MethodIndex, "标签页选择点");
            if (sm == null) continue;
            list.Add(new ClassEditorEndpoint { ElemId = (short)cls.Methods.IndexOf(sm), Offset = e.Offset });
        }
        return list;
    }

    // ================================================================ 配置
    private static object? Get(JObject? o, string k) => o != null && o.TryGetValue(k, out var v) ? v : null;
    private static int GetInt(JObject? o, string k, int def) => Get(o, k) is long l ? unchecked((int)l) : def;
    private static bool GetBool(JObject? o, string k, bool def) => Get(o, k) is bool b ? b : def;
    private static string GetStr(JObject? o, string k) => Get(o, k) as string ?? "";
    private static (int, int) GetVer(JObject? o, string k, int ma, int mi) =>
        Get(o, k) is JObject v ? (GetInt(v, "Major", ma), GetInt(v, "Minor", mi)) : (ma, mi);

    private ESystemInfoSection BuildSystemInfo()
    {
        var s = _t.SystemConfig;
        var (vma, vmi) = GetVer(s, "Version", 5, 6);
        var (pma, pmi) = GetVer(s, "ProjectVersion", 1, 7);
        return new ESystemInfoSection
        {
            ESystemVersion = new Version(vma, vmi),
            Language = GetInt(s, "Lang", 1),
            EProjectFormatVersion = new Version(pma, pmi),
            FileType = GetInt(s, "Type", 1),
            ProjectType = GetInt(s, "ProjectType", 0),
        };
    }

    private ProjectConfigSection BuildUserInfo()
    {
        var u = _t.UserConfig;
        var (rma, rmi) = GetVer(u, "ReleaseVersion", 1, 0);
        var (bma, bmi) = GetVer(u, "BuilderVersion", 0, 0);
        return new ProjectConfigSection
        {
            Name = GetStr(u, "Name"),
            Description = GetStr(u, "Description"),
            Author = GetStr(u, "Author"),
            ZipCode = GetStr(u, "ZipCode"),
            Address = GetStr(u, "Address"),
            TelephoneNumber = GetStr(u, "TelephoneNumber"),
            FaxNumber = GetStr(u, "FaxNumber"),
            Email = GetStr(u, "Email"),
            Homepage = GetStr(u, "Homepage"),
            Copyright = GetStr(u, "Copyright"),
            Version = new Version(rma, rmi, bma, bmi),
            WriteVersion = GetBool(u, "WriteVersion", true),
            CompilePlugins = GetStr(u, "CompilePlugins"),
            ExportPublicClassMethod = GetBool(u, "ExportPublicClassMethod", false),
        };
    }

    // ================================================================ 支持库
    private void LoadLibraries()
    {
        LibRefs = _t.Libs.Select(l => new LibraryRefInfo
        {
            FileName = l.Key,
            GuidString = l.Guid,
            Version = new Version(l.Major, l.Minor),
            Name = l.Name,
            MinRequiredCmd = l.CmdCount,
            MinRequiredDataType = (short)l.MaxRefObjectPos,
            MinRequiredConstant = (short)l.MaxRefConstPos,
        }).ToArray();
        Libs = EProject.LoadLibs(LibRefs);
        Krnln = Math.Max(0, Array.FindIndex(LibRefs, x => string.Equals(x.FileName, "krnln", StringComparison.OrdinalIgnoreCase)));
        for (int i = 0; i < Libs.Length; i++)
            if (Libs[i] == null) Warn("config/lib.config.json", $"支持库未能加载: {LibRefs[i].FileName} {LibRefs[i].Name}（引用它的代码将无法绑定）");

        foreach (var lib in LibOrder())
        {
            var m = Libs[lib];
            if (m == null) continue;
            var methodCmds = new HashSet<int>();
            for (int t = 0; t < m.DataTypes.Length; t++)
            {
                LibTypes.TryAdd(m.DataTypes[t].Name, (lib, t));
                foreach (var c in m.DataTypes[t].Methods) methodCmds.Add(c);
            }
            for (int c = 0; c < m.Cmds.Length; c++)
                if (!methodCmds.Contains(c)) LibGlobalCmds.TryAdd(m.Cmds[c].Name, (lib, c));
            for (int c = 0; c < m.Constants.Length; c++)
                LibConsts.TryAdd(m.Constants[c].Name, (lib, c));
        }
    }

    /// <summary>名称查找时的库顺序：krnln 优先，其余按库表顺序</summary>
    internal IEnumerable<int> LibOrder()
    {
        if (Krnln < Libs.Length) yield return Krnln;
        for (int i = 0; i < Libs.Length; i++) if (i != Krnln) yield return i;
    }

    internal ELibDataTypeInfo? LibType(int lib, int type)
    {
        var m = lib >= 0 && lib < Libs.Length ? Libs[lib] : null;
        return m != null && type >= 0 && type < m.DataTypes.Length ? m.DataTypes[type] : null;
    }

    internal ELibCmdInfo? LibCmd(int lib, int cmd)
    {
        var m = lib >= 0 && lib < Libs.Length ? Libs[lib] : null;
        return m != null && cmd >= 0 && cmd < m.Cmds.Length ? m.Cmds[cmd] : null;
    }

    /// <summary>
    /// 支持库元数据里的数据类型 ID → 工程里的 ID。库内引用本库类型时高字为 0（按本库补上），
    /// 引用核心库类型时高字为 1（= 工程库表第 0 项 krnln）。
    /// </summary>
    internal int LibDataType(int lib, int dt)
    {
        if (!EplSystemId.IsLibDataType(dt)) return dt;
        int hi = (dt >> 16) & 0xFFFF, lo = dt & 0xFFFF;
        if (hi == 0) return EplSystemId.MakeLibDataTypeId((short)lib, (short)(lo - 1));
        if (hi == 1) return EplSystemId.MakeLibDataTypeId((short)Krnln, (short)(lo - 1));
        return dt;
    }

    internal int WindowTypeIndex => LibTypes.TryGetValue("窗口", out var w) && w.Lib == Krnln ? w.Type : 0;

    // ================================================================ 模块顺序
    /// <summary>
    /// 模块目录按名称序排列，原始顺序只在根排序表里留下了痕迹（e2txt 按 Code.Classes 顺序写，模块程序集混排其中）：
    /// 取各模块「独有程序集名」在根排序表中的最早位置排序；找不到的保持原相对顺序排在后面。
    /// </summary>
    private void OrderModules()
    {
        var owners = new Dictionary<string, int>(StringComparer.Ordinal);
        foreach (var c in _t.Classes) owners[c.Name] = -2;
        for (int i = 0; i < _t.Modules.Count; i++)
            foreach (var c in _t.Modules[i].Classes)
                owners[c.Name] = owners.TryGetValue(c.Name, out var o) && o != i ? -2 : i;
        var pos = new int[_t.Modules.Count];
        Array.Fill(pos, int.MaxValue);
        for (int k = 0; k < _t.RootOrder.Count; k++)
            if (owners.TryGetValue(_t.RootOrder[k], out var mi) && mi >= 0 && pos[mi] == int.MaxValue) pos[mi] = k;
        _modules.AddRange(Enumerable.Range(0, _t.Modules.Count).OrderBy(i => pos[i]).ThenBy(i => i).Select(i => _t.Modules[i]));
    }

    // ================================================================ ID 分配
    private int _next = 0xFFFF;
    private int _maxId;
    private readonly Dictionary<TResource, int> _resIds = new(ReferenceEqualityComparer.Instance);
    private static readonly Regex PrivateTypeRx = new(@"^<\?未知本地类型:0x([0-9A-Fa-f]{8})\?>$", RegexOptions.CultureInvariant);

    private int New(int type)
    {
        do
        {
            _next++;
            if (_next > EplSystemId.Mask_Num) throw new InvalidOperationException("ID 空间耗尽");
        } while (!_usedNum.Add(_next));
        _fresh++;
        return Track(_next | type);
    }

    private int Track(int id)
    {
        _maxId = Math.Max(_maxId, id & EplSystemId.Mask_Num);
        return id;
    }

    private IEnumerable<string?> AllTypeStrings()
    {
        IEnumerable<string?> Decls(TDeclSet d) =>
            d.Globals.Select(x => x.Type)
                .Concat(d.Structs.SelectMany(s => s.Members.Select(x => x.Type)))
                .Concat(d.Dlls.SelectMany(x => x.Params.Select(p => p.Type).Append(x.ReturnType)));
        IEnumerable<string?> Cls(TClass c) =>
            c.Vars.Select(x => x.Type).Append(c.Base)
                .Concat(c.Methods.SelectMany(m => m.Params.Select(p => p.Type).Concat(m.Locals.Select(l => l.Type)).Append(m.ReturnType)));
        return Decls(_t.Main).Concat(_t.Classes.SelectMany(Cls))
            .Concat(_t.Modules.SelectMany(m => Decls(m.Decls).Concat(m.Classes.SelectMany(Cls))));
    }

    /// <summary>文本里带着固定 ID 的东西：窗口 ID、模块私有类型 ID、无名资源的占位 ID。计数器从它们之上开始，保证模块区段连续。</summary>
    private void ReserveIds()
    {
        int max = 0xFFFF;
        foreach (var f in _t.Forms)
        {
            var id = GetInt(f.Json, "Id", 0);
            if (EplSystemId.GetType(id) == EplSystemId.Type_Form) max = Math.Max(max, id & EplSystemId.Mask_Num);
        }
        foreach (var s in AllTypeStrings())
        {
            if (s == null) continue;
            var m = PrivateTypeRx.Match(s);
            if (m.Success) max = Math.Max(max, int.Parse(m.Groups[1].Value, NumberStyles.HexNumber) & EplSystemId.Mask_Num);
        }
        foreach (var r in _t.Resources)
        {
            if (r.Kind == TResourceKind.LongText) continue;
            var prefix = r.Kind == TResourceKind.Sound ? "声音资源_" : "图片资源_";
            if (r.Name.StartsWith(prefix, StringComparison.Ordinal) && r.Name.Length == prefix.Length + 8
                && uint.TryParse(r.Name.AsSpan(prefix.Length), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var be))
            {
                // 文件名是 ID 的小端十六进制
                var bytes = BitConverter.GetBytes(be);
                Array.Reverse(bytes);
                var id = BitConverter.ToInt32(bytes);
                int want = r.Kind == TResourceKind.Sound ? EplSystemId.Type_SoundResource : EplSystemId.Type_ImageResource;
                if (EplSystemId.GetType(id) == want)
                {
                    _resIds[r] = id;
                    Fixed(r, id);
                    max = Math.Max(max, id & EplSystemId.Mask_Num);
                }
            }
        }
        foreach (var f in _t.Forms)
        {
            var id = GetInt(f.Json, "Id", 0);
            if (EplSystemId.GetType(id) == EplSystemId.Type_Form) Fixed(f, id);
        }
        _maxId = max;
        // 有旁路时新 ID 从原工程的计数器之上分配，不与原有（含已删除）ID 相撞
        if (_sc != null) max = Math.Max(max, _sc.AllocatedIdNum & EplSystemId.Mask_Num);
        _next = max;
    }

    // ================================================================ ① 实体
    private static int[] Dims(string? d, out bool bad)
    {
        bad = false;
        if (d == null) return Array.Empty<int>();
        if (d == "0") return new[] { 0 };
        var parts = d.Split(',');
        var r = new int[parts.Length];
        for (int i = 0; i < parts.Length; i++)
        {
            var p = parts[i].Trim();
            if (p.Length == 0) r[i] = 0;
            else if (!int.TryParse(p, NumberStyles.Integer, CultureInfo.InvariantCulture, out r[i])) bad = true;
        }
        return r;
    }

    private int[] DimsOf(TVar v, string file)
    {
        var r = Dims(v.Dims, out var bad);
        if (bad) Error($"{file}:{v.Line}", $"数组维数无法解析: \"{v.Dims}\"");
        return r;
    }

    private static int VarFlags(TVar v) => (v.Static ? 0x1 : 0) | (v.Array ? 0x8 : 0);

    // 类型字符串待解析表：(实体, 类型原文, 所属模块, 位置)
    private readonly List<(Action<int> Set, string? Type, int Mod, string Where)> _pendingTypes = new();

    private void PendType(Action<int> set, string? type, int mod, string where) => _pendingTypes.Add((set, type, mod, where));

    private void CreateEntities()
    {
        Scopes.Add(new SScope { Mod = -1, Name = "" });
        for (int i = 0; i < _modules.Count; i++) Scopes.Add(new SScope { Mod = i, Name = _modules[i].Name });

        // ---- 主工程
        var main = Main;
        foreach (var s in _t.Main.Structs) AddStruct(s, main, "struct.e.txt", s.Public ? 0x1 : 0);
        foreach (var d in _t.Main.Dlls) AddDll(d, main, "dll.e.txt", d.Public ? 0x2 : 0);
        foreach (var c in _t.Main.Consts) AddConst(c, main, "const.e.txt", c.Public ? 0x2 : 0);
        AddResources();
        foreach (var g in _t.Main.Globals) AddGlobal(g, main, "global.e.txt", g.Public ? 0x100 : 0);
        foreach (var c in _t.Classes) AddClass(c, main);

        // ---- 模块：每类实体连续分配（DefinedIds 区段），顺序 数据类型→子程序→程序集→DLL→常量→全局变量
        for (int mi = 0; mi < _modules.Count; mi++)
        {
            var m = _modules[mi];
            var sc = ScopeOf(mi);
            var ids = new List<ECDependencyInfo.PackedIds>();
            var dir = "module/" + m.Name + "/";
            void Range(int type, int start, int count)
            {
                if (count > 0) ids.Add(new ECDependencyInfo.PackedIds { Start = type | start, Count = count });
            }

            int fresh0 = _fresh, kept0 = _consumed.Count;
            int sN = _structs.Count, dN = _dlls.Count, kN = _consts.Count, gN = _globals.Count, cN = sc.ClassList.Count;

            int s0 = _next + 1;
            foreach (var s in m.Decls.Structs) AddStruct(s, sc, dir + "struct.e.txt", 0x2);
            int structs = _next + 1 - s0;

            // 类：0x49 在前、0x09（__HIDDEN_TEMP_MOD__ 等）在后，子程序按同样的类顺序连续分配
            var clsOrder = m.Classes.Where(c => c.Kind != TClassKind.Static).Concat(m.Classes.Where(c => c.Kind == TClassKind.Static)).ToList();
            int m0 = _next + 1;
            var methodIds = new Dictionary<TMethod, int>(ReferenceEqualityComparer.Instance);
            foreach (var c in clsOrder) foreach (var mt in c.Methods) methodIds[mt] = Id(mt, EplSystemId.Type_Method);
            int methods = _next + 1 - m0;
            int c0 = _next + 1;
            foreach (var c in clsOrder) AddClass(c, sc, methodIds);
            int classes = _next + 1 - c0;
            int d0 = _next + 1;
            foreach (var d in m.Decls.Dlls) AddDll(d, sc, dir + "dll.e.txt", 0x4);
            int dlls = _next + 1 - d0;
            int k0 = _next + 1;
            foreach (var c in m.Decls.Consts) AddConst(c, sc, dir + "const.e.txt", 0x4);
            int consts = _next + 1 - k0;
            int g0 = _next + 1;
            foreach (var g in m.Decls.Globals) AddGlobal(g, sc, dir + "global.e.txt", 0x200);
            int globals = _next + 1 - g0;

            Sidecar.DepRaw? sdep = null;
            _sc?.Deps.TryGetValue(m.Name, out sdep);
            if (_consumed.Count == kept0)
            {
                // 全部新分配：区段连续
                Range(EplSystemId.Type_Struct, s0, structs);
                Range(EplSystemId.Type_Global, g0, globals);
                Range(EplSystemId.Type_Dll, d0, dlls);
                Range(EplSystemId.Type_Constant, k0, consts);
                Range(EplSystemId.Type_Class, c0, classes);
                Range(EplSystemId.Type_Method, m0, methods);
            }
            else if (sdep != null && _fresh == fresh0 && ModuleKeysAllKept(m.Name, _consumed.Count - kept0))
            {
                // 模块内容与原件相同：原样沿用 DefinedIds
                ids.AddRange(sdep.DefinedIds.Where(x => x.Length == 2).Select(x => new ECDependencyInfo.PackedIds { Start = x[0], Count = x[1] }));
            }
            else
            {
                // 沿用了部分原 ID：按实际 ID 求连续段
                Runs(ids, EplSystemId.Type_Struct, _structs.Skip(sN).Select(x => x.Id));
                Runs(ids, EplSystemId.Type_Global, _globals.Skip(gN).Select(x => x.Id));
                Runs(ids, EplSystemId.Type_Dll, _dlls.Skip(dN).Select(x => x.Id));
                Runs(ids, EplSystemId.Type_Constant, _consts.Skip(kN).Select(x => x.Id));
                Runs(ids, EplSystemId.Type_Class, sc.ClassList.Skip(cN).Select(x => x.Info.Id));
                Runs(ids, EplSystemId.Type_Method, methodIds.Values);
            }

            var dep = new ECDependencyInfo
            {
                InfoVersion = 2,
                Name = m.Name,
                Path = m.Source ?? "",
                ReExport = false,
                DefinedIds = ids,
                FileSize = 0,
                FileLastModifiedDate = DateTime.FromFileTime(0),
            };
            bool found = false;
            try
            {
                if (!string.IsNullOrEmpty(m.Source) && File.Exists(m.Source))
                {
                    var fi = new FileInfo(m.Source);
                    dep.FileSize = (int)Math.Min(int.MaxValue, fi.Length);
                    dep.FileLastModifiedDate = fi.LastWriteTime;
                    found = true;
                }
            }
            catch (Exception) { /* 路径无效：按找不到处理 */ }
            if (sdep != null)
            {
                dep.ReExport = sdep.ReExport;
                if (sdep.InfoVersion != 0) dep.InfoVersion = sdep.InfoVersion;
                // .ec 不在本机：用旁路记录的大小/时间（路径未改时）
                if (!found && string.Equals(sdep.Path, dep.Path, StringComparison.Ordinal))
                {
                    dep.FileSize = sdep.FileSize;
                    if (sdep.FileTime > 0) dep.FileLastModifiedDate = DateTime.FromFileTime(sdep.FileTime);
                }
            }
            _deps.Add(dep);
        }

        OrderClasses();
    }

    /// <summary>旁路里该模块的顶层实体（数据类型/DLL/全局/常量/程序集/子程序）是否全部被沿用</summary>
    private bool ModuleKeysAllKept(string module, int kept)
    {
        var pre = Sidecar.Pre(module);
        int n = 0;
        foreach (var k in _sc!.Items.Keys)
        {
            if (!k.StartsWith(pre, StringComparison.Ordinal)) continue;
            var rest = k.AsSpan(pre.Length);
            int slash = rest.IndexOf('/');
            if (slash < 0 || (rest.Slice(slash).StartsWith("/M:") && rest.Slice(slash + 1).IndexOf('/') < 0)) n++;
        }
        return n == kept;
    }

    /// <summary>按 ID 数值求连续段（模块 DefinedIds）</summary>
    private static void Runs(List<ECDependencyInfo.PackedIds> ids, int type, IEnumerable<int> all)
    {
        var nums = all.Where(x => EplSystemId.GetType(x) == type).Select(x => x & EplSystemId.Mask_Num).OrderBy(x => x).ToList();
        for (int i = 0; i < nums.Count;)
        {
            int j = i + 1;
            while (j < nums.Count && nums[j] == nums[j - 1] + 1) j++;
            ids.Add(new ECDependencyInfo.PackedIds { Start = type | nums[i], Count = j - i });
            i = j;
        }
    }

    private void AddStruct(TStruct s, SScope sc, string file, int flags)
    {
        var info = new StructInfo(Id(s, EplSystemId.Type_Struct))
        {
            MemoryAddress = Mem(s),
            Flags = Fl(s, flags, sc.Mod >= 0 ? 0 : 0x1),
            Name = s.Name,
            Comment = s.Comment ?? "",
            Members = new List<StructMemberInfo>(),
        };
        _structs.Add(info);
        StructById[info.Id] = info;
        StructMembers[info.Id] = new Dictionary<string, StructMemberInfo>(StringComparer.Ordinal);
        var refName = s.Name;
        if (sc.Mod >= 0)
            foreach (var (src, dst) in _modules[sc.Mod].StructRenames)
                if (src == s.Name) { refName = dst; break; }
        if (s.Name.Length > 0 && !sc.Types.TryAdd(refName, info.Id))
            Warn($"{file}:{s.Line}", $"数据类型 {s.Name} 重名");
        _structSrc[info] = (s, sc.Mod, file);
    }

    private readonly Dictionary<StructInfo, (TStruct T, int Mod, string File)> _structSrc = new(ReferenceEqualityComparer.Instance);
    private readonly Dictionary<DllDeclareInfo, (TDll T, int Mod, string File)> _dllSrc = new(ReferenceEqualityComparer.Instance);

    private void AddDll(TDll d, SScope sc, string file, int flags)
    {
        var info = new DllDeclareInfo(Id(d, EplSystemId.Type_Dll))
        {
            MemoryAddress = Mem(d),
            Flags = Fl(d, flags, sc.Mod >= 0 ? 0 : 0x2),
            Name = d.Name,
            Comment = d.Comment ?? "",
            LibraryName = d.LibraryName ?? "",
            EntryPoint = d.EntryPoint ?? "",
            Parameters = new List<DllParameterInfo>(),
        };
        _dlls.Add(info);
        if (d.Name.Length > 0) sc.Dlls.TryAdd(d.Name, info);
        _dllSrc[info] = (d, sc.Mod, file);
        PendType(t => info.ReturnDataType = t, d.ReturnType, sc.Mod, $"{file}:{d.Line}");
    }

    private void AddConst(TConst c, SScope sc, string file, int flags)
    {
        var info = new ConstantInfo(Id(c, EplSystemId.Type_Constant))
        {
            Flags = Fl(c, flags, sc.Mod >= 0 ? 0 : 0x12),
            Name = c.Name,
            Comment = c.Comment ?? "",
            Value = ConstValue(c.RawValue, $"{file}:{c.Line}"),
        };
        _consts.Add(info);
        if (c.Name.Length > 0) sc.Consts.TryAdd(c.Name, info);
    }

    /// <summary>常量值原文 → 值（与 Exporter.ConstValue 对称）</summary>
    private object? ConstValue(string? raw, string where)
    {
        if (raw == null || raw.Length == 0) return null;
        if (raw.Length >= 2 && raw[0] == '“' && raw[^1] == '”')
            // e2txt 把多行文本的 CRLF 写成裸 CR（保证一条常量一行），这里还原
            return raw.Substring(1, raw.Length - 2).Replace("\r\n", "\r").Replace("\r", "\r\n");
        if (raw == "真") return true;
        if (raw == "假") return false;
        if (raw[0] == '[')
        {
            var d = CodeParser.ParseDate(raw);
            if (d != null) return d.Value;
        }
        var n = CodeParser.ParseNumber(raw);
        if (n != null) return n.Value;
        Warn(where, $"常量值 {raw} 不是文本/数值/逻辑/日期，按文本保存");
        return raw;
    }

    private void AddResources()
    {
        var main = Main;
        // 长文本 → 图片 → 声音
        foreach (var kind in new[] { TResourceKind.LongText, TResourceKind.Image, TResourceKind.Sound })
            foreach (var r in _t.Resources.Where(x => x.Kind == kind))
            {
                bool placeholder = _resIds.TryGetValue(r, out var fixedId);
                int id = placeholder ? fixedId
                    : Id(r, kind switch
                    {
                        TResourceKind.Image => EplSystemId.Type_ImageResource,
                        TResourceKind.Sound => EplSystemId.Type_SoundResource,
                        _ => EplSystemId.Type_Constant,
                    });
                var info = new ConstantInfo(id)
                {
                    Name = placeholder ? "" : r.Name,
                    Comment = r.Comment ?? "",
                    Value = kind == TResourceKind.LongText ? Enc.GetString(r.Data) : r.Data,
                };
                info.Public = r.Public;
                if (kind == TResourceKind.LongText) info.LongText = true;
                info.Flags = Fl(r, info.Flags, 0x12);
                (kind == TResourceKind.LongText ? _consts : _resConsts).Add(info);
                if (info.Name.Length > 0) main.Consts.TryAdd(info.Name, info);
            }
    }

    private void AddGlobal(TVar g, SScope sc, string file, int flags)
    {
        var info = new GlobalVariableInfo(Id(g, EplSystemId.Type_Global))
        {
            Flags = flags,
            Name = g.Name,
            Comment = g.Comment ?? "",
            UBound = DimsOf(g, file),
        };
        VarX(g, info, sc.Mod >= 0 ? 0 : 0x100, false);
        _globals.Add(info);
        if (g.Name.Length > 0) sc.Globals.TryAdd(g.Name, info);
        PendType(t => info.DataType = t, g.Type, sc.Mod, $"{file}:{g.Line}");
    }

    private void AddClass(TClass c, SScope sc, Dictionary<TMethod, int>? methodIds = null)
    {
        int type = c.Kind switch
        {
            TClassKind.Class => EplSystemId.Type_Class,
            TClassKind.Form => EplSystemId.Type_FormClass,
            _ => EplSystemId.Type_StaticClass,
        };
        var info = new ClassInfo(Id(c, type))
        {
            MemoryAddress = Mem(c),
            Name = c.Name,
            Comment = c.Comment ?? "",
            BaseClass = c.Kind == TClassKind.Class ? -1 : 0,
            Methods = new List<int>(),
            Variables = new List<ClassVariableInfo>(),
        };
        var sc2 = new SClass { T = c, Info = info, Mod = sc.Mod };
        ClassById[info.Id] = sc2;
        sc.ClassList.Add(sc2);
        if (c.Name.Length > 0)
        {
            if (!sc.Classes.TryAdd(c.Name, sc2)) Warn($"{c.File}:{c.Line}", $"程序集 {c.Name} 重名");
            if (c.Kind == TClassKind.Class) sc.Types.TryAdd(c.Name, info.Id);
        }

        foreach (var m in c.Methods)
        {
            int id = methodIds != null && methodIds.TryGetValue(m, out var pre) ? pre : Id(m, EplSystemId.Type_Method);
            int flags;
            if (sc.Mod >= 0) flags = c.Kind == TClassKind.Class ? 0x88 : 0x80;
            else flags = m.Public ? 0x8 : 0;
            var mi = new MethodInfo(id)
            {
                MemoryAddress = Mem(m),
                Class = info.Id,
                Flags = Fl(m, flags, sc.Mod >= 0 ? 0 : 0x8),
                Name = m.Name,
                Comment = m.Comment ?? "",
                Parameters = new List<MethodParameterInfo>(),
                Variables = new List<LocalVariableInfo>(),
            };
            var sm = new SMethod { T = m, Info = mi, Class = sc2 };
            sc2.Methods.Add(sm);
            info.Methods.Add(id);
            if (m.Name.Length > 0 && !sc2.MethodByName.TryAdd(m.Name, sm))
                Warn($"{c.File}:{m.Line}", $"子程序 {m.Name} 在程序集 {c.Name} 中重名");
            PendType(t => mi.ReturnDataType = t, m.ReturnType, sc.Mod, $"{c.File}:{m.Line}");
        }
        if (c.Kind != TClassKind.Class)
        {
            // 同名时公开的优先
            var dict = c.Kind == TClassKind.Static ? sc.StaticMethods : sc.FormMethods;
            foreach (var sm in sc2.Methods.Where(x => x.T.Public || sc.Mod >= 0)) if (sm.T.Name.Length > 0) dict.TryAdd(sm.T.Name, sm);
            foreach (var sm in sc2.Methods) if (sm.T.Name.Length > 0) dict.TryAdd(sm.T.Name, sm);
        }
    }

    /// <summary>
    /// Code.Classes 顺序：按根排序表把模块程序集插回主工程程序集之间（e2txt 按原 Classes 顺序写排序表）。
    /// </summary>
    private void OrderClasses()
    {
        var mainList = Main.ClassList;
        var taken = new HashSet<SClass>(ReferenceEqualityComparer.Instance);
        var mainByName = new Dictionary<string, SClass>(StringComparer.Ordinal);
        foreach (var c in mainList.Where(c => c.T.Folder.Length == 0)) mainByName.TryAdd(c.T.Name, c);
        int lastMod = -1;
        void Take(SClass c) { if (taken.Add(c)) Classes.Add(c); }

        foreach (var name in _t.RootOrder)
        {
            var safe = SafeName(name);
            if (_t.FolderOrders.ContainsKey(safe))
            {
                foreach (var c in mainList.Where(c => c.T.Folder == safe || c.T.Folder.StartsWith(safe + "/", StringComparison.Ordinal))) Take(c);
                continue;
            }
            if (mainByName.TryGetValue(name, out var mc) && !taken.Contains(mc)) { Take(mc); continue; }
            SClass? pick = null;
            if (lastMod >= 0) pick = ScopeOf(lastMod).ClassList.FirstOrDefault(c => c.T.Name == name && !taken.Contains(c));
            for (int mi = 0; pick == null && mi < _modules.Count; mi++)
                pick = ScopeOf(mi).ClassList.FirstOrDefault(c => c.T.Name == name && !taken.Contains(c));
            if (pick != null) { Take(pick); lastMod = pick.Mod; }
        }
        foreach (var c in mainList) Take(c);
        for (int mi = 0; mi < _modules.Count; mi++) foreach (var c in ScopeOf(mi).ClassList) Take(c);
    }

    internal static string SafeName(string name)
    {
        var bad = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder(name.Length);
        foreach (var c in name) sb.Append(bad.Contains(c) ? '_' : c);
        return sb.ToString();
    }

    // ================================================================ ①b 成员 / 参数 / 局部变量（同一计数器，排在所有区段之后）
    private void CreateSubEntities()
    {
        foreach (var s in _structs)
        {
            var (t, mod, file) = _structSrc[s];
            foreach (var v in t.Members)
            {
                var m = new StructMemberInfo(Id(v, EplSystemId.Type_StructMember))
                {
                    Name = v.Name,
                    Comment = v.Comment ?? "",
                    UBound = DimsOf(v, file),
                };
                m.ByRef = v.ByRef;
                VarX(v, m, 0x2, false);
                s.Members.Add(m);
                if (v.Name.Length > 0) StructMembers[s.Id].TryAdd(v.Name, m);
                PendType(x => m.DataType = x, v.Type, mod, $"{file}:{v.Line}");
            }
        }
        foreach (var d in _dlls)
        {
            var (t, mod, file) = _dllSrc[d];
            foreach (var p in t.Params)
            {
                var a = new DllParameterInfo(Id(p, EplSystemId.Type_DllParameter))
                {
                    Name = p.Name,
                    Comment = p.Comment ?? "",
                    UBound = Array.Empty<int>(),
                };
                a.ByRef = p.ByRef;
                a.ArrayParameter = p.Array;
                VarX(p, a, 0xA, true);
                d.Parameters.Add(a);
                PendType(x => a.DataType = x, p.Type, mod, $"{file}:{p.Line}");
            }
        }
        foreach (var c in Classes)
        {
            foreach (var v in c.T.Vars)
            {
                var cv = new ClassVariableInfo(Id(v, EplSystemId.Type_ClassMember))
                {
                    Name = v.Name,
                    Comment = v.Comment ?? "",
                    Flags = VarFlags(v),
                    UBound = DimsOf(v, c.T.File),
                };
                VarX(v, cv, 0x9, false);
                c.Info.Variables.Add(cv);
                if (v.Name.Length > 0) c.Vars.TryAdd(v.Name, cv);
                PendType(x => cv.DataType = x, v.Type, c.Mod, $"{c.T.File}:{v.Line}");
            }
            foreach (var m in c.Methods)
            {
                foreach (var p in m.T.Params)
                {
                    var a = new MethodParameterInfo(Id(p, EplSystemId.Type_Local))
                    {
                        Name = p.Name,
                        Comment = p.Comment ?? "",
                        UBound = Array.Empty<int>(),
                    };
                    a.ByRef = p.ByRef;
                    a.OptionalParameter = p.Optional;
                    a.ArrayParameter = p.Array;
                    VarX(p, a, 0xE, true);
                    m.Info.Parameters.Add(a);
                    PendType(x => a.DataType = x, p.Type, c.Mod, $"{c.T.File}:{p.Line}");
                }
                foreach (var v in m.T.Locals)
                {
                    var l = new LocalVariableInfo(Id(v, EplSystemId.Type_Local))
                    {
                        Name = v.Name,
                        Comment = v.Comment ?? "",
                        Flags = VarFlags(v),
                        UBound = DimsOf(v, c.T.File),
                    };
                    VarX(v, l, 0x9, false);
                    m.Info.Variables.Add(l);
                    PendType(x => l.DataType = x, v.Type, c.Mod, $"{c.T.File}:{v.Line}");
                }
            }
        }
    }

    // ================================================================ ② 类型解析
    private static readonly Dictionary<string, int> SystemTypes = new(StringComparer.Ordinal)
    {
        { "字节集", EplSystemId.DataType_Bin },
        { "逻辑型", EplSystemId.DataType_Bool },
        { "字节型", EplSystemId.DataType_Byte },
        { "日期时间型", EplSystemId.DataType_DateTime },
        { "双精度小数型", EplSystemId.DataType_Double },
        { "小数型", EplSystemId.DataType_Float },
        { "整数型", EplSystemId.DataType_Int },
        { "长整数型", EplSystemId.DataType_Long },
        { "子程序指针", EplSystemId.DataType_MethodPtr },
        { "短整数型", EplSystemId.DataType_Short },
        { "文本型", EplSystemId.DataType_String },
        { "通用型", EplSystemId.DataType_Any },
        { "条件语句型", EplSystemId.DataType_Lambda },
    };

    /// <summary>数据类型名 → ID。顺序：系统类型 → 本模块 → 主工程 → 其他模块 → 支持库（krnln 优先）</summary>
    internal bool TryResolveType(string? name, int mod, out int id)
    {
        id = 0;
        if (string.IsNullOrEmpty(name)) return true;
        var pm = PrivateTypeRx.Match(name);
        if (pm.Success) { id = unchecked((int)uint.Parse(pm.Groups[1].Value, NumberStyles.HexNumber)); return true; }
        if (SystemTypes.TryGetValue(name, out id)) return true;
        if (mod >= 0 && ScopeOf(mod).Types.TryGetValue(name, out id)) return true;
        if (Main.Types.TryGetValue(name, out id)) return true;
        for (int i = 0; i < _modules.Count; i++)
            if (i != mod && ScopeOf(i).Types.TryGetValue(name, out id)) return true;
        if (LibTypes.TryGetValue(name, out var lt)) { id = EplSystemId.MakeLibDataTypeId((short)lt.Lib, (short)lt.Type); return true; }
        id = 0;
        return false;
    }

    private void ResolveDeclarations()
    {
        // 窗口名也是数据类型（主工程）
        foreach (var f in _t.Forms)
        {
            var name = GetStr(f.Json, "Name");
            var id = GetInt(f.Json, "Id", 0);
            if (name.Length > 0 && EplSystemId.GetType(id) == EplSystemId.Type_Form) Main.Types.TryAdd(name, id);
        }
        foreach (var (set, type, mod, where) in _pendingTypes)
        {
            if (TryResolveType(type, mod, out var id)) set(id);
            else Error(where, $"未知数据类型 {type}");
        }
        // 基类
        foreach (var c in Classes)
        {
            if (string.IsNullOrEmpty(c.T.Base)) continue;
            if (TryResolveType(c.T.Base, c.Mod, out var id) && ClassById.TryGetValue(id, out var b))
            {
                c.Info.BaseClass = id;
                c.Base = b;
            }
            else if (TryResolveType(c.T.Base, c.Mod, out id) && id != 0) c.Info.BaseClass = id;
            else Error($"{c.T.File}:{c.T.Line}", $"未知基类 {c.T.Base}");
        }
    }

    // ================================================================ 窗口
    private static byte[] Hex(string s)
    {
        try { return Convert.FromHexString(s); }
        catch (FormatException) { return Array.Empty<byte>(); }
    }

    private int ElemType(string name, string where)
    {
        if (LibTypes.TryGetValue(name, out var lt)) return EplSystemId.MakeLibDataTypeId((short)lt.Lib, (short)lt.Type);
        Error(where, $"未知组件类型 {name}");
        return 0;
    }

    private void BuildForms()
    {
        var mainForms = Main.ClassList.Where(c => c.T.Kind == TClassKind.Form).ToDictionary(c => c.T.Name, c => c, StringComparer.Ordinal);
        var windowType = EplSystemId.MakeLibDataTypeId((short)Krnln, (short)WindowTypeIndex);
        foreach (var tf in _t.Forms)
        {
            var j = tf.Json;
            int id = GetInt(j, "Id", 0);
            if (EplSystemId.GetType(id) != EplSystemId.Type_Form)
            {
                Error(tf.File, $"窗口 Id 0x{id:X8} 不是窗口类型 ID，已重新分配");
                id = New(EplSystemId.Type_Form);
            }
            Track(id);
            var info = new FormInfo(id)
            {
                Name = GetStr(j, "Name"),
                Comment = GetStr(j, "Comment"),
                MemoryAddress = GetInt(j, "UnknownAfterId", 0),
                UnknownBeforeClass = GetInt(j, "UnknownBeforeClass", 0),
                Elements = new List<FormElementInfo>(),
            };
            var f = new SForm { T = tf, Info = info, Name = info.Name };
            var cls = GetStr(j, "Class");
            if (cls.Length > 0)
            {
                if (mainForms.TryGetValue(cls, out var c))
                {
                    info.Class = c.Info.Id;
                    c.Info.Form = id;
                    c.Form = f;
                    f.Class = c;
                }
                else Error(tf.File, $"找不到窗口程序集 {cls}");
            }
            Forms.Add(f);
            FormById[id] = f;
            if (info.Name.Length > 0 && !FormByName.TryAdd(info.Name, f)) Error(tf.File, $"窗口 {info.Name} 重名");

            // ---- 组件
            var ctrlJson = (Get(j, "Controls") as List<object?> ?? new()).OfType<JObject>().ToList();
            var ctrls = new List<FormControlInfo>();
            foreach (var cj in ctrlJson)
            {
                int dt = ElemType(GetStr(cj, "DataType"), tf.File);
                var c = new FormControlInfo(Id(cj, dt == windowType && f.Self == null ? EplSystemId.Type_FormSelf : EplSystemId.Type_FormControl))
                {
                    DataType = dt,
                    Name = GetStr(cj, "Name"),
                    Comment = GetStr(cj, "Comment"),
                    Visible = GetBool(cj, "Visible", true),
                    Disable = GetBool(cj, "Disable", false),
                    UnknownBeforeName = ImmutableArray.Create(Hex(GetStr(cj, "UnknownBeforeName"))),
                    CWndAddress = GetInt(cj, "UnknownBeforeLeft", 0),
                    Left = GetInt(cj, "Left", 0),
                    Top = GetInt(cj, "Top", 0),
                    Width = GetInt(cj, "Width", 0),
                    Height = GetInt(cj, "Height", 0),
                    UnknownBeforeParent = GetInt(cj, "UnknownBeforeParent", 0),
                    Cursor = Hex(GetStr(cj, "Cursor")),
                    Tag = GetStr(cj, "Tag"),
                    UnknownBeforeVisible = GetInt(cj, "UnknownBeforeVisible", 0),
                    TabStop = GetBool(cj, "TabStop", false),
                    Locked = GetBool(cj, "Locked", false),
                    TabIndex = GetInt(cj, "UnknownBeforeEvents", 0),
                    UnknownBeforeExtensionData = ImmutableArray.Create(Hex(GetStr(cj, "UnknownBeforeExtensionData"))),
                    ExtensionData = Hex(GetStr(cj, "ExtensionData")),
                    Events = Array.Empty<KeyValuePair<int, int>>(),
                };
                if (EplSystemId.GetType(c.Id) == EplSystemId.Type_FormSelf) f.Self = c;
                ctrls.Add(c);
            }
            int At(object? idx) => idx is long l && l >= 1 && l <= ctrls.Count ? ctrls[(int)l - 1].Id : 0;
            for (int i = 0; i < ctrls.Count; i++)
                ctrls[i].Children = (Get(ctrlJson[i], "Children") as List<object?> ?? new()).Select(At).ToArray();
            var parentOf = new Dictionary<int, int>();
            foreach (var c in ctrls) foreach (var ch in c.Children) if (ch != 0) parentOf[ch] = c.Id;
            for (int i = 0; i < ctrls.Count; i++)
            {
                var c = ctrls[i];
                c.Parent = ctrlJson[i].ContainsKey("Parent") ? At(Get(ctrlJson[i], "Parent")) : parentOf.GetValueOrDefault(c.Id);
                if (Get(ctrlJson[i], "Events") is List<object?> evs)
                    c.Events = evs.OfType<JObject>()
                        .Select(e => new KeyValuePair<int, int>(GetInt(e, "Event", 0), FormMethod(f, GetStr(e, "Method"), tf.File)))
                        .ToArray();
                info.Elements.Add(c);
                if (!string.IsNullOrEmpty(c.Name)) f.Elems.TryAdd(c.Name, c);
            }

            // ---- 菜单
            foreach (var mj in (Get(j, "Menus") as List<object?> ?? new()).OfType<JObject>())
            {
                var m = new FormMenuInfo(Id(mj, EplSystemId.Type_FormMenu))
                {
                    DataType = ElemType(GetStr(mj, "DataType"), tf.File),
                    Name = GetStr(mj, "Name"),
                    Visible = GetBool(mj, "Visible", true),
                    Disable = GetBool(mj, "Disable", false),
                    UnknownBeforeName = ImmutableArray.Create(Hex(GetStr(mj, "UnknownBeforeName"))),
                    HotKey = GetInt(mj, "HotKey", 0),
                    Level = GetInt(mj, "Level", 0),
                    Selected = GetBool(mj, "Selected", false),
                    Text = GetStr(mj, "Text"),
                    UnknownAfterClickEvent = ImmutableArray.Create(Hex(GetStr(mj, "UnknownAfterClickEvent"))),
                };
                var ce = GetStr(mj, "ClickEvent");
                if (ce.Length > 0) m.ClickEvent = FormMethod(f, ce, tf.File);
                info.Elements.Add(m);
                if (!string.IsNullOrEmpty(m.Name)) f.Elems.TryAdd(m.Name, m);
            }
        }
        foreach (var c in Main.ClassList.Where(c => c.T.Kind == TClassKind.Form && c.Form == null))
            Warn(c.T.File, $"窗口程序集 {c.T.Name} 没有对应的窗口（*.form.json 的 Class）");
    }

    private int FormMethod(SForm f, string name, string where)
    {
        if (f.Class != null && f.Class.MethodByName.TryGetValue(name, out var m)) return m.Info.Id;
        Error(where, $"事件子程序 {name} 不在窗口程序集里");
        return 0;
    }

    // ================================================================ ③ 代码
    private void BindCode()
    {
        var binder = new Binder(this);
        foreach (var c in Classes)
            foreach (var m in c.Methods)
            {
                if (c.Mod >= 0)
                {
                    // 模块里的子程序在 .e 中不带代码
                    if (m.T.Body.Any(l => l.Trim().Length > 0))
                        Warn($"{c.T.File}:{m.T.BodyLine}", $"模块子程序 {m.T.Name} 的代码不会写入 .e（模块代码以 .ec 为准）");
                    // 原件里模块子程序的代码是一条空语句
                    m.Info.CodeData = new StatementBlock { new ExpressionStatement() }.ToCodeData(Enc);
                    continue;
                }
                int w0 = _r.Warnings.Count, u0 = _r.Unexamined;
                var diags = new List<CodeDiagnostic>();
                var body = CodeParser.ParseBody(m.T.Body, m.T.BodyLine, c.T.File, diags);
                foreach (var d in diags) Warn($"{d.File}:{d.Line}:{d.Col + 1}", d.Message + "（按未验证代码写入）");
                var block = binder.BindMethod(m, body);
                m.Info.CodeData = block.ToCodeData(Enc);
                _bindLog[m] = (w0, _r.Warnings.Count, _r.Unexamined - u0);
            }
        foreach (var c in Classes) foreach (var m in c.Methods) _methods.Add(m.Info);
    }

    // ================================================================ 段
    private CodeSection BuildCode() => new()
    {
        AllocatedIdNum = _maxId,
        Libraries = LibRefs,
        Flag = 0,
        // IDE 保存的工程（大项目测试 / 窗口属性无法解析）此值为 0，只有 e2txt 产物会填 _启动子程序；无旁路时按 IDE 写 0，有旁路时还原原值
        MainMethod = 0,
        IconData = _t.Icon,
        DebugCommandParameters = "",
        Classes = Classes.Select(c => c.Info).ToList(),
        Methods = _methods,
        GlobalVariables = _globals,
        Structs = _structs,
        DllDeclares = _dlls,
    };

    /// <summary>内容为空的可省略段：e2txt 的产物不写这些段；有旁路且原件没有时不补</summary>
    private static bool IsEmptySection(ISection s) => s switch
    {
        EPackageInfoSection e => e.FileNames == null || e.FileNames.All(string.IsNullOrEmpty),
        ECDependenciesSection d => d.ECDependencies == null || d.ECDependencies.Count == 0,
        LosableSection l => string.IsNullOrEmpty(l.OutFile) && (l.RemovedDefinedItems == null || l.RemovedDefinedItems.Count == 0),
        EventIndicesSection ev => ev.Indices == null || ev.Indices.Count == 0,
        _ => false,
    };

    private string[] EPackageNames()
    {
        if (_t.EpackFileNames.Count == _methods.Count) return _t.EpackFileNames.ToArray();
        if (_t.EpackFileNames.Count > 0)
            Warn("config/epack.files.config.json", $"FileNames 有 {_t.EpackFileNames.Count} 项，与子程序数 {_methods.Count} 不符，已忽略");
        return _methods.Select(_ => "").ToArray();
    }

    private InitECSection BuildInitEC()
    {
        var names = new List<string>();
        var methods = new List<int>();
        foreach (var (mod, method) in _t.EcInit)
        {
            int mi = _modules.FindIndex(x => x.Name == mod);
            SMethod? m = null;
            if (mi >= 0) ScopeOf(mi).StaticMethods.TryGetValue(method, out m);
            for (int i = 0; m == null && i < _modules.Count; i++) ScopeOf(i).StaticMethods.TryGetValue(method, out m);
            if (m == null) { Error("config/ec.init.config.json", $"找不到模块 {mod} 的初始化子程序 {method}"); continue; }
            names.Add(mod);
            methods.Add(m.Info.Id);
        }
        return new InitECSection { ECName = names.ToArray(), InitMethod = methods.ToArray() };
    }

    private ClassPublicitySection BuildClassPublicity()
    {
        var list = new List<ClassPublicityInfo>();
        foreach (var c in Classes)
        {
            if (c.Mod >= 0) list.Add(new ClassPublicityInfo { Class = c.Info.Id, Flags = 0x2 });
            else if (c.T.Kind != TClassKind.Form && c.T.Public) list.Add(new ClassPublicityInfo { Class = c.Info.Id, Flags = 0x1 });
        }
        return new ClassPublicitySection { ClassPublicities = list };
    }

    /// <summary>通用事件（所有组件共有），事件号为负</summary>
    private static readonly string[] CommonEvents =
    {
        "鼠标左键被按下", "鼠标左键被放开", "被双击", "鼠标右键被按下", "鼠标右键被放开", "鼠标位置被移动",
        "获得焦点", "失去焦点", "按下某键", "放开某键", "字符输入", "滚轮被滚动",
    };

    /// <summary>
    /// 事件绑定只存在于辅助信息段 1（组件自身的 Events 为空）。按 IDE 命名约定重建：
    /// 窗口程序集里名为 `_单元名_事件名` 的子程序（窗口自身的单元名 = 窗口名）。
    /// </summary>
    private List<IndexedEventInfo> BuildEventIndices()
    {
        var list = new List<IndexedEventInfo>();
        foreach (var f in Forms)
        {
            if (f.Class == null) continue;
            var units = new List<(string Name, FormControlInfo Ctrl)>();
            foreach (var c in f.Info.Elements.OfType<FormControlInfo>())
            {
                var n = ReferenceEquals(c, f.Self) ? f.Name : c.Name;
                if (!string.IsNullOrEmpty(n)) units.Add((n, c));
            }
            // 单元名长的优先（`_图片框1_设置_x` 不应匹配到 `图片框1`）
            units.Sort((a, b) => b.Name.Length.CompareTo(a.Name.Length));
            foreach (var m in f.Class.Methods)
            {
                var name = m.T.Name;
                foreach (var (un, ctrl) in units)
                {
                    var prefix = "_" + un + "_";
                    if (!name.StartsWith(prefix, StringComparison.Ordinal)) continue;
                    var ev = name.Substring(prefix.Length);
                    if (TryEvent(ctrl.DataType, ev, out var evId))
                    {
                        list.Add(new IndexedEventInfo { FormId = f.Info.Id, UnitId = ctrl.Id, EventId = evId, MethodId = m.Info.Id });
                        break;
                    }
                }
            }
        }
        return list;
    }

    private bool TryEvent(int dataType, string ev, out int id)
    {
        id = 0;
        if (EplSystemId.IsLibDataType(dataType))
        {
            EplSystemId.DecomposeLibDataTypeId(dataType, out var lib, out var type);
            var t = LibType(lib, type);
            if (t != null)
                for (int i = 0; i < t.Events.Length; i++)
                    if (t.Events[i].Name == ev) { id = i; return true; }
        }
        int k = Array.IndexOf(CommonEvents, ev);
        if (k >= 0) { id = -(k + 1); return true; }
        return false;
    }

    /// <summary>编辑过滤器（程序集分组）：class/ 子目录 → 分组，按根序 DFS 前序</summary>
    private FolderSection? BuildFolders()
    {
        if (_t.FolderOrders.Count == 0) return null;
        var fs = new FolderSection();
        // 有旁路：同路径的分组沿用原 key，新分组从原计数器之上分配
        // （原件的计数器可能小于实际最大 key，按原值还原，新 key 跳过所有预留值）
        var usedKeys = new HashSet<int>();
        var reserved = new HashSet<int>();
        if (_sc != null)
        {
            foreach (var (path, k) in _sc.Folders) if (_t.FolderOrders.ContainsKey(path)) reserved.Add(k);
            if (_sc.FolderAlloc > 0) while (fs.AllocKey() < _sc.FolderAlloc) { }
        }
        int FolderKey(string path)
        {
            if (_sc != null && _sc.Folders.TryGetValue(path, out var k) && reserved.Contains(k) && usedKeys.Add(k)) return k;
            int n;
            do n = fs.AllocKey(); while (reserved.Contains(n) || !usedKeys.Add(n));
            return n;
        }
        var expanded = new HashSet<string>(_t.ExpandedDirs, StringComparer.Ordinal);
        var done = new HashSet<string>(StringComparer.Ordinal);

        void Visit(string key, string name, int parent)
        {
            if (!done.Add(key)) return;
            var f = new CodeFolderInfo(FolderKey(key)) { Name = name, ParentKey = parent, Expand = expanded.Contains(key) };
            fs.Folders.Add(f);
            var children = new List<int>();
            var pending = new List<(string Key, string Name, int Index)>();
            foreach (var entry in _t.FolderOrders[key])
            {
                if (entry.Length == 0) continue;
                var sub = key + "/" + SafeName(entry);
                if (_t.FolderOrders.ContainsKey(sub))
                {
                    pending.Add((sub, entry, children.Count));
                    children.Add(0);  // 占位，子分组 key 分配后回填
                    continue;
                }
                var cls = Main.ClassList.FirstOrDefault(c => c.T.Folder == key && c.T.Name == entry);
                if (cls != null) { children.Add(cls.Info.Id); continue; }
                var fm = Forms.FirstOrDefault(x => x.T.Folder == key && x.Name == entry);
                if (fm != null) { children.Add(fm.Info.Id); continue; }
                Warn($"class/{key}/order.list.txt", $"找不到 {entry}");
            }
            foreach (var (k, n, idx) in pending)
            {
                Visit(k, n, f.Key);
                children[idx] = fs.Folders.Last(x => x.Name == n && x.ParentKey == f.Key).Key;
            }
            f.Children = children.ToArray();
        }

        foreach (var entry in _t.RootOrder)
        {
            var key = SafeName(entry);
            if (_t.FolderOrders.ContainsKey(key)) Visit(key, entry, 0);
        }
        foreach (var key in _t.FolderOrders.Keys.Where(k => !k.Contains('/')).OrderBy(k => k, StringComparer.Ordinal))
            Visit(key, key, 0);
        return fs;
    }
}
