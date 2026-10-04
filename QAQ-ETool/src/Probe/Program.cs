// QAQ-ETool 探针：验证 EProjectFile 在夹具上的读写保真度
//   1. 段级往返：每个段 Parse -> ToBytes，与原始段数据逐字节比对
//   2. 整文件往返：EplDocument.Load -> Save，与原文件逐字节比对
//   3. 代码级往返：每个子程序 ExpressionData -> 语句树 -> ToCodeData，六个数组逐字节比对
//   4. 文本渲染：把指定程序集渲染成文本，检查窗口自身属性引用
using System.Text;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Context;
using QIQI.EProjectFile.EditorTabInfo;
using QIQI.EProjectFile.Sections;

Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
Console.OutputEncoding = Encoding.UTF8;

if (args.Length >= 1 && args[0] == "--cmp")
    return QAQ.Probe.Cmp.Run(args.Skip(1).ToArray()); // 两个 .e 的语义结构比对（见 Cmp.cs）

if (args.Length == 3 && args[0] == "--secbytes")
{
    // 逐段比较两份 .e 的段字节（定位逐字节不一致出在哪个段）
    EplDocument L(string p) { var d = new EplDocument(); using var fs = File.OpenRead(p); d.Load(fs); return d; }
    var da = L(args[1]); var db = L(args[2]);
    var enc = da.DetermineEncoding();
    var ctx = new QIQI.EProjectFile.Context.BlockByteifierContext(enc, false);
    int n = Math.Max(da.Sections.Count, db.Sections.Count), bad = 0;
    for (int i = 0; i < n; i++)
    {
        var sa = i < da.Sections.Count ? da.Sections[i] : null;
        var sb = i < db.Sections.Count ? db.Sections[i] : null;
        var ba = sa?.ToBytes(ctx) ?? Array.Empty<byte>();
        var bb = sb?.ToBytes(ctx) ?? Array.Empty<byte>();
        int first = -1;
        for (int k = 0; k < Math.Min(ba.Length, bb.Length); k++) if (ba[k] != bb[k]) { first = k; break; }
        if (first < 0 && ba.Length != bb.Length) first = Math.Min(ba.Length, bb.Length);
        bool opA = sa != null && (da.OptionalOverrides.TryGetValue(sa, out var oa) ? oa : sa.IsOptional);
        bool opB = sb != null && (db.OptionalOverrides.TryGetValue(sb, out var ob) ? ob : sb.IsOptional);
        var st = first < 0 && sa?.SectionKey == sb?.SectionKey && opA == opB ? "same" : "DIFF";
        if (st == "DIFF") bad++;
        Console.WriteLine($"{st} #{i} a=0x{sa?.SectionKey:X8}({ba.Length},opt={opA}) b=0x{sb?.SectionKey:X8}({bb.Length},opt={opB}) first@{first}");
        if (first >= 0)
        {
            string H(byte[] x) => BitConverter.ToString(x, Math.Max(0, first - 8), Math.Min(48, Math.Max(0, x.Length - Math.Max(0, first - 8))));
            Console.WriteLine("   a: " + H(ba));
            Console.WriteLine("   b: " + H(bb));
        }
    }
    return bad == 0 ? 0 : 1;
}

