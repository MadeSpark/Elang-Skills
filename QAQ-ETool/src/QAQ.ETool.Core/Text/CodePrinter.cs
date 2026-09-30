using System.Text;
using QAQ.ETool.Core.Text.Ast;

namespace QAQ.ETool.Core.Text;

/// <summary>
/// 语法树 → e2txt 文本，规则与 CodeRenderer 完全一致（全角运算符、`名称 (a, b)`、` [i]`、`{ a, b }` …）。
/// 括号按 CodeRenderer.Operand 的优先级规则重新推导，忽略 Expr.Parenthesized；数值直接输出 Raw。
/// 用途：解析 → 打印 的恒等自检，以及把语法树回写成规范文本。
/// </summary>
public static class CodePrinter
{
    // ================================================================ 语句
    public static List<string> PrintBody(List<Stmt> body, int indent = 1)
    {
        var lines = new List<string>();
        Block(body, lines, indent);
        return lines;
    }

    private static void Block(List<Stmt> body, List<string> lines, int indent)
    {
        foreach (var s in body) Statement(s, lines, indent);
    }

    private static string Ind(int indent) => new(' ', indent * 4);
    private static string Mask(bool m) => m ? "' " : "";
    private static string TailComment(string? c) => c == null ? "" : " ' " + c;

    private static string Head(string keyword, Cond cond)
    {
        if (cond.Expr != null) return keyword + " (" + Print(cond.Expr) + ")";
        if (cond.Raw != null) return "." + cond.Raw;
        return keyword + " ()";
    }

    private static void Statement(Stmt s, List<string> lines, int indent)
    {
        var ind = Ind(indent);
        switch (s)
        {
            case ExprStmt es:
                if (es.Expr == null)
                    lines.Add(ind + Mask(es.Mask) + (es.Comment == null ? "" : "' " + es.Comment));
                else
                    lines.Add(ind + Mask(es.Mask) + Print(es.Expr) + TailComment(es.Comment));
                break;
            case RawStmt rs:
                lines.Add(ind + Mask(rs.Mask) + rs.Code);
                break;
            case IfTrueStmt it:
                lines.Add(ind + Mask(it.Mask) + Head(".如果真", it.Cond) + TailComment(it.Comment));
                Block(it.Body, lines, indent + 1);
                lines.Add(ind + Mask(it.Mask) + ".如果真结束");
                break;
            case IfElseStmt ie:
                lines.Add(ind + Mask(ie.Mask) + Head(".如果", ie.Cond) + TailComment(ie.Comment));
                Block(ie.Then, lines, indent + 1);
                lines.Add(ind + Mask(ie.Mask) + ".否则");
                Block(ie.Else, lines, indent + 1);
                lines.Add(ind + Mask(ie.Mask) + ".如果结束");
                break;
            case SwitchStmt sw:
                {
                    bool m0 = sw.Cases.Count > 0 ? sw.Cases[0].Mask : sw.Mask;
                    for (int i = 0; i < sw.Cases.Count; i++)
                    {
                        var c = sw.Cases[i];
                        lines.Add(ind + Mask(c.Mask) + Head(i == 0 ? ".判断开始" : ".判断", c.Cond) + TailComment(c.Comment));
                        Block(c.Body, lines, indent + 1);
                    }
                    // CodeRenderer 恒写 .默认；这里按语法树如实输出（缺省分支由 Binder 补）
                    if (sw.HasDefault)
                    {
                        lines.Add(ind + Mask(m0) + ".默认");
                        Block(sw.Default, lines, indent + 1);
                    }
                    lines.Add(ind + Mask(m0) + ".判断结束");
                    break;
                }
            case WhileStmt ws:
                lines.Add(ind + Mask(ws.Mask) + Head(".判断循环首", ws.Cond) + TailComment(ws.Comment));
                Block(ws.Body, lines, indent + 1);
                lines.Add(ind + Mask(ws.End.Mask) + ".判断循环尾 ()" + TailComment(ws.End.Comment));
                break;
            case DoWhileStmt dw:
                lines.Add(ind + Mask(dw.Mask) + ".循环判断首 ()" + TailComment(dw.Comment));
                Block(dw.Body, lines, indent + 1);
                lines.Add(ind + Mask(dw.End.Mask) + Head(".循环判断尾", dw.Cond) + TailComment(dw.End.Comment));
                break;
            case CounterStmt cs:
                lines.Add(ind + Mask(cs.Mask) + (cs.Raw != null ? "." + cs.Raw : ".计次循环首 (" + Args(cs.Args) + ")") + TailComment(cs.Comment));
                Block(cs.Body, lines, indent + 1);
                lines.Add(ind + Mask(cs.End.Mask) + ".计次循环尾 ()" + TailComment(cs.End.Comment));
                break;
            case ForStmt fs:
                lines.Add(ind + Mask(fs.Mask) + (fs.Raw != null ? "." + fs.Raw : ".变量循环首 (" + Args(fs.Args) + ")") + TailComment(fs.Comment));
                Block(fs.Body, lines, indent + 1);
                lines.Add(ind + Mask(fs.End.Mask) + ".变量循环尾 ()" + TailComment(fs.End.Comment));
                break;
            default:
                lines.Add(ind + $"<?未支持的语句:{s.GetType().Name}?>");
                break;
        }
    }

