namespace QAQ.ETool.Core.Text;

/// <summary>
/// t2e：*.e.txt 声明解析（Exporter.RenderClass / WriteDecls 的逆）。只做词法级拆分，不做名称解析。
/// 版式依据 docs/e2txt-format-spec.md §5.1、§5.3、§6。
/// 报错格式：「相对路径:行号: 消息」，errors 收集格式错误，warnings（可选）收集可容忍的版式偏差。
/// </summary>
public static class DeclParser
{
    // ================================================================ 分行 / 字段
    /// <summary>
    /// 分行：只要出现 "\r\n" 就只按 "\r\n" 分（常量值里的裸 CR / TAB 保留在行内）；
    /// 完全没有 "\r\n" 时按 "\n" 分（容忍手工 LF 编辑）。最后一个换行之后的空串丢掉。
    /// </summary>
    public static string[] SplitLines(string text)
    {
        if (text.Length == 0) return Array.Empty<string>();
        var parts = text.Contains("\r\n", StringComparison.Ordinal) ? text.Split("\r\n") : text.Split('\n');
        return parts.Length > 0 && parts[^1].Length == 0 ? parts[..^1] : parts;
    }

    /// <summary>
    /// 按固定槽位数拆分声明字段（spec §5.3）：字段以 ", " 分隔，最后一槽吞掉余下全部文本。
    /// 非末槽若以半角 " 开头则引号感知：以 "“ 开头找配对的 ”"，否则找下一个 "；
    /// 优先取「其后是行尾或 ", "」的那个闭引号。返回原文（**保留外层引号**，用 <see cref="Unquote"/> 去掉），
    /// 空串 / 缺省槽 → null。结果长度恒为 slots。
    /// </summary>
    public static List<string?> SplitFields(string s, int slots)
    {
        var r = new List<string?>(slots);
        int n = s.Length, pos = 0;
        bool done = false;
        for (int k = 0; k < slots; k++)
        {
            if (done) { r.Add(null); continue; }
            if (k == slots - 1) { r.Add(Nz(s.Substring(pos))); done = true; continue; }
            int end = FieldEnd(s, pos);
            r.Add(Nz(s.Substring(pos, end - pos)));
            if (end >= n) done = true;
            else pos = end + 2;   // 跳过 ", "
        }
        return r;
    }

    /// <summary>字段结束位置（指向分隔符 ", " 或串尾）</summary>
    private static int FieldEnd(string s, int pos)
    {
        int n = s.Length;
        if (pos < n && s[pos] == '"')
        {
            bool text = pos + 1 < n && s[pos + 1] == '“';
            string closer = text ? "”\"" : "\"";
            int from = text ? pos + 2 : pos + 1;
            int first = -1;
            for (int i = s.IndexOf(closer, from, StringComparison.Ordinal); i >= 0;
                 i = s.IndexOf(closer, i + 1, StringComparison.Ordinal))
            {
                int e = i + closer.Length;
                if (first < 0) first = e;
                if (e == n || string.CompareOrdinal(s, e, ", ", 0, 2) == 0) return e;
            }
            if (first >= 0)
            {
                int c = s.IndexOf(", ", first, StringComparison.Ordinal);
                return c < 0 ? n : c;
            }
            // 没有闭引号：按普通字段处理
        }
        int sep = s.IndexOf(", ", pos, StringComparison.Ordinal);
        return sep < 0 ? n : sep;
    }

    private static string? Nz(string s) => s.Length == 0 ? null : s;

    /// <summary>去掉一层外层半角引号（"0" → 0，"“文本”" → “文本”）；没有引号则原样</summary>
    public static string? Unquote(string? x) =>
        x != null && x.Length >= 2 && x[0] == '"' && x[^1] == '"' ? x.Substring(1, x.Length - 2) : x;

    // ================================================================ 关键字
    /// <summary>line == kw 或以 kw + " " 开头（顶格）；rest = 其后的字段文本</summary>
    private static bool IsKw(string line, string kw, out string rest)
    {
        if (line.Length == kw.Length && line == kw) { rest = ""; return true; }
        if (line.Length > kw.Length && line[kw.Length] == ' ' && line.StartsWith(kw, StringComparison.Ordinal))
        {
            rest = line.Substring(kw.Length + 1);
            return true;
        }
        rest = "";
        return false;
    }

    /// <summary>缩进行关键字（.参数 / .局部变量 / .成员）：去掉行首空格/TAB 后匹配</summary>
    private static bool IsSubKw(string line, string kw, out string rest) =>
        IsKw(line.TrimStart(' ', '\t'), kw, out rest);

