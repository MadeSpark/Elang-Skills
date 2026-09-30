// QAQ-ETool 探针：两个 .e 文件的语义结构比对器（Probe --cmp a.e b.e）
//   用于验证 t2e 往返：e2t → t2e 产物与原 .e 应语义等价，但 ID 会被重新分配。
//   做法：两边各自把每个用户定义实体映射成「规范名路径」（与 ID 无关），
//         属性/代码里出现的 ID 全部翻译成规范路径后再逐项比较。
using System.Diagnostics;
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Expressions;
using QIQI.EProjectFile.Sections;
using QIQI.EProjectFile.Statements;

namespace QAQ.Probe;

public static class Cmp
{
    internal sealed record Diff(string Cat, string Path, string Desc, string? A, string? B);

    internal static readonly CultureInfo Inv = CultureInfo.InvariantCulture;
    private static readonly Regex PrivRe = new(@"\?PRIVATE:0x[0-9A-F]{8}", RegexOptions.Compiled);
    internal static string NormPriv(string s) => s.Contains("?PRIVATE:") ? PrivRe.Replace(s, "?PRIVATE") : s;

    /// <summary>不计入退出码的类别（[顺序] 仅在 --ignore-order 时忽略）</summary>
    internal static bool Ignorable(string cat, bool ignoreOrder) =>
        cat == "杂项" || cat == "私有类型ID" || (ignoreOrder && cat == "顺序");

    private static void Usage()
    {
        Console.WriteLine("用法: Probe --cmp <a.e> <b.e> [--max N] [--no-code] [--ignore-order] [--json out.json]");
        Console.WriteLine("  退出码: 0=无差异（忽略 [杂项]/[私有类型ID]，--ignore-order 时也忽略 [顺序]） 1=有差异 2=加载失败/参数错误");
    }

    public static int Run(string[] args)
    {
        string? fa = null, fb = null, json = null;
        int max = 200;
        bool noCode = false, ignoreOrder = false;
        try
        {
            for (int i = 0; i < args.Length; i++)
            {
                switch (args[i])
                {
                    case "--max": max = int.Parse(args[++i], Inv); break;
                    case "--no-code": noCode = true; break;
                    case "--ignore-order": ignoreOrder = true; break;
                    case "--json": json = args[++i]; break;
                    default:
                        if (fa == null) fa = args[i];
                        else if (fb == null) fb = args[i];
                        else { Usage(); return 2; }
                        break;
                }
            }
        }
        catch (Exception) { Usage(); return 2; }
        if (fa == null || fb == null) { Usage(); return 2; }

        var sw = Stopwatch.StartNew();
        Side a, b;
        try { a = new Side(fa); }
        catch (Exception ex) { Console.WriteLine($"加载失败 a={fa}: {ex.GetType().Name}: {ex.Message}"); return 2; }
        var tA = sw.ElapsedMilliseconds;
        try { b = new Side(fb); }
        catch (Exception ex) { Console.WriteLine($"加载失败 b={fb}: {ex.GetType().Name}: {ex.Message}"); if (Environment.GetEnvironmentVariable("CMP_TRACE") == "1") Console.WriteLine(ex.StackTrace); return 2; }
        var tB = sw.ElapsedMilliseconds - tA;

        sw.Restart();
        var cmp = new Comparer(a, b, ignoreOrder, noCode);
        cmp.Run();
        var tC = sw.ElapsedMilliseconds;

        // ---------- 输出 ----------
        Console.WriteLine($"=== 语义比对 ===");
        Console.WriteLine($"  a = {fa}");
        Console.WriteLine($"  b = {fb}");
        Console.WriteLine($"  加载+建索引: a {tA} ms, b {tB} ms；比对 {tC} ms{(noCode ? "（--no-code）" : "")}{(ignoreOrder ? "（--ignore-order）" : "")}");
        Console.WriteLine($"  AllocatedIdNum（仅打印）: a=0x{a.Code.AllocatedIdNum:X8} b=0x{b.Code.AllocatedIdNum:X8}");
        Console.WriteLine($"  实体数: a={a.Ents.Count} b={b.Ents.Count}；子程序: a={a.Code.Methods.Count} b={b.Code.Methods.Count}；代码比对 {cmp.CodeCompared} 个；顶层回退对齐 {cmp.Aligned} 个");

        var groups = cmp.Diffs.GroupBy(d => d.Cat)
            .Select(g => (Cat: g.Key, Count: g.Count(), Ign: Ignorable(g.Key, ignoreOrder)))
            .OrderBy(x => x.Ign).ThenByDescending(x => x.Count).ThenBy(x => x.Cat, StringComparer.Ordinal).ToList();
        int errors = groups.Where(g => !g.Ign).Sum(g => g.Count);
        Console.WriteLine();
        Console.WriteLine("  类别汇总:");
        if (groups.Count == 0) Console.WriteLine("    （无差异）");
        foreach (var g in groups)
            Console.WriteLine($"    {g.Count,7}  [{g.Cat}]{(g.Ign ? "  (忽略)" : "")}");
        Console.WriteLine($"  计入差异 {errors} 条，忽略 {cmp.Diffs.Count - errors} 条");

        var ordered = cmp.Diffs.Select((d, i) => (d, i))
            .OrderBy(x => Ignorable(x.d.Cat, ignoreOrder)).ThenBy(x => x.i).Select(x => x.d).ToList();
        if (ordered.Count > 0)
        {
            Console.WriteLine();
            Console.WriteLine($"  明细（前 {Math.Min(max, ordered.Count)} / {ordered.Count} 条，计入差异在前）:");
            foreach (var d in ordered.Take(max))
                Console.WriteLine($"[{d.Cat}] {d.Path}: {d.Desc} (a={Short(d.A)} b={Short(d.B)})");
        }

        if (json != null)
        {
            var opts = new JsonSerializerOptions { WriteIndented = true, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping };
            var obj = new
            {
                a = Path.GetFullPath(fa),
                b = Path.GetFullPath(fb),
                ignoreOrder,
                noCode,
                errors,
                summary = groups.Select(g => new { cat = g.Cat, count = g.Count, ignored = g.Ign }),
                diffs = ordered.Select(d => new { cat = d.Cat, path = d.Path, desc = d.Desc, a = d.A, b = d.B, ignored = Ignorable(d.Cat, ignoreOrder) }),
            };
            File.WriteAllText(json, JsonSerializer.Serialize(obj, opts), new UTF8Encoding(false));
            Console.WriteLine($"\n  JSON 已写出: {json}");
        }
        return errors == 0 ? 0 : 1;
    }

    private static string Short(string? s)
    {
        if (s == null) return "<无>";
        s = s.Replace("\r", "\\r").Replace("\n", "\\n");
        return s.Length > 180 ? s[..180] + $"…(共{s.Length}字)" : s;
    }