    // ================================================================ 表达式
    private enum OpKind { Unary, Binary, Multi }

    /// <summary>与 CodeRenderer.Ops 相同：命令号 → (符号, 优先级, 类型)，优先级越小越紧</summary>
    private static readonly Dictionary<int, (string Sym, int Prec, OpKind Kind)> Ops = new()
    {
        { 15, ("×", 2, OpKind.Multi) },
        { 16, ("÷", 2, OpKind.Multi) },
        { 17, ("＼", 3, OpKind.Multi) },
        { 18, ("％", 4, OpKind.Multi) },
        { 19, ("＋", 5, OpKind.Multi) },
        { 20, ("－", 5, OpKind.Multi) },
        { 21, ("－", 1, OpKind.Unary) },
        { 38, ("＝", 6, OpKind.Binary) },
        { 39, ("≠", 6, OpKind.Binary) },
        { 40, ("＜", 6, OpKind.Binary) },
        { 41, ("＞", 6, OpKind.Binary) },
        { 42, ("≤", 6, OpKind.Binary) },
        { 43, ("≥", 6, OpKind.Binary) },
        { 44, ("≈", 6, OpKind.Binary) },
        { 45, ("且", 7, OpKind.Multi) },
        { 46, ("或", 8, OpKind.Multi) },
        { 52, ("＝", 9, OpKind.Binary) },
    };

    private static string Args(IEnumerable<Expr?> xs) => string.Join(", ", xs.Select(x => Print(x)));

    public static string Print(Expr? e)
    {
        switch (e)
        {
            case null: return "";
            case OpExpr op:
                return Ops.TryGetValue(op.Cmd, out var info) ? Operator(op, info) : $"<?未知运算符:{op.Cmd}?>";
            case CallExpr c:
                {
                    var sb = new StringBuilder();
                    if (c.Target != null) sb.Append(Print(c.Target)).Append('.');
                    sb.Append(c.Name).Append(" (").Append(Args(c.Args)).Append(')');
                    return sb.ToString();
                }
            case NumberLit n: return n.Raw.Length > 0 ? n.Raw : CodeRendererNumber(n.Value);
            case StringLit s: return "“" + s.Value + "”";
            case BoolLit b: return b.Value ? "真" : "假";
            case DateLit d: return DateText(d.Value);
            case ConstRef cr: return "#" + cr.Name + (cr.Member != null ? "." + cr.Member : "");
            case MethodPtr mp: return "&" + mp.Name;
            case NameRef nr: return nr.Name;
            case MemberExpr me: return Print(me.Target) + "." + me.Name;
            case IndexExpr ix: return Print(ix.Target) + " [" + Print(ix.Index) + "]";
            case ArrayLit al: return al.Items.Count == 0 ? "{ }" : "{ " + Args(al.Items) + " }";
            default: return $"<?未支持的表达式:{e.GetType().Name}?>";
        }
    }

    private static string Operator(OpExpr op, (string Sym, int Prec, OpKind Kind) info)
    {
        var ps = op.Operands;
        if (info.Kind == OpKind.Unary)
            return info.Sym + (ps.Count > 0 ? Operand(ps[0], info.Prec, first: true) : "");
        if (ps.Count == 0) return " " + info.Sym + " ";
        var parts = new List<string> { Operand(ps[0], info.Prec, first: true) };
        for (int i = 1; i < ps.Count; i++) parts.Add(Operand(ps[i], info.Prec, first: false));
        return ps.Count == 1 ? parts[0] + " " + info.Sym + " " : string.Join(" " + info.Sym + " ", parts);
    }

    private static int DisplayPrec(int prec) => prec == 7 ? 8 : prec;   // 且 与 或 同级显示

    private static string Operand(Expr e, int parentPrec, bool first)
    {
        if (e is OpExpr c && Ops.TryGetValue(c.Cmd, out var cop))
        {
            bool paren = first ? cop.Prec > parentPrec : DisplayPrec(cop.Prec) >= DisplayPrec(parentPrec);
            var s = Operator(c, cop);
            return paren ? "(" + s + ")" : s;
        }
        return Print(e);
    }

    // 与 CodeRenderer.Number / DateLit 相同（此处不能引用 CodeRenderer：它依赖 EProjectFile）
    private static string CodeRendererNumber(double v)
    {
        if (v == Math.Floor(v) && Math.Abs(v) < 1e15)
            return ((long)v).ToString(System.Globalization.CultureInfo.InvariantCulture);
        var s = v.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
        int e = s.IndexOfAny(new[] { 'E', 'e' });
        if (e < 0) return s;
        var mant = s.Substring(0, e);
        var exp = s.Substring(e + 1);
        char sign = '+';
        if (exp.StartsWith('-')) { sign = '-'; exp = exp.Substring(1); }
        else if (exp.StartsWith('+')) exp = exp.Substring(1);
        return mant + "e" + sign + exp.PadLeft(3, '0');
    }

    private static string DateText(DateTime d)
    {
        var s = d.ToString("yyyy年M月d日", System.Globalization.CultureInfo.InvariantCulture);
        if (d.Second != 0) s += $"{d.Hour}时{d.Minute}分{d.Second}秒";
        else if (d.Minute != 0) s += $"{d.Hour}时{d.Minute}分";
        else if (d.Hour != 0) s += $"{d.Hour}时";
        return "[" + s + "]";
    }
}
