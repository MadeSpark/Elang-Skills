using System.Text.Json;
using System.Text.Json.Serialization;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Context;
using QIQI.EProjectFile.Sections;
using QAQ.ETool.Core.Text;

namespace QAQ.ETool.Core.Model;

/// <summary>
/// 旁路文件 &lt;文本目录&gt;/.qaq/sidecar.json：保存文本格式表达不了、但无损回写需要的信息
/// （实体 ID、隐藏标志位、参数 UBound、内存地址、变量尾部字节、原始代码字节、IDE 编辑状态段、易模块文件信息等）。
///
/// 以「实体路径」为键（与文本一一对应，如 `C:程序集/M:子程序/L:局部变量`、`[模块]K:常量`），t2e 时逐项套用：
///   · 键在文本里仍存在 → 沿用原 ID 与隐藏位（文本可见的位以文本为准）；新增的实体分配新 ID；
///   · 子程序代码：把原始字节按「新工程」重新渲染，与文本逐行相同才用原始字节，否则按文本重新绑定；
///   · 编辑信息段/未知段只在原有实体全部保留时带回。
/// 文件缺失或损坏时 t2e 照常工作（等同 e2txt 目录）。
/// </summary>
public sealed class Sidecar
{
    public const string RelDir = ".qaq";
    public const string FileName = "sidecar.json";
    public const int CurrentVersion = 1;

    public int Version { get; set; } = CurrentVersion;
    public string Tool { get; set; } = "QAQ-ETool";
    public int AllocatedIdNum { get; set; }
    public CodeHeader? Code { get; set; }
    /// <summary>原文件的全部段（顺序即原顺序）；Data 只对 t2e 不生成的段保存</summary>
    public List<SectionRaw> Sections { get; set; } = new();
    /// <summary>模块目录名 → 易模块引用信息</summary>
    public Dictionary<string, DepRaw> Deps { get; set; } = new(StringComparer.Ordinal);
    public Dictionary<string, Item> Items { get; set; } = new(StringComparer.Ordinal);
    /// <summary>程序段子程序表的原顺序（与程序集内顺序不一定相同）</summary>
    public List<int> MethodOrder { get; set; } = new();
    /// <summary>程序集公开性段原表 [程序集ID, 标志]（含文本表达不了的隐藏位）</summary>
    public List<int[]> ClassPublicity { get; set; } = new();
    /// <summary>事件索引段原表 [窗口ID, 单元ID, 事件号, 子程序ID]（IDE 不按命名约定维护，可能含非约定条目）</summary>
    public List<int[]> Events { get; set; } = new();
    /// <summary>编辑过滤器：分组路径（class/ 下的目录）→ 分组 key</summary>
    public Dictionary<string, int> Folders { get; set; } = new(StringComparer.Ordinal);
    public int FolderAlloc { get; set; }
    /// <summary>结束段名称按密钥异或（e2txt 产物）；IDE 产物为 false</summary>
    public bool EofNameXored { get; set; }

    public sealed class CodeHeader
    {
        public int Flag { get; set; }
        public int MainMethod { get; set; }
        public string? UnknownBeforeIconData { get; set; }
        public string? DebugCommandParameters { get; set; }
    }

    public sealed class SectionRaw
    {
        public int Key { get; set; }
        public string Name { get; set; } = "";
        public bool Optional { get; set; }
        public string? Data { get; set; }
    }

    public sealed class DepRaw
    {
        public string Path { get; set; } = "";
        public int FileSize { get; set; }
        public long FileTime { get; set; }
        public bool ReExport { get; set; }
        public int InfoVersion { get; set; }
        public List<int[]> DefinedIds { get; set; } = new();
    }

    public sealed class Item
    {
        public int Id { get; set; }
        public int? Flags { get; set; }
        public int[]? UBound { get; set; }
        public string? Tail { get; set; }
        public int? Mem { get; set; }
        /// <summary>子程序代码 6 段（LineOffest, BlockOffest, MethodRef, VarRef, ConstRef, ExpressionData），base64</summary>
        public string[]? Code { get; set; }
    }

    // ================================================================ 读写
    private static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    public static string PathOf(string root) => System.IO.Path.Combine(root, RelDir, FileName);

