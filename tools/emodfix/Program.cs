// EModFix: 易语言 .e 模块完整性检查/修复工具
// 用法:
//   EModFix check <file.e> [--json]
//   EModFix restore <target.e> <donor.e> <out.e>
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Sections;

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
try
{
    Console.OutputEncoding = new UTF8Encoding(false);
    if (args.Length >= 1 && args[0] == "check")
    {
        var rest = args.Skip(1).ToList();
        bool json = rest.Remove("--json");
        if (rest.Count != 1) { Usage(); return 1; }
        return CheckCommand(rest[0], json);
    }
    if (args.Length == 4 && args[0] == "restore")
        return RestoreCommand(args[1], args[2], args[3]);
    Usage();
    return args.Length == 0 ? 0 : 1;
}
catch (Exception ex)
{
    Console.Error.WriteLine("错误: " + ex.GetType().Name + ": " + ex.Message);
    return 1;
}

static void Usage()
{
    Console.WriteLine("用法:");
    Console.WriteLine("  EModFix check <file.e> [--json]          检查模块声明完整性(有缺口退出码2,无缺口0)");
    Console.WriteLine("  EModFix restore <target.e> <donor.e> <out.e>  从供体补回缺失的模块方法");
}

static int CheckCommand(string path, bool json)
{
    var doc = LoadDoc(path);
    var report = Audit.Analyze(doc, path);
    Console.OutputEncoding = new UTF8Encoding(false);
    if (json)
    {
        var opts = new JsonSerializerOptions { WriteIndented = true, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping, DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull };
        Console.WriteLine(JsonSerializer.Serialize(report, opts));
    }
    else
    {
        Console.Write(report.Render());
    }
    return report.HasGap ? 2 : 0;
}

static int RestoreCommand(string targetPath, string donorPath, string outPath)
{
    var target = LoadDoc(targetPath);
    var donor = LoadDoc(donorPath);
    Restore.Apply(target, donor);
    // 写入内存流 -> 重新加载复检(check 语义: 无缺口) -> 成功才落盘
    byte[] bytes;
    using (var ms = new MemoryStream()) { target.Save(ms); bytes = ms.ToArray(); }
    using (var ms2 = new MemoryStream(bytes))
    {
        var reloaded = new EplDocument();
        reloaded.Load(ms2);
        var recheck = Audit.Analyze(reloaded, "(output recheck)");
        if (recheck.HasGap)
        {
            Console.Error.WriteLine("复检失败: 输出仍存在声明缺口, 不写文件");
            Console.Error.Write(recheck.Render());
            return 1;
        }
    }
    File.WriteAllBytes(outPath, bytes);
    Console.WriteLine($"已写入 {outPath} ({bytes.Length:N0} B)");
    return 0;
}

static EplDocument LoadDoc(string path)
{
    var doc = new EplDocument();
    using var fs = File.OpenRead(path);
    doc.Load(fs);
    return doc;
}

