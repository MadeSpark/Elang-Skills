using System.Text;

namespace QAQ.ETool.Core.Text;

/// <summary>
/// t2e：e2txt 兼容文本目录 → <see cref="TextProject"/>（Exporter 的逆，只做读取与词法拆分）。
///
/// 顺序约定：
///   · Classes：从 class/ 的根排序表递归展开——名称是子分组就展开该分组排序表，是程序集就取该文件，
///     是窗口就记入窗口顺序；根排序表里的模块程序集名（e2txt 会列出）静默跳过。
///     排序表没提到的程序集按相对路径序数排序追加（warning）。
///   · Forms：排序表展开中遇到的分组窗口 → 未被引用的分组窗口（路径序数，warning）→ form/ 下按文件名序数。
///   · Resources / 模块内程序集：文件名序数；Modules：模块名序数（文本目录不记录模块在 .e 中的顺序）。
/// 除根目录不存在外不抛异常，问题写入 TextProject.Errors / Warnings。
/// </summary>
public static class Importer
{
    public static TextProject Load(string root)
    {
        if (!Directory.Exists(root)) throw new DirectoryNotFoundException($"文本工程目录不存在：{root}");
        var ctx = new Ctx(Path.GetFullPath(root));
        ctx.Run();
        return ctx.P;
    }

    private const string StaticSuffix = ".static.e.txt";
    private const string ClassSuffix = ".class.e.txt";
    private const string FormCodeSuffix = ".form.e.txt";
    private const string FormJsonSuffix = ".form.json";
    private const string ModuleDescSuffix = ".desc.json";

    /// <summary>与 Exporter.SafeName 相同：文件名非法字符 → '_'</summary>
    internal static string SafeName(string name)
    {
        var bad = Path.GetInvalidFileNameChars();
        var sb = new StringBuilder(name.Length);
        foreach (var c in name) sb.Append(Array.IndexOf(bad, c) >= 0 ? '_' : c);
        return sb.ToString();
    }

    internal static bool ClassKindOf(string fileName, out TClassKind kind, out string baseName)
    {
        foreach (var (suf, k) in new[] { (StaticSuffix, TClassKind.Static), (ClassSuffix, TClassKind.Class), (FormCodeSuffix, TClassKind.Form) })
        {
            if (fileName.EndsWith(suf, StringComparison.Ordinal))
            {
                kind = k;
                baseName = fileName.Substring(0, fileName.Length - suf.Length);
                return true;
            }
        }
        kind = TClassKind.Static;
        baseName = "";
        return false;
    }

    private sealed class Folder
    {
        public required string Key;
        public required string Dir;
        public List<string> Order = new();
        public readonly List<(string Base, TClass Cls)> Classes = new();
        public readonly List<(string Base, TForm Form)> Forms = new();
    }

    private sealed class Ctx
    {
        public readonly TextProject P;
        private readonly string _root;
        private readonly Layout _l;

        public Ctx(string root)
        {
            _root = root;
            _l = Layout.Detect(root);
            P = new TextProject { Root = root, Layout = _l };
        }

        public void Run()
        {
            Guard("project", LoadProjectFile);
            Guard("config", LoadConfigs);
            Guard("decls", () => LoadDecls(_root, P.Main));
            Guard("module", LoadModules);
            Guard("class", LoadClassTree);
            Guard("resource", LoadResources);
            Guard("icon", () =>
            {
                var path = Path.Combine(_root, _l.Icon);
                if (File.Exists(path)) P.Icon = ReadBytes(path) ?? Array.Empty<byte>();
            });
        }

        /// <summary>兜底：任何未预料的异常都转成 error，不中断其余部分</summary>
        private void Guard(string what, Action a)
        {
            try { a(); }
            catch (Exception ex) { P.Errors.Add($"{what}: 读取时发生异常：{ex.GetType().Name}: {ex.Message}"); }
        }