    public void Save(string root)
    {
        var p = PathOf(root);
        Directory.CreateDirectory(System.IO.Path.GetDirectoryName(p)!);
        File.WriteAllBytes(p, JsonSerializer.SerializeToUtf8Bytes(this, Json));
    }

    /// <summary>读取旁路文件；不存在返回 null，损坏时返回 null 并给出原因</summary>
    public static Sidecar? TryLoad(string root, out string? problem)
    {
        problem = null;
        var p = PathOf(root);
        if (!File.Exists(p)) return null;
        try
        {
            var s = JsonSerializer.Deserialize<Sidecar>(File.ReadAllBytes(p), Json);
            if (s == null) { problem = "内容为空"; return null; }
            if (s.Version != CurrentVersion) { problem = $"版本 {s.Version} 不受支持"; return null; }
            return s;
        }
        catch (Exception ex)
        {
            problem = ex.Message;
            return null;
        }
    }

    // ================================================================ 路径键
    /// <summary>同一父节点下重名时按出现顺序加 #n（第一个不加）</summary>
    internal sealed class KeyGen
    {
        private readonly Dictionary<string, int> _seen = new(StringComparer.Ordinal);
        public string Next(string key)
        {
            _seen.TryGetValue(key, out var n);
            _seen[key] = n + 1;
            return n == 0 ? key : key + "#" + n;
        }
    }

    internal static string Pre(string? module) => module == null ? "" : "[" + module + "]";