// ---------- 核心模型 ----------
static class Audit
{
    public static Report Analyze(EplDocument doc, string path)
    {
        var code = doc.Get(CodeSection.Key);
        var res = doc.GetOrNull(ResourceSection.Key);
        var deps = doc.GetOrNull(ECDependenciesSection.Key);
        var pkg = doc.GetOrNull(EPackageInfoSection.Key);
        var rep = new Report { Path = path, MethodTotal = code.Methods.Count, AllocatedIdNum = code.AllocatedIdNum };
        var entities = new List<Entity>();
        foreach (var m in code.Methods) entities.Add(new Entity(m.Id, "Method", m.Name));
        foreach (var c in code.Classes) entities.Add(new Entity(c.Id, "Class", c.Name) { ClassMethods = c.Methods });
        foreach (var s in code.Structs) entities.Add(new Entity(s.Id, "Struct", s.Name));
        foreach (var x in code.DllDeclares) entities.Add(new Entity(x.Id, "Dll", x.Name));
        foreach (var g in code.GlobalVariables) entities.Add(new Entity(g.Id, "Global", g.Name));
        if (res != null) foreach (var c in res.Constants) entities.Add(new Entity(c.Id, "Constant", c.Name));
        // 名称映射(跨文档不合并, 本工具单文档审计)
        var names = new Dictionary<string, string>();
        foreach (var e in entities) if (!string.IsNullOrWhiteSpace(e.Name)) names.TryAdd(Key(e.Type, e.Num), e.Name);
        var methods = code.Methods.Select((m, i) => new MethodRef(m, i)).ToList();
        var methodById = methods.GroupBy(m => m.Info.Id).ToDictionary(g => g.Key, g => g.First());
        var depList = deps?.ECDependencies ?? new List<ECDependencyInfo>();
        var methodRanges = new List<(int DepIndex, string DepName, int Start, int Count)>();
        foreach (var (dep, di) in depList.Select((d, i) => (d, i)))
            foreach (var r in dep.DefinedIds)
            {
                if (r.Count <= 0) continue;
                var range = new RangeInfo
                {
                    DepIndex = di,
                    DepName = dep.Name,
                    Start = r.Start,
                    StartNum = r.Start & 0x00FFFFFF,
                    Count = r.Count,
                    Type = r.Start & unchecked((int)0xFF000000),
                    TypeName = TypeName(r.Start & unchecked((int)0xFF000000)),
                };
                var presentNums = new HashSet<int>();
                foreach (var e in entities)
                    if (TypeFamilyMatch(e.Type, range.Type) && e.Num >= range.StartNum && e.Num - range.StartNum < range.Count)
                        presentNums.Add(e.Num);
                range.Actual = presentNums.Count;
                for (int k = 0; k < range.Count; k++)
                {
                    int num = range.StartNum + k;
                    if (!presentNums.Contains(num))
                    {
                        var id = num | range.Type;
                        range.MissingIds.Add(new MissingId { Id = id, Num = num, Name = names.GetValueOrDefault(Key(range.Type, num)) });
                    }
                }
                rep.Ranges.Add(range);
                if (range.Type == 0x04000000) methodRanges.Add((di, dep.Name ?? "", r.Start, r.Count));
            }
        // 累计窗口模型(方法)
        bool IsModuleMethod(int id) => methodRanges.Any(x => InRange(id, x.Start, x.Count));
        int cursor = 0;
        foreach (var (di, depName, start, count) in methodRanges)
        {
            var w = new WindowInfo { DepIndex = di, DepName = depName, Start = start, Count = count, CursorBefore = cursor };
            int? exact = methodById.TryGetValue(start, out var first) ? first.Index : null;
            if (exact == null)
            {
                var hit = methods.FirstOrDefault(m => InRange(m.Info.Id, start, count));
                if (hit != null) exact = hit.Index;
            }
            if (exact == null)
            {
                w.Note = "no actual method in declared range; window not placeable";
                rep.Windows.Add(w);
                continue;
            }
            cursor = Math.Max(cursor, exact.Value);
            w.WindowStart = cursor;
            w.WindowEnd = cursor + count - 1;
            for (int i = cursor; i < Math.Min(methods.Count, cursor + count); i++)
            {
                var m = methods[i];
                bool belongs = InRange(m.Info.Id, start, count);
                if (!belongs)
                {
                    w.WronglyIncluded.Add(new SwallowedMethod { Index = i, Id = m.Info.Id, Name = m.Info.Name, Class = m.Info.Class });
                    if (!IsModuleMethod(m.Info.Id))
                        w.SwallowedMain.Add(w.WronglyIncluded.Last());
                }
            }
            w.OutOfBounds = Math.Max(0, cursor + count - methods.Count);
            cursor += count;
            w.CursorAfter = cursor;
            rep.Windows.Add(w);
        }
        rep.HasGap = rep.Ranges.Any(r => r.MissingIds.Count > 0);
        return rep;
    }

    public static bool InRange(int id, int start, int count)
    {
        // 方法窗口比较: 类型必须严格一致(04), 只比较数值部分
        if ((id & unchecked((int)0xFF000000)) != (start & unchecked((int)0xFF000000))) return false;
        uint num = (uint)(id & 0x00FFFFFF);
        uint s = (uint)(start & 0x00FFFFFF);
        return num >= s && num - s < (uint)count;
    }

    public static bool TypeFamilyMatch(int a, int b) =>
        a == b
        || (a is 0x09000000 or 0x49000000 && b is 0x09000000 or 0x49000000)
        || (a is 0x18000000 or 0x28000000 or 0x38000000 && b is 0x18000000 or 0x28000000 or 0x38000000);

    public static string TypeName(int type) => type switch
    {
        0x04000000 => "方法",
        0x09000000 or 0x49000000 => "程序集",
        0x18000000 or 0x28000000 or 0x38000000 => "常量/资源",
        0x41000000 => "结构",
        0x43000000 => "DLL",
        0x00000000 => "全局变量",
        _ => $"0x{type:X8}",
    };

    public static string Key(int type, int num) => $"{type & unchecked((int)0xFF000000):X8}:{num & 0x00FFFFFF:X8}";
}