    private sealed class Ctx
    {
        public required string File;
        public required List<string> Errors;
        public List<string>? Warnings;
        public void Err(int idx, string msg) => Errors.Add($"{File}:{idx + 1}: {msg}");
        public void Warn(int idx, string msg) => Warnings?.Add($"{File}:{idx + 1}: {msg}");
    }

    private static readonly string[] WPublic = { "公开" };
    private static readonly string[] WVar = { "静态", "数组" };
    private static readonly string[] WParam = { "参考", "可空", "数组" };
    private static readonly string[] WDllParam = { "传址", "数组" };
    private static readonly string[] WMember = { "传址" };

    /// <summary>属性槽：单空格连接的关键字；未知关键字 → error</summary>
    private static HashSet<string> Words(string? slot, string[] allowed, Ctx c, int idx, string what)
    {
        var set = new HashSet<string>(StringComparer.Ordinal);
        if (slot == null) return set;
        foreach (var w in slot.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            if (Array.IndexOf(allowed, w) < 0)
                c.Err(idx, $"{what}的属性槽出现未知关键字「{w}」（允许：{string.Join("/", allowed)}）");
            else set.Add(w);
        }
        return set;
    }

    private static string Short(string s) => s.Length > 60 ? s.Substring(0, 60) + "…" : s;

    // ================================================================ 单行声明
    /// <summary>名称, 类型, 第3槽, 数组维数, 备注（程序集变量/局部变量/全局变量/成员）</summary>
    private static TVar ParseVar(string f, int idx, string[] slot3, Ctx c, string what)
    {
        var x = SplitFields(f, 5);
        var v = new TVar { Line = idx + 1, Name = x[0] ?? "", Type = x[1], Dims = Unquote(x[3]), Comment = x[4] };
        var w = Words(x[2], slot3, c, idx, what);
        v.Static = w.Contains("静态");
        v.Array = w.Contains("数组");
        v.Public = w.Contains("公开");
        v.ByRef = w.Contains("传址");
        return v;
    }

    /// <summary>名称, 类型, 属性, 备注（子程序参数 / DLL 参数）</summary>
    private static TParam ParseParam(string f, int idx, bool dll, Ctx c)
    {
        var x = SplitFields(f, 4);
        var p = new TParam { Line = idx + 1, Name = x[0] ?? "", Type = x[1], Comment = x[3] };
        var w = Words(x[2], dll ? WDllParam : WParam, c, idx, dll ? "DLL 参数" : "参数");
        p.ByRef = w.Contains(dll ? "传址" : "参考");
        p.Optional = w.Contains("可空");
        p.Array = w.Contains("数组");
        return p;
    }

    private static bool Pub(string? slot, Ctx c, int idx, string what) =>
        Words(slot, WPublic, c, idx, what).Contains("公开");

    /// <summary>跳过 `.版本 2` + 一个空行；返回下一行下标</summary>
    private static int Header(string[] L, Ctx c)
    {
        int i = 0;
        if (L.Length == 0) { c.Warn(0, "空文件（缺少 .版本 2 文件头）"); return 0; }
        if (IsKw(L[0], ".版本", out var ver))
        {
            if (ver != "2") c.Warn(0, $".版本 {ver}（预期 2）");
            i = 1;
        }
        else c.Warn(0, "缺少文件头 .版本 2");
        return i;
    }

    /// <summary>
    /// 备注里可能含真实的 CRLF（e2t 原样写出，如 精易模块/dll.e.txt 的 `…规定的\r\n␠␠`）。
    /// 声明行之后紧跟的非空、且不是任何合法关键字开头的行视为上一行的续行，用 CRLF 拼回。
    /// i 指向当前声明行，返回时指向最后一个被吞掉的续行。
    /// 不适用于 .子程序 行（其后紧跟的缩进行就是子程序体，无法区分）。
    /// </summary>
    private static string Cont(string[] L, ref int i, string text, Func<string, bool> isKw, Func<int, bool>? isSep = null)
    {
        while (i + 1 < L.Length && L[i + 1].Length != 0 && !isKw(L[i + 1]) && !(isSep?.Invoke(i + 1) ?? false))
            text += "\r\n" + L[++i];
        return text;
    }