    // ================================================================ e2t：从原工程采集
    public static Sidecar Capture(EProject p)
    {
        var s = new Sidecar
        {
            AllocatedIdNum = p.Code.AllocatedIdNum,
            MethodOrder = p.Code.Methods.Select(x => x.Id).ToList(),
            EofNameXored = p.Doc.EofNameXored,
        };
        s.Code = new CodeHeader
        {
            Flag = p.Code.Flag,
            MainMethod = p.Code.MainMethod,
            UnknownBeforeIconData = p.Code.UnknownBeforeIconData.IsDefault ? null : Convert.ToBase64String(p.Code.UnknownBeforeIconData.ToArray()),
            DebugCommandParameters = p.Code.DebugCommandParameters,
        };

        // ---- 段
        var ctx = new BlockByteifierContext(p.Encoding, false);
        foreach (var sec in p.Doc.Sections)
        {
            var raw = new SectionRaw
            {
                Key = sec.SectionKey,
                Name = sec.SectionName,
                Optional = p.Doc.OptionalOverrides.TryGetValue(sec, out var o) ? o : sec.IsOptional,
            };
            if (Passthrough(sec.SectionKey)) raw.Data = Convert.ToBase64String(sec.ToBytes(ctx));
            s.Sections.Add(raw);
        }

        // ---- IDE 辅助段（t2e 会按文本重建，这里留原表供合并）
        if (p.Doc.GetOrNull(ClassPublicitySection.Key) is { } cp)
            s.ClassPublicity = cp.ClassPublicities.Select(x => new[] { x.Class, x.Flags }).ToList();
        if (p.Doc.GetOrNull(EventIndicesSection.Key) is { } ev)
            s.Events = ev.Indices.Select(x => new[] { x.FormId, x.UnitId, x.EventId, x.MethodId }).ToList();
        if (p.Doc.GetOrNull(FolderSection.Key) is { } fs)
        {
            var byKey = fs.Folders.ToDictionary(x => x.Key);
            string PathOf(CodeFolderInfo f, int depth = 0) =>
                depth < 64 && f.ParentKey != 0 && byKey.TryGetValue(f.ParentKey, out var pf)
                    ? PathOf(pf, depth + 1) + "/" + Importer.SafeName(f.Name)
                    : Importer.SafeName(f.Name);
            foreach (var f in fs.Folders) s.Folders.TryAdd(PathOf(f), f.Key);
            // allocatedKey 是私有字段：按序列化结果的头 4 字节取
            var b = fs.ToBytes(new BlockByteifierContext(p.Encoding, false));
            if (b.Length >= 4) s.FolderAlloc = BitConverter.ToInt32(b, 0);
        }

        // ---- 易模块
        foreach (var m in p.Modules)
            s.Deps[Importer.SafeName(m.Name)] = new DepRaw
            {
                Path = m.Dep.Path ?? "",
                FileSize = m.Dep.FileSize,
                FileTime = SafeFileTime(m.Dep.FileLastModifiedDate),
                ReExport = m.Dep.ReExport,
                InfoVersion = m.Dep.InfoVersion,
                DefinedIds = m.Dep.DefinedIds.Select(x => new[] { x.Start, x.Count }).ToList(),
            };

        // ---- 实体
        var kg = new KeyGen();
        string P(int id) { var mi = p.ModuleIndexOf(id); return mi < 0 ? "" : Pre(Importer.SafeName(p.Modules[mi].Name)); }
        Item Add(string key, int id)
        {
            var it = new Item { Id = id };
            s.Items[key] = it;
            return it;
        }
        void Var(string key, AbstractVariableInfo v, bool ubound)
        {
            var it = Add(key, v.Id);
            it.Flags = v.Flags;
            if (ubound && v.UBound is { Length: > 0 }) it.UBound = v.UBound;
            if (v.TrailingBytes is { Length: > 0 }) it.Tail = Convert.ToBase64String(v.TrailingBytes);
        }

        foreach (var st in p.Code.Structs)
        {
            var k = kg.Next(P(st.Id) + "S:" + st.Name);
            var it = Add(k, st.Id);
            it.Flags = st.Flags;
            it.Mem = st.MemoryAddress;
            foreach (var m in st.Members) Var(kg.Next(k + "/F:" + m.Name), m, false);
        }
        foreach (var d in p.Code.DllDeclares)
        {
            var k = kg.Next(P(d.Id) + "D:" + d.Name);
            var it = Add(k, d.Id);
            it.Flags = d.Flags;
            it.Mem = d.MemoryAddress;
            foreach (var a in d.Parameters) Var(kg.Next(k + "/P:" + a.Name), a, true);
        }
        foreach (var g in p.Code.GlobalVariables) Var(kg.Next(P(g.Id) + "G:" + g.Name), g, false);
        foreach (var c in p.Resource.Constants)
        {
            var kind = (uint)c.Id >> 28;
            var tag = kind switch { 2 => "I:", 3 => "A:", _ => "K:" };
            // 无名图片/声音：文本里的占位名带 ID（小端 hex），键也用它
            var name = kind is 2 or 3 && string.IsNullOrEmpty(c.Name) ? "@" + Convert.ToHexString(BitConverter.GetBytes(c.Id)) : c.Name;
            var it = Add(kg.Next(P(c.Id) + tag + name), c.Id);
            it.Flags = c.Flags;
        }
        foreach (var cls in p.Code.Classes)
        {
            var k = kg.Next(P(cls.Id) + "C:" + cls.Name);
            Add(k, cls.Id).Mem = cls.MemoryAddress;
            foreach (var v in cls.Variables) Var(kg.Next(k + "/V:" + v.Name), v, false);
            foreach (var mid in cls.Methods)
            {
                if (!p.MethodById.TryGetValue(mid, out var m)) continue;
                var mk = kg.Next(k + "/M:" + m.Name);
                var it = Add(mk, m.Id);
                it.Flags = m.Flags;
                it.Mem = m.MemoryAddress;
                if (p.ModuleIndexOf(cls.Id) < 0)
                {
                    var cd = m.CodeData;
                    it.Code = new[] { cd.LineOffest, cd.BlockOffest, cd.MethodReference, cd.VariableReference, cd.ConstantReference, cd.ExpressionData }
                        .Select(b => Convert.ToBase64String(b ?? Array.Empty<byte>())).ToArray();
                }
                foreach (var a in m.Parameters) Var(kg.Next(mk + "/P:" + a.Name), a, true);
                foreach (var v in m.Variables) Var(kg.Next(mk + "/L:" + v.Name), v, false);
            }
        }
        foreach (var f in p.Resource.Forms)
        {
            var k = kg.Next("W:" + f.Name);
            Add(k, f.Id).Mem = f.MemoryAddress;
            foreach (var e in f.Elements)
                Add(kg.Next(k + (e is FormMenuInfo ? "/U:" : "/E:") + e.Name), e.Id);
        }
        return s;
    }

    /// <summary>t2e 不生成、需要原样带回的段（可丢失程序段也带回：编译输出文件名、已删除定义的名称）</summary>
    internal static bool Passthrough(int key) => key switch
    {
        0x02007319 or 0x01007319 or 0x04007319 or 0x03007319 or 0x0D007319 or 0x08007319
            or 0x0C007319 or 0x0B007319 or 0x0A007319 or 0x0E007319 or 0x07007319 => false,
        _ => true,
    };