static class Restore
{
    public static void Apply(EplDocument target, EplDocument donor)
    {
        var tCode = target.Get(CodeSection.Key);
        var dCode = donor.Get(CodeSection.Key);
        var tDeps = target.GetOrNull(ECDependenciesSection.Key) ?? throw new InvalidOperationException("目标缺少易模块记录段");
        var dDeps = donor.GetOrNull(ECDependenciesSection.Key);
        var tPkg = target.GetOrNull(EPackageInfoSection.Key);
        var dPkg = donor.GetOrNull(EPackageInfoSection.Key);
        var tAudit = Audit.Analyze(target, "(target)");
        // 收集缺失的方法(只处理 04 类型)
        var missing = new List<(int DepIndex, string DepName, int Start, int Count, MissingId Miss)>();
        foreach (var r in tAudit.Ranges.Where(r => r.Type == 0x04000000))
            foreach (var m in r.MissingIds)
                missing.Add((r.DepIndex, r.DepName ?? "", r.Start, r.Count, m));
        if (missing.Count == 0) throw new InvalidOperationException("目标没有缺失的模块方法, 无需修复");
        var dMethods = dCode.Methods.GroupBy(m => m.Id).ToDictionary(g => g.Key, g => g.First());
        foreach (var x in missing)
            if (!dMethods.ContainsKey(x.Miss.Id))
                throw new InvalidOperationException($"缺失方法 0x{x.Miss.Id:X8} ({x.Miss.Name ?? "?"}) 在供体中不存在");
        // 按模块区间分组, 保持供体中的相对顺序(Id 数值升序)
        var groups = missing.GroupBy(x => (x.DepIndex, x.Start)).Select(g => new
        {
            g.Key.DepIndex,
            g.Key.Start,
            Items = g.OrderBy(x => x.Miss.Num).ToList(),
        }).ToList();
        // 从后往前插入, 避免下标位移影响后续计算
        foreach (var g in groups.OrderByDescending(x => x.Start))
        {
            var dep = tDeps.ECDependencies[g.DepIndex];
            var rangeStart = g.Start;
            var donorIds = g.Items.Select(x => x.Miss.Id).ToList();
            // 插入位置: 该模块已有方法块末尾(方法表中最后一个 Id 属于该区间的位置+1); 若区间内一个都没有, 用下一个模块块起点或表尾
            int insertAt = -1;
            for (int i = tCode.Methods.Count - 1; i >= 0; i--)
                if (Audit.InRange(tCode.Methods[i].Id, rangeStart, g.Items.Count + CountPresent(tCode, rangeStart)))
                { insertAt = i + 1; break; }
            if (insertAt < 0)
            {
                // 区间内无既有方法: 找方法表中第一个 Id 数值大于 rangeStart 的模块方法位置, 否则表尾
                insertAt = tCode.Methods.Count;
                for (int i = 0; i < tCode.Methods.Count; i++)
                {
                    var id = tCode.Methods[i].Id;
                    if ((id & 0x00FFFFFF) > (rangeStart & 0x00FFFFFF) && (id & unchecked((int)0xFF000000)) == 0x04000000)
                    { insertAt = i; break; }
                }
            }
            var newMethods = donorIds.Select(id => dMethods[id]).ToList();
            tCode.Methods.InsertRange(insertAt, newMethods);
            // ClassInfo.Methods: 恢复所属程序集的方法列表
            foreach (var m in newMethods)
            {
                var cls = tCode.Classes.FirstOrDefault(c => c.Id == m.Class);
                if (cls == null) throw new InvalidOperationException($"目标缺少方法 0x{m.Id:X8} 所属程序集 0x{m.Class:X8}");
                if (cls.Methods == null) cls.Methods = new List<int>();
                if (!cls.Methods.Contains(m.Id)) cls.Methods.Add(m.Id);
                // 程序集方法列表按 Id 数值排序, 与 IDE 写出一致
                cls.Methods.Sort((a, b) => (a & 0x00FFFFFF).CompareTo(b & 0x00FFFFFF));
            }
            // 0x0D 易包信息段1 FileNames: 同一下标插 null
            if (tPkg != null)
            {
                var names = tPkg.FileNames.ToList();
                if (names.Count != tCode.Methods.Count - newMethods.Count)
                    throw new InvalidOperationException($"易包信息段1 FileNames 数量({names.Count})与方法表({tCode.Methods.Count - newMethods.Count})不一致");
                for (int k = 0; k < newMethods.Count; k++) names.Insert(insertAt + k, null);
                tPkg.FileNames = names.ToArray();
            }
            Console.WriteLine($"模块[{dep.Name}] 区间 0x{rangeStart:X8}: 在方法表下标 {insertAt} 插入 {newMethods.Count} 个方法");
            foreach (var m in newMethods)
                Console.WriteLine($"  + 0x{m.Id:X8} {m.Name} (程序集 0x{m.Class:X8})");
        }
        // AllocatedIdNum 不改; 其它段按结论不按方法下标对齐
    }