    /// <summary>
    /// 声明区的「仅含空白的分隔行」：手写/AI 生成的文本常把声明区后的分隔空行写成缩进空白（如 4 个空格），e2txt 也按分隔处理。
    /// 为不误伤备注里的真实 CRLF 续行（如 `…\r\n␠␠`），只在「至少 4 个空白、且下一行是代码体/空行/.子程序/文件末尾」时成立。
    /// </summary>
    private static bool IsWsSep(string[] L, int j, Func<string, bool> isMethod)
    {
        var s = L[j];
        if (s.Length < 4 || !string.IsNullOrWhiteSpace(s)) return false;
        if (j + 1 >= L.Length) return true;
        var nx = L[j + 1];
        // 下一行是空行时不算：e2t 自己的产物在备注续行后必跟真正的空分隔行，那种「␠␠␠␠」是备注内容
        return (nx.StartsWith("    ", StringComparison.Ordinal) && !string.IsNullOrWhiteSpace(nx)) || isMethod(nx);
    }

    /// <summary>一段连续无法识别的行只报一次</summary>
    private static int SkipUnknown(string[] L, int i, Func<string, bool> isStart, Ctx c)
    {
        int s = i;
        while (i < L.Length && L[i].Length != 0 && !isStart(L[i])) i++;
        if (i == s) i++;
        c.Err(s, i - s == 1 ? $"无法识别的行：{Short(L[s])}" : $"无法识别的行（连续 {i - s} 行，已忽略）：{Short(L[s])}");
        return i;
    }

    // ================================================================ 程序集文件
    public static TClass ParseClassFile(string relFile, string text, TClassKind kind, List<string> errors) =>
        ParseClassFile(relFile, text, kind, errors, null);