        // ------------------------------------------------------------ 基础读取
        private string Rel(string path) => Path.GetRelativePath(_root, path).Replace('\\', '/');
        private void Err(string path, string msg) => P.Errors.Add($"{Rel(path)}: {msg}");
        private void Warn(string path, string msg) => P.Warnings.Add($"{Rel(path)}: {msg}");

        private byte[]? ReadBytes(string path)
        {
            try { return File.ReadAllBytes(path); }
            catch (Exception ex) { Err(path, $"读取失败：{ex.Message}"); return null; }
        }

        private string? ReadText(string path)
        {
            var b = ReadBytes(path);
            if (b == null) return null;
            try { return TextFiles.ReadUtf8(b); }
            catch (DecoderFallbackException) { Err(path, "不是合法的 UTF-8 文本"); return null; }
        }

        private object? ParseJson(string path, string text)
        {
            try { return EJson.Parse(text); }
            catch (Exception ex) { Err(path, $"JSON 解析失败：{ex.Message}"); return null; }
        }

        /// <summary>读 UTF-8 JSON；文件不存在返回 null（不报错）</summary>
        private object? ReadJson(string path)
        {
            if (!File.Exists(path)) return null;
            var t = ReadText(path);
            return t == null ? null : ParseJson(path, t);
        }

        private JObject? ReadJsonObject(string path)
        {
            var v = ReadJson(path);
            if (v == null) return null;
            if (v is JObject o) return o;
            Err(path, "顶层应为 JSON 对象");
            return null;
        }

        private static int Int(JObject o, string key, Action<string> bad)
        {
            if (!o.TryGetValue(key, out var v)) { bad($"缺少键 {key}"); return 0; }
            switch (v)
            {
                case long l when l >= int.MinValue && l <= uint.MaxValue: return unchecked((int)l);
                case double d when d == Math.Floor(d) && d >= int.MinValue && d <= int.MaxValue: return (int)d;
                default: bad($"键 {key} 应为整数"); return 0;
            }
        }

        private static string Str(JObject o, string key, Action<string> bad, bool required = true)
        {
            if (!o.TryGetValue(key, out var v)) { if (required) bad($"缺少键 {key}"); return ""; }
            if (v is string s) return s;
            if (v != null) bad($"键 {key} 应为字符串");
            return "";
        }

        // ------------------------------------------------------------ project.qtprj（GBK 无 BOM；也接受 UTF-8）
        private void LoadProjectFile()
        {
            var path = Path.Combine(_root, _l.Project);
            if (!File.Exists(path)) { Warn(path, "缺少工程文件"); return; }
            var b = ReadBytes(path);
            if (b == null) return;
            string text;
            try { text = TextFiles.ReadUtf8(b); }
            catch (DecoderFallbackException) { text = TextFiles.Gbk.GetString(b); }
            var v = ParseJson(path, text);
            if (v is JObject o) P.ProjectInfo = o;
            else if (v != null) Err(path, "顶层应为 JSON 对象");
        }

