using QAQ.ETool.Core.Text.Ast;

/// <summary>统计语法树里各类节点的数量（用来确认自检覆盖了哪些语法）</summary>
internal static class Stats
{
    private static void Inc(SortedDictionary<string, int> k, string key) => k[key] = k.TryGetValue(key, out var v) ? v + 1 : 1;

    public static void Walk(IEnumerable<Stmt> body, SortedDictionary<string, int> k)
    {
        foreach (var s in body)
        {
            if (s.Mask) Inc(k, "屏蔽流程");
            switch (s)
            {
                case ExprStmt es:
                    Inc(k, es.Expr != null ? "语句" : es.Comment != null ? "整行注释" : "空语句");
                    if (es.Expr != null && es.Comment != null) Inc(k, "行尾注释");
                    E(es.Expr, k);
                    break;
                case RawStmt: Inc(k, "Raw"); break;
                case IfTrueStmt it: Inc(k, "如果真"); C(it.Cond, k); Walk(it.Body, k); break;
                case IfElseStmt ie: Inc(k, "如果"); C(ie.Cond, k); Walk(ie.Then, k); Walk(ie.Else, k); break;
                case SwitchStmt sw:
                    Inc(k, "判断");
                    foreach (var c in sw.Cases) { C(c.Cond, k); Walk(c.Body, k); }
                    Walk(sw.Default, k);
                    break;
                case WhileStmt ws: Inc(k, "判断循环"); C(ws.Cond, k); Walk(ws.Body, k); break;
                case DoWhileStmt dw: Inc(k, "循环判断"); C(dw.Cond, k); Walk(dw.Body, k); break;
                case CounterStmt cs: Inc(k, "计次循环"); foreach (var a in cs.Args) E(a, k); Walk(cs.Body, k); break;
                case ForStmt fs: Inc(k, "变量循环"); foreach (var a in fs.Args) E(a, k); Walk(fs.Body, k); break;
            }
        }
    }

    private static void C(Cond c, SortedDictionary<string, int> k) { if (c.Raw != null) Inc(k, "RawCond"); E(c.Expr, k); }

    private static void E(Expr? e, SortedDictionary<string, int> k)
    {
        switch (e)
        {
            case null: return;
            case OpExpr op: Inc(k, "Op" + op.Cmd); foreach (var o in op.Operands) E(o, k); break;
            case CallExpr c: Inc(k, "Call"); E(c.Target, k); foreach (var a in c.Args) { if (a == null) Inc(k, "省略参数"); E(a, k); } break;
            case MemberExpr m: Inc(k, "Member"); E(m.Target, k); break;
            case IndexExpr i: Inc(k, "Index"); E(i.Target, k); E(i.Index, k); break;
            case ArrayLit a: Inc(k, "Array"); foreach (var x in a.Items) E(x, k); break;
            default: Inc(k, e.GetType().Name); break;
        }
    }
}
