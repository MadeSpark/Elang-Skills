using System.Globalization;
using System.Text;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Expressions;
using QIQI.EProjectFile.Statements;
using QAQ.ETool.Core.Model;

namespace QAQ.ETool.Core.Text;

/// <summary>
/// 语句/表达式 → e2txt 风格文本。
/// 与 EProjectFile 自带 ToTextCode 的区别：全角运算符、行尾注释单空格、窗口自身成员带窗口名、
/// 名称解析走 EProject（含模块重命名），以及修正 e2txt 的窗口属性 bug。
/// </summary>
public sealed class CodeRenderer
{
    private readonly EProject _p;
    private readonly NameResolver _n;
    /// <summary>本次渲染用到的非核心支持库下标，按首次使用顺序（用于写 .支持库 行）</summary>
    public OrderedIntSet UsedLibs { get; set; } = new();
    /// <summary>
    /// 经「#常量 / #枚举」引用到的支持库。e2txt 把它们排在 .支持库 行最前（大项目 5 个文件验证）
    /// </summary>
    public OrderedIntSet ConstLibs { get; set; } = new();
    /// <summary>无法解析的名称计数（判据①：必须为 0）</summary>
    public int Unresolved { get; private set; }

    public CodeRenderer(EProject p, NameResolver n) { _p = p; _n = n; }

    /// <summary>当前渲染所在的窗口程序集对应的窗口（窗口自身成员访问时用）</summary>
    public FormInfo? CurrentForm { get; set; }

    // ================================================================ 语句
    public void WriteBlock(StatementBlock block, List<string> lines, int indent)
    {
        foreach (var s in block) WriteStatement(s, lines, indent);
    }

    private static string Ind(int indent) => new(' ', indent * 4);

    private static string Mask(bool m) => m ? "' " : "";

    private static string TailComment(string? c) => c == null ? "" : " ' " + c;

    public void WriteStatement(Statement s, List<string> lines, int indent)
    {
        switch (s)
        {
            case ExpressionStatement es:
                if (es.Expression == null)
                    lines.Add(Ind(indent) + Mask(es.Mask) + (es.Comment == null ? "" : "' " + es.Comment));
                else
                    lines.Add(Ind(indent) + Mask(es.Mask) + Call(es.Expression) + TailComment(es.Comment));
                break;
            case UnexaminedStatement us:
                lines.Add(Ind(indent) + Mask(us.Mask) + us.UnexaminedCode + TailComment(us.Comment));
                break;
            case IfStatement ifs:
                lines.Add(Ind(indent) + Mask(ifs.Mask) + Head(".如果真", ifs.UnexaminedCode, ifs.Condition) + TailComment(ifs.Comment));
                WriteBlock(ifs.Block, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(ifs.Mask) + ".如果真结束");
                break;
            case IfElseStatement ife:
                lines.Add(Ind(indent) + Mask(ife.Mask) + Head(".如果", ife.UnexaminedCode, ife.Condition) + TailComment(ife.Comment));
                WriteBlock(ife.BlockOnTrue, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(ife.Mask) + ".否则");
                WriteBlock(ife.BlockOnFalse, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(ife.Mask) + ".如果结束");
                break;
            case SwitchStatement sw:
                for (int i = 0; i < sw.Case.Count; i++)
                {
                    var c = sw.Case[i];
                    var kw = i == 0 ? ".判断开始" : ".判断";
                    lines.Add(Ind(indent) + Mask(c.Mask) + Head(kw, c.UnexaminedCode, c.Condition, i == 0 ? "判断" : null) + TailComment(c.Comment));
                    WriteBlock(c.Block, lines, indent + 1);
                }
                lines.Add(Ind(indent) + Mask(sw.Case[0].Mask) + ".默认");
                WriteBlock(sw.DefaultBlock, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(sw.Case[0].Mask) + ".判断结束");
                break;
            case WhileStatement ws:
                lines.Add(Ind(indent) + Mask(ws.MaskOnStart) + Head(".判断循环首", ws.UnexaminedCode, ws.Condition) + TailComment(ws.CommentOnStart));
                WriteBlock(ws.Block, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(ws.MaskOnEnd) + ".判断循环尾 ()" + TailComment(ws.CommentOnEnd));
                break;
            case DoWhileStatement dw:
                lines.Add(Ind(indent) + Mask(dw.MaskOnStart) + ".循环判断首 ()" + TailComment(dw.CommentOnStart));
                WriteBlock(dw.Block, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(dw.MaskOnEnd) + Head(".循环判断尾", dw.UnexaminedCode, dw.Condition) + TailComment(dw.CommentOnEnd));
                break;
            case CounterStatement cs:
                lines.Add(Ind(indent) + Mask(cs.MaskOnStart) + (cs.UnexaminedCode != null ? "." + cs.UnexaminedCode : ".计次循环首 (" + Args(cs.Count, cs.Var) + ")") + TailComment(cs.CommentOnStart));
                WriteBlock(cs.Block, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(cs.MaskOnEnd) + ".计次循环尾 ()" + TailComment(cs.CommentOnEnd));
                break;
            case ForStatement fs:
                lines.Add(Ind(indent) + Mask(fs.MaskOnStart) + (fs.UnexaminedCode != null ? "." + fs.UnexaminedCode : ".变量循环首 (" + Args(fs.Start, fs.End, fs.Step, fs.Var) + ")") + TailComment(fs.CommentOnStart));
                WriteBlock(fs.Block, lines, indent + 1);
                lines.Add(Ind(indent) + Mask(fs.MaskOnEnd) + ".变量循环尾 ()" + TailComment(fs.CommentOnEnd));
                break;
            default:
                throw new NotSupportedException($"未支持的语句类型: {s.GetType().Name}");
        }
    }

