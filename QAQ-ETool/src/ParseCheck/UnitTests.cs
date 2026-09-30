using System.Globalization;
using System.Text;
using QAQ.ETool.Core.Text;
using QAQ.ETool.Core.Text.Ast;

/// <summary>手工单元用例（直接断言，失败打印原因）</summary>
internal static class UnitTests
{
    private static int _fail, _pass;

    private static void Check(bool ok, string name, string detail = "")
    {
        if (ok) { _pass++; return; }
        _fail++;
        Console.WriteLine($"[失败] {name} {detail}");
    }

    private static void Eq(string actual, string expected, string name) =>
        Check(actual == expected, name, $"\n    期望: {expected}\n    实际: {actual}");

    /// <summary>结构转储：(命令号 操作数…)，^ 表示显式括号</summary>
    public static string Dump(Expr? e)
    {
        string p = e is { Parenthesized: true } ? "^" : "";
        return p + e switch
        {
            null => "_",
            OpExpr op => "(" + op.Cmd + " " + string.Join(" ", op.Operands.Select(Dump)) + ")",
            NumberLit n => "n:" + n.Raw + "=" + n.Value.ToString("R", CultureInfo.InvariantCulture),
            StringLit s => "s:" + s.Value,
            BoolLit b => b.Value ? "真" : "假",
            DateLit d => "d:" + d.Value.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture),
            ConstRef c => "#" + c.Name + (c.Member != null ? "|" + c.Member : ""),
            MethodPtr m => "&" + m.Name,
            NameRef r => r.Name,
            CallExpr c => "call:" + (c.Target != null ? Dump(c.Target) + "->" : "") + c.Name + "[" + string.Join(",", c.Args.Select(Dump)) + "]",
            MemberExpr m => "mem(" + Dump(m.Target) + "," + m.Name + ")",
            IndexExpr i => "idx(" + Dump(i.Target) + "," + Dump(i.Index) + ")",
            ArrayLit a => "{" + string.Join(",", a.Items.Select(Dump)) + "}",
            _ => "?",
        };
    }

    private static Expr? Expr(string s, List<CodeDiagnostic>? d = null) => CodeParser.ParseExpression(s, 1, "t", d ?? new());

    /// <summary>解析一条语句（体只有一行）</summary>
    private static Stmt Stmt1(string code, List<CodeDiagnostic>? d = null) =>
        CodeParser.ParseBody(new[] { "    " + code }, 1, "t", d ?? new())[0];

    private static void ExprCase(string src, string dump, string? printed = null)
    {
        var d = new List<CodeDiagnostic>();
        var e = Expr(src, d);
        Eq(Dump(e), dump, $"结构 `{src}`");
        Check(d.Count == 0, $"无 diag `{src}`", string.Join("; ", d));
        if (printed != null) Eq(CodePrinter.Print(e), printed, $"打印 `{src}`");
    }

    private static void StmtCase(string src, string dump, string? printed = null)
    {
        var d = new List<CodeDiagnostic>();
        var s = Stmt1(src, d);
        var es = s as ExprStmt;
        Eq(es == null ? s.GetType().Name : Dump(es.Expr), dump, $"语句结构 `{src}`");
        Check(d.Count == 0, $"无 diag `{src}`", string.Join("; ", d));
        if (printed != null) Eq(string.Join("\n", CodePrinter.PrintBody(new List<Stmt> { s })), "    " + printed, $"语句打印 `{src}`");
    }

    /// <summary>体的解析 → 打印 恒等，并返回语法树</summary>
    private static List<Stmt> BodyRoundTrip(string name, string[] lines, int expectDiags = 0, string[]? expectPrinted = null)
    {
        var d = new List<CodeDiagnostic>();
        var ast = CodeParser.ParseBody(lines, 10, "t", d);
        var printed = CodePrinter.PrintBody(ast);
        Eq(string.Join("\n", printed), string.Join("\n", expectPrinted ?? lines), $"体打印 {name}");
        Check(d.Count == expectDiags, $"diag 数 {name}", $"期望 {expectDiags}，实际 {d.Count}: {string.Join("; ", d)}");
        return ast;
    }

    public static int Run()
    {
        // ---- 半角运算符
        ExprCase("a + b * c - d", "(20 (19 a (15 b c)) d)", "a ＋ b × c － d");
        ExprCase("a / b \\ c % d", "(18 (17 (16 a b) c) d)", "a ÷ b ＼ c ％ d");
        ExprCase("a <> b", "(39 a b)", "a ≠ b");
        ExprCase("a != b", "(39 a b)");
        ExprCase("a == b", "(38 a b)");
        ExprCase("a = b", "(38 a b)");            // 表达式里的 = 是比较
        ExprCase("a <= b", "(42 a b)", "a ≤ b");
        ExprCase("a >= b", "(43 a b)", "a ≥ b");
        ExprCase("a < b", "(40 a b)");
        ExprCase("a > b", "(41 a b)");
        ExprCase("a && b || c", "(46 (45 a b) c)", "a 且 b 或 c");
        ExprCase("a ≈ b", "(44 a b)");

        // ---- 优先级 / 扁平化 / 括号
        ExprCase("a 或 b 且 c", "(46 a (45 b c))", "a 或 (b 且 c)");
        ExprCase("(a 或 b) 且 c", "(45 ^(46 a b) c)", "(a 或 b) 且 c");
        ExprCase("a 且 b 或 c", "(46 (45 a b) c)", "a 且 b 或 c");
        ExprCase("a ＋ b － c", "(20 (19 a b) c)", "a ＋ b － c");
        ExprCase("a － (b － c)", "(20 a ^(20 b c))", "a － (b － c)");
        ExprCase("a － b － c", "(20 a b c)", "a － b － c");
        ExprCase("(a － b) － c", "(20 ^(20 a b) c)", "a － b － c");   // 显式括号不合并；打印按规则省括号
        ExprCase("a × b ÷ c", "(16 (15 a b) c)");
        ExprCase("(a ＋ b) × c", "(15 ^(19 a b) c)", "(a ＋ b) × c");
        ExprCase("a ＝ b ＝ c", "(38 (38 a b) c)");
        ExprCase("a ＞ 0 且 b ＜ 1 且 c", "(45 (41 a n:0=0) (40 b n:1=1) c)");
        ExprCase("或者 ＝ 且慢", "(38 或者 且慢)");

        // ---- 负号
        ExprCase("－x", "(21 x)", "－x");
        ExprCase("-x", "(21 x)", "－x");
        ExprCase("-1", "n:-1=-1", "-1");
        ExprCase("－1", "(21 n:1=1)", "－1");
        ExprCase("- 1", "(21 n:1=1)");
        ExprCase("a -1", "(20 a n:1=1)");
        ExprCase("a ≠ -1", "(39 a n:-1=-1)", "a ≠ -1");
        ExprCase("－(a ＋ b)", "(21 ^(19 a b))", "－(a ＋ b)");
        ExprCase("－－a", "(21 (21 a))", "－－a");
        ExprCase("a × －b", "(15 a (21 b))", "a × －b");

        // ---- 字面量
        ExprCase("1.16444736e+017", "n:1.16444736e+017=1.16444736E+17", "1.16444736e+017");
        ExprCase("1.5", "n:1.5=1.5");
        ExprCase(".5", "n:.5=0.5");
        ExprCase("“a, (b) ' \"c”", "s:a, (b) ' \"c");
        ExprCase("“”", "s:");
        ExprCase("真 或 假", "(46 真 假)");
        ExprCase("[2020年1月2日3时]", "d:2020-01-02 03:00:00", "[2020年1月2日3时]");
        ExprCase("[1970年1月1日8时]", "d:1970-01-01 08:00:00", "[1970年1月1日8时]");
        ExprCase("[2020年1月2日3时4分5秒]", "d:2020-01-02 03:04:05", "[2020年1月2日3时4分5秒]");
        ExprCase("a ＝ [2020年1月2日]", "(38 a d:2020-01-02 00:00:00)");
        ExprCase("#常量", "#常量");
        ExprCase("#类型.成员", "#类型|成员");
        ExprCase("&子程序1", "&子程序1");
        ExprCase("{ }", "{}", "{ }");
        ExprCase("{ 1, , 3 }", "{n:1=1,_,n:3=3}", "{ 1, , 3 }");
        ExprCase("{ #a, #b }", "{#a,#b}");

        // ---- 调用 / 成员 / 下标
        ExprCase("f ()", "call:f[]", "f ()");
        ExprCase("f ( )", "call:f[]", "f ()");
        ExprCase("f (, )", "call:f[_,_]", "f (, )");
        ExprCase("f (, x)", "call:f[_,x]", "f (, x)");
        ExprCase("f (x, )", "call:f[x,_]", "f (x, )");
        ExprCase("插入表项 (, , , , , )", "call:插入表项[_,_,_,_,_,_]", "插入表项 (, , , , , )");
        ExprCase("f(x)", "call:f[x]", "f (x)");
        ExprCase("a.b (x)", "call:a->b[x]");
        ExprCase("a.b.c (x)", "call:mem(a,b)->c[x]", "a.b.c (x)");
        ExprCase("a [1].b", "mem(idx(a,n:1=1),b)", "a [1].b");
        ExprCase("f () [i ＋ 1]", "idx(call:f[],(19 i n:1=1))", "f () [i ＋ 1]");
        ExprCase("取文本右边 (path, 取文本长度 (path) － a)", "call:取文本右边[path,(20 call:取文本长度[path] a)]");

        // ---- 语句（赋值）
        StmtCase("x ＝ y ＝ z", "(52 x (38 y z))", "x ＝ y ＝ z");
        StmtCase("x = y = z", "(52 x (38 y z))", "x ＝ y ＝ z");
        StmtCase("x == y", "(38 x y)");
        StmtCase("a [i] ＝ f (b ＝ c)", "(52 idx(a,i) call:f[(38 b c)])", "a [i] ＝ f (b ＝ c)");
        StmtCase("是否为频道 ＝ _fromgrouppro ＞ 0", "(52 是否为频道 (41 _fromgrouppro n:0=0))");
        StmtCase("x ＝ a 或 b", "(52 x (46 a b))", "x ＝ a 或 b");
        {
            var s = Stmt1("f (“含'引号”) ' 注释") as ExprStmt;
            Eq(Dump(s?.Expr), "call:f[s:含'引号]", "引号内 ' 不是注释");
            Eq(s?.Comment ?? "<null>", "注释", "行尾注释");
            var s2 = Stmt1("f () '  两空格") as ExprStmt;
            Eq(s2?.Comment ?? "<null>", " 两空格", "行尾注释只去一个空格");
        }

        // ---- 失败兜底
        {
            var d = new List<CodeDiagnostic>();
            var s = Stmt1("a ＝ <?未支持的表达式:X?>", d);
            Check(s is RawStmt r && r.Code == "a ＝ <?未支持的表达式:X?>", "占位符 → RawStmt");
            Check(d.Count == 1, "占位符 diag");
            d.Clear();
            s = Stmt1("f (a, ' 注释", d);
            Check(s is RawStmt r2 && r2.Code == "f (a, ' 注释", "RawStmt 保留整行原文（含注释）");
            Check(Expr("a ＋", d) == null, "ParseExpression 失败返回 null");
            Check(Expr("x ＝ ", d) == null, "ParseExpression 失败返回 null 2");
            Check(CodeParser.ParseDate("2020年2月30日") == null, "非法日期");
            Check(CodeParser.ParseDate("2020年2月3日") == new DateTime(2020, 2, 3), "日期无方括号");
            Check(CodeParser.ParseNumber("-1") == -1 && CodeParser.ParseNumber("1.16444736e+017") == 1.16444736e17, "ParseNumber");
            Check(CodeParser.ParseNumber("abc") == null && CodeParser.ParseNumber("Infinity") == null, "ParseNumber 失败");
        }

        // ---- 块结构
        BodyRoundTrip("基本流程", new[]
        {
            "    .如果真 (a) ' 注释1",
            "        b ()",
            "    .如果真结束",
            "    .如果 (a ＝ 1)",
            "        ",
            "    .否则",
            "        c ＝ 2",
            "    .如果结束",
            "    .判断开始 (a ＝ 1)",
            "        x ()",
            "    .判断 (a ＝ 2) ' 注释2",
            "        y ()",
            "    .默认",
            "        ",
            "    .判断结束",
            "    .判断循环首 (真)",
            "        跳出循环 ()",
            "    .判断循环尾 () ' 尾注释",
            "    .循环判断首 ()",
            "        z ()",
            "    .循环判断尾 (i ＜ 10)",
            "    .计次循环首 (次数, )",
            "        ' 整行注释",
            "    .计次循环尾 ()",
            "    .变量循环首 (1, 10, 1, i)",
            "        '  两空格注释",
            "    .变量循环尾 ()",
            "    ",
        });

        {
            var ast = BodyRoundTrip("嵌套屏蔽流程", new[]
            {
                "    ' .如果真 (a)",
                "        ' .如果真 (b)",
                "            c ()",
                "        ' .如果真结束",
                "    ' .如果真结束",
                "    d ()",
            });
            Check(ast.Count == 2 && ast[0] is IfTrueStmt { Mask: true } it && it.Body[0] is IfTrueStmt { Mask: true } it2
                  && it2.Body[0] is ExprStmt { Expr: CallExpr }, "嵌套屏蔽结构");
        }
        {
            var ast = BodyRoundTrip("屏蔽循环首 + 未屏蔽尾", new[]
            {
                "    ' .计次循环首 (3, i)",
                "        a ()",
                "    .计次循环尾 ()",
            });
            Check(ast[0] is CounterStmt { Mask: true, End.Mask: false } cs && cs.Args.Count == 2, "循环首尾屏蔽独立");
        }
        {
            var ast = BodyRoundTrip("屏蔽的 .判断 分支", new[]
            {
                "    .判断开始 (a)",
                "    ' .判断 (b)",
                "        x ()",
                "    .默认",
                "    .判断结束",
            });
            Check(ast[0] is SwitchStmt { HasDefault: true } sw && sw.Cases.Count == 2 && sw.Cases[1].Mask && !sw.Cases[0].Mask, "分支屏蔽");
        }
        {
            var ast = BodyRoundTrip("孤立的 ' .否则 退化为注释", new[]
            {
                "    ' .否则",
                "    ' .如果真结束",
                "    a ()",
            });
            Check(ast[0] is ExprStmt { Comment: ".否则", Expr: null } && ast[1] is ExprStmt { Comment: ".如果真结束" }, "孤立屏蔽行 → 注释");
        }
        {
            var ast = BodyRoundTrip("未闭合的屏蔽开头退化为注释", new[]
            {
                "    ' .如果真 (a) 这是注释",
                "    b ()",
            }, expectPrinted: null);
            Check(ast[0] is ExprStmt { Comment: ".如果真 (a) 这是注释" }, "未闭合屏蔽开头 → 注释");
        }
        {
            var ast = BodyRoundTrip("屏蔽开头 + 外层未屏蔽结束", new[]
            {
                "    .如果真 (x)",
                "        ' .如果真 (a)",
                "        b ()",
                "    .如果真结束",
            });
            Check(ast[0] is IfTrueStmt it && it.Body.Count == 2 && it.Body[0] is ExprStmt { Comment: ".如果真 (a)" }, "屏蔽开头让位给外层");
        }
        {
            var ast = BodyRoundTrip("未闭合块自动闭合", new[]
            {
                "    .如果真 (a)",
                "        b ()",
            }, expectDiags: 1, expectPrinted: new[] { "    .如果真 (a)", "        b ()", "    .如果真结束" });
            Check(ast[0] is IfTrueStmt it && it.Body.Count == 1, "未闭合块");
        }
        {
            var ast = BodyRoundTrip("内层未闭合，外层结束时闭合", new[]
            {
                "    .如果真 (a)",
                "        .计次循环首 (3, i)",
                "            b ()",
                "    .如果真结束",
                "    c ()",
            }, expectDiags: 1, expectPrinted: new[]
            {
                "    .如果真 (a)",
                "        .计次循环首 (3, i)",
                "            b ()",
                "        .计次循环尾 ()",
                "    .如果真结束",
                "    c ()",
            });
            Check(ast.Count == 2 && ast[0] is IfTrueStmt it && it.Body[0] is CounterStmt, "内层自动闭合");
        }
        {
            var ast = BodyRoundTrip("多余的结束行", new[]
            {
                "    a ()",
                "    .如果真结束",
                "    .否则 ' x",
            }, expectDiags: 2);
            Check(ast[1] is RawStmt && ast[2] is RawStmt { Code: ".否则 ' x" }, "多余结束 → RawStmt");
        }
        {
            var ast = BodyRoundTrip("缺 .默认 的判断", new[]
            {
                "    .判断开始 (a)",
                "        x ()",
                "    .判断 (b)",
                "    .判断结束",
            });
            Check(ast[0] is SwitchStmt { HasDefault: false } sw && sw.Cases.Count == 2, "HasDefault=false");
        }
        {
            var d = new List<CodeDiagnostic>();
            var ast = CodeParser.ParseBody(new[] { "    .如果真 (a ＝ ) ' c", "    .如果真结束", "    .计次循环首 (3)", "    .计次循环尾 ()" }, 1, "t", d);
            Check(ast[0] is IfTrueStmt { Cond: { Expr: null, Raw: "如果真 (a ＝ )" }, Comment: "c" }, "条件失败 → Raw");
            Check(ast[1] is CounterStmt { Raw: "计次循环首 (3)" }, "计次参数个数不对 → Raw");
            Check(d.Count == 2, "流程头失败 diag", string.Join("; ", d));
            Eq(string.Join("|", CodePrinter.PrintBody(ast)), "    .如果真 (a ＝ ) ' c|    .如果真结束|    .计次循环首 (3)|    .计次循环尾 ()", "Raw 流程头打印");
        }
        {
            // Pos
            var ast = CodeParser.ParseBody(new[] { "        x ＝ f (a)" }, 7, "t", new());
            var op = (OpExpr)((ExprStmt)ast[0]).Expr!;
            var call = (CallExpr)op.Operands[1];
            Check(ast[0].Pos == new SrcPos(7, 8) && op.Operands[0].Pos == new SrcPos(7, 8) && call.Pos == new SrcPos(7, 12)
                  && call.Args[0]!.Pos == new SrcPos(7, 15), "Pos 行列");
        }
        {
            // 任意输入不抛异常
            var junk = new[] { "", "    ", "'", "    '", "    .", "    .如果真", "    .如果真(", "    )))", "    “", "    [", "    #", "    &", "    f (,,", "    ' .判断结束", "    .判断", "    {", "    a ＝ ＝ b", "    a..b", "\t\tx" };
            bool ok = true;
            try { CodeParser.ParseBody(junk, 1, "t", new()); CodePrinter.PrintBody(CodeParser.ParseBody(junk, 1, "t", new())); }
            catch (Exception ex) { ok = false; Console.WriteLine(ex); }
            Check(ok, "垃圾输入不抛异常");
        }

        Console.WriteLine($"单元用例: 通过 {_pass}，失败 {_fail}");
        return _fail;
    }
}
