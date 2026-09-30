using QAQ.ETool.Core.Text;

namespace ImportCheck;

/// <summary>手工用例：边界输入的解析结果断言 + 回写往返</summary>
public static class ManualCases
{
    private static int _fail;

    private static void Eq<T>(string what, T actual, T expected)
    {
        if (EqualityComparer<T>.Default.Equals(actual, expected)) return;
        _fail++;
        Console.WriteLine($"  ✗ {what}: 实际 {Fmt(actual)}，预期 {Fmt(expected)}");
    }

    private static string Fmt(object? o) => o == null ? "null" : o is string s ? Rewriter.Show(s) : o.ToString()!;

    private static string Crlf(params string[] lines) => string.Join("\r\n", lines) + "\r\n";

    private static void RoundTrip(string what, string text, List<string> lines)
    {
        var back = string.Join("\r\n", lines) + "\r\n";
        if (back != text)
        {
            _fail++;
            Console.WriteLine($"  ✗ {what}: 回写不一致\n     原 {Rewriter.Show(text)}\n     回 {Rewriter.Show(back)}");
        }
    }

    public static int Run()
    {
        Console.WriteLine("################ 手工用例");

        // 1. SplitFields：备注含 ", " 与引号；中间空槽；引号感知
        var f = DeclParser.SplitFields("a, 整数型, 公开, 备注, \"x\", y", 4);
        Eq("SplitFields.备注", f[3], "备注, \"x\", y");
        Eq("SplitFields.公开", f[2], "公开");
        f = DeclParser.SplitFields("x, 整数型, , \"GetLastError\"", 6);
        Eq("SplitFields.空库名", f[2], null);
        Eq("SplitFields.入口名", f[3], "\"GetLastError\"");
        Eq("SplitFields.尾部", f[5], null);
        f = DeclParser.SplitFields("c, \"“a, b”\", 公开, 注, 释", 4);
        Eq("SplitFields.文本值", f[1], "\"“a, b”\"");
        Eq("SplitFields.文本值后备注", f[3], "注, 释");
        f = DeclParser.SplitFields("c, \"“q”\"x”\", , 备注", 4);   // 文本里含 ”" 但其后不是分隔 → 继续找
        Eq("SplitFields.文本含闭引号", f[1], "\"“q”\"x”\"");
        Eq("SplitFields.文本含闭引号.备注", f[3], "备注");
        f = DeclParser.SplitFields("", 3);
        Eq("SplitFields.空串", f.Count == 3 && f.All(x => x == null), true);

        // 2. 程序集：备注含 ", " 与引号 / 无名程序集变量 / 无名局部变量在最后 / 无参无体
        var errs = new List<string>();
        var warns = new List<string>();
        var t = Crlf(
            ".版本 2",
            ".支持库 spec",
            "",
            ".程序集 甲, , 公开, 备注, 带 \"引号\", 和逗号",
            "",
            ".程序集变量 b, 整数型, 静态 数组, \"0\", 注释, \"x\"",
            "",
            "",
            ".子程序 f, 文本型, 公开, 说明, 含 \", \" 分隔",
            "    .参数 p1, 整数型, 参考 可空 数组, 备, 注",
            "    .局部变量 x, 整数型, , \"2,3\"",
            "",
            "",
            "    返回 (“, ”)",
            "        ",
            "",
            ".子程序 g",
            "    ",
            "",
            ".子程序 h",
            "",
            ".子程序 k",
            "    .局部变量 y, 文本型",
            "",
            "    ",
            "");
        var c = DeclParser.ParseClassFile("t/甲.static.e.txt", t, TClassKind.Static, errs, warns);
        Eq("类.名称", c.Name, "甲");
        Eq("类.公开", c.Public, true);
        Eq("类.备注", c.Comment, "备注, 带 \"引号\", 和逗号");
        Eq("类.支持库", string.Join("|", c.LibLines), "spec");
        Eq("类.变量数", c.Vars.Count, 3);
        Eq("类.变量0无名", c.Vars[0].Name, "");
        Eq("类.变量1", c.Vars[1].Name + "|" + c.Vars[1].Static + c.Vars[1].Array + "|" + c.Vars[1].Dims + "|" + c.Vars[1].Comment, "b|TrueTrue|0|注释, \"x\"");
        Eq("类.变量2无名", c.Vars[2].Name, "");
        Eq("类.子程序数", c.Methods.Count, 4);
        var m = c.Methods[0];
        Eq("f.备注", m.Comment, "说明, 含 \", \" 分隔");
        Eq("f.返回", m.ReturnType, "文本型");
        Eq("f.参数", m.Params.Count == 1 && m.Params[0].ByRef && m.Params[0].Optional && m.Params[0].Array && m.Params[0].Comment == "备, 注", true);
        Eq("f.局部变量数", m.Locals.Count, 2);
        Eq("f.局部变量0维数", m.Locals[0].Dims, "2,3");
        Eq("f.无名局部变量在最后", m.Locals[1].Name, "");
        Eq("f.体", string.Join("|", m.Body), "    返回 (“, ”)|        ");
        Eq("f.体行号", m.BodyLine, 14);
        Eq("g.体", c.Methods[1].Body.Count == 1 && c.Methods[1].Locals.Count == 0, true);
        Eq("h.无体", c.Methods[2].Body.Count + c.Methods[2].Params.Count + c.Methods[2].Locals.Count, 0);
        Eq("k.局部变量", c.Methods[3].Locals.Count == 1 && c.Methods[3].Body.Count == 1, true);
        Eq("类.errors", errs.Count, 0);
        Eq("类.warnings", warns.Count, 0);
        // 回写：h 无体在 e2t 里写不出来（e2t 至少一行体），去掉 h 后比较
        var t2 = t.Replace(".子程序 h\r\n\r\n", "");
        var c2 = DeclParser.ParseClassFile("t/甲.static.e.txt", t2, TClassKind.Static, errs, warns);
        RoundTrip("类.回写", t2, Rewriter.Class(c2));

        // 只有无名程序集变量
        t = Crlf(".版本 2", "", ".程序集 乙", "", "", ".子程序 _启动", "    ", "");
        c = DeclParser.ParseClassFile("t/乙.static.e.txt", t, TClassKind.Static, errs, warns);
        Eq("乙.无名变量", c.Vars.Count == 1 && c.Vars[0].Name == "" && c.Vars[0].Line == 4, true);
        RoundTrip("乙.回写", t, Rewriter.Class(c));

        // 子程序只有无名局部变量
        t = Crlf(".版本 2", "", ".程序集 丙", ".子程序 a", "", "", "    ", "");
        c = DeclParser.ParseClassFile("t/丙.static.e.txt", t, TClassKind.Static, errs, warns);
        Eq("丙.无名局部", c.Methods[0].Locals.Count == 1 && c.Methods[0].Body.Count == 1, true);
        RoundTrip("丙.回写", t, Rewriter.Class(c));

        // LF 分行（手工编辑）
        t = ".版本 2\n\n.程序集 丁\n.子程序 a\n    .参数 x, 整数型\n\n    返回 ()\n\n";
        c = DeclParser.ParseClassFile("t/丁.static.e.txt", t, TClassKind.Static, errs, warns);
        Eq("丁.LF", c.Methods.Count == 1 && c.Methods[0].Params.Count == 1 && c.Methods[0].Body.Count == 1, true);

        // 3. 常量：“a, b” / 裸 CR 与 TAB / 无名常量
        var consts = new List<TConst>();
        t = Crlf(".版本 2", "", ".常量 c1, \"“a, b”\", 公开, 注, 释", "", ".常量 c2, \"“第一行\r第二行\t尾”\"", ".常量 c3, \"0\"", ".常量 c4");
        DeclParser.ParseConstFile("t/const.e.txt", t, consts, errs, warns);
        Eq("常量数", consts.Count, 5);
        Eq("c1.值", consts[0].RawValue, "“a, b”");
        Eq("c1.公开", consts[0].Public, true);
        Eq("c1.备注", consts[0].Comment, "注, 释");
        Eq("无名常量", consts[1].Name, "");
        Eq("c2.裸CR", consts[2].RawValue, "“第一行\r第二行\t尾”");
        Eq("c2.行号", consts[2].Line, 5);
        Eq("c3.值", consts[3].RawValue, "0");
        Eq("c4.无值", consts[4].RawValue, null);
        var ds = new TDeclSet();
        ds.Consts.AddRange(consts);
        RoundTrip("常量.回写", t, Rewriter.Consts(ds));

        // 4. DLL：入口名为空 / 库名为空
        var dlls = new List<TDll>();
        t = Crlf(".版本 2", "", ".DLL命令 x, 整数型, , \"GetLastError\"", "",
                 ".DLL命令 y, , \"k.dll\", , 公开, 备, 注", "    .参数 a, 整数型, 传址 数组, 说明", "    .参数 b", "");
        DeclParser.ParseDllFile("t/dll.e.txt", t, dlls, errs, warns);
        Eq("DLL数", dlls.Count, 2);
        Eq("x.库名", dlls[0].LibraryName, null);
        Eq("x.入口", dlls[0].EntryPoint, "GetLastError");
        Eq("y.入口空", dlls[1].EntryPoint, null);
        Eq("y.库名", dlls[1].LibraryName, "k.dll");
        Eq("y.备注", dlls[1].Comment, "备, 注");
        Eq("y.参数", dlls[1].Params.Count == 2 && dlls[1].Params[0].ByRef && dlls[1].Params[0].Array && dlls[1].Params[1].Type == null, true);
        ds = new TDeclSet();
        ds.Dlls.AddRange(dlls);
        RoundTrip("DLL.回写", t, Rewriter.Dlls(ds));

        // 5. 数据类型 + 块间多余空行（warning）
        var structs = new List<TStruct>();
        warns.Clear();
        t = Crlf(".版本 2", "", ".数据类型 S, 公开, 备注", "    .成员 a, 整数型, 传址, \"10\", 注", "", "", ".数据类型 T", "");
        DeclParser.ParseStructFile("t/struct.e.txt", t, structs, errs, warns);
        Eq("结构数", structs.Count, 2);
        Eq("S.成员", structs[0].Members.Count == 1 && structs[0].Members[0].ByRef && structs[0].Members[0].Dims == "10", true);
        Eq("多余空行 warning", warns.Count, 1);

        // 6. 全局变量 + 无名
        var globals = new List<TVar>();
        t = Crlf(".版本 2", "", "", ".全局变量 g, 文本型, 公开, \"0\", 注");
        DeclParser.ParseGlobalFile("t/global.e.txt", t, globals, errs, warns);
        Eq("全局数", globals.Count, 2);
        Eq("g", globals[1].Public && globals[1].Dims == "0" && globals[1].Comment == "注", true);

        Eq("以上用例 errors", errs.Count, 0);
        foreach (var e in errs) Console.WriteLine("    " + e);

        // 7. 备注含 CRLF（精易模块 dll.e.txt:2032 实例）：续行拼回备注
        dlls.Clear();
        warns.Clear();
        t = Crlf(".版本 2", "", ".DLL命令 E, 整数型, \"u.dll\", \"E\", 公开, 多行\r\n  续", "    .参数 a, 整数型, , 规定的\r\n  ", "    .参数 b, 整数型", "");
        DeclParser.ParseDllFile("t/dll2.e.txt", t, dlls, errs, warns);
        Eq("多行备注.DLL", dlls.Count == 1 ? dlls[0].Comment : "?", "多行\r\n  续");
        Eq("多行备注.参数", dlls.Count == 1 && dlls[0].Params.Count == 2 ? dlls[0].Params[0].Comment : "?", "规定的\r\n  ");
        ds = new TDeclSet();
        ds.Dlls.AddRange(dlls);
        RoundTrip("多行备注.回写", t, Rewriter.Dlls(ds));
        t = Crlf(".版本 2", "", ".程序集 戊, , , 类\r\n备注", ".程序集变量 v, 整数型, , , 变量\r\n备注", "",
                 ".子程序 a", "    .参数 p, 整数型, , 参\r\n数", "    .局部变量 q, 整数型, , , 局\r\n部", "", "    ", "");
        c = DeclParser.ParseClassFile("t/戊.static.e.txt", t, TClassKind.Static, errs, warns);
        Eq("多行备注.程序集", c.Comment, "类\r\n备注");
        Eq("多行备注.程序集变量", c.Vars.Count == 1 ? c.Vars[0].Comment : "?", "变量\r\n备注");
        Eq("多行备注.子程序声明", c.Methods.Count == 1 && c.Methods[0].Params.Count == 1 && c.Methods[0].Locals.Count == 1
            ? c.Methods[0].Params[0].Comment + "|" + c.Methods[0].Locals[0].Comment : "?", "参\r\n数|局\r\n部");
        Eq("多行备注.体", c.Methods.Count == 1 ? string.Join("|", c.Methods[0].Body) : "?", "    ");
        Eq("多行备注.warnings", warns.Count, 0);
        RoundTrip("多行备注.类回写", t, Rewriter.Class(c));
        Eq("以上用例 errors(2)", errs.Count, 0);

        // 8. 未知属性关键字 → error
        errs.Clear();
        DeclParser.ParseClassFile("t/坏.static.e.txt", Crlf(".版本 2", "", ".程序集 坏, , 私有", ".子程序 a, , 公开", "    .参数 x, 整数型, 传址", "", "    ", ""), TClassKind.Static, errs, warns);
        Eq("未知属性 errors", errs.Count, 2);
        foreach (var e in errs) Console.WriteLine("    (预期) " + e);

        Console.WriteLine(_fail == 0 ? "手工用例全部通过" : $"手工用例失败 {_fail} 项");
        return _fail;
    }
}