        // ------------------------------------------------------------ config/
        private void LoadConfigs()
        {
            var dir = Path.Combine(_root, _l.Config);
            if (!Directory.Exists(dir)) return;

            var libPath = Path.Combine(dir, _l.LibConfig);
            var libs = ReadJson(libPath);
            if (libs is List<object?> arr)
            {
                for (int k = 0; k < arr.Count; k++)
                {
                    if (arr[k] is not JObject o) { Err(libPath, $"第 {k} 项应为对象"); continue; }
                    int kk = k;
                    void Bad(string m) => Err(libPath, $"第 {kk} 项：{m}");
                    var lib = new TLib
                    {
                        Key = Str(o, "Key", Bad),
                        Guid = Str(o, "Guid", Bad),
                        Name = Str(o, "Name", Bad),
                        CmdCount = Int(o, "CmdCount", Bad),
                        MaxRefConstPos = Int(o, "MaxRefConstPos", Bad),
                        MaxRefObjectPos = Int(o, "MaxRefObjectPos", Bad),
                    };
                    if (o.TryGetValue("Version", out var ver) && ver is JObject vo)
                    {
                        lib.Major = Int(vo, "Major", Bad);
                        lib.Minor = Int(vo, "Minor", Bad);
                    }
                    else Bad("缺少 Version 对象");
                    P.Libs.Add(lib);
                }
            }
            else if (libs != null) Err(libPath, "顶层应为数组");

            P.SystemConfig = ReadJsonObject(Path.Combine(dir, _l.SystemConfig));
            P.UserConfig = ReadJsonObject(Path.Combine(dir, _l.UserConfig));

            var initPath = Path.Combine(dir, _l.EcInitConfig);
            var init = ReadJsonObject(initPath);
            if (init != null)
                foreach (var kv in init)   // JObject 是 Dictionary，枚举顺序 = 文件中键的顺序
                {
                    if (kv.Value is string s) P.EcInit.Add((kv.Key, s));
                    else Err(initPath, $"模块 {kv.Key} 的初始化子程序名应为字符串");
                }

            var epPath = Path.Combine(dir, _l.EpackFilesConfig);
            var ep = ReadJsonObject(epPath);
            if (ep != null && ep.TryGetValue("FileNames", out var fnv))
            {
                if (fnv is List<object?> fns)
                    foreach (var x in fns)
                    {
                        if (x is string s) P.EpackFileNames.Add(s);
                        else if (x == null) P.EpackFileNames.Add("");
                        else Err(epPath, "FileNames 的元素应为字符串");
                    }
                else if (fnv != null) Err(epPath, "FileNames 应为数组");
            }
        }

        // ------------------------------------------------------------ const/dll/struct/global
        private void LoadDecls(string dir, TDeclSet set)
        {
            void One(string file, Action<string, string> parse)
            {
                var path = Path.Combine(dir, file);
                if (!File.Exists(path)) return;
                var t = ReadText(path);
                if (t != null) parse(Rel(path), t);
            }
            One(_l.Const, (r, t) => DeclParser.ParseConstFile(r, t, set.Consts, P.Errors, P.Warnings));
            One(_l.Dll, (r, t) => DeclParser.ParseDllFile(r, t, set.Dlls, P.Errors, P.Warnings));
            One(_l.Struct, (r, t) => DeclParser.ParseStructFile(r, t, set.Structs, P.Errors, P.Warnings));
            One(_l.Global, (r, t) => DeclParser.ParseGlobalFile(r, t, set.Globals, P.Errors, P.Warnings));
        }

        private TClass? LoadClassFile(string path, TClassKind kind)
        {
            var t = ReadText(path);
            return t == null ? null : DeclParser.ParseClassFile(Rel(path), t, kind, P.Errors, P.Warnings);
        }

        private static IEnumerable<string> FilesOrdinal(string dir) =>
            Directory.GetFiles(dir).OrderBy(Path.GetFileName, StringComparer.Ordinal);

        private static IEnumerable<string> DirsOrdinal(string dir) =>
            Directory.GetDirectories(dir).OrderBy(Path.GetFileName, StringComparer.Ordinal);

        // ------------------------------------------------------------ module/
        private void LoadModules()
        {
            var dir = Path.Combine(_root, _l.Module);
            if (!Directory.Exists(dir)) return;

            var descs = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var f in FilesOrdinal(dir))
            {
                var fn = Path.GetFileName(f);
                if (fn.EndsWith(ModuleDescSuffix, StringComparison.Ordinal))
                    descs[fn.Substring(0, fn.Length - ModuleDescSuffix.Length)] = f;
                else Warn(f, "模块目录下的未知文件，已忽略");
            }
            var dirs = DirsOrdinal(dir).ToDictionary(x => Path.GetFileName(x), x => x, StringComparer.Ordinal);
            var names = descs.Keys.Union(dirs.Keys).OrderBy(x => x, StringComparer.Ordinal);

