using System.Text;
using QAQ.ETool.Core.Text;

namespace ImportCheck;

/// <summary>
/// 自检用回写器：按 Exporter 的版式把 TextProject 重新写成文本（程序集文件、四个声明文件、排序表）。
/// 返回 (相对工程根路径, 字节)。与 Exporter.RenderClass / WriteDecls / WriteClasses 逐项对应。
/// </summary>
public static class Rewriter
{
    public static string Fields(params string?[] xs)
    {
        int n = xs.Length;
        while (n > 0 && string.IsNullOrEmpty(xs[n - 1])) n--;
        return string.Join(", ", xs.Take(n).Select(x => x ?? ""));
    }

    private static string Q(string? s) => s == null ? "" : "\"" + s + "\"";
    private static string Join(params (string W, bool On)[] xs) => string.Join(" ", xs.Where(x => x.On).Select(x => x.W));
    private static string Decl(string kw, string name, Func<string> f) => name.Length == 0 ? "" : kw + f();
    private static string VarAttr(TVar v) => Join(("静态", v.Static), ("数组", v.Array));

    public static byte[] Text(List<string> lines) => TextFiles.Utf8Bom(string.Join("\r\n", lines) + "\r\n");

    public static List<string> Class(TClass c)
    {
        var o = new List<string> { ".版本 2" };
        o.AddRange(c.LibLines.Select(x => ".支持库 " + x));
        o.Add("");
        o.Add(".程序集 " + Fields(c.Name, c.Base, c.Public ? "公开" : "", c.Comment));
        foreach (var v in c.Vars)
            o.Add(Decl(".程序集变量 ", v.Name, () => Fields(v.Name, v.Type, VarAttr(v), Q(v.Dims), v.Comment)));
        if (c.Vars.Count > 0) o.Add("");
        foreach (var m in c.Methods)
        {
            o.Add(".子程序 " + Fields(m.Name, m.ReturnType, m.Public ? "公开" : "", m.Comment));
            foreach (var a in m.Params)
                o.Add("    .参数 " + Fields(a.Name, a.Type, Join(("参考", a.ByRef), ("可空", a.Optional), ("数组", a.Array)), a.Comment));
            foreach (var v in m.Locals)
                o.Add(Decl("    .局部变量 ", v.Name, () => Fields(v.Name, v.Type, VarAttr(v), Q(v.Dims), v.Comment)));
            if (m.Params.Count > 0 || m.Locals.Count > 0) o.Add("");
            o.AddRange(m.Body);
            o.Add("");
        }
        return o;
    }

    public static List<string> Consts(TDeclSet s)
    {
        var o = new List<string> { ".版本 2", "" };
        foreach (var c in s.Consts)
            o.Add(Decl(".常量 ", c.Name, () => Fields(c.Name, Q(c.RawValue), c.Public ? "公开" : "", c.Comment)));
        return o;
    }

    public static List<string> Globals(TDeclSet s)
    {
        var o = new List<string> { ".版本 2", "" };
        foreach (var g in s.Globals)
            o.Add(Decl(".全局变量 ", g.Name, () => Fields(g.Name, g.Type, g.Public ? "公开" : "", Q(g.Dims), g.Comment)));
        return o;
    }

    public static List<string> Structs(TDeclSet s)
    {
        var o = new List<string> { ".版本 2", "" };
        foreach (var t in s.Structs)
        {
            o.Add(".数据类型 " + Fields(t.Name, t.Public ? "公开" : "", t.Comment));
            foreach (var m in t.Members)
                o.Add("    .成员 " + Fields(m.Name, m.Type, m.ByRef ? "传址" : "", Q(m.Dims), m.Comment));
            o.Add("");
        }
        return o;
    }

    public static List<string> Dlls(TDeclSet s)
    {
        var o = new List<string> { ".版本 2", "" };
        foreach (var d in s.Dlls)
        {
            o.Add(".DLL命令 " + Fields(d.Name, d.ReturnType, Q(d.LibraryName), Q(d.EntryPoint), d.Public ? "公开" : "", d.Comment));
            foreach (var a in d.Params)
                o.Add("    .参数 " + Fields(a.Name, a.Type, Join(("传址", a.ByRef), ("数组", a.Array)), a.Comment));
            o.Add("");
        }
        return o;
    }

    private static string Suffix(TClassKind k) => k switch
    {
        TClassKind.Form => ".form.e.txt",
        TClassKind.Class => ".class.e.txt",
        _ => ".static.e.txt",
    };

    public static List<(string Rel, byte[] Data)> Rewrite(TextProject p, List<string> pathProblems)
    {
        var l = p.Layout;
        var r = new List<(string, byte[])>();

        void Decls(string prefix, TDeclSet s, bool module)
        {
            void Emit(string file, List<string> lines)
            {
                if (module && lines.Count <= 2) return;   // 模块内空文件不写（spec §2.3）
                r.Add((prefix + file, Text(lines)));
            }
            Emit(l.Const, Consts(s));
            Emit(l.Dll, Dlls(s));
            Emit(l.Struct, Structs(s));
            Emit(l.Global, Globals(s));
        }

        // 程序集：用 File 作为回写路径，同时用 Folder/Name/Kind 推导路径做一致性检查
        foreach (var c in p.Classes)
        {
            var expect = l.Class + "/" + (c.Folder.Length == 0 ? "" : c.Folder + "/") + Importer.SafeName(c.Name) + Suffix(c.Kind);
            if (expect != c.File) pathProblems.Add($"{c.File}: 由 Folder/Name/Kind 推导出的路径是 {expect}");
            r.Add((c.File, Text(Class(c))));
        }
        Decls("", p.Main, false);
        foreach (var m in p.Modules)
        {
            var md = l.Module + "/" + m.Name + "/";
            Decls(md, m.Decls, true);
            foreach (var c in m.Classes)
            {
                var expect = md + l.Class + "/" + Importer.SafeName(c.Name) + Suffix(c.Kind);
                if (expect != c.File) pathProblems.Add($"{c.File}: 推导路径 {expect}");
                r.Add((c.File, Text(Class(c))));
            }
        }

        // 排序表：UTF-8 BOM，CRLF 连接，末尾无换行
        r.Add((l.Class + "/" + l.OrderList, TextFiles.Utf8Bom(string.Join("\r\n", p.RootOrder))));
        foreach (var kv in p.FolderOrders)
            r.Add((l.Class + "/" + kv.Key + "/" + l.OrderList, TextFiles.Utf8Bom(string.Join("\r\n", kv.Value))));
        return r;
    }

    /// <summary>显示用：控制字符转义</summary>
    public static string Show(string s)
    {
        var sb = new StringBuilder();
        foreach (var ch in s)
        {
            if (ch == '\r') sb.Append("\\r");
            else if (ch == '\t') sb.Append("\\t");
            else if (ch < 0x20) sb.Append($"\\x{(int)ch:X2}");
            else sb.Append(ch);
            if (sb.Length > 120) { sb.Append('…'); break; }
        }
        return "«" + sb + "»";
    }
}