    /// <summary>流程控制头：`.关键字 (条件)`；未验证代码原样输出</summary>
    private string Head(string keyword, string? unexamined, Expression? cond, string? unexaminedPrefix = null)
    {
        if (unexamined != null)
        {
            if (unexaminedPrefix != null)
            {
                var t = unexamined.TrimStart();
                if (t.StartsWith(unexaminedPrefix, StringComparison.Ordinal))
                    return keyword + t.Substring(unexaminedPrefix.Length);
            }
            return "." + unexamined;
        }
        return keyword + " (" + Expr(cond) + ")";
    }

    // 注意：不能写 xs.Select(Expr)——会命中 Select((x, i) => …) 重载，把下标当成优先级传进去
    private string Args(params Expression?[] xs) => string.Join(", ", xs.Select(x => Expr(x)));

    // ================================================================ 表达式
    // 运算符：命令号（krnln）→ (符号, 优先级, 类型)。优先级数值越小越紧（与 EProjectFile 一致）。
    private enum OpKind { Unary, Binary, Multi }
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

    public string Expr(Expression? e, int maxPrec = int.MaxValue)
    {
        switch (e)
        {
            case null:
            case DefaultValueExpression:
                return "";
            case CallExpression c:
                return Call(c, maxPrec);
            case NumberLiteral n:
                return Number(n.Value);
            case StringLiteral s:
                return "“" + s.Value + "”";
            case BoolLiteral b:
                return b.Value ? "真" : "假";
            case DateTimeLiteral d:
                return DateLit(d.Value);
            case ConstantExpression ce:
                return "#" + (ce.LibraryId == -2 ? _n.User(ce.ConstantId) : _n.LibConstant(ce.LibraryId, ce.ConstantId, ConstLibs));
            case EmnuConstantExpression en:
                return "#" + _n.LibTypeName(en.LibraryId, en.StructId, ConstLibs) + "." + _n.LibTypeMember(en.LibraryId, en.StructId, en.MemberId);
            case MethodPtrExpression mp:
                return "&" + _n.User(mp.MethodId);
            case VariableExpression v:
                return Variable(v.Id);
            case AccessMemberExpression am:
                return Member(am);
            case AccessArrayExpression aa:
                return Expr(aa.Target) + " [" + Expr(aa.Index) + "]";
            case ArrayLiteralExpression al:
                return al.Count == 0 ? "{ }" : "{ " + string.Join(", ", al.Select(x => Expr(x))) + " }";
            case ParamListExpression pl:
                return "(" + string.Join(", ", pl.Select(x => Expr(x))) + ")";
            default:
                Unresolved++;
                return $"<?未支持的表达式:{e.GetType().Name}?>";
        }
    }

    private string Variable(int id)
    {
        if (EplSystemId.GetType(id) == EplSystemId.Type_FormSelf)
        {
            // 窗口程序集里指代窗口自身：写窗口名（e2txt 行为：`_启动窗口.宽度`）
            if (CurrentForm != null) return CurrentForm.Name;
            Unresolved++;
            return "<?窗口自身?>";
        }
        return _n.User(id);
    }

    private string Member(AccessMemberExpression am)
    {
        string target = Expr(am.Target);
        string member;
        if (am.LibraryId == -2)
        {
            // 窗口名.系统属性 / 窗口名.组件名 / 自定义类型成员
            if (EplSystemId.GetType(am.StructId) == EplSystemId.Type_Form && (am.MemberId & unchecked((int)0xFF000000)) == 0)
                member = _n.LibTypeMember(0, 0, am.MemberId - 1);   // krnln 窗口 类型的属性
            else
                member = _n.User(am.MemberId);
        }
        else
        {
            UsedLibsAdd(am.LibraryId, 'M');
            member = _n.LibTypeMember(am.LibraryId, am.StructId, am.MemberId);
        }
        return target + "." + member;
    }

    private void UsedLibsAdd(int lib, char kind = '?')
    {
        if (lib <= 0) return;
        Trace?.Invoke(lib, kind);
        UsedLibs.Add(lib);
    }