    internal static string Hex(int v) => "0x" + v.ToString("X", Inv);
    internal static string Sha(byte[]? b) =>
        b == null ? "null" : $"len={b.Length} sha256={Convert.ToHexString(SHA256.HashData(b))[..32]}";
    internal static string Sha(System.Collections.Immutable.ImmutableArray<byte> b) =>
        b.IsDefault ? "default" : Sha(b.ToArray());
    internal static string Esc(string? s)
    {
        if (s == null) return "null";
        var sb = new StringBuilder(s.Length + 8);
        foreach (var ch in s)
        {
            switch (ch)
            {
                case '\r': sb.Append("\\r"); break;
                case '\n': sb.Append("\\n"); break;
                case '\t': sb.Append("\\t"); break;
                case '\\': sb.Append("\\\\"); break;
                default:
                    if (ch < 0x20) sb.Append("\\x").Append(((int)ch).ToString("X2", Inv));
                    else sb.Append(ch);
                    break;
            }
        }
        return sb.ToString();
    }
    internal static string Q(string? s) => s == null ? "null" : "“" + Esc(s) + "”";
    internal static string CVal(object? v) => v switch
    {
        null => "空",
        byte[] b => "字节集:" + Sha(b),
        double d => "数值:" + d.ToString("R", Inv),
        bool x => "逻辑:" + (x ? "真" : "假"),
        DateTime t => "日期:" + t.ToString("yyyy-MM-dd HH:mm:ss.fff", Inv),
        string s => "文本:" + Esc(s),
        _ => v.GetType().Name + ":" + v,
    };
}

// =====================================================================
// 实体：规范路径 + 已翻译成字符串的属性表
// =====================================================================
internal sealed class Ent
{
    public string Path { get; }
    public string Kind { get; }
    public string? Parent { get; }
    public string? MissingCat { get; set; }
    public MethodInfo? Method { get; set; }
    public List<(string Key, string Val, string Cat)> Props { get; } = new();
    public Ent(string path, string kind, string? parent) { Path = path; Kind = kind; Parent = parent; }
    public void P(string key, string? val, string cat = "属性") => Props.Add((key, val ?? "", cat));
}

// =====================================================================
// 单边：加载、模块归属、规范命名、实体属性
// =====================================================================
internal sealed class Side
{
    public string FilePath { get; }
    public EplDocument Doc { get; } = new();
    public CodeSection Code { get; }
    public ResourceSection Res { get; }
    public List<ECDependencyInfo> Deps { get; }
    public Encoding Enc { get; }
    public string[] LibKeys { get; }

    public Dictionary<int, string> PathOf { get; } = new();
    public Dictionary<int, int> ModuleOf { get; } = new();
    public Dictionary<int, string> Removed { get; } = new();
    public Dictionary<string, Ent> Ents { get; } = new(StringComparer.Ordinal);
    /// <summary>有序列表：键 → (是否严格, 规范路径序列)。严格列表（参数/成员）顺序不一致永远计入差异</summary>
    public Dictionary<string, (bool Strict, List<string> Items)> Lists { get; } = new(StringComparer.Ordinal);

    private static readonly Dictionary<int, string> SysTypes = new()
    {
        [EplSystemId.DataType_Any] = "通用型",
        [EplSystemId.DataType_Byte] = "字节型",
        [EplSystemId.DataType_Short] = "短整数型",
        [EplSystemId.DataType_Int] = "整数型",
        [EplSystemId.DataType_Long] = "长整数型",
        [EplSystemId.DataType_Float] = "小数型",
        [EplSystemId.DataType_Double] = "双精度小数型",
        [EplSystemId.DataType_Bool] = "逻辑型",
        [EplSystemId.DataType_DateTime] = "日期时间型",
        [EplSystemId.DataType_String] = "文本型",
        [EplSystemId.DataType_Bin] = "字节集",
        [EplSystemId.DataType_MethodPtr] = "子程序指针",
        [EplSystemId.DataType_Lambda] = "条件语句型",
    };

    public Side(string file)
    {
        FilePath = file;
        using (var fs = File.OpenRead(file)) Doc.Load(fs);
        Code = Doc.Get(CodeSection.Key);
        Res = Doc.GetOrNull(ResourceSection.Key) ?? new ResourceSection { Forms = new(), Constants = new() };
        Deps = Doc.GetOrNull(ECDependenciesSection.Key)?.ECDependencies ?? new();
        Enc = Doc.DetermineEncoding();
        var used = new Dictionary<string, int>(StringComparer.Ordinal);
        LibKeys = (Code.Libraries ?? Array.Empty<LibraryRefInfo>()).Select(l => Uniq(used, l.FileName ?? "")).ToArray();
        var los = Doc.GetOrNull(LosableSection.Key);
        if (los?.RemovedDefinedItems != null)
            foreach (var x in los.RemovedDefinedItems) Removed.TryAdd(x.Id, x.Name ?? "");
        BuildModules();
        BuildNames();
        BuildEnts();
    }

    // ---------------- 引用翻译 ----------------
    public string Lib(int idx) => idx >= 0 && idx < LibKeys.Length ? "lib" + LibKeys[idx] : "lib?" + idx.ToString(Inv);

    /// <summary>用户定义 ID → 规范路径</summary>
    public string Ref(int id)
    {
        if (id == 0) return "无";
        if (PathOf.TryGetValue(id, out var p)) return p;
        if (Removed.TryGetValue(id, out var n)) return "?DEL:" + n;
        var t = EplSystemId.GetType(id);
        if (t == EplSystemId.Type_Struct || t == EplSystemId.Type_Class || t == EplSystemId.Type_StructMember
            || t == EplSystemId.Type_ClassMember || t == EplSystemId.Type_StaticClass)
            return $"?PRIVATE:0x{id:X8}";
        return $"?UNK:0x{id:X8}";
    }

    /// <summary>数据类型 ID → 系统类型名 / 库类型 lib&lt;Key&gt;#t&lt;号&gt; / 用户类型规范路径</summary>
    public string T(int t)
    {
        if (t == 0) return "无";
        if (SysTypes.TryGetValue(t, out var n)) return n;
        if (EplSystemId.IsLibDataType(t))
        {
            EplSystemId.DecomposeLibDataTypeId(t, out var lib, out var type);
            return $"{Lib(lib)}#t{type}";
        }
        if (unchecked((uint)t) >= 0x80000000u) return $"sys:0x{t:X8}";
        return Ref(t);
    }

    // ---------------- 模块归属（同 Core/Model/EProject.cs 的算法） ----------------
    private bool InModule(int id, out int mi)
    {
        for (int i = 0; i < Deps.Count; i++)
            if (Deps[i].DefinedIds != null)
                foreach (var r in Deps[i].DefinedIds)
                    if (unchecked((uint)id - (uint)r.Start) < (uint)r.Count) { mi = i; return true; }
        mi = -1;
        return false;
    }