            foreach (var name in names)
            {
                var m = new TModule { Name = name };
                if (descs.TryGetValue(name, out var dp)) LoadModuleDesc(dp, m);
                else Warn(Path.Combine(dir, name), $"模块目录缺少对应的 {name}{ModuleDescSuffix}");

                if (dirs.TryGetValue(name, out var md))
                {
                    LoadDecls(md, m.Decls);
                    var known = new HashSet<string>(StringComparer.Ordinal) { _l.Const, _l.Dll, _l.Struct, _l.Global };
                    foreach (var f in FilesOrdinal(md))
                        if (!known.Contains(Path.GetFileName(f))) Warn(f, "模块目录下的未知文件，已忽略");
                    foreach (var sd in DirsOrdinal(md))
                        if (Path.GetFileName(sd) != _l.Class) Warn(sd, "模块目录下的未知子目录，已忽略");

                    var cd = Path.Combine(md, _l.Class);
                    if (Directory.Exists(cd))
                    {
                        foreach (var f in FilesOrdinal(cd))
                        {
                            if (!ClassKindOf(Path.GetFileName(f), out var kind, out var bn)) { Warn(f, "未知文件（不是 *.static/.class/.form.e.txt），已忽略"); continue; }
                            var cls = LoadClassFile(f, kind);
                            if (cls == null) continue;
                            if (cls.Name.Length > 0 && SafeName(cls.Name) != bn) Warn(f, $"文件名与程序集名 {cls.Name} 不一致");
                            m.Classes.Add(cls);
                        }
                        foreach (var sd in DirsOrdinal(cd)) Warn(sd, "模块程序集目录不支持分组子目录，已忽略");
                    }
                }
                else Warn(descs[name], $"缺少对应的模块目录 {name}/");
                P.Modules.Add(m);
            }
        }

        private void LoadModuleDesc(string path, TModule m)
        {
            var o = ReadJsonObject(path);
            if (o == null) return;
            if (o.TryGetValue("Source", out var src))
            {
                if (src is string s) m.Source = s;
                else if (src != null) Err(path, "Source 应为字符串");
            }
            if (o.TryGetValue("Rename", out var rn) && rn != null)
            {
                if (rn is not JObject ro) { Err(path, "Rename 应为对象"); return; }
                foreach (var kv in ro)
                {
                    if (kv.Key != "Struct") { Warn(path, $"Rename.{kv.Key} 暂不支持，已忽略"); continue; }
                    if (kv.Value is not List<object?> list) { Err(path, "Rename.Struct 应为数组"); continue; }
                    foreach (var item in list)
                    {
                        if (item is not JObject io) { Err(path, "Rename.Struct 的元素应为对象"); continue; }
                        void Bad(string msg) => Err(path, $"Rename.Struct：{msg}");
                        m.StructRenames.Add((Str(io, "Src", Bad), Str(io, "Dst", Bad)));
                    }
                }
            }
            foreach (var k in o.Keys)
                if (k != "Source" && k != "Rename") Warn(path, $"未知键 {k}，已忽略");
        }

        // ------------------------------------------------------------ class/ 分组树 + form/
        private List<string> ReadOrderList(string path)
        {
            var list = new List<string>();
            if (!File.Exists(path)) { Warn(path, "缺少排序表"); return list; }
            var t = ReadText(path);
            if (!string.IsNullOrEmpty(t)) list.AddRange(DeclParser.SplitLines(t));
            return list;
        }

        private TForm? LoadForm(string path, string folder)
        {
            var o = ReadJsonObject(path);
            return o == null ? null : new TForm { File = Rel(path), Folder = folder, Json = o };
        }