if (args.Length == 1 && args[0] == "--reflect")
{
    var asm = typeof(OpenEpl.ELibInfo.ELibManifest).Assembly;
    foreach (var t in asm.GetExportedTypes().OrderBy(x => x.FullName))
    {
        Console.WriteLine(t.FullName + (t.IsEnum ? " [enum: " + string.Join(",", Enum.GetNames(t)) + "]" : ""));
        foreach (var pr in t.GetProperties(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.DeclaredOnly))
            Console.WriteLine($"   P {pr.PropertyType.Name} {pr.Name}");
        foreach (var m in t.GetMethods(System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.DeclaredOnly).Where(m => !m.IsSpecialName))
            Console.WriteLine($"   M {m.ReturnType.Name} {m.Name}({string.Join(", ", m.GetParameters().Select(a => a.ParameterType.Name + " " + a.Name))})");
        foreach (var c in t.GetConstructors())
            Console.WriteLine($"   C ({string.Join(", ", c.GetParameters().Select(a => a.ParameterType.Name + " " + a.Name))})");
    }
    return 0;
}
if (args.Length == 2 && args[0] == "--deps")
{
    // 打印易模块记录与各段概况（用于对比 e2txt 的 t2e 产物）
    var d0 = new EplDocument();
    using (var fs0 = File.OpenRead(args[1])) d0.Load(fs0);
    foreach (var s in d0.Sections) Console.WriteLine($"SEC 0x{s.SectionKey:X8} {s.SectionName}");
    var c0 = d0.Get(CodeSection.Key);
    Console.WriteLine($"AllocatedIdNum=0x{c0.AllocatedIdNum:X8} classes={c0.Classes.Count} methods={c0.Methods.Count} structs={c0.Structs.Count} dlls={c0.DllDeclares.Count} globals={c0.GlobalVariables.Count}");
    var deps = d0.GetOrNull(ECDependenciesSection.Key);
    if (deps != null)
        foreach (var x in deps.ECDependencies)
            Console.WriteLine($"DEP v{x.InfoVersion} size={x.FileSize} time={x.FileLastModifiedDate:O} re={x.ReExport} {x.Name} | {x.Path} | {string.Join(" ", x.DefinedIds.Select(r => $"{r.Start:X8}+{r.Count}"))}");
    foreach (var m in c0.Methods.Take(3000).Where(m => m.Name is "Len" or "文本_取出中间文本" or "彗星置窗口阴影" or "_启动子程序"))
        Console.WriteLine($"M 0x{m.Id:X8} cls=0x{m.Class:X8} flags=0x{m.Flags:X} {m.Name}");
    foreach (var c in c0.Classes.Take(400)) Console.WriteLine($"C 0x{c.Id:X8} base=0x{c.BaseClass:X8} n={c.Methods.Count} {c.Name}");
    return 0;
}
if (args.Length == 2 && args[0] == "--events")
{
    // 事件索引段（辅助信息段1）：窗口 / 单元 / 事件号 / 子程序
    var d0 = new EplDocument();
    using (var fs0 = File.OpenRead(args[1])) d0.Load(fs0);
    var c0 = d0.Get(CodeSection.Key);
    var names = c0.Methods.ToDictionary(m => m.Id, m => m.Name);
    var res0 = d0.Get(ResourceSection.Key);
    var elems = res0.Forms.SelectMany(f => f.Elements).ToDictionary(e => e.Id, e => e);
    foreach (var x in d0.GetOrNull(EventIndicesSection.Key)?.Indices ?? new())
        Console.WriteLine($"EV form=0x{x.FormId:X8} unit=0x{x.UnitId:X8}({(elems.TryGetValue(x.UnitId, out var el) ? el.Name + "/" + el.DataType.ToString("X8") : "?")}) ev={x.EventId} m=0x{x.MethodId:X8} {names.GetValueOrDefault(x.MethodId)}");
    foreach (var f in res0.Forms)
        foreach (var e in f.Elements.OfType<FormControlInfo>())
            if (e.Events is { Length: > 0 })
                Console.WriteLine($"CTRLEV {f.Name}.{e.Name}: {string.Join(",", e.Events.Select(kv => $"{kv.Key}->{names.GetValueOrDefault(kv.Value)}"))}");
    return 0;
}
if (args.Length == 2 && args[0] == "--flags")
{
    // 各类定义的标志位分布：主工程 vs 模块（t2e 需要按此还原无文本对应的字段）
    var d0 = new EplDocument();
    using (var fs0 = File.OpenRead(args[1])) d0.Load(fs0);
    var c0 = d0.Get(CodeSection.Key);
    var deps = d0.GetOrNull(ECDependenciesSection.Key)?.ECDependencies ?? new();
    string Where(int id) { for (int i = 0; i < deps.Count; i++) foreach (var r in deps[i].DefinedIds) if (unchecked((uint)id - (uint)r.Start) < (uint)r.Count) return "M"; return "P"; }
    var mcls = c0.Methods.ToDictionary(m => m.Id, m => m.Class);
    var hist = new SortedDictionary<string, int>(StringComparer.Ordinal);
    void H(string k) => hist[k] = hist.TryGetValue(k, out var v) ? v + 1 : 1;
    foreach (var c in c0.Classes)
    {
        var w = c.Methods.Count > 0 ? Where(c.Methods[0]) : Where(c.Id);
        H($"class {w} type=0x{EplSystemId.GetType(c.Id):X8} form={(c.Form == 0 ? "0" : c.Form.ToString("X"))} base=0x{c.BaseClass:X8} mem={(c.MemoryAddress == 0 ? 0 : 1)}");
        foreach (var v in c.Variables) H($"classvar {w} flags=0x{v.Flags:X} tb={v.TrailingBytes?.Length ?? 0}");
    }
    foreach (var m in c0.Methods)
    {
        var w = Where(m.Id);
        H($"method {w} flags=0x{m.Flags:X} clsType=0x{EplSystemId.GetType(m.Class):X8} mem={(m.MemoryAddress == 0 ? 0 : 1)} code={(m.CodeData.ExpressionData?.Length ?? 0) > 0}");
        foreach (var v in m.Parameters) H($"param {w} flags=0x{v.Flags:X}");
        foreach (var v in m.Variables) H($"local {w} flags=0x{v.Flags:X}");
    }
    foreach (var s in c0.Structs) { var w = Where(s.Id); H($"struct {w} flags=0x{s.Flags:X} mem={(s.MemoryAddress == 0 ? 0 : 1)}"); foreach (var v in s.Members) H($"member {w} flags=0x{v.Flags:X}"); }
    foreach (var s in c0.DllDeclares) { var w = Where(s.Id); H($"dll {w} flags=0x{s.Flags:X} mem={(s.MemoryAddress == 0 ? 0 : 1)}"); foreach (var v in s.Parameters) H($"dllparam {w} flags=0x{v.Flags:X}"); }
    foreach (var g in c0.GlobalVariables) H($"global {Where(g.Id)} flags=0x{g.Flags:X}");
    var res = d0.GetOrNull(ResourceSection.Key);
    foreach (var k in res.Constants) H($"const {Where(k.Id)} kind={(uint)k.Id >> 28} flags=0x{k.Flags:X}");
    foreach (var f in res.Forms) { H($"form mem={(f.MemoryAddress == 0 ? 0 : 1)}"); foreach (var e in f.Elements) H($"elem {e.GetType().Name} idType=0x{EplSystemId.GetType(e.Id):X8}"); }
    var pub = d0.GetOrNull(ClassPublicitySection.Key);
    if (pub != null) foreach (var x in pub.ClassPublicities) H($"classpub {Where(x.Class)} flags=0x{x.Flags:X}");
    var ev = d0.GetOrNull(EventIndicesSection.Key);
    Console.WriteLine($"events={ev?.Indices.Count} epack={d0.GetOrNull(EPackageInfoSection.Key)?.FileNames.Length} methods={c0.Methods.Count} code.Flag={c0.Flag} main=0x{c0.MainMethod:X8} dbg='{c0.DebugCommandParameters}' ubi={c0.UnknownBeforeIconData.IsDefault}");
    var los = d0.GetOrNull(LosableSection.Key);
    Console.WriteLine($"losable: {los}");
    foreach (var kv in hist) Console.WriteLine($"{kv.Value,7} {kv.Key}");
    return 0;
}
if (args.Length == 2 && args[0] == "--tabs")
{
    // 编辑信息段2（打开的标签页）：逐个打印类型 / 类+子程序（或表格、窗口）/ 光标（组包还原验收用）
    var d0 = new EplDocument();
    using (var fs0 = File.OpenRead(args[1])) d0.Load(fs0);
    var code0 = d0.GetOrNull(CodeSection.Key);
    var classes = code0?.Classes.ToDictionary(c => c.Id, c => c) ?? new Dictionary<int, ClassInfo>();
    var methods = code0?.Methods.ToDictionary(m => m.Id, m => m) ?? new Dictionary<int, MethodInfo>();
    var forms = d0.GetOrNull(ResourceSection.Key)?.Forms.ToDictionary(f => f.Id, f => f) ?? new Dictionary<int, FormInfo>();
    var sec = d0.GetOrNull(EditorInfoSection.Key);
    if (sec == null) { Console.WriteLine("（无编辑信息段2）"); return 0; }
    int i = 0;
    foreach (var t in sec.Tabs)
    {
        int n = i++;
        switch (t)
        {
            case null:
                Console.WriteLine($"TAB {n}: (空占位)");
                break;
            case ClassEditorTabInfo c:
            {
                classes.TryGetValue(c.ClassId, out var cls);
                string elem = c.ElemId < 0 ? $"表格{-c.ElemId}"
                    : cls != null && c.ElemId < cls.Methods.Count && methods.TryGetValue(cls.Methods[c.ElemId], out var m) ? m.Name
                    : $"子程序?{c.ElemId}";
                Console.WriteLine($"TAB {n}: code [{cls?.Name ?? $"0x{c.ClassId:X8}"}].{elem} offset={c.Offset} col={c.ColumnInTable} sel=({c.SelectionStart},{c.SelectionCurrent}) ep={c.SelectionEndpoints?.Count ?? 0}");
                break;
            }
            case FormDesignerTabInfo f:
                Console.WriteLine($"TAB {n}: form {(forms.TryGetValue(f.FormId, out var fm) ? fm.Name : $"0x{f.FormId:X8}")} units=[{string.Join(",", (f.UnitIds ?? new List<int>()).Select(x => $"0x{x:X8}"))}]");
                break;
            case PureTableEditorTabInfo p:
                Console.WriteLine($"TAB {n}: table type={p.TypeId} offset={p.Offset} col={p.ColumnInTable} sel=({p.SelectionStart},{p.SelectionCurrent}) ep={p.SelectionEndpoints?.Count ?? 0}");
                break;
            case GeneralEditorTabInfo g:
                Console.WriteLine($"TAB {n}: raw type={g.TypeId} len={g.Data?.Length ?? 0}");
                break;
            default:
                Console.WriteLine($"TAB {n}: {t.GetType().Name}");
                break;
        }
    }
    return 0;
}
if (args.Length < 1)
{
    Console.WriteLine("用法: Probe <file.e> [要渲染的程序集名子串...]");
    return 2;
}
var path = args[0];
var original = File.ReadAllBytes(path);
Console.WriteLine($"=== {Path.GetFileName(path)}  {original.Length:N0} B ===");