    private void BuildModules()
    {
        if (Deps.Count == 0) return;
        foreach (var m in Code.Methods)
            if (InModule(m.Id, out var mi))
            {
                ModuleOf[m.Id] = mi;
                foreach (var v in m.Parameters) ModuleOf[v.Id] = mi;
                foreach (var v in m.Variables) ModuleOf[v.Id] = mi;
            }
        foreach (var c in Code.Classes)
        {
            if (!InModule(c.Id, out var mi))
            {
                // 普通程序集不在 DefinedIds 区段里：按其子程序归属判定
                var owners = c.Methods.Select(x => ModuleOf.TryGetValue(x, out var o) ? o : -1).ToList();
                mi = owners.Count > 0 && owners.All(o => o == owners[0]) ? owners[0] : -1;
            }
            if (mi >= 0)
            {
                ModuleOf[c.Id] = mi;
                foreach (var v in c.Variables) ModuleOf[v.Id] = mi;
            }
        }
        foreach (var s in Code.Structs)
            if (InModule(s.Id, out var mi)) { ModuleOf[s.Id] = mi; foreach (var v in s.Members) ModuleOf[v.Id] = mi; }
        foreach (var d in Code.DllDeclares)
            if (InModule(d.Id, out var mi)) { ModuleOf[d.Id] = mi; foreach (var v in d.Parameters) ModuleOf[v.Id] = mi; }
        foreach (var g in Code.GlobalVariables)
            if (InModule(g.Id, out var mi)) ModuleOf[g.Id] = mi;
        foreach (var k in Res.Constants)
            if (InModule(k.Id, out var mi)) ModuleOf[k.Id] = mi;
    }

    private string ModName(int mi) => string.IsNullOrEmpty(Deps[mi].Name) ? "模块" + mi.ToString(Inv) : Deps[mi].Name;
    private string ModPrefix(int id) => ModuleOf.TryGetValue(id, out var m) ? "[" + ModName(m) + "]" : "";

    private static CultureInfo Inv => Cmp.Inv;

    /// <summary>重名时附 ~2、~3…（首个保持原名），两边算法一致</summary>
    internal static string Uniq(Dictionary<string, int> used, string n)
    {
        if (!used.TryGetValue(n, out var c)) { used[n] = 1; return n; }
        string r;
        do { c++; r = n + "~" + c.ToString(Inv); } while (used.ContainsKey(r));
        used[n] = c;
        used[r] = 1;
        return r;
    }

    /// <summary>顶层实体命名：同名冲突时模块内实体加 [模块名] 前缀（主工程不加），仍冲突则附 ~n</summary>
    private List<string> NameTop<T>(IList<T> items, Func<T, int> id, Func<T, string?> name, Func<T, string> tag)
    {
        var unnamed = new Dictionary<string, int>(StringComparer.Ordinal);
        var bases = new List<string>(items.Count);
        foreach (var x in items)
        {
            var tg = tag(x);
            var nm = name(x);
            if (string.IsNullOrEmpty(nm))
            {
                unnamed.TryGetValue(tg, out var k);
                unnamed[tg] = k + 1;
                nm = "#" + k.ToString(Inv);
            }
            bases.Add(tg + nm);
        }
        var dup = bases.GroupBy(s => s, StringComparer.Ordinal).Where(g => g.Count() > 1).Select(g => g.Key).ToHashSet(StringComparer.Ordinal);
        var used = new Dictionary<string, int>(StringComparer.Ordinal);
        var res = new List<string>(items.Count);
        for (int i = 0; i < items.Count; i++)
        {
            var n = bases[i];
            if (dup.Contains(n)) n = ModPrefix(id(items[i])) + n;
            n = Uniq(used, n);
            PathOf.TryAdd(id(items[i]), n);
            res.Add(n);
        }
        return res;
    }

    /// <summary>下级实体命名：parent/tag:名；无名用 #序号（在无名者中计数）；重名附 ~n</summary>
    private List<string> NameChildren<T>(string parent, string tag, IList<T>? items, Func<T, int> id, Func<T, string?> name)
    {
        var res = new List<string>();
        if (items == null) return res;
        var used = new Dictionary<string, int>(StringComparer.Ordinal);
        int unnamed = 0;
        foreach (var x in items)
        {
            var nm = name(x);
            var n = parent + "/" + tag + ":" + (string.IsNullOrEmpty(nm) ? "#" + (unnamed++).ToString(Inv) : nm);
            n = Uniq(used, n);
            PathOf.TryAdd(id(x), n);
            res.Add(n);
        }
        return res;
    }

    // 命名阶段的中间结果
    private List<string> _classPaths = new(), _globalPaths = new(), _structPaths = new(), _dllPaths = new(), _constPaths = new(), _formPaths = new();
    private readonly Dictionary<int, string> _methodPath = new();
    private readonly Dictionary<string, List<MethodInfo>> _methodsOfParent = new(StringComparer.Ordinal);
    private readonly Dictionary<int, List<string>> _elemPaths = new();

    private static string ConstTag(ConstantInfo k) => (unchecked((uint)k.Id) >> 28) switch
    {
        1 => "K:",
        2 => "K:图片",
        3 => "K:声音",
        var x => "K:种类" + x.ToString(Inv),
    };

    private void BuildNames()
    {
        // 程序集
        _classPaths = NameTop(Code.Classes, c => c.Id, c => c.Name, _ => "C:");
        var methodById = new Dictionary<int, MethodInfo>();
        foreach (var m in Code.Methods) methodById.TryAdd(m.Id, m);
        var owned = new HashSet<int>();
        for (int i = 0; i < Code.Classes.Count; i++)
        {
            var c = Code.Classes[i];
            NameChildren(_classPaths[i], "V", c.Variables, v => v.Id, v => v.Name);
            var ms = new List<MethodInfo>();
            foreach (var mid in c.Methods)
                if (owned.Add(mid) && methodById.TryGetValue(mid, out var m)) ms.Add(m);
            _methodsOfParent[_classPaths[i]] = ms;
        }
        // 未被任何程序集列出的子程序：按 MethodInfo.Class 归到父
        foreach (var m in Code.Methods)
        {
            if (owned.Contains(m.Id)) continue;
            owned.Add(m.Id);
            var parent = PathOf.TryGetValue(m.Class, out var cp) ? cp : $"C:?0x{m.Class:X8}";
            if (!_methodsOfParent.TryGetValue(parent, out var list)) _methodsOfParent[parent] = list = new();
            list.Add(m);
        }
        foreach (var (parent, ms) in _methodsOfParent)
        {
            var paths = NameChildren(parent, "M", ms, m => m.Id, m => m.Name);
            for (int i = 0; i < ms.Count; i++)
            {
                _methodPath[ms[i].Id] = paths[i];
                NameChildren(paths[i], "P", ms[i].Parameters, v => v.Id, v => v.Name);
                NameChildren(paths[i], "L", ms[i].Variables, v => v.Id, v => v.Name);
            }
        }
        // 全局变量 / 数据类型 / DLL / 常量资源
        _globalPaths = NameTop(Code.GlobalVariables, g => g.Id, g => g.Name, _ => "G:");
        _structPaths = NameTop(Code.Structs, s => s.Id, s => s.Name, _ => "S:");
        for (int i = 0; i < Code.Structs.Count; i++)
            NameChildren(_structPaths[i], "F", Code.Structs[i].Members, v => v.Id, v => v.Name);
        _dllPaths = NameTop(Code.DllDeclares, d => d.Id, d => d.Name, _ => "D:");
        for (int i = 0; i < Code.DllDeclares.Count; i++)
            NameChildren(_dllPaths[i], "P", Code.DllDeclares[i].Parameters, v => v.Id, v => v.Name);
        // 常量/资源：无名的按种类分开计序号（K:#n / K:图片#n / K:声音#n）
        _constPaths = NameTop(Res.Constants, k => k.Id, k => k.Name, ConstTag);
        // 窗口与元素
        _formPaths = NameTop(Res.Forms, f => f.Id, f => f.Name, _ => "W:");
        for (int i = 0; i < Res.Forms.Count; i++)
        {
            var f = Res.Forms[i];
            var used = new Dictionary<string, int>(StringComparer.Ordinal);
            var list = new List<string>();
            int unnamed = 0;
            foreach (var e in f.Elements ?? new())
            {
                string n;
                if (EplSystemId.GetType(e.Id) == EplSystemId.Type_FormSelf) n = _formPaths[i] + "/SELF";
                else n = _formPaths[i] + "/E:" + (string.IsNullOrEmpty(e.Name) ? "#" + (unnamed++).ToString(Inv) : e.Name);
                n = Uniq(used, n);
                PathOf.TryAdd(e.Id, n);
                list.Add(n);
            }
            _elemPaths[f.Id] = list;
        }
    }