    /// <summary>与原工程的实体 ID 相关的段：原有实体有丢失时不带回</summary>
    internal static bool IdSensitive(int key) => key == 0x09007319 || key == 0x0F007319;

    private static long SafeFileTime(DateTime d)
    {
        try { return d.ToFileTime(); }
        catch (ArgumentOutOfRangeException) { return 0; }
    }

    // ================================================================ t2e：文本对象 → 路径键
    /// <summary>与 Capture 同构地给 TextProject 里的每个声明生成键（引用相等字典）</summary>
    internal static Dictionary<object, string> KeysOf(TextProject t)
    {
        var map = new Dictionary<object, string>(ReferenceEqualityComparer.Instance);
        var kg = new KeyGen();

        void Decls(string pre, TDeclSet d)
        {
            foreach (var st in d.Structs)
            {
                var k = map[st] = kg.Next(pre + "S:" + st.Name);
                foreach (var m in st.Members) map[m] = kg.Next(k + "/F:" + m.Name);
            }
            foreach (var dl in d.Dlls)
            {
                var k = map[dl] = kg.Next(pre + "D:" + dl.Name);
                foreach (var a in dl.Params) map[a] = kg.Next(k + "/P:" + a.Name);
            }
            foreach (var g in d.Globals) map[g] = kg.Next(pre + "G:" + g.Name);
            foreach (var c in d.Consts) map[c] = kg.Next(pre + "K:" + c.Name);
        }
        void Classes(string pre, IEnumerable<TClass> cs)
        {
            foreach (var c in cs)
            {
                var k = map[c] = kg.Next(pre + "C:" + c.Name);
                foreach (var v in c.Vars) map[v] = kg.Next(k + "/V:" + v.Name);
                foreach (var m in c.Methods)
                {
                    var mk = map[m] = kg.Next(k + "/M:" + m.Name);
                    foreach (var a in m.Params) map[a] = kg.Next(mk + "/P:" + a.Name);
                    foreach (var v in m.Locals) map[v] = kg.Next(mk + "/L:" + v.Name);
                }
            }
        }

        Decls("", t.Main);
        foreach (var r in t.Resources)
        {
            var tag = r.Kind switch { TResourceKind.Image => "I:", TResourceKind.Sound => "A:", _ => "K:" };
            // 无名资源在文本里用占位名 `图片资源_<ID 小端 hex>`
            var name = IsPlaceholder(r) ? "@" + r.Name.Substring(r.Name.IndexOf('_') + 1).ToUpperInvariant() : r.Name;
            map[r] = kg.Next(tag + name);
        }
        Classes("", t.Classes);
        foreach (var m in t.Modules)
        {
            var pre = Pre(m.Name);
            Decls(pre, m.Decls);
            Classes(pre, m.Classes);
        }
        foreach (var f in t.Forms)
        {
            var k = map[f] = kg.Next("W:" + (f.Json.TryGetValue("Name", out var n) ? n as string ?? "" : ""));
            if (f.Json.TryGetValue("Controls", out var cs) && cs is List<object?> cl)
                foreach (var c in cl.OfType<JObject>()) map[c] = kg.Next(k + "/E:" + (c.TryGetValue("Name", out var cn) ? cn as string ?? "" : ""));
            if (f.Json.TryGetValue("Menus", out var ms) && ms is List<object?> ml)
                foreach (var c in ml.OfType<JObject>()) map[c] = kg.Next(k + "/U:" + (c.TryGetValue("Name", out var cn) ? cn as string ?? "" : ""));
        }
        return map;
    }

    internal static bool IsPlaceholder(TResource r)
    {
        if (r.Kind == TResourceKind.LongText) return false;
        var prefix = r.Kind == TResourceKind.Sound ? "声音资源_" : "图片资源_";
        return r.Name.StartsWith(prefix, StringComparison.Ordinal) && r.Name.Length == prefix.Length + 8;
    }

    internal static MethodCodeData DecodeCode(string[] parts, System.Text.Encoding enc)
    {
        byte[] B(int i) => i < parts.Length ? Convert.FromBase64String(parts[i]) : Array.Empty<byte>();
        return new MethodCodeData(B(0), B(1), B(2), B(3), B(4), B(5), enc);
    }
}