// ---------- 1. 段级往返 ----------
Console.WriteLine("\n[1] 段级往返");
var gbk = Encoding.GetEncoding("gbk");
var encoding = gbk;
int sectionFail = 0;
using (var fs = File.OpenRead(path))
using (var reader = new ProjectFileReader(fs))
{
    while (!reader.IsFinish)
    {
        var raw = reader.ReadSection();
        string status;
        if (PredefinedSections.Keys.TryGetValue(raw.Key, out var key))
        {
            try
            {
                var sec = key.Parse(new BlockParserContext(raw.Data, encoding, reader.CryptEC));
                if (sec is ESystemInfoSection si) encoding = si.DetermineEncoding();
                var back = sec.ToBytes(new BlockByteifierContext(encoding, reader.CryptEC));
                if (back.AsSpan().SequenceEqual(raw.Data)) status = "OK";
                else
                {
                    sectionFail++;
                    var dumpDir = Environment.GetEnvironmentVariable("PROBE_DUMP");
                    if (!string.IsNullOrEmpty(dumpDir))
                    {
                        Directory.CreateDirectory(dumpDir);
                        File.WriteAllBytes(Path.Combine(dumpDir, $"{raw.Key:X8}.orig.bin"), raw.Data);
                        File.WriteAllBytes(Path.Combine(dumpDir, $"{raw.Key:X8}.back.bin"), back);
                    }
                    int i = 0;
                    while (i < Math.Min(back.Length, raw.Data.Length) && back[i] == raw.Data[i]) i++;
                    status = $"DIFF 原{raw.Data.Length} 回{back.Length} 首差@{i}";
                }
            }
            catch (Exception ex) { sectionFail++; status = "EXC " + ex.GetType().Name + ": " + ex.Message; }
        }
        else status = "(未知段，原样保留)";
        Console.WriteLine($"  0x{raw.Key:X8} {raw.Name,-12} {raw.Data.Length,10:N0} B  {status}");
    }
}