    /// <summary>
    /// 名称对齐回退：把本侧若干顶层路径改名为对侧路径（含全部下级、所有引用处），然后重建实体。
    /// 用于模块前缀因归属判定不同而对不上的情况（归属差异本身由 [模块归属] 属性报告）。
    /// </summary>
    public void ApplyAlias(Dictionary<string, string> alias)
    {
        string Al(string p)
        {
            int i = p.IndexOf('/');
            var top = i < 0 ? p : p[..i];
            return alias.TryGetValue(top, out var t) ? (i < 0 ? t : t + p[i..]) : p;
        }
        foreach (var k in PathOf.Keys.ToList()) PathOf[k] = Al(PathOf[k]);
        foreach (var k in _methodPath.Keys.ToList()) _methodPath[k] = Al(_methodPath[k]);
        foreach (var k in _elemPaths.Keys.ToList()) _elemPaths[k] = _elemPaths[k].Select(Al).ToList();
        var mop = _methodsOfParent.ToList();
        _methodsOfParent.Clear();
        foreach (var (k, v) in mop) _methodsOfParent[Al(k)] = v;
        _classPaths = _classPaths.Select(Al).ToList();
        _globalPaths = _globalPaths.Select(Al).ToList();
        _structPaths = _structPaths.Select(Al).ToList();
        _dllPaths = _dllPaths.Select(Al).ToList();
        _constPaths = _constPaths.Select(Al).ToList();
        _formPaths = _formPaths.Select(Al).ToList();
        Ents.Clear();
        Lists.Clear();
        BuildEnts();
    }

    private string Owner(int id) => ModuleOf.TryGetValue(id, out var m) ? ModName(m) : "主工程";

    // ---------------- 实体与属性 ----------------
    private Ent Add(string path, string kind, string? parent)
    {
        var e = new Ent(path, kind, parent);
        if (!Ents.TryAdd(path, e))
        {
            // 理论上 Uniq 已保证唯一；防御：再附序号
            var used = new Dictionary<string, int>(StringComparer.Ordinal);
            foreach (var k in Ents.Keys) used[k] = 1;
            e = new Ent(Uniq(used, path), kind, parent);
            Ents.Add(e.Path, e);
        }
        return e;
    }

    private void AddList(string key, bool strict, IEnumerable<string> items) => Lists[key] = (strict, items.ToList());

    private void VarProps(Ent e, AbstractVariableInfo v)
    {
        e.P("类型", T(v.DataType), "类型");
        e.P("标志", Cmp.Hex(v.Flags), "标志");
        e.P("数组", v.UBound == null || v.UBound.Length == 0 ? "无" : string.Join(",", v.UBound), "数组");
        e.P("备注", v.Comment, "备注");
        if (v.TrailingBytes is { Length: > 0 }) e.P("尾部字节", Cmp.Sha(v.TrailingBytes), "杂项");
    }

    private void Vars(string parent, string kind, IList<AbstractVariableInfo>? vars, string listKey, bool strict)
    {
        if (vars == null) return;
        var paths = new List<string>();
        foreach (var v in vars)
        {
            var e = Add(PathOf[v.Id], kind, parent);
            VarProps(e, v);
            paths.Add(e.Path);
        }
        AddList(listKey, strict, paths);
    }