    /// <summary>调试：每次引用支持库时回调 (库下标, 类别 C=命令 M=成员)</summary>
    public Action<int, char>? Trace { get; set; }

    public string Call(CallExpression c, int maxPrec = int.MaxValue)
    {
        if (c.Target == null && c.LibraryId == 0 && Ops.TryGetValue(c.MethodId, out var op))
            return Operator(c, op, maxPrec);

        var sb = new StringBuilder();
        if (c.Target != null)
        {
            sb.Append(Expr(c.Target)).Append('.');
        }
        if (c.InvokeSpecial && c.LibraryId == -2 && _p.MethodToClass.TryGetValue(c.MethodId, out var cls)
            && EplSystemId.GetType(cls) == EplSystemId.Type_Class)
        {
            sb.Append(_n.User(cls)).Append('.');
        }
        if (c.LibraryId == -2 || c.LibraryId == -3)
            sb.Append(_n.User(c.MethodId));
        else
        {
            UsedLibsAdd(c.LibraryId, c.Target == null ? 'C' : 'O');   // C=全局命令 O=对象方法
            sb.Append(_n.LibCmd(c.LibraryId, c.MethodId));
        }
        sb.Append(" (");
        if (c.ParamList != null) sb.Append(string.Join(", ", c.ParamList.Select(x => Expr(x))));
        sb.Append(')');
        return sb.ToString();
    }

    /// <summary>
    /// 运算符表达式的括号规则（与 e2txt 一致，且保证语义不变）：
    ///   第 1 个操作数：子运算优先级 &gt; 本运算 → 加括号（真实优先级）
    ///   其后操作数：  子运算「显示优先级」≥ 本运算 → 加括号；显示优先级把 且/或 视为同级，
    ///                 所以 `a 或 (b 且 c)` 会带括号（大项目 main.static.e.txt:793 验证）
    /// </summary>
    private string Operator(CallExpression c, (string Sym, int Prec, OpKind Kind) op, int maxPrec)
    {
        var ps = c.ParamList?.ToList() ?? new List<Expression>();
        string body;
        if (op.Kind == OpKind.Unary)
        {
            body = op.Sym + (ps.Count > 0 ? Operand(ps[0], op.Prec, first: true) : "");
        }
        else
        {
            if (ps.Count == 0) body = " " + op.Sym + " ";
            else
            {
                var parts = new List<string> { Operand(ps[0], op.Prec, first: true) };
                for (int i = 1; i < ps.Count; i++) parts.Add(Operand(ps[i], op.Prec, first: false));
                body = ps.Count == 1 ? parts[0] + " " + op.Sym + " " : string.Join(" " + op.Sym + " ", parts);
            }
        }
        return body;
    }

    private static int DisplayPrec(int prec) => prec == 7 ? 8 : prec;   // 且 与 或 同级显示

    private string Operand(Expression e, int parentPrec, bool first)
    {
        if (e is CallExpression c && c.Target == null && c.LibraryId == 0 && Ops.TryGetValue(c.MethodId, out var cop))
        {
            bool paren = first
                ? cop.Prec > parentPrec
                : DisplayPrec(cop.Prec) >= DisplayPrec(parentPrec);
            var s = Operator(c, cop, int.MaxValue);
            return paren ? "(" + s + ")" : s;
        }
        return Expr(e);
    }

    /// <summary>
    /// 数值字面量：整数直接写；其余用最短往返表示，指数部分按 MSVC printf 风格（e+017：小写 e、带符号、至少 3 位）。
    /// 例：1.16444736e+017（大项目 哈希.static.e.txt:44）
    /// </summary>
    public static string Number(double v)
    {
        if (v == Math.Floor(v) && Math.Abs(v) < 1e15)
            return ((long)v).ToString(CultureInfo.InvariantCulture);
        var s = v.ToString("R", CultureInfo.InvariantCulture);
        int e = s.IndexOfAny(new[] { 'E', 'e' });
        if (e < 0) return s;
        var mant = s.Substring(0, e);
        var exp = s.Substring(e + 1);
        char sign = '+';
        if (exp.StartsWith('-')) { sign = '-'; exp = exp.Substring(1); }
        else if (exp.StartsWith('+')) exp = exp.Substring(1);
        return mant + "e" + sign + exp.PadLeft(3, '0');
    }

    /// <summary>日期时间字面量：尾部为 0 的时/分/秒省略（e2txt：[1970年1月1日8时]）</summary>
    public static string DateLit(DateTime d)
    {
        var s = d.ToString("yyyy年M月d日", CultureInfo.InvariantCulture);
        if (d.Second != 0) s += $"{d.Hour}时{d.Minute}分{d.Second}秒";
        else if (d.Minute != 0) s += $"{d.Hour}时{d.Minute}分";
        else if (d.Hour != 0) s += $"{d.Hour}时";
        return "[" + s + "]";
    }
}