    public static TClass ParseClassFile(string relFile, string text, TClassKind kind, List<string> errors, List<string>? warnings)
    {
        var c = new Ctx { File = relFile, Errors = errors, Warnings = warnings };
        var cls = new TClass { File = relFile, Kind = kind };
        var L = SplitLines(text);
        int n = L.Length;
        int i = Header(L, c);

        // ---- 文件头：.支持库* + 空行
        while (i < n && IsKw(L[i], ".支持库", out var lib)) { cls.LibLines.Add(lib.Trim()); i++; }
        if (i < n && L[i].Length == 0) i++;
        else if (i < n) c.Warn(i, "文件头后缺少空行");
        while (i < n && L[i].Length == 0) { c.Warn(i, "多余的空行，已忽略"); i++; }

        // ---- .程序集
        // 程序集头与程序集变量区里，合法的下一行只有 .程序集变量 / .子程序 / 空行，其余视为备注续行
        bool ZoneKw(string s) => IsKw(s, ".程序集变量", out _) || IsKw(s, ".子程序", out _);
        if (i < n && IsKw(L[i], ".程序集", out var cf))
        {
            int at = i;
            cf = Cont(L, ref i, cf, ZoneKw);
            var x = SplitFields(cf, 4);
            cls.Line = at + 1;
            cls.Name = x[0] ?? "";
            cls.Base = x[1];
            cls.Public = Pub(x[2], c, at, "程序集");
            cls.Comment = x[3];
            if (cls.Name.Length == 0) c.Err(at, "程序集名称为空");
            i++;
        }
        else c.Err(Math.Min(i, Math.Max(n - 1, 0)), "缺少 .程序集 行");

        // ---- 程序集变量区：「.程序集变量」与空行（无名变量）；最后一个空行是与 .子程序 的分隔
        var zone = new List<(int Idx, string? Fields)>();   // Fields == null → 空行
        while (i < n)
        {
            if (L[i].Length == 0 || (zone.Count > 0 && zone[^1].Fields != null && IsWsSep(L, i, s => IsKw(s, ".子程序", out _)))) zone.Add((i, null));
            else if (IsKw(L[i], ".程序集变量", out var vf))
            {
                int at = i;
                zone.Add((at, Cont(L, ref i, vf, ZoneKw, j => IsWsSep(L, j, s => IsKw(s, ".子程序", out _)))));
            }
            else break;
            i++;
        }
        if (zone.Count > 0)
        {
            if (zone[^1].Fields == null) zone.RemoveAt(zone.Count - 1);
            else c.Warn(zone[^1].Idx, "程序集变量后缺少分隔空行");
        }
        foreach (var (k, vf) in zone)
            cls.Vars.Add(vf == null ? new TVar { Line = k + 1 } : ParseVar(vf, k, WVar, c, "程序集变量"));

        // ---- 子程序
        bool IsMethod(string s) => IsKw(s, ".子程序", out _);
        while (i < n)
        {
            if (L[i].Length == 0) { c.Warn(i, "多余的空行，已忽略"); i++; continue; }
            if (!IsKw(L[i], ".子程序", out var mf)) { i = SkipUnknown(L, i, IsMethod, c); continue; }

            var mx = SplitFields(mf, 4);
            var m = new TMethod
            {
                Line = i + 1,
                Name = mx[0] ?? "",
                ReturnType = mx[1],
                Public = Pub(mx[2], c, i, "子程序"),
                Comment = mx[3],
            };
            if (m.Name.Length == 0) c.Err(i, "子程序名称为空");
            cls.Methods.Add(m);
            i++;

            // 声明区：.参数 / .局部变量 / 空行（无名局部变量或分隔）。
            // e2t 在声明后必写分隔空行，所以紧跟声明行的非空、非关键字行只能是备注续行（备注含 CRLF）。
            bool DeclKw(string s) => IsSubKw(s, ".参数", out _) || IsSubKw(s, ".局部变量", out _) || IsMethod(s);
            var decls = new List<(int Idx, bool Param, string? Fields)>();   // Fields == null → 空行
            while (i < n)
            {
                bool isParam = IsSubKw(L[i], ".参数", out var df);
                // 仅紧跟声明行的空白行才可能是分隔；真空行之后的「␠␠␠␠」是子程序体的空语句行
                if (L[i].Length == 0 || (decls.Count > 0 && decls[^1].Fields != null && IsWsSep(L, i, IsMethod))) decls.Add((i, false, null));
                else if (isParam || IsSubKw(L[i], ".局部变量", out df))
                {
                    int at = i;
                    df = Cont(L, ref i, df, DeclKw, j => IsWsSep(L, j, IsMethod));
                    if (i > at && L[at + 1].StartsWith("    ", StringComparison.Ordinal))
                        c.Warn(at + 1, "声明后的行被当作备注续行，但它像是代码（声明区后是否缺少分隔空行？）");
                    decls.Add((at, isParam, df));
                }
                else break;
                i++;
            }
            bool hasBody = i < n && !IsMethod(L[i]);
            if (hasBody)
            {
                if (decls.Count > 0)
                {
                    if (decls[^1].Fields == null) decls.RemoveAt(decls.Count - 1);
                    else c.Warn(decls[^1].Idx, "参数/局部变量后缺少分隔空行");
                }
                m.BodyLine = i + 1;
                while (i < n && L[i].Length != 0 && !IsMethod(L[i])) m.Body.Add(L[i++]);
                if (i < n && L[i].Length == 0) i++;
                else if (i < n) c.Warn(i, "子程序之间缺少分隔空行");
            }
            else
            {
                // 无体：最后一个空行是子程序分隔；若还剩声明且末尾仍是空行，那是声明区分隔
                if (decls.Count > 0 && decls[^1].Fields == null) decls.RemoveAt(decls.Count - 1);
                if (decls.Count > 0 && decls[^1].Fields == null) decls.RemoveAt(decls.Count - 1);
            }

            foreach (var (k, isParam, df) in decls)
            {
                if (df == null) m.Locals.Add(new TVar { Line = k + 1 });
                else if (isParam)
                {
                    if (m.Locals.Count > 0) c.Warn(k, ".参数 出现在 .局部变量 之后");
                    m.Params.Add(ParseParam(df, k, false, c));
                }
                else m.Locals.Add(ParseVar(df, k, WVar, c, "局部变量"));
            }
        }
        return cls;
    }

    // ================================================================ const / global：每行一条，空行 = 无名
    public static void ParseConstFile(string relFile, string text, List<TConst> into, List<string> errors) =>
        ParseConstFile(relFile, text, into, errors, null);

    public static void ParseConstFile(string relFile, string text, List<TConst> into, List<string> errors, List<string>? warnings)
    {
        var c = new Ctx { File = relFile, Errors = errors, Warnings = warnings };
        var L = SplitLines(text);
        int i = HeaderBlank(L, c);
        for (; i < L.Length; i++)
        {
            if (L[i].Length == 0) { into.Add(new TConst { Line = i + 1 }); continue; }
            if (!IsKw(L[i], ".常量", out var f)) { c.Err(i, $"无法识别的行：{Short(L[i])}"); continue; }
            int at = i;
            var x = SplitFields(Cont(L, ref i, f, s => IsKw(s, ".常量", out _)), 4);
            into.Add(new TConst
            {
                Line = at + 1,
                Name = x[0] ?? "",
                RawValue = Unquote(x[1]),
                Public = Pub(x[2], c, at, "常量"),
                Comment = x[3],
            });
        }
    }

    public static void ParseGlobalFile(string relFile, string text, List<TVar> into, List<string> errors) =>
        ParseGlobalFile(relFile, text, into, errors, null);