    private void BuildEnts()
    {
        // ---- 段列表 ----
        {
            var used = new Dictionary<string, int>(StringComparer.Ordinal);
            var paths = new List<string>();
            foreach (var s in Doc.Sections)
            {
                var e = Add(Uniq(used, $"SEC:0x{s.SectionKey:X8}"), "段", null);
                e.MissingCat = "段";
                e.P("段名", s.SectionName, "段");
                paths.Add(e.Path);
            }
            AddList("段顺序", false, paths);
        }
        // ---- 系统信息 / 用户信息 ----
        var si = Doc.GetOrNull(ESystemInfoSection.Key);
        if (si != null)
        {
            var e = Add("SYS", "系统信息", null);
            e.P("易语言版本", si.ESystemVersion?.ToString(), "系统信息");
            e.P("语言", si.Language.ToString(Inv), "系统信息");
            e.P("格式版本", si.EProjectFormatVersion?.ToString(), "系统信息");
            e.P("文件类型", si.FileType.ToString(Inv), "系统信息");
            e.P("工程类型", si.ProjectType.ToString(Inv), "系统信息");
        }
        var pc = Doc.GetOrNull(ProjectConfigSection.Key);
        if (pc != null)
        {
            var e = Add("USER", "用户信息", null);
            e.P("名称", pc.Name, "用户信息");
            e.P("描述", pc.Description, "用户信息");
            e.P("作者", pc.Author, "用户信息");
            e.P("邮编", pc.ZipCode, "用户信息");
            e.P("地址", pc.Address, "用户信息");
            e.P("电话", pc.TelephoneNumber, "用户信息");
            e.P("传真", pc.FaxNumber, "用户信息");
            e.P("邮箱", pc.Email, "用户信息");
            e.P("主页", pc.Homepage, "用户信息");
            e.P("版权", pc.Copyright, "用户信息");
            e.P("版本", pc.Version?.ToString(), "用户信息");
            e.P("写版本", pc.WriteVersion ? "真" : "假", "用户信息");
            e.P("编译插件", pc.CompilePlugins, "用户信息");
            e.P("导出公开类方法", pc.ExportPublicClassMethod ? "真" : "假", "用户信息");
        }
        // ---- 程序段标量 ----
        {
            var e = Add("CODE", "程序段", null);
            e.P("Flag", Cmp.Hex(Code.Flag), "程序段");
            e.P("MainMethod", Ref(Code.MainMethod), "程序段");
            e.P("图标", Cmp.Sha(Code.IconData), "程序段");
            e.P("调试命令行", Code.DebugCommandParameters, "程序段");
            e.P("UnknownBeforeIconData", Cmp.Sha(Code.UnknownBeforeIconData), "杂项");
        }
        // ---- 支持库引用（按 FileName 对齐） ----
        {
            var paths = new List<string>();
            var libs = Code.Libraries ?? Array.Empty<LibraryRefInfo>();
            for (int i = 0; i < libs.Length; i++)
            {
                var l = libs[i];
                var e = Add("LIB:" + LibKeys[i], "支持库", null);
                e.P("Guid", l.GuidString, "库");
                e.P("版本", l.Version?.ToString(), "库");
                e.P("名称", l.Name, "库");
                e.P("MinRequiredCmd", l.MinRequiredCmd.ToString(Inv), "库");
                e.P("MinRequiredDataType", l.MinRequiredDataType.ToString(Inv), "库");
                e.P("MinRequiredConstant", l.MinRequiredConstant.ToString(Inv), "库");
                paths.Add(e.Path);
            }
            AddList("支持库顺序", false, paths);
        }
        // ---- 易模块记录（不比 ID 区段） ----
        {
            var used = new Dictionary<string, int>(StringComparer.Ordinal);
            var paths = new List<string>();
            foreach (var d in Deps)
            {
                var e = Add(Uniq(used, "EC:" + (d.Name ?? "")), "易模块", null);
                e.P("InfoVersion", d.InfoVersion.ToString(Inv), "模块");
                e.P("FileSize", d.FileSize.ToString(Inv), "模块");
                e.P("修改时间", d.FileLastModifiedDate.ToString("O", Inv), "模块");
                e.P("ReExport", d.ReExport ? "真" : "假", "模块");
                e.P("路径", d.Path, "模块");
                paths.Add(e.Path);
            }
            AddList("易模块顺序", false, paths);
        }
        // ---- 初始模块段 ----
        var init = Doc.GetOrNull(InitECSection.Key);
        if (init != null)
        {
            var e = Add("INIT", "初始模块", null);
            var names = init.ECName ?? Array.Empty<string>();
            var ms = init.InitMethod ?? Array.Empty<int>();
            var items = Enumerable.Range(0, Math.Max(names.Length, ms.Length))
                .Select(i => (i < names.Length ? names[i] : "?") + "→" + (i < ms.Length ? Ref(ms[i]) : "?"));
            e.P("列表", string.Join(" | ", items), "初始模块");
        }

        // ---- 程序集 / 子程序 ----
        var pub = new Dictionary<int, int>();
        var cps = Doc.GetOrNull(ClassPublicitySection.Key);
        if (cps?.ClassPublicities != null) foreach (var x in cps.ClassPublicities) pub.TryAdd(x.Class, x.Flags);
        var epk = Doc.GetOrNull(EPackageInfoSection.Key)?.FileNames;
        var epkOf = new Dictionary<int, string>();
        if (epk != null)
            for (int i = 0; i < Math.Min(epk.Length, Code.Methods.Count); i++)
                if (epk[i] != null) epkOf[Code.Methods[i].Id] = epk[i];

        AddList("程序集表", false, _classPaths);
        for (int i = 0; i < Code.Classes.Count; i++)
        {
            var c = Code.Classes[i];
            var e = Add(_classPaths[i], "程序集", null);
            e.P("种类", Cmp.Hex(EplSystemId.GetType(c.Id)), "属性");
            e.P("模块归属", Owner(c.Id), "模块归属");
            e.P("基类", c.BaseClass == 0 || c.BaseClass == -1 ? "无" : T(c.BaseClass), "类型");
            e.P("窗口", Ref(c.Form), "属性");
            e.P("备注", c.Comment, "备注");
            if (pub.TryGetValue(c.Id, out var pf)) e.P("公开性", Cmp.Hex(pf), "标志");
            e.P("内存地址", Cmp.Hex(c.MemoryAddress), "杂项");
            Vars(e.Path, "程序集变量", c.Variables?.Cast<AbstractVariableInfo>().ToList(), e.Path + " 程序集变量表", false);
        }
        foreach (var (parent, ms) in _methodsOfParent)
        {
            AddList(parent + " 子程序表", false, ms.Select(m => _methodPath[m.Id]));
            foreach (var m in ms)
            {
                var e = Add(_methodPath[m.Id], "子程序", Ents.ContainsKey(parent) ? parent : null);
                e.Method = m;
                e.P("模块归属", Owner(m.Id), "模块归属");
                e.P("返回类型", T(m.ReturnDataType), "类型");
                e.P("标志", Cmp.Hex(m.Flags), "标志");
                e.P("备注", m.Comment, "备注");
                if (epkOf.TryGetValue(m.Id, out var ep)) e.P("易包", ep, "易包");
                e.P("内存地址", Cmp.Hex(m.MemoryAddress), "杂项");
                Vars(e.Path, "参数", m.Parameters?.Cast<AbstractVariableInfo>().ToList(), e.Path + " 参数表", true);
                Vars(e.Path, "局部变量", m.Variables?.Cast<AbstractVariableInfo>().ToList(), e.Path + " 局部变量表", false);
            }
        }
        // ---- 全局变量 ----
        AddList("全局变量表", false, _globalPaths);
        for (int i = 0; i < Code.GlobalVariables.Count; i++)
        {
            var e = Add(_globalPaths[i], "全局变量", null);
            e.P("模块归属", Owner(Code.GlobalVariables[i].Id), "模块归属");
            VarProps(e, Code.GlobalVariables[i]);
        }
        // ---- 数据类型 ----
        AddList("数据类型表", false, _structPaths);
        for (int i = 0; i < Code.Structs.Count; i++)
        {
            var s = Code.Structs[i];
            var e = Add(_structPaths[i], "数据类型", null);
            e.P("模块归属", Owner(s.Id), "模块归属");
            e.P("标志", Cmp.Hex(s.Flags), "标志");
            e.P("备注", s.Comment, "备注");
            e.P("内存地址", Cmp.Hex(s.MemoryAddress), "杂项");
            Vars(e.Path, "成员", s.Members?.Cast<AbstractVariableInfo>().ToList(), e.Path + " 成员表", true);
        }
        // ---- DLL ----
        AddList("DLL表", false, _dllPaths);
        for (int i = 0; i < Code.DllDeclares.Count; i++)
        {
            var d = Code.DllDeclares[i];
            var e = Add(_dllPaths[i], "DLL", null);
            e.P("模块归属", Owner(d.Id), "模块归属");
            e.P("返回类型", T(d.ReturnDataType), "类型");
            e.P("标志", Cmp.Hex(d.Flags), "标志");
            e.P("库名", d.LibraryName, "属性");
            e.P("入口名", d.EntryPoint, "属性");
            e.P("备注", d.Comment, "备注");
            e.P("内存地址", Cmp.Hex(d.MemoryAddress), "杂项");
            Vars(e.Path, "DLL参数", d.Parameters?.Cast<AbstractVariableInfo>().ToList(), e.Path + " 参数表", true);
        }
        // ---- 常量 / 资源 ----
        AddList("常量资源表", false, _constPaths);
        for (int i = 0; i < Res.Constants.Count; i++)
        {
            var k = Res.Constants[i];
            var e = Add(_constPaths[i], "常量资源", null);
            e.P("模块归属", Owner(k.Id), "模块归属");
            e.P("种类", ((unchecked((uint)k.Id) >> 28)).ToString(Inv), "属性");
            e.P("标志", Cmp.Hex(k.Flags), "标志");
            e.P("备注", k.Comment, "备注");
            e.P("值", Cmp.CVal(k.Value), "常量值");
        }
        // ---- 窗口 ----
        AddList("窗口表", false, _formPaths);
        for (int i = 0; i < Res.Forms.Count; i++)
        {
            var f = Res.Forms[i];
            var e = Add(_formPaths[i], "窗口", null);
            e.P("备注", f.Comment, "窗口");
            e.P("程序集", Ref(f.Class), "窗口");
            e.P("UnknownBeforeClass", Cmp.Hex(f.UnknownBeforeClass), "杂项");
            e.P("内存地址", Cmp.Hex(f.MemoryAddress), "杂项");
            var eps = _elemPaths[f.Id];
            AddList(e.Path + " 元素表", false, eps);
            var elems = f.Elements ?? new();
            for (int j = 0; j < elems.Count; j++)
            {
                var el = elems[j];
                if (el is FormMenuInfo mn)
                {
                    var x = Add(eps[j], "菜单", e.Path);
                    x.P("类型", T(mn.DataType), "窗口");
                    x.P("标题", mn.Text, "窗口");
                    x.P("热键", Cmp.Hex(mn.HotKey), "窗口");
                    x.P("层级", mn.Level.ToString(Inv), "窗口");
                    x.P("选中", mn.Selected ? "真" : "假", "窗口");
                    x.P("可视", mn.Visible ? "真" : "假", "窗口");
                    x.P("禁止", mn.Disable ? "真" : "假", "窗口");
                    x.P("点击事件", Ref(mn.ClickEvent), "窗口");
                    x.P("UnknownBeforeName", Cmp.Sha(mn.UnknownBeforeName), "杂项");
                    x.P("UnknownAfterClickEvent", Cmp.Sha(mn.UnknownAfterClickEvent), "杂项");
                }
                else if (el is FormControlInfo fc)
                {
                    var x = Add(eps[j], EplSystemId.GetType(fc.Id) == EplSystemId.Type_FormSelf ? "窗口自身" : "组件", e.Path);
                    x.P("类型", T(fc.DataType), "窗口");
                    x.P("备注", fc.Comment, "窗口");
                    x.P("位置尺寸", $"{fc.Left},{fc.Top},{fc.Width},{fc.Height}", "窗口");
                    x.P("可视", fc.Visible ? "真" : "假", "窗口");
                    x.P("禁止", fc.Disable ? "真" : "假", "窗口");
                    x.P("停留", fc.TabStop ? "真" : "假", "窗口");
                    x.P("锁定", fc.Locked ? "真" : "假", "窗口");
                    x.P("停留顺序", fc.TabIndex.ToString(Inv), "窗口");
                    x.P("父", Ref(fc.Parent), "窗口");
                    x.P("子", string.Join(",", (fc.Children ?? Array.Empty<int>()).Select(Ref)), "窗口");
                    x.P("事件", string.Join(",", (fc.Events ?? Array.Empty<KeyValuePair<int, int>>()).Select(kv => kv.Key.ToString(Inv) + "→" + Ref(kv.Value))), "窗口");
                    x.P("鼠标指针", Cmp.Sha(fc.Cursor), "窗口");
                    x.P("标记", fc.Tag, "窗口");
                    x.P("扩展数据", Cmp.Sha(fc.ExtensionData), "窗口");
                    x.P("CWndAddress", Cmp.Hex(fc.CWndAddress), "杂项");
                    x.P("UnknownBeforeName", Cmp.Sha(fc.UnknownBeforeName), "杂项");
                    x.P("UnknownBeforeParent", Cmp.Hex(fc.UnknownBeforeParent), "杂项");
                    x.P("UnknownBeforeVisible", Cmp.Hex(fc.UnknownBeforeVisible), "杂项");
                    x.P("UnknownBeforeExtensionData", Cmp.Sha(fc.UnknownBeforeExtensionData), "杂项");
                }
            }
        }
        // ---- 分组树（编辑过滤器信息段，按名称路径） ----
        var fol = Doc.GetOrNull(FolderSection.Key);
        if (fol?.Folders != null)
        {
            var byKey = new Dictionary<int, CodeFolderInfo>();
            foreach (var f in fol.Folders) byKey.TryAdd(f.Key, f);
            string NameOf(CodeFolderInfo f)
            {
                var parts = new List<string>();
                var seen = new HashSet<int>();
                for (var cur = f; cur != null && seen.Add(cur.Key); cur = byKey.GetValueOrDefault(cur.ParentKey))
                    parts.Insert(0, cur.Name ?? "");
                return "F:" + string.Join("/", parts);
            }
            var used = new Dictionary<string, int>(StringComparer.Ordinal);
            var pathOfKey = new Dictionary<int, string>();
            foreach (var f in fol.Folders) pathOfKey[f.Key] = Uniq(used, NameOf(f));
            foreach (var f in fol.Folders)
            {
                var parent = byKey.ContainsKey(f.ParentKey) && f.ParentKey != f.Key ? pathOfKey[f.ParentKey] : null;
                var e = Add(pathOfKey[f.Key], "分组", parent);
                e.P("成员", string.Join(",", (f.Children ?? Array.Empty<int>()).Select(Ref)), "分组");
                e.P("展开", f.Expand ? "真" : "假", "杂项");
            }
            AddList("分组表", false, fol.Folders.Select(f => pathOfKey[f.Key]));
        }
    }
}