    static int CountPresent(CodeSection code, int start)
    {
        int count = 0;
        foreach (var m in code.Methods) if (Audit.InRange(m.Id, start, int.MaxValue / 2)) count++;
        return count;
    }
}

// ---------- 数据模型 ----------
public sealed class Report
{
    public string Path { get; set; } = "";
    public int AllocatedIdNum { get; set; }
    public int MethodTotal { get; set; }
    public bool HasGap { get; set; }
    public List<RangeInfo> Ranges { get; set; } = new();
    public List<WindowInfo> Windows { get; set; } = new();

    public string Render()
    {
        var sb = new StringBuilder();
        sb.AppendLine($"文件: {Path}");
        sb.AppendLine($"方法总数: {MethodTotal}, AllocatedIdNum=0x{AllocatedIdNum:X8}");
        sb.AppendLine(HasGap ? "结论: 存在声明缺口" : "结论: 无缺口");
        foreach (var g in Ranges.GroupBy(r => (r.DepIndex, r.DepName)))
        {
            sb.AppendLine($"模块[{g.Key.DepIndex}] {g.Key.DepName}:");
            foreach (var r in g.OrderBy(x => x.Start))
            {
                sb.AppendLine($"  {r.TypeName} 0x{r.Start:X8}+{r.Count}: 声明 {r.Count} / 实际 {r.Actual}" + (r.MissingIds.Count > 0 ? $"  缺 {r.MissingIds.Count}" : "  ✓"));
                foreach (var m in r.MissingIds)
                    sb.AppendLine($"    缺失 0x{m.Id:X8} {m.Name ?? "?"}");
            }
        }
        var swallowed = Windows.SelectMany(w => w.SwallowedMain.Select(s => (w, s))).ToList();
        if (swallowed.Count > 0)
        {
            sb.AppendLine("被模块窗口错误吞入的主工程方法(累计窗口模型):");
            foreach (var (w, s) in swallowed)
                sb.AppendLine($"  下标 {s.Index} 0x{s.Id:X8} {s.Name} (被模块[{w.DepIndex}] {w.DepName} 的窗口 {w.WindowStart}..{w.WindowEnd} 吞入)");
        }
        return sb.ToString();
    }
}

public sealed class RangeInfo
{
    public int DepIndex { get; set; }
    public string? DepName { get; set; }
    public int Start { get; set; }
    public int StartNum { get; set; }
    public int Count { get; set; }
    public int Type { get; set; }
    public string TypeName { get; set; } = "";
    public int Actual { get; set; }
    public List<MissingId> MissingIds { get; set; } = new();
}

public sealed class MissingId
{
    public int Id { get; set; }
    public int Num { get; set; }
    public string? Name { get; set; }
}

public sealed class WindowInfo
{
    public int DepIndex { get; set; }
    public string DepName { get; set; } = "";
    public int Start { get; set; }
    public int Count { get; set; }
    public int CursorBefore { get; set; }
    public int? WindowStart { get; set; }
    public int? WindowEnd { get; set; }
    public int CursorAfter { get; set; }
    public int OutOfBounds { get; set; }
    public string? Note { get; set; }
    public List<SwallowedMethod> WronglyIncluded { get; set; } = new();
    public List<SwallowedMethod> SwallowedMain { get; set; } = new();
}

public sealed class SwallowedMethod
{
    public int Index { get; set; }
    public int Id { get; set; }
    public string? Name { get; set; }
    public int Class { get; set; }
}

public sealed class Entity
{
    public Entity(int id, string kind, string? name)
    {
        Id = id;
        Type = id & unchecked((int)0xFF000000);
        Num = id & 0x00FFFFFF;
        Kind = kind;
        Name = name;
    }
    public int Id { get; }
    public int Type { get; }
    public int Num { get; }
    public string Kind { get; }
    public string? Name { get; }
    public List<int>? ClassMethods { get; set; }
}

public sealed class MethodRef
{
    public MethodRef(MethodInfo info, int index) { Info = info; Index = index; }
    public MethodInfo Info { get; }
    public int Index { get; }
}