        private void LoadClassTree()
        {
            var classDir = Path.Combine(_root, _l.Class);
            var folders = new Dictionary<string, Folder>(StringComparer.Ordinal);
            if (Directory.Exists(classDir))
            {
                Scan(classDir, "", folders);
                var dd = ReadJsonObject(Path.Combine(classDir, _l.DirsDesc));
                if (dd != null && dd.TryGetValue("ExpandedDirs", out var ed))
                {
                    if (ed is List<object?> el)
                        foreach (var x in el)
                        {
                            if (x is string s) P.ExpandedDirs.Add(s);
                            else Err(Path.Combine(classDir, _l.DirsDesc), "ExpandedDirs 的元素应为字符串");
                        }
                    else if (ed != null) Err(Path.Combine(classDir, _l.DirsDesc), "ExpandedDirs 应为数组");
                }
            }
            else Warn(classDir, "缺少程序集目录");

            // ---- 按排序表展开
            var moduleClassNames = new HashSet<string>(P.Modules.SelectMany(m => m.Classes).Select(c => c.Name), StringComparer.Ordinal);
            var takenC = new HashSet<TClass>(ReferenceEqualityComparer.Instance);
            var takenF = new HashSet<TForm>(ReferenceEqualityComparer.Instance);
            var visited = new HashSet<string>(StringComparer.Ordinal);
            var forms = new List<TForm>();

            void Expand(string key)
            {
                if (!visited.Add(key)) return;
                var f = folders[key];
                var orderFile = Path.Combine(f.Dir, _l.OrderList);
                foreach (var name in f.Order)
                {
                    if (name.Length == 0) { Warn(orderFile, "排序表中有空行，已忽略"); continue; }
                    var safe = SafeName(name);
                    var sub = key.Length == 0 ? safe : key + "/" + safe;
                    bool found = false, dup = false;
                    if (folders.ContainsKey(sub))
                    {
                        found = true;
                        if (visited.Contains(sub)) dup = true;
                        else Expand(sub);
                    }
                    foreach (var (b, c) in f.Classes)
                        if (b == safe) { found = true; if (takenC.Add(c)) P.Classes.Add(c); else dup = true; }
                    foreach (var (b, fm) in f.Forms)
                        if (b == safe) { found = true; if (takenF.Add(fm)) forms.Add(fm); else dup = true; }
                    if (dup) Warn(orderFile, $"排序表中重复的名称 {name}");
                    else if (!found && !(key.Length == 0 && moduleClassNames.Contains(name)))
                        Warn(orderFile, $"排序表中的 {name} 找不到对应的程序集/窗口/分组");
                }
            }
            if (folders.ContainsKey("")) Expand("");

            foreach (var f in folders.Values.Where(f => !visited.Contains(f.Key)).OrderBy(f => f.Key, StringComparer.Ordinal))
                Warn(f.Dir, "分组未被任何排序表引用");

            foreach (var (path, c) in folders.Values.SelectMany(f => f.Classes).Where(x => !takenC.Contains(x.Cls))
                         .Select(x => (x.Cls.File, x.Cls)).OrderBy(x => x.File, StringComparer.Ordinal))
            {
                P.Warnings.Add($"{path}: 程序集未出现在所在分组的排序表中，已追加到末尾");
                P.Classes.Add(c);
            }
            P.Forms.AddRange(forms);
            foreach (var fm in folders.Values.SelectMany(f => f.Forms).Select(x => x.Form).Where(x => !takenF.Contains(x))
                         .OrderBy(x => x.File, StringComparer.Ordinal))
            {
                P.Warnings.Add($"{fm.File}: 窗口未出现在所在分组的排序表中，已追加");
                P.Forms.Add(fm);
            }

            // ---- form/
            var formDir = Path.Combine(_root, _l.Form);
            if (Directory.Exists(formDir))
            {
                foreach (var f in FilesOrdinal(formDir))
                {
                    if (!Path.GetFileName(f).EndsWith(FormJsonSuffix, StringComparison.Ordinal)) { Warn(f, "窗口目录下的未知文件，已忽略"); continue; }
                    var fm = LoadForm(f, "");
                    if (fm != null) P.Forms.Add(fm);
                }
                foreach (var sd in DirsOrdinal(formDir)) Warn(sd, "窗口目录不支持子目录，已忽略");
            }
        }