// ---------- 2. 整文件往返 ----------
Console.WriteLine("\n[2] 整文件往返");
var doc = new EplDocument();
using (var fs = File.OpenRead(path)) doc.Load(fs);
byte[] saved;
using (var ms = new MemoryStream()) { doc.Save(ms); saved = ms.ToArray(); }
if (saved.AsSpan().SequenceEqual(original)) Console.WriteLine("  逐字节一致 OK");
else
{
    Console.WriteLine($"  DIFF 原{original.Length} 回{saved.Length}");
    // 按段头定位差异：段头 = magic(4) chk(4) key(4) name(30) pad(2) index(4) opt(4) dchk(4) len(4) rsv(40)
    int off = 8, shown = 0;
    while (off + 100 <= original.Length && shown < 20)
    {
        int key = BitConverter.ToInt32(original, off + 8);
        int len = BitConverter.ToInt32(original, off + 56);
        int end = off + 100 + len;
        for (int j = off; j < Math.Min(end, Math.Min(saved.Length, original.Length)) && shown < 20; j++)
        {
            if (saved[j] == original[j]) continue;
            int rel = j - off;
            string field = rel switch
            {
                < 4 => "magic", < 8 => "头校验", < 12 => "key", < 42 => "名称", < 44 => "填充",
                < 48 => "序号", < 52 => "可选", < 56 => "数据校验", < 60 => "长度", < 100 => "保留",
                _ => $"数据+{rel - 100}"
            };
            Console.WriteLine($"    @0x{j:X6} 段0x{key:X8} {field,-8} 原{original[j]:X2} 回{saved[j]:X2}");
            shown++;
        }
        if (key == 0x07007319) break;
        off = end;
    }
}