// =====================================================================
// 代码规范化：语句树 → 行文本（ID 全部替换为规范路径）
// =====================================================================
internal sealed class CodeRender
{
    private readonly Side s;
    private readonly bool defs;   // 是否保留 ParamList 末尾的默认参数（DefaultValueExpression）
    private readonly bool misc;   // 是否输出杂项字段（EmptyLine*、CallExpression.ExtraFlags）
    private readonly List<string> lines = new();
    private readonly StringBuilder sb = new();

    private CodeRender(Side s, bool defs, bool misc) { this.s = s; this.defs = defs; this.misc = misc; }

    public static List<string> Render(Side s, StatementBlock block, bool defs, bool misc)
    {
        var r = new CodeRender(s, defs, misc);
        r.B(block, 0);
        return r.lines;
    }

    private void B(StatementBlock? blk, int d)
    {
        if (blk == null) return;
        foreach (var st in blk) S(st, d);
    }

    private void L(int d, string text, bool mask = false, string? cmt = null, string? unex = null)
    {
        sb.Clear();
        sb.Append(' ', d * 2);
        if (mask) sb.Append("屏蔽│");
        sb.Append(text);
        if (unex != null) sb.Append(" ⟨未验证:").Append(Cmp.Q(unex)).Append('⟩');
        if (cmt != null) sb.Append("  '").Append(Cmp.Esc(cmt));
        lines.Add(sb.ToString());
    }