    public static void ParseGlobalFile(string relFile, string text, List<TVar> into, List<string> errors, List<string>? warnings)
    {
        var c = new Ctx { File = relFile, Errors = errors, Warnings = warnings };
        var L = SplitLines(text);
        int i = HeaderBlank(L, c);
        for (; i < L.Length; i++)
        {
            if (L[i].Length == 0) { into.Add(new TVar { Line = i + 1 }); continue; }
            if (!IsKw(L[i], ".全局变量", out var f)) { c.Err(i, $"无法识别的行：{Short(L[i])}"); continue; }
            int at = i;
            into.Add(ParseVar(Cont(L, ref i, f, s => IsKw(s, ".全局变量", out _)), at, WPublic, c, "全局变量"));
        }
    }

    /// <summary>文件头 `.版本 2` + 恰好一个空行（其后的空行属于内容）</summary>
    private static int HeaderBlank(string[] L, Ctx c)
    {
        int i = Header(L, c);
        if (i < L.Length && L[i].Length == 0) i++;
        else if (i < L.Length) c.Warn(i, "文件头后缺少空行");
        return i;
    }

    // ================================================================ struct / dll：块 + 块后空行
    public static void ParseStructFile(string relFile, string text, List<TStruct> into, List<string> errors) =>
        ParseStructFile(relFile, text, into, errors, null);

    public static void ParseStructFile(string relFile, string text, List<TStruct> into, List<string> errors, List<string>? warnings)
    {
        var c = new Ctx { File = relFile, Errors = errors, Warnings = warnings };
        var L = SplitLines(text);
        int n = L.Length, i = HeaderBlank(L, c);
        bool IsHead(string s) => IsKw(s, ".数据类型", out _);
        bool AnyKw(string s) => IsHead(s) || IsSubKw(s, ".成员", out _);
        while (i < n)
        {
            if (L[i].Length == 0) { c.Warn(i, "多余的空行，已忽略"); i++; continue; }
            if (!IsKw(L[i], ".数据类型", out var f)) { i = SkipUnknown(L, i, IsHead, c); continue; }
            int at = i;
            var x = SplitFields(Cont(L, ref i, f, AnyKw), 3);
            var s = new TStruct { Line = at + 1, Name = x[0] ?? "", Public = Pub(x[1], c, at, "数据类型"), Comment = x[2] };
            if (s.Name.Length == 0) c.Err(at, "数据类型名称为空");
            into.Add(s);
            i++;
            while (i < n && IsSubKw(L[i], ".成员", out var mf))
            {
                at = i;
                s.Members.Add(ParseVar(Cont(L, ref i, mf, AnyKw), at, WMember, c, "成员"));
                i++;
            }
            if (i < n && L[i].Length == 0) i++;
            else if (i < n) c.Warn(i, "数据类型块后缺少空行");
        }
    }

    public static void ParseDllFile(string relFile, string text, List<TDll> into, List<string> errors) =>
        ParseDllFile(relFile, text, into, errors, null);

    public static void ParseDllFile(string relFile, string text, List<TDll> into, List<string> errors, List<string>? warnings)
    {
        var c = new Ctx { File = relFile, Errors = errors, Warnings = warnings };
        var L = SplitLines(text);
        int n = L.Length, i = HeaderBlank(L, c);
        bool IsHead(string s) => IsKw(s, ".DLL命令", out _);
        bool AnyKw(string s) => IsHead(s) || IsSubKw(s, ".参数", out _);
        while (i < n)
        {
            if (L[i].Length == 0) { c.Warn(i, "多余的空行，已忽略"); i++; continue; }
            if (!IsKw(L[i], ".DLL命令", out var f)) { i = SkipUnknown(L, i, IsHead, c); continue; }
            int at = i;
            var x = SplitFields(Cont(L, ref i, f, AnyKw), 6);
            var d = new TDll
            {
                Line = at + 1,
                Name = x[0] ?? "",
                ReturnType = x[1],
                LibraryName = Unquote(x[2]),
                EntryPoint = Unquote(x[3]),
                Public = Pub(x[4], c, at, "DLL命令"),
                Comment = x[5],
            };
            if (d.Name.Length == 0) c.Err(at, "DLL命令名称为空");
            into.Add(d);
            i++;
            while (i < n && IsSubKw(L[i], ".参数", out var pf))
            {
                at = i;
                d.Params.Add(ParseParam(Cont(L, ref i, pf, AnyKw), at, true, c));
                i++;
            }
            if (i < n && L[i].Length == 0) i++;
            else if (i < n) c.Warn(i, "DLL命令块后缺少空行");
        }
    }
}