// ---------- 3. 代码级往返 ----------
Console.WriteLine("\n[3] 代码级往返（语句树 -> 字节）");
var code = doc.Get(CodeSection.Key);
int total = 0, ok = 0, exc = 0, permCount = 0;
var fails = new List<string>();
foreach (var m in code.Methods)
{
    total++;
    try
    {
        var block = CodeDataParser.ParseStatementBlock(m.CodeData.ExpressionData, m.CodeData.Encoding);
        var cd = block.ToCodeData(m.CodeData.Encoding);
        bool same =
            cd.LineOffest.AsSpan().SequenceEqual(m.CodeData.LineOffest) &&
            cd.BlockOffest.AsSpan().SequenceEqual(m.CodeData.BlockOffest) &&
            cd.MethodReference.AsSpan().SequenceEqual(m.CodeData.MethodReference) &&
            cd.VariableReference.AsSpan().SequenceEqual(m.CodeData.VariableReference) &&
            cd.ConstantReference.AsSpan().SequenceEqual(m.CodeData.ConstantReference) &&
            cd.ExpressionData.AsSpan().SequenceEqual(m.CodeData.ExpressionData);
        static int[] Sorted(byte[] b) => Enumerable.Range(0, b.Length / 4).Select(k => BitConverter.ToInt32(b, k * 4)).OrderBy(v => v).ToArray();
        bool SameSet(byte[] a, byte[] b) => a.Length == b.Length && Sorted(a).SequenceEqual(Sorted(b));
        static string[] Recs(byte[] b, int n) => Enumerable.Range(0, b.Length / n).Select(k => Convert.ToHexString(b, k * n, n)).OrderBy(s => s).ToArray();
        bool SameRecs(byte[] a, byte[] b, int n) => a.Length == b.Length && Recs(a, n).SequenceEqual(Recs(b, n));
        bool permOnly = !same &&
            cd.LineOffest.AsSpan().SequenceEqual(m.CodeData.LineOffest) &&
            cd.ExpressionData.AsSpan().SequenceEqual(m.CodeData.ExpressionData) &&
            SameRecs(cd.BlockOffest, m.CodeData.BlockOffest, 9) &&
            SameSet(cd.MethodReference, m.CodeData.MethodReference) &&
            SameSet(cd.VariableReference, m.CodeData.VariableReference) &&
            SameSet(cd.ConstantReference, m.CodeData.ConstantReference);
        if (same) ok++;
        else if (permOnly) permCount++;
        else if (fails.Count < 80)
        {
            var parts = new List<string>();
            if (!cd.LineOffest.AsSpan().SequenceEqual(m.CodeData.LineOffest)) parts.Add("Line");
            if (!cd.BlockOffest.AsSpan().SequenceEqual(m.CodeData.BlockOffest)) parts.Add("Block");
            if (!cd.MethodReference.AsSpan().SequenceEqual(m.CodeData.MethodReference)) parts.Add("MethodRef");
            if (!cd.VariableReference.AsSpan().SequenceEqual(m.CodeData.VariableReference)) parts.Add("VarRef");
            if (!cd.ConstantReference.AsSpan().SequenceEqual(m.CodeData.ConstantReference)) parts.Add("ConstRef");
            if (!cd.ExpressionData.AsSpan().SequenceEqual(m.CodeData.ExpressionData)) parts.Add("Expr");
            fails.Add($"    {m.Name} (0x{m.Id:X8}): {string.Join(",", parts)}");
            if (false)
            {
                static string Ints(byte[] b) => string.Join(" ", Enumerable.Range(0, b.Length / 4).Select(k => BitConverter.ToInt32(b, k * 4).ToString()));
                void Show(string n, byte[] o, byte[] r)
                {
                    if (o.AsSpan().SequenceEqual(r)) return;
                    fails.Add($"        {n} 原[{o.Length / 4}]: {Ints(o)}");
                    fails.Add($"        {n} 回[{r.Length / 4}]: {Ints(r)}");
                }
                Show("MethodRef", m.CodeData.MethodReference, cd.MethodReference);
                Show("VarRef", m.CodeData.VariableReference, cd.VariableReference);
                Show("ConstRef", m.CodeData.ConstantReference, cd.ConstantReference);
            }
            if (!cd.ExpressionData.AsSpan().SequenceEqual(m.CodeData.ExpressionData))
            {
                var o = m.CodeData.ExpressionData; var r = cd.ExpressionData;
                int k = 0; while (k < Math.Min(o.Length, r.Length) && o[k] == r[k]) k++;
                int s = Math.Max(0, k - 24);
                fails.Add($"        Expr 原{o.Length} 回{r.Length} 首差@{k}");
                fails.Add($"        原: {Convert.ToHexString(o, s, Math.Min(56, o.Length - s))}");
                fails.Add($"        回: {Convert.ToHexString(r, s, Math.Min(56, r.Length - s))}");
            }
            else if (!SameSet(cd.MethodReference, m.CodeData.MethodReference) || !SameSet(cd.VariableReference, m.CodeData.VariableReference))
            {
                static string Ints2(byte[] b) => string.Join(" ", Enumerable.Range(0, b.Length / 4).Select(k => BitConverter.ToInt32(b, k * 4)).OrderBy(v => v));
                fails.Add($"        MethodRef 原(排序): {Ints2(m.CodeData.MethodReference)}");
                fails.Add($"        MethodRef 回(排序): {Ints2(cd.MethodReference)}");
                fails.Add($"        VarRef 原(排序): {Ints2(m.CodeData.VariableReference)}");
                fails.Add($"        VarRef 回(排序): {Ints2(cd.VariableReference)}");
            }
        }
    }
    catch (Exception ex)
    {
        exc++;
        if (fails.Count < 15) fails.Add($"    {m.Name} (0x{m.Id:X8}): EXC {ex.Message}");
    }
}
Console.WriteLine($"  子程序 {total}，逐字节一致 {ok}，仅引用表顺序不同 {permCount}，真实差异 {total - ok - exc - permCount}，异常 {exc}");
fails.ForEach(Console.WriteLine);