    private void S(Statement st, int d)
    {
        switch (st)
        {
            case ExpressionStatement x:
                if (x.Expression == null)
                {
                    var t = "<空行>";
                    if (misc && (x.EmptyLineMethodId != 0 || x.EmptyLineExtraFlags != 0))
                        t += $"{{elm=0x{x.EmptyLineMethodId:X},elf=0x{x.EmptyLineExtraFlags:X}}}";
                    L(d, t, x.Mask, x.Comment);
                }
                else L(d, Call(x.Expression), x.Mask, x.Comment);
                break;
            case UnexaminedStatement x:
                L(d, "未验证 " + Cmp.Q(x.UnexaminedCode), x.Mask);
                break;
            case IfElseStatement x:
                L(d, "如果 " + E(x.Condition), x.Mask, x.Comment, x.UnexaminedCode);
                B(x.BlockOnTrue, d + 1);
                L(d, "否则");
                B(x.BlockOnFalse, d + 1);
                L(d, "如果结束");
                break;
            case IfStatement x:
                L(d, "如果真 " + E(x.Condition), x.Mask, x.Comment, x.UnexaminedCode);
                B(x.Block, d + 1);
                L(d, "如果真结束");
                break;
            case SwitchStatement x:
                foreach (var c in x.Case)
                {
                    L(d, "判断 " + E(c.Condition), c.Mask, c.Comment, c.UnexaminedCode);
                    B(c.Block, d + 1);
                }
                L(d, "默认");
                B(x.DefaultBlock, d + 1);
                L(d, "判断结束");
                break;
            case WhileStatement x:
                L(d, "判断循环首 " + E(x.Condition), x.MaskOnStart, x.CommentOnStart, x.UnexaminedCode);
                B(x.Block, d + 1);
                L(d, "判断循环尾", x.MaskOnEnd, x.CommentOnEnd);
                break;
            case DoWhileStatement x:
                L(d, "循环判断首", x.MaskOnStart, x.CommentOnStart);
                B(x.Block, d + 1);
                L(d, "循环判断尾 " + E(x.Condition), x.MaskOnEnd, x.CommentOnEnd, x.UnexaminedCode);
                break;
            case CounterStatement x:
                L(d, $"计次循环首 ({E(x.Count)}, {E(x.Var)})", x.MaskOnStart, x.CommentOnStart, x.UnexaminedCode);
                B(x.Block, d + 1);
                L(d, "计次循环尾", x.MaskOnEnd, x.CommentOnEnd);
                break;
            case ForStatement x:
                L(d, $"变量循环首 ({E(x.Start)}, {E(x.End)}, {E(x.Step)}, {E(x.Var)})", x.MaskOnStart, x.CommentOnStart, x.UnexaminedCode);
                B(x.Block, d + 1);
                L(d, "变量循环尾", x.MaskOnEnd, x.CommentOnEnd);
                break;
            default:
                L(d, "<" + st.GetType().Name + ">");
                break;
        }
    }

    private string PL(ParamListExpression? pl)
    {
        if (pl == null || pl.Count == 0) return "()";
        int n = pl.Count;
        if (!defs) while (n > 0 && pl[n - 1] is DefaultValueExpression) n--;
        var parts = new string[n];
        for (int i = 0; i < n; i++) parts[i] = E(pl[i]);
        return "(" + string.Join(", ", parts) + ")";
    }

    private string Call(CallExpression c)
    {
        var t = new StringBuilder();
        if (c.Target != null) t.Append(E(c.Target)).Append('.');
        if (c.InvokeSpecial) t.Append("!special:");
        t.Append(c.LibraryId switch
        {
            -2 or -3 => s.Ref(c.MethodId),
            -1 => "<空>",
            >= 0 => s.Lib(c.LibraryId) + "#" + c.MethodId.ToString(Cmp.Inv),
            _ => $"lib?{c.LibraryId}#{c.MethodId}",
        });
        if (misc && c.ExtraFlags != 0) t.Append("{xf=0x").Append(c.ExtraFlags.ToString("X", Cmp.Inv)).Append('}');
        t.Append(PL(c.ParamList));
        return t.ToString();
    }

    private string Mem(AccessMemberExpression a)
    {
        string mem;
        if (a.LibraryId == -2)
        {
            if (EplSystemId.GetType(a.StructId) == EplSystemId.Type_Form && (a.MemberId & unchecked((int)0xFF000000)) == 0)
                mem = $"{s.Lib(0)}#t0.m{a.MemberId - 1}"; // 窗口.系统属性
            else
            {
                mem = s.Ref(a.MemberId);
                if (mem.StartsWith('?')) mem += "@" + s.T(a.StructId);
            }
        }
        else mem = $"{s.Lib(a.LibraryId)}#t{a.StructId}.m{a.MemberId}";
        return E(a.Target) + "." + mem;
    }

    private string E(Expression? e) => e switch
    {
        null => "∅",
        CallExpression c => Call(c),
        AccessMemberExpression a => Mem(a),
        AccessArrayExpression a => E(a.Target) + "[" + E(a.Index) + "]",
        VariableExpression v => s.Ref(v.Id),
        ConstantExpression k => k.LibraryId == -2 ? "#" + s.Ref(k.ConstantId) : $"#{s.Lib(k.LibraryId)}#k{k.ConstantId}",
        EmnuConstantExpression k => $"#{s.Lib(k.LibraryId)}#t{k.StructId}.m{k.MemberId}",
        MethodPtrExpression p => "&" + s.Ref(p.MethodId),
        StringLiteral x => Cmp.Q(x.Value),
        NumberLiteral x => x.Value.ToString("R", Cmp.Inv),
        BoolLiteral x => x.Value ? "真" : "假",
        DateTimeLiteral x => "[" + x.Value.ToString("yyyy-MM-dd HH:mm:ss.fff", Cmp.Inv) + "]",
        ArrayLiteralExpression x => "{" + string.Join(", ", x.Select(E)) + "}",
        DefaultValueExpression => "_",
        ParamListExpression pl => PL(pl),
        _ => "<" + e.GetType().Name + ">",
    };
}

// =====================================================================
// 比对
// =====================================================================
internal sealed class Comparer
{
    private readonly Side A, B;
    private readonly bool ignoreOrder, noCode;
    public List<Cmp.Diff> Diffs { get; } = new();
    public int CodeCompared { get; private set; }

    public Comparer(Side a, Side b, bool ignoreOrder, bool noCode)
    {
        A = a; B = b; this.ignoreOrder = ignoreOrder; this.noCode = noCode;
    }

    private void Add(string cat, string path, string desc, string? a, string? b)
    {
        // 两边仅 ?PRIVATE:0x.. 不同：单独计为「私有类型 ID 不同」
        if (a != null && b != null && a != b && Cmp.NormPriv(a) == Cmp.NormPriv(b)) cat = "私有类型ID";
        Diffs.Add(new Cmp.Diff(cat, path, desc, a, b));
    }

    private static readonly System.Text.RegularExpressions.Regex StripRe = new(@"^\[[^\]]*\]|~\d+$", System.Text.RegularExpressions.RegexOptions.Compiled);