        private void Scan(string dir, string key, Dictionary<string, Folder> folders)
        {
            var f = new Folder { Key = key, Dir = dir, Order = ReadOrderList(Path.Combine(dir, _l.OrderList)) };
            folders[key] = f;
            if (key.Length == 0) P.RootOrder.AddRange(f.Order);
            else P.FolderOrders[key] = new List<string>(f.Order);

            foreach (var path in FilesOrdinal(dir))
            {
                var fn = Path.GetFileName(path);
                if (fn == _l.OrderList || (key.Length == 0 && fn == _l.DirsDesc)) continue;
                if (ClassKindOf(fn, out var kind, out var bn))
                {
                    var cls = LoadClassFile(path, kind);
                    if (cls == null) continue;
                    cls.Folder = key;
                    if (cls.Name.Length > 0 && SafeName(cls.Name) != bn) Warn(path, $"文件名与程序集名 {cls.Name} 不一致");
                    f.Classes.Add((bn, cls));
                }
                else if (fn.EndsWith(FormJsonSuffix, StringComparison.Ordinal))
                {
                    var fm = LoadForm(path, key);
                    if (fm != null) f.Forms.Add((fn.Substring(0, fn.Length - FormJsonSuffix.Length), fm));
                }
                else if (fn.EndsWith(".e.txt", StringComparison.Ordinal)) Warn(path, "未知的代码文件后缀（应为 .static/.class/.form.e.txt），已忽略");
                else Warn(path, "未知文件，已忽略");
            }
            foreach (var sd in DirsOrdinal(dir))
                Scan(sd, key.Length == 0 ? Path.GetFileName(sd) : key + "/" + Path.GetFileName(sd), folders);
        }

        // ------------------------------------------------------------ resource/
        private void LoadResources()
        {
            var dir = Path.Combine(_root, _l.Resource);
            if (!Directory.Exists(dir)) return;
            var descPath = Path.Combine(dir, _l.ResourceDesc);
            var desc = ReadJsonObject(descPath) ?? new JObject();
            var used = new HashSet<string>(StringComparer.Ordinal);

            foreach (var f in FilesOrdinal(dir))
            {
                var fn = Path.GetFileName(f);
                if (fn == _l.ResourceDesc) continue;
                TResourceKind kind;
                string name;
                if (Cut(fn, ".image.dat", out name)) kind = TResourceKind.Image;
                else if (Cut(fn, ".sound.dat", out name)) kind = TResourceKind.Sound;
                else if (Cut(fn, ".png", out name)) kind = TResourceKind.Image;
                else if (Cut(fn, ".txt", out name)) kind = TResourceKind.LongText;
                else { Warn(f, "未知的资源文件扩展名，已忽略"); continue; }

                var data = ReadBytes(f);
                if (data == null) continue;
                var r = new TResource { File = Rel(f), Name = name, Kind = kind, Data = data };

                // desc 键是资源原名，文件名经过 SafeName；先精确匹配，再按 SafeName 匹配
                string? key = desc.ContainsKey(name) ? name : desc.Keys.FirstOrDefault(k => !used.Contains(k) && SafeName(k) == name);
                if (key != null && used.Add(key))
                {
                    if (key != name) r.Name = key;
                    if (desc[key] is JObject d)
                    {
                        void Bad(string m) => Err(descPath, $"{key}：{m}");
                        if (d.TryGetValue("Comment", out var cm))
                        {
                            if (cm is string s) r.Comment = s.Length == 0 ? null : s;
                            else if (cm != null) Bad("Comment 应为字符串");
                        }
                        if (d.TryGetValue("IsPublic", out var ip))
                        {
                            if (ip is bool b) r.Public = b;
                            else if (ip != null) Bad("IsPublic 应为布尔值");
                        }
                    }
                    else Err(descPath, $"{key} 的值应为对象");
                }
                P.Resources.Add(r);
            }
            foreach (var k in desc.Keys.Where(k => !used.Contains(k)))
                Warn(descPath, $"资源 {k} 没有对应的文件");
            foreach (var sd in DirsOrdinal(dir)) Warn(sd, "资源目录不支持子目录，已忽略");
        }

        private static bool Cut(string fn, string suffix, out string name)
        {
            if (fn.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                name = fn.Substring(0, fn.Length - suffix.Length);
                return true;
            }
            name = "";
            return false;
        }
    }
}