// ---------- 3c. 语法树转储 ----------
var dumpMethods = Environment.GetEnvironmentVariable("PROBE_TREE");
if (!string.IsNullOrEmpty(dumpMethods))
{
    var want = dumpMethods.Split(',');
    string E(QIQI.EProjectFile.Expressions.Expression? e) => e switch
    {
        null => "null",
        QIQI.EProjectFile.Expressions.CallExpression c =>
            $"Call(lib={c.LibraryId},m={(c.LibraryId < 0 ? $"0x{c.MethodId:X8}" : c.MethodId.ToString())},sp={c.InvokeSpecial},xf={c.ExtraFlags},tgt={E(c.Target)},[{string.Join("; ", (c.ParamList ?? new QIQI.EProjectFile.Expressions.ParamListExpression()).Select(E))}])",
        QIQI.EProjectFile.Expressions.AccessMemberExpression a => $"Mem(lib={a.LibraryId},st=0x{a.StructId:X8},m=0x{a.MemberId:X8},{E(a.Target)})",
        QIQI.EProjectFile.Expressions.AccessArrayExpression a => $"Idx({E(a.Target)}[{E(a.Index)}])",
        QIQI.EProjectFile.Expressions.VariableExpression v => $"Var(0x{v.Id:X8})",
        QIQI.EProjectFile.Expressions.ConstantExpression k => $"Const(lib={k.LibraryId},0x{k.ConstantId:X8})",
        QIQI.EProjectFile.Expressions.EmnuConstantExpression k => $"Enum(lib={k.LibraryId},st={k.StructId},m={k.MemberId})",
        QIQI.EProjectFile.Expressions.MethodPtrExpression p => $"Ptr(0x{p.MethodId:X8})",
        QIQI.EProjectFile.Expressions.StringLiteral s => $"Str({s.Value})",
        QIQI.EProjectFile.Expressions.NumberLiteral n => $"Num({n.Value})",
        QIQI.EProjectFile.Expressions.BoolLiteral b => $"Bool({b.Value})",
        QIQI.EProjectFile.Expressions.ArrayLiteralExpression al => $"Arr{{{string.Join("; ", al.Select(E))}}}",
        QIQI.EProjectFile.Expressions.DefaultValueExpression => "Def",
        _ => e.GetType().Name,
    };
    void S(QIQI.EProjectFile.Statements.StatementBlock b, int d)
    {
        foreach (var s in b)
        {
            var ind = new string(' ', d * 2);
            switch (s)
            {
                case QIQI.EProjectFile.Statements.ExpressionStatement es:
                    Console.WriteLine($"{ind}Stmt mask={es.Mask} cmt={es.Comment ?? "-"} {E(es.Expression)}"); break;
                case QIQI.EProjectFile.Statements.IfStatement x:
                    Console.WriteLine($"{ind}IfTrue mask={x.Mask} {E(x.Condition)}"); S(x.Block, d + 1); break;
                case QIQI.EProjectFile.Statements.SwitchStatement x:
                    foreach (var c in x.Case) { Console.WriteLine($"{ind}Case {E(c.Condition)}"); S(c.Block, d + 1); }
                    Console.WriteLine($"{ind}Default"); S(x.DefaultBlock, d + 1); break;
                case QIQI.EProjectFile.Statements.CounterStatement x:
                    Console.WriteLine($"{ind}Counter mS={x.MaskOnStart} mE={x.MaskOnEnd} {E(x.Count)} {E(x.Var)}"); S(x.Block, d + 1); break;
                case QIQI.EProjectFile.Statements.IfElseStatement x:
                    Console.WriteLine($"{ind}If mask={x.Mask} {E(x.Condition)}"); S(x.BlockOnTrue, d + 1);
                    Console.WriteLine($"{ind}Else"); S(x.BlockOnFalse, d + 1); break;
                case QIQI.EProjectFile.Statements.WhileStatement x:
                    Console.WriteLine($"{ind}While mS={x.MaskOnStart} mE={x.MaskOnEnd} {E(x.Condition)}"); S(x.Block, d + 1); break;
                case QIQI.EProjectFile.Statements.DoWhileStatement x:
                    Console.WriteLine($"{ind}DoWhile mS={x.MaskOnStart} mE={x.MaskOnEnd} {E(x.Condition)}"); S(x.Block, d + 1); break;
                case QIQI.EProjectFile.Statements.LoopStatement x:
                    Console.WriteLine($"{ind}{x.GetType().Name} mS={x.MaskOnStart} mE={x.MaskOnEnd}"); S(x.Block, d + 1); break;
                default: Console.WriteLine($"{ind}{s.GetType().Name}"); break;
            }
        }
    }
    foreach (var m in code.Methods.Where(m => want.Contains(m.Name)))
    {
        Console.WriteLine($"\n#### {m.Name} (0x{m.Id:X8})");
        S(CodeDataParser.ParseStatementBlock(m.CodeData.ExpressionData, m.CodeData.Encoding), 1);
    }
}