    /// <summary>
    /// 顶层实体的回退对齐：精确路径对不上时，去掉 [模块] 前缀与 ~n 后缀后按（种类, 名称, 出现顺序）配对，
    /// 把 b 侧改名成 a 侧路径。解决「一边判定为模块内同名冲突加了前缀、另一边因 ID 区段不同没加」的误报。
    /// </summary>
    public int AlignTop()
    {
        List<Ent> Left(Side s, Side o) => s.Ents.Values.Where(e => e.Parent == null && !e.Path.Contains('/') && !o.Ents.ContainsKey(e.Path)).ToList();
        var la = Left(A, B);
        var lb = Left(B, A);
        if (la.Count == 0 || lb.Count == 0) return 0;
        string Key(Ent e) => e.Kind + "|" + StripRe.Replace(e.Path, "");
        var qa = new Dictionary<string, Queue<Ent>>(StringComparer.Ordinal);
        foreach (var e in la)
        {
            if (!qa.TryGetValue(Key(e), out var q)) qa[Key(e)] = q = new();
            q.Enqueue(e);
        }
        var alias = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var e in lb)
            if (qa.TryGetValue(Key(e), out var q) && q.Count > 0) alias[e.Path] = q.Dequeue().Path;
        if (alias.Count > 0) B.ApplyAlias(alias);
        return alias.Count;
    }

    public int Aligned { get; private set; }

    public void Run()
    {
        Aligned = AlignTop();
        var onlyA = A.Ents.Keys.Where(k => !B.Ents.ContainsKey(k)).ToHashSet(StringComparer.Ordinal);
        var onlyB = B.Ents.Keys.Where(k => !A.Ents.ContainsKey(k)).ToHashSet(StringComparer.Ordinal);
        ReportMissing(A, onlyA, "a");
        ReportMissing(B, onlyB, "b");

        foreach (var (p, ea) in A.Ents)
            if (B.Ents.TryGetValue(p, out var eb)) CompareProps(ea, eb);

        CompareLists();

        if (!noCode)
            foreach (var (p, ea) in A.Ents)
                if (ea.Method != null && B.Ents.TryGetValue(p, out var eb) && eb.Method != null)
                    CompareCode(p, ea.Method, eb.Method);
    }

    /// <summary>只在一边的实体：只报告最上层缺失者，下级计数附在描述里</summary>
    private void ReportMissing(Side side, HashSet<string> missing, string which)
    {
        var sub = new Dictionary<string, int>(StringComparer.Ordinal);
        var tops = new List<Ent>();
        foreach (var e in side.Ents.Values)
        {
            if (!missing.Contains(e.Path)) continue;
            string? top = null;
            for (var p = e.Parent; p != null && missing.Contains(p); p = side.Ents.TryGetValue(p, out var pe) ? pe.Parent : null)
                top = p;
            if (top == null) tops.Add(e);
            else sub[top] = sub.GetValueOrDefault(top) + 1;
        }
        foreach (var e in tops)
        {
            var n = sub.GetValueOrDefault(e.Path);
            var desc = $"只在 {which}（{e.Kind}）" + (n > 0 ? $"，含 {n} 个下级实体" : "");
            Add(e.MissingCat ?? "集合:" + e.Kind, e.Path, desc, which == "a" ? "有" : "无", which == "b" ? "有" : "无");
        }
    }

    private void CompareProps(Ent ea, Ent eb)
    {
        var db = new Dictionary<string, (string Val, string Cat)>(StringComparer.Ordinal);
        foreach (var (k, v, c) in eb.Props) db.TryAdd(k, (v, c));
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var (k, v, c) in ea.Props)
        {
            if (!seen.Add(k)) continue;
            if (db.TryGetValue(k, out var x))
            {
                if (!string.Equals(v, x.Val, StringComparison.Ordinal)) Add(c, ea.Path, $"{ea.Kind}.{k}", v, x.Val);
            }
            else Add(c, ea.Path, $"{ea.Kind}.{k}", v, null);
        }
        foreach (var (k, v, c) in eb.Props)
            if (seen.Add(k)) Add(c, ea.Path, $"{ea.Kind}.{k}", null, v);
    }

    private void CompareLists()
    {
        foreach (var (key, la) in A.Lists)
        {
            if (!B.Lists.TryGetValue(key, out var lb)) continue;
            var setA = la.Items.ToHashSet(StringComparer.Ordinal);
            var setB = lb.Items.ToHashSet(StringComparer.Ordinal);
            var seqA = la.Items.Where(setB.Contains).ToList();
            var seqB = lb.Items.Where(setA.Contains).ToList();
            if (seqA.SequenceEqual(seqB, StringComparer.Ordinal)) continue;
            int i = 0;
            while (i < Math.Min(seqA.Count, seqB.Count) && seqA[i] == seqB[i]) i++;
            var strict = la.Strict || lb.Strict;
            Add(strict ? "顺序(严格)" : "顺序", key, $"共有项第 {i + 1} 个起顺序不同（共有 {seqA.Count} 项）",
                i < seqA.Count ? seqA[i] : null, i < seqB.Count ? seqB[i] : null);
        }
    }

    private static (StatementBlock? Block, string? Err) Parse(MethodInfo m)
    {
        var data = m.CodeData.ExpressionData;
        if (data == null || data.Length == 0) return (new StatementBlock(), null);
        try { return (CodeDataParser.ParseStatementBlock(data, m.CodeData.Encoding), null); }
        catch (Exception ex) { return (null, $"<解析失败:{ex.GetType().Name}:{ex.Message} {Cmp.Sha(data)}>"); }
    }

    private static List<string> R(Side s, (StatementBlock? Block, string? Err) p, bool defs, bool misc) =>
        p.Err != null ? new List<string> { p.Err } : CodeRender.Render(s, p.Block!, defs, misc);

    private void CompareCode(string path, MethodInfo ma, MethodInfo mb)
    {
        CodeCompared++;
        var pa = Parse(ma);
        var pb = Parse(mb);
        // 1. 核心（不含末尾默认参数、不含杂项）
        var ca = R(A, pa, false, false);
        var cb = R(B, pb, false, false);
        if (!ca.SequenceEqual(cb, StringComparer.Ordinal))
        {
            bool privOnly = ca.Count == cb.Count && ca.Zip(cb).All(t => t.First == t.Second || Cmp.NormPriv(t.First) == Cmp.NormPriv(t.Second));
            ReportFirst(privOnly ? "私有类型ID" : "代码", path, ca, cb, !privOnly);
            return;
        }
        // 2. 末尾默认参数个数
        var da = R(A, pa, true, false);
        var db = R(B, pb, true, false);
        if (!da.SequenceEqual(db, StringComparer.Ordinal)) ReportFirst("默认参数", path, da, db, false);
        // 3. 杂项（空行残留命令号/标志、调用 ExtraFlags）
        var xa = R(A, pa, false, true);
        var xb = R(B, pb, false, true);
        if (!xa.SequenceEqual(xb, StringComparer.Ordinal)) ReportFirst("杂项", path, xa, xb, false);
    }

    private void ReportFirst(string cat, string path, List<string> la, List<string> lb, bool skipPrivOnlyLines)
    {
        int n = Math.Max(la.Count, lb.Count);
        int first = -1, cnt = 0;
        for (int i = 0; i < n; i++)
        {
            var x = i < la.Count ? la[i] : null;
            var y = i < lb.Count ? lb[i] : null;
            if (x == y) continue;
            if (skipPrivOnlyLines && x != null && y != null && Cmp.NormPriv(x) == Cmp.NormPriv(y)) continue;
            cnt++;
            if (first < 0) first = i;
        }
        if (first < 0) first = 0;
        var a = first < la.Count ? la[first].Trim() : null;
        var b = first < lb.Count ? lb[first].Trim() : null;
        Diffs.Add(new Cmp.Diff(cat, path, $"第 {first + 1} 行起不同（a {la.Count} 行 / b {lb.Count} 行，逐行位置不同 {cnt} 行）", a, b));
    }
}