// ---------- 3a. 语句类别统计 ----------
if (Environment.GetEnvironmentVariable("PROBE_STMT") == "1")
{
    var kinds = new Dictionary<string, int>();
    void Count(string k) => kinds[k] = kinds.TryGetValue(k, out var v) ? v + 1 : 1;
    void Walk(QIQI.EProjectFile.Statements.StatementBlock b)
    {
        foreach (var s in b)
        {
            Count(s.GetType().Name);
            switch (s)
            {
                case QIQI.EProjectFile.Statements.ExpressionStatement es:
                    if (es.Mask) Count("Mask:" + (es.Expression == null ? "空" : "代码"));
                    if (es.Expression == null && es.Comment != null) Count("注释行");
                    if (es.Expression == null && es.Comment == null) Count("空行");
                    if (es.Expression != null && es.Comment != null) Count("行尾注释");
                    if (es.EmptyLineMethodId != 0 || es.EmptyLineExtraFlags != 0) Count("空行残留号");
                    break;
                case QIQI.EProjectFile.Statements.UnexaminedStatement us:
                    Count("未验证" + (us.Mask ? "+屏蔽" : ""));
                    break;
                case QIQI.EProjectFile.Statements.IfStatement x: if (x.Mask) Count("Mask:如果真"); Walk(x.Block); break;
                case QIQI.EProjectFile.Statements.IfElseStatement x: if (x.Mask) Count("Mask:如果"); Walk(x.BlockOnTrue); Walk(x.BlockOnFalse); break;
                case QIQI.EProjectFile.Statements.SwitchStatement x: foreach (var c in x.Case) { if (c.Mask) Count("Mask:判断"); Walk(c.Block); } Walk(x.DefaultBlock); break;
                case QIQI.EProjectFile.Statements.LoopStatement x: if (x.MaskOnStart || x.MaskOnEnd) Count("Mask:循环"); Walk(x.Block); break;
            }
        }
    }
    foreach (var m in code.Methods)
    {
        try { Walk(CodeDataParser.ParseStatementBlock(m.CodeData.ExpressionData, m.CodeData.Encoding)); } catch { Count("解析异常"); }
    }
    Console.WriteLine("\n[3a] 语句类别");
    foreach (var kv in kinds.OrderByDescending(x => x.Value)) Console.WriteLine($"  {kv.Value,8}  {kv.Key}");
}

// ---------- 3b. 结构转储 ----------
if (Environment.GetEnvironmentVariable("PROBE_STRUCT") == "1")
{
    Console.WriteLine("\n[3b] 结构");
    var ecs = doc.GetOrNull(ECDependenciesSection.Key);
    if (ecs != null)
        foreach (var d in ecs.ECDependencies)
            Console.WriteLine($"  EC  {d.Name,-16} v{d.InfoVersion} reexp={d.ReExport} path={d.Path}  ids=[{string.Join(" ", d.DefinedIds.Select(x => $"{x.Start:X8}+{x.Count}"))}]");
    Console.WriteLine($"  MainMethod=0x{code.MainMethod:X8} Flag=0x{code.Flag:X} AllocatedIdNum=0x{code.AllocatedIdNum:X}");
    foreach (var c in code.Classes)
        Console.WriteLine($"  CLS 0x{c.Id:X8} form=0x{c.Form:X8} base=0x{c.BaseClass:X8} methods={c.Methods.Count,4} vars={c.Variables.Count,3} {c.Name}");
    var res = doc.GetOrNull(ResourceSection.Key);
    if (res != null)
    {
        foreach (var f in res.Forms)
            Console.WriteLine($"  FORM 0x{f.Id:X8} class=0x{f.Class:X8} elems={f.Elements.Count} ubc={f.UnknownBeforeClass} (0x{f.UnknownBeforeClass:X8}) mem=0x{f.MemoryAddress:X8} cmt='{f.Comment}' {f.Name}");
        Console.WriteLine($"  CONST {res.Constants.Count}");
    }
    Console.WriteLine($"  GLOBAL {code.GlobalVariables.Count}  STRUCT {code.Structs.Count}  DLL {code.DllDeclares.Count}");
    var formName = Environment.GetEnvironmentVariable("PROBE_FORM");
    if (res != null && !string.IsNullOrEmpty(formName))
        foreach (var f in res.Forms.Where(f => f.Name == formName))
            foreach (var e in f.Elements)
                if (e is FormControlInfo fc)
                    Console.WriteLine($"  CTRL 0x{fc.Id:X8} '{fc.Name}' parent=0x{fc.Parent:X8} children=[{string.Join(",", (fc.Children ?? Array.Empty<int>()).Select(x => $"0x{x:X8}"))}] vis={fc.Visible} dis={fc.Disable} tab={fc.TabStop} lock={fc.Locked} tabidx={fc.TabIndex} ev={fc.Events?.Length}");
                else if (e is FormMenuInfo fm)
                    Console.WriteLine($"  MENU 0x{fm.Id:X8} '{fm.Name}' lvl={fm.Level} text='{fm.Text}'");
    var cps = doc.GetOrNull(ClassPublicitySection.Key);
    if (cps != null)
        foreach (var x in cps.ClassPublicities)
            Console.WriteLine($"  PUB 0x{x.Class:X8} flags=0x{x.Flags:X} {(code.Classes.FirstOrDefault(c => c.Id == x.Class)?.Name)}");
    foreach (var c in code.Classes.Where(c => (c.Id & 0xFF000000) != 0x49000000 || c.Name.Contains("CQ")))
    {
        var mf = c.Methods.Select(id => code.Methods.FirstOrDefault(m => m.Id == id)).Where(m => m != null).Select(m => $"{m!.Flags:X}").Distinct();
        Console.WriteLine($"  CLSRAW 0x{c.Id:X8} {c.Name} base=0x{c.BaseClass:X8} form=0x{c.Form:X8} mem=0x{c.MemoryAddress:X8} cmt='{c.Comment}' mflags=[{string.Join(",", mf)}]");
    }
    Console.WriteLine($"  sections: {string.Join(", ", doc.Sections.Select(s => s.GetType().Name))}");
}

// ---------- 4. 文本渲染 ----------
if (args.Length > 1)
{
    Console.WriteLine("\n[4] 文本渲染");
    IdToNameMap map;
    try { map = new IdToNameMap(doc); }
    catch (Exception ex) { Console.WriteLine("  IdToNameMap 失败: " + ex.Message); return 1; }
    for (int li = 0; li < map.LibDefinedName.Length; li++)
        Console.WriteLine($"  库[{li}] {code.Libraries[li].FileName,-12} -> {(map.LibDefinedName[li] == null ? "未加载" : "已加载")}");
    foreach (var cls in code.Classes)
    {
        if (!args.Skip(1).Any(s => (cls.Name ?? "").Contains(s))) continue;
        Console.WriteLine($"\n----- {cls.Name} -----");
        var sw = new StringWriter();
        cls.ToTextCode(map, sw, 0, code);
        Console.WriteLine(sw.ToString());
    }
}
return sectionFail == 0 ? 0 : 1;
