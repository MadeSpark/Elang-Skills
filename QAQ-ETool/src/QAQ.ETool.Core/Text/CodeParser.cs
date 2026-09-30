using System.Globalization;
using System.Text.RegularExpressions;
using QAQ.ETool.Core.Text.Ast;

namespace QAQ.ETool.Core.Text;

/// <summary>
/// t2e：子程序体文本 → 语法树（<see cref="Ast"/>）。是 CodeRenderer 的逆。
/// 宽松模式：任何输入都能得到结果，不抛异常；无法解析的语句成为 RawStmt、流程头成为 Cond.Raw，并写入 diags。
/// </summary>
public static class CodeParser
{
    // ================================================================ 公开 API

    /// <summary>
    /// 解析子程序体。lines = 体的原文行（含缩进，4 空格一层，体的第 1 层是 4 空格）；
    /// firstLine = lines[0] 的文件行号（1 基）。块结构按关键字配对，不依赖缩进。
    /// </summary>
    public static List<Stmt> ParseBody(IReadOnlyList<string> lines, int firstLine, string file, List<CodeDiagnostic> diags)
    {
        var ctx = new BodyParser(lines, firstLine, file, diags);
        return ctx.Run();
    }

    /// <summary>解析单个表达式（不允许顶层赋值，`＝` 为比较）；失败返回 null 并写 diags</summary>
    public static Expr? ParseExpression(string text, int line, string file, List<CodeDiagnostic> diags)
    {
        var ts = CodeLexer.Tokenize(text ?? "", 0);
        return TryParse(ts, line, file, diags, p => p.ParseWhole(), out var e, out _) ? e : null;
    }

    private static readonly Regex DateRx = new(
        @"^\s*\[?\s*(\d{1,4})年(\d{1,2})月(\d{1,2})日(?:(\d{1,2})时)?(?:(\d{1,2})分)?(?:(\d{1,2})秒)?\s*\]?\s*$",
        RegexOptions.CultureInvariant);

    /// <summary>解析日期字面量内容（`[2020年1月2日3时4分5秒]`，方括号可有可无，时/分/秒可省）；失败返回 null</summary>
    public static DateTime? ParseDate(string text)
    {
        if (text == null) return null;
        var m = DateRx.Match(text);
        if (!m.Success) return null;
        // 方括号必须成对（都有或都没有）
        var t = text.Trim();
        if (t.StartsWith('[') != t.EndsWith(']')) return null;
        int G(int i) => m.Groups[i].Success ? int.Parse(m.Groups[i].Value, CultureInfo.InvariantCulture) : 0;
        int y = G(1), mo = G(2), d = G(3), h = G(4), mi = G(5), s = G(6);
        if (y < 1 || mo < 1 || mo > 12 || d < 1 || d > DateTime.DaysInMonth(y, mo) || h > 23 || mi > 59 || s > 59) return null;
        return new DateTime(y, mo, d, h, mi, s);
    }

    /// <summary>解析数值字面量原文（`-1`、`1.5`、`1.16444736e+017`）；失败返回 null</summary>
    public static double? ParseNumber(string raw)
    {
        if (string.IsNullOrWhiteSpace(raw)) return null;
        var t = raw.Trim();
        // 只接受 [-]数字[.数字][e[+-]数字] 或 [-].数字…，拒绝 Infinity/NaN/千分位等
        foreach (char c in t)
            if (!(char.IsAsciiDigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) return null;
        if (double.TryParse(t, NumberStyles.AllowLeadingSign | NumberStyles.AllowDecimalPoint | NumberStyles.AllowExponent,
                CultureInfo.InvariantCulture, out var v) && double.IsFinite(v))
            return v;
        return null;
    }

    // ================================================================ 表达式解析

    private sealed class ParseError : Exception
    {
        public int Col { get; }
        public ParseError(string msg, int col) : base(msg) { Col = col; }
    }

    /// <summary>运行解析函数，捕获内部错误。成功返回 true；失败写 diag，并给出错误信息与列。</summary>
    private static bool TryParse<T>(List<CodeToken> ts, int line, string file, List<CodeDiagnostic>? diags,
        Func<ExprParser, T> f, out T? result, out string error, int start = 0, int end = -1)
    {
        result = default;
        error = "";
        var err = ts.FirstOrDefault(t => t.Kind == CodeTokenKind.Error);
        if (err.Kind == CodeTokenKind.Error)
        {
            error = err.Text;
            diags?.Add(new CodeDiagnostic(file, line, err.Col, err.Text));
            return false;
        }
        try
        {
            result = f(new ExprParser(ts, line, start, end < 0 ? ts.Count - 1 : end));
            return true;
        }
        catch (ParseError pe)
        {
            error = pe.Message;
            diags?.Add(new CodeDiagnostic(file, line, pe.Col, pe.Message));
            return false;
        }
    }

    /// <summary>基于 token 区间 [start, end) 的递归下降表达式解析器（end 位置视为 End）</summary>
    private sealed class ExprParser
    {
        private readonly List<CodeToken> _t;
        private readonly int _line, _end;
        private int _p;

        public ExprParser(List<CodeToken> t, int line, int start, int end)
        {
            _t = t; _line = line; _p = start; _end = end;
        }

        private CodeToken Peek => _p < _end ? _t[_p] : new CodeToken(CodeTokenKind.End, "", EndCol, EndCol);
        private CodeToken PeekAt(int k) => _p + k < _end ? _t[_p + k] : new CodeToken(CodeTokenKind.End, "", EndCol, EndCol);
        private int EndCol => _end < _t.Count ? _t[_end].Col : (_t.Count > 0 ? _t[^1].EndCol : 0);
        private CodeToken Next() { var t = Peek; if (_p < _end) _p++; return t; }
        public bool AtEnd => Peek.Kind == CodeTokenKind.End;

        private SrcPos P(CodeToken t) => new(_line, t.Col);

        private static string Show(CodeToken t) => t.Kind switch
        {
            CodeTokenKind.End => "行尾",
            CodeTokenKind.String => "“" + t.Text + "”",
            _ => "“" + t.Text + "”",
        };

        private ParseError Err(string msg, CodeToken at) => new(msg, at.Col);

        private CodeToken Expect(CodeTokenKind k, string what)
        {
            var t = Peek;
            if (t.Kind != k) throw Err($"此处应为 {what}，实际为 {Show(t)}", t);
            return Next();
        }

        private void ExpectEnd()
        {
            if (!AtEnd) throw Err($"表达式后有多余内容 {Show(Peek)}", Peek);
        }

        /// <summary>整个区间是一个表达式</summary>
        public Expr ParseWhole()
        {
            if (AtEnd) throw Err("缺少表达式", Peek);
            var e = ParseExpr();
            ExpectEnd();
            return e;
        }

        /// <summary>整个区间是 `( 表达式 )`</summary>
        public Expr ParseParenWhole()
        {
            Expect(CodeTokenKind.LParen, "(");
            if (Peek.Kind == CodeTokenKind.RParen) throw Err("括号内缺少条件表达式", Peek);
            var e = ParseExpr();
            Expect(CodeTokenKind.RParen, ")");
            ExpectEnd();
            return e;
        }

        /// <summary>整个区间是 `(参数, …)`</summary>
        public List<Expr?> ParseArgsWhole()
        {
            var a = ParseList(CodeTokenKind.LParen, CodeTokenKind.RParen, "(", ")");
            ExpectEnd();
            return a;
        }

        /// <summary>语句：顶层第一个 `＝`/`=` 是赋值（52）</summary>
        public Expr ParseStatement()
        {
            if (AtEnd) throw Err("缺少表达式", Peek);
            int depth = 0, assign = -1;
            for (int k = _p; k < _end; k++)
            {
                var t = _t[k];
                switch (t.Kind)
                {
                    case CodeTokenKind.LParen: case CodeTokenKind.LBracket: case CodeTokenKind.LBrace: depth++; break;
                    case CodeTokenKind.RParen: case CodeTokenKind.RBracket: case CodeTokenKind.RBrace: depth--; break;
                    case CodeTokenKind.Operator when depth == 0 && t.IsAssign: assign = k; break;
                }
                if (assign >= 0) break;
            }
            if (assign < 0) return ParseWhole();

            var at = _t[assign];
            if (assign == _p) throw Err("赋值号左边缺少被赋值的对象", at);
            var lhs = new ExprParser(_t, _line, _p, assign).ParseWhole();
            var rp = new ExprParser(_t, _line, assign + 1, _end);
            if (rp.AtEnd) throw Err("赋值号右边缺少表达式", rp.Peek);
            var rhs = rp.ParseWhole();
            var op = new OpExpr { Cmd = 52, Pos = lhs.Pos };
            op.Operands.Add(lhs);
            op.Operands.Add(rhs);
            _p = _end;
            return op;
        }

        // ---- 二元运算（优先级爬升）

        /// <summary>命令号 → 层级（越小越紧），与 CodeRenderer.Ops 的 Prec 一致</summary>
        private static int Level(int cmd) => cmd switch
        {
            15 or 16 => 2,
            17 => 3,
            18 => 4,
            19 or 20 => 5,
            >= 38 and <= 44 => 6,
            45 => 7,
            46 => 8,
            _ => 0,
        };

        private static bool IsMulti(int cmd) => cmd is >= 15 and <= 20 or 45 or 46;

        public Expr ParseExpr() => ParseBinary(8);

        private Expr ParseBinary(int maxLevel)
        {
            var left = ParseUnary();
            while (true)
            {
                var t = Peek;
                if (t.Kind != CodeTokenKind.Operator) break;
                int lv = Level(t.Cmd);
                if (lv == 0 || lv > maxLevel) break;
                Next();
                if (IsOperandEnd(Peek)) throw Err($"运算符 {t.Text} 后缺少操作数", Peek);
                var right = ParseBinary(lv - 1);   // 左结合
                if (IsMulti(t.Cmd) && left is OpExpr lo && lo.Cmd == t.Cmd && !lo.Parenthesized)
                    lo.Operands.Add(right);        // 扁平化
                else
                {
                    var op = new OpExpr { Cmd = t.Cmd, Pos = left.Pos };
                    op.Operands.Add(left);
                    op.Operands.Add(right);
                    left = op;
                }
            }
            return left;
        }

        private static bool IsOperandEnd(CodeToken t) =>
            t.Kind is CodeTokenKind.End or CodeTokenKind.RParen or CodeTokenKind.RBracket or CodeTokenKind.RBrace or CodeTokenKind.Comma;

        private Expr ParseUnary()
        {
            var t = Peek;
            if (t.Kind == CodeTokenKind.Operator && t.Cmd == 20)
            {
                Next();
                var nx = Peek;
                // 半角 `-` 紧跟数字 → 负数字面量
                if (t.Text == "-" && nx.Kind == CodeTokenKind.Number && nx.Col == t.EndCol)
                {
                    Next();
                    var raw = "-" + nx.Text;
                    var v = ParseNumber(raw) ?? throw Err("无法识别的数值 " + raw, t);
                    return ParsePostfix(new NumberLit { Raw = raw, Value = v, Pos = P(t) });
                }
                if (IsOperandEnd(nx)) throw Err("负号后缺少操作数", nx);
                var operand = ParseUnary();
                var op = new OpExpr { Cmd = 21, Pos = P(t) };
                op.Operands.Add(operand);
                return op;
            }
            return ParsePostfix(ParsePrimary());
        }

        private Expr ParsePostfix(Expr e)
        {
            while (true)
            {
                var t = Peek;
                if (t.Kind == CodeTokenKind.Dot)
                {
                    Next();
                    var name = Expect(CodeTokenKind.Ident, "成员名");
                    if (Peek.Kind == CodeTokenKind.LParen)
                    {
                        var call = new CallExpr { Target = e, Name = name.Text, Pos = e.Pos };
                        call.Args.AddRange(ParseList(CodeTokenKind.LParen, CodeTokenKind.RParen, "(", ")"));
                        e = call;
                    }
                    else e = new MemberExpr { Target = e, Name = name.Text, Pos = e.Pos };
                }
                else if (t.Kind == CodeTokenKind.LBracket)
                {
                    Next();
                    if (Peek.Kind == CodeTokenKind.RBracket) throw Err("下标 [] 内缺少表达式", Peek);
                    var idx = ParseExpr();
                    Expect(CodeTokenKind.RBracket, "]");
                    e = new IndexExpr { Target = e, Index = idx, Pos = e.Pos };
                }
                else return e;
            }
        }

        private Expr ParsePrimary()
        {
            var t = Peek;
            switch (t.Kind)
            {
                case CodeTokenKind.Number:
                    {
                        Next();
                        var v = ParseNumber(t.Text) ?? throw Err("无法识别的数值 " + t.Text, t);
                        return new NumberLit { Raw = t.Text, Value = v, Pos = P(t) };
                    }
                case CodeTokenKind.String:
                    Next();
                    return new StringLit { Value = t.Text, Pos = P(t) };
                case CodeTokenKind.Date:
                    Next();
                    return new DateLit { Value = t.Date, Pos = P(t) };
                case CodeTokenKind.ConstRef:
                    Next();
                    return new ConstRef { Name = t.Text, Member = t.Member, Pos = P(t) };
                case CodeTokenKind.MethodPtr:
                    Next();
                    return new MethodPtr { Name = t.Text, Pos = P(t) };
                case CodeTokenKind.Ident:
                    {
                        Next();
                        if (Peek.Kind == CodeTokenKind.LParen)
                        {
                            var call = new CallExpr { Name = t.Text, Pos = P(t) };
                            call.Args.AddRange(ParseList(CodeTokenKind.LParen, CodeTokenKind.RParen, "(", ")"));
                            return call;
                        }
                        if (t.Text == "真") return new BoolLit { Value = true, Pos = P(t) };
                        if (t.Text == "假") return new BoolLit { Value = false, Pos = P(t) };
                        return new NameRef { Name = t.Text, Pos = P(t) };
                    }
                case CodeTokenKind.LParen:
                    {
                        Next();
                        if (Peek.Kind == CodeTokenKind.RParen) throw Err("括号内缺少表达式", Peek);
                        var e = ParseExpr();
                        Expect(CodeTokenKind.RParen, ")");
                        e.Parenthesized = true;
                        return e;
                    }
                case CodeTokenKind.LBrace:
                    {
                        var arr = new ArrayLit { Pos = P(t) };
                        arr.Items.AddRange(ParseList(CodeTokenKind.LBrace, CodeTokenKind.RBrace, "{", "}"));
                        return arr;
                    }
                case CodeTokenKind.End:
                    throw Err("缺少操作数", t);
                default:
                    throw Err($"此处应为操作数，实际为 {Show(t)}", t);
            }
        }

        /// <summary>`(a, , b)` / `{ a, b }`：空位为 null；紧接闭括号（`()`、`( )`、`{ }`）为空列表</summary>
        private List<Expr?> ParseList(CodeTokenKind open, CodeTokenKind close, string os, string cs)
        {
            Expect(open, os);
            var list = new List<Expr?>();
            if (Peek.Kind == close) { Next(); return list; }
            while (true)
            {
                if (Peek.Kind == CodeTokenKind.Comma || Peek.Kind == close) list.Add(null);
                else list.Add(ParseExpr());
                if (Peek.Kind == CodeTokenKind.Comma) { Next(); continue; }
                if (Peek.Kind == close) { Next(); return list; }
                throw Err($"此处应为 , 或 {cs}，实际为 {Show(Peek)}", Peek);
            }
        }
    }

    // ================================================================ 语句 / 块解析

    private enum LineKind { Empty, Comment, Code, Flow }

    private sealed class LineInfo
    {
        public int LineNo;
        public int Indent;
        public LineKind Kind;
        public string Text = "";          // 去缩进后的原文
        public bool Mask;                  // 流程行带 `' ` 前缀
        public string? MaskedRest;         // 屏蔽流程行退化成注释时的注释内容
        public string Kw = "";             // 流程关键字
        public int KwCol;                  // `.` 所在列
        public string Args = "";           // 关键字之后、行尾注释之前的文本
        public int ArgsCol;
        public string HeadRaw = "";        // 关键字 + Args（去尾空格），即去掉 `.`/屏蔽前缀/行尾注释
        public string? Comment;            // 流程行行尾注释 / 整行注释内容
        public int Owner = -1;             // 中间/结束行：所属开头行下标
        public bool Error;                 // 多余的中间/结束行
    }

    private static readonly string[] Keywords = new[]
    {
        "如果真结束", "判断循环首", "判断循环尾", "循环判断首", "循环判断尾", "计次循环首", "计次循环尾", "变量循环首", "变量循环尾",
        "判断开始", "判断结束", "如果结束",
        "如果真",
        "如果", "否则", "判断", "默认",
    };

    private static bool IsOpener(string kw) => kw is "如果真" or "如果" or "判断开始" or "判断循环首" or "循环判断首" or "计次循环首" or "变量循环首";
    private static bool IsMiddle(string kw) => kw is "否则" or "判断" or "默认";

    private static string? OpenerOf(string endKw) => endKw switch
    {
        "如果真结束" => "如果真",
        "如果结束" => "如果",
        "判断结束" => "判断开始",
        "判断循环尾" => "判断循环首",
        "循环判断尾" => "循环判断首",
        "计次循环尾" => "计次循环首",
        "变量循环尾" => "变量循环首",
        _ => null,
    };

    /// <summary>s 在 pos 处是否为 `.关键字`（关键字后为行尾、空白、`(` 或 `'`）；返回关键字</summary>
    private static string? MatchKeyword(string s, int pos)
    {
        if (pos >= s.Length || s[pos] != '.') return null;
        foreach (var kw in Keywords)
        {
            if (string.CompareOrdinal(s, pos + 1, kw, 0, kw.Length) != 0 || pos + 1 + kw.Length > s.Length) continue;
            int after = pos + 1 + kw.Length;
            if (after == s.Length || s[after] == ' ' || s[after] == '(' || s[after] == '\'' || char.IsWhiteSpace(s[after]))
                return kw;
        }
        return null;
    }

    private sealed class BodyParser
    {
        private readonly IReadOnlyList<string> _src;
        private readonly int _first;
        private readonly string _file;
        private readonly List<CodeDiagnostic> _diags;
        private readonly LineInfo[] _l;

        public BodyParser(IReadOnlyList<string> lines, int firstLine, string file, List<CodeDiagnostic> diags)
        {
            _src = lines ?? Array.Empty<string>();
            _first = firstLine;
            _file = file ?? "";
            _diags = diags ?? new List<CodeDiagnostic>();
            _l = new LineInfo[_src.Count];
        }

        private void Diag(int line, int col, string msg) => _diags.Add(new CodeDiagnostic(_file, line, col, msg));

        public List<Stmt> Run()
        {
            for (int i = 0; i < _l.Length; i++) _l[i] = Classify(_src[i] ?? "", _first + i);
            Match();
            int p = 0;
            var chain = new List<int>();
            var body = ParseSeq(ref p, chain);
            while (p < _l.Length)
            {
                // 理论上不会发生（Match 保证归属合法）；兜底当 RawStmt
                body.Add(Raw(_l[p], "无法归属的流程控制行"));
                p++;
                body.AddRange(ParseSeq(ref p, chain));
            }
            return body;
        }

        // ---------------------------------------------------------- 行分类
        private static LineInfo Classify(string line, int lineNo)
        {
            int ind = 0;
            while (ind < line.Length && line[ind] == ' ') ind++;
            var li = new LineInfo { LineNo = lineNo, Indent = ind, Text = line[ind..] };
            var rest = li.Text;
            if (rest.Trim().Length == 0) { li.Kind = LineKind.Empty; return li; }

            if (rest[0] == '\'')
            {
                int s = 1;
                if (s < rest.Length && rest[s] == ' ') s++;
                var r = rest[s..];
                var kw = MatchKeyword(rest, s);
                if (kw != null)
                {
                    li.Mask = true;
                    li.MaskedRest = r;
                    SetFlow(li, kw, ind + s);
                    return li;
                }
                li.Kind = LineKind.Comment;
                li.Comment = r;
                return li;
            }

            var k2 = MatchKeyword(rest, 0);
            if (k2 != null) { SetFlow(li, k2, ind); return li; }
            li.Kind = LineKind.Code;
            return li;
        }

        /// <summary>col = `.` 在整行中的列</summary>
        private static void SetFlow(LineInfo li, string kw, int col)
        {
            li.Kind = LineKind.Flow;
            li.Kw = kw;
            li.KwCol = col;
            int textOff = col - li.Indent;                   // `.` 在 Text 中的位置
            int argStart = textOff + 1 + kw.Length;
            int cm = CodeLexer.FindCommentStart(li.Text, argStart);
            int argEnd = cm < 0 ? li.Text.Length : cm;
            li.Args = li.Text[argStart..argEnd];
            li.ArgsCol = li.Indent + argStart;
            li.HeadRaw = (kw + li.Args).TrimEnd();
            if (cm >= 0)
            {
                var c = li.Text[(cm + 1)..];
                li.Comment = c.StartsWith(' ') ? c[1..] : c;
            }
        }

        // ---------------------------------------------------------- 配对（决定屏蔽行是流程还是注释）
        private sealed class Open
        {
            public int Idx;
            public string Kw = "";
            public bool Mask;
            public bool SawElse, SawDefault;
            public readonly List<int> Middles = new();
        }

        private bool Compat(Open o, LineInfo l)
        {
            switch (l.Kw)
            {
                case "否则": return o.Kw == "如果" && !o.SawElse && l.Mask == o.Mask;
                case "判断": return o.Kw == "判断开始" && !o.SawDefault;
                case "默认": return o.Kw == "判断开始" && !o.SawDefault && l.Mask == o.Mask;
            }
            if (OpenerOf(l.Kw) != o.Kw) return false;
            // 如果真/如果/判断 的结束行与开头共用屏蔽标记；循环首尾各自独立
            return l.Kw is not ("如果真结束" or "如果结束" or "判断结束") || l.Mask == o.Mask;
        }

        private void Demote(LineInfo l)
        {
            l.Kind = LineKind.Comment;
            l.Comment = l.MaskedRest ?? "";
            l.Owner = -1;
        }

        private void DemoteOpen(Open o)
        {
            Demote(_l[o.Idx]);
            foreach (var m in o.Middles)
            {
                var ml = _l[m];
                if (ml.Mask) Demote(ml);
                else
                {
                    ml.Error = true; ml.Owner = -1;
                    Diag(ml.LineNo, ml.KwCol, $"多余的 .{ml.Kw}（找不到对应的开头）");
                }
            }
        }

        private void Accept(Open o, LineInfo l, List<Open> st)
        {
            l.Owner = o.Idx;
            if (l.Kw == "否则") o.SawElse = true;
            else if (l.Kw == "默认") o.SawDefault = true;
            if (IsMiddle(l.Kw)) o.Middles.Add(Array.IndexOf(_l, l));
            else st.Remove(o);
        }

        private void Match()
        {
            var st = new List<Open>();
            for (int i = 0; i < _l.Length; i++)
            {
                var l = _l[i];
                if (l.Kind != LineKind.Flow) continue;
                if (IsOpener(l.Kw)) { st.Add(new Open { Idx = i, Kw = l.Kw, Mask = l.Mask }); continue; }

                if (st.Count > 0 && Compat(st[^1], l)) { Accept(st[^1], l, st); continue; }
                if (l.Mask) { Demote(l); continue; }

                // 未屏蔽的中间/结束行：先丢弃顶上无法匹配的屏蔽开头（它们其实是注释）
                while (st.Count > 0 && st[^1].Mask && !Compat(st[^1], l))
                {
                    DemoteOpen(st[^1]);
                    st.RemoveAt(st.Count - 1);
                }
                if (st.Count > 0 && Compat(st[^1], l)) { Accept(st[^1], l, st); continue; }

                int j = st.Count - 1;
                while (j >= 0 && !Compat(st[j], l)) j--;
                if (j >= 0)
                {
                    for (int k = st.Count - 1; k > j; k--)
                    {
                        var o = st[k];
                        if (o.Mask) DemoteOpen(o);
                        else Diag(_l[o.Idx].LineNo, _l[o.Idx].KwCol, $".{o.Kw} 缺少结束，已在第 {l.LineNo} 行前自动闭合");
                        st.RemoveAt(k);
                    }
                    Accept(st[j], l, st);
                    continue;
                }
                l.Error = true;
                Diag(l.LineNo, l.KwCol, $"多余的 .{l.Kw}（找不到对应的开头）");
            }
            for (int k = st.Count - 1; k >= 0; k--)
            {
                var o = st[k];
                if (o.Mask) DemoteOpen(o);
                else Diag(_l[o.Idx].LineNo, _l[o.Idx].KwCol, $".{o.Kw} 缺少结束，已在子程序末尾自动闭合");
            }
        }

        // ---------------------------------------------------------- 建树
        private bool IsCloser(LineInfo l) => l.Kind == LineKind.Flow && !l.Error && !IsOpener(l.Kw);

        private List<Stmt> ParseSeq(ref int p, List<int> chain)
        {
            var list = new List<Stmt>();
            while (p < _l.Length)
            {
                var l = _l[p];
                if (IsCloser(l))
                {
                    if (chain.Contains(l.Owner)) return list;
                    list.Add(Raw(l, $"多余的 .{l.Kw}")); p++;
                    continue;
                }
                if (l.Kind == LineKind.Flow && l.Error) { list.Add(Raw(l, $"多余的 .{l.Kw}")); p++; continue; }
                if (l.Kind == LineKind.Flow) { list.Add(ParseBlock(ref p, chain)); continue; }
                list.Add(Simple(l));
                p++;
            }
            return list;
        }

        private RawStmt Raw(LineInfo l, string err) =>
            new() { Pos = new SrcPos(l.LineNo, l.Indent), Code = l.Text, Error = err };

        private Stmt Simple(LineInfo l)
        {
            var pos = new SrcPos(l.LineNo, l.Indent);
            switch (l.Kind)
            {
                case LineKind.Empty: return new ExprStmt { Pos = pos };
                case LineKind.Comment: return new ExprStmt { Pos = pos, Comment = l.Comment };
            }
            var text = l.Text;
            int cm = CodeLexer.FindCommentStart(text);
            string code = cm < 0 ? text : text[..cm].TrimEnd();
            string? comment = null;
            if (cm >= 0)
            {
                var c = text[(cm + 1)..];
                comment = c.StartsWith(' ') ? c[1..] : c;
            }
            var ts = CodeLexer.Tokenize(code, l.Indent);
            if (TryParse(ts, l.LineNo, _file, _diags, p => p.ParseStatement(), out var e, out var err) && e != null)
                return new ExprStmt { Pos = pos, Expr = e, Comment = comment };
            return new RawStmt { Pos = pos, Code = text, Error = err };
        }

        /// <summary>`( 条件 )`；失败时 Raw = 关键字 + 原文</summary>
        private Cond ParseCond(LineInfo l)
        {
            var ts = CodeLexer.Tokenize(l.Args, l.ArgsCol);
            if (TryParse(ts, l.LineNo, _file, _diags, p => p.ParseParenWhole(), out var e, out _) && e != null)
                return new Cond { Expr = e };
            return new Cond { Raw = l.HeadRaw };
        }

        /// <summary>计次/变量循环头的参数表；个数不对也当失败</summary>
        private List<Expr?>? ParseLoopArgs(LineInfo l, int count)
        {
            var ts = CodeLexer.Tokenize(l.Args, l.ArgsCol);
            if (!TryParse(ts, l.LineNo, _file, _diags, p => p.ParseArgsWhole(), out var a, out _) || a == null) return null;
            if (a.Count != count)
            {
                Diag(l.LineNo, l.ArgsCol, $".{l.Kw} 需要 {count} 个参数，实际为 {a.Count} 个");
                return null;
            }
            return a;
        }

        /// <summary>无参数的流程行（`.否则`、`.如果真结束` …）或固定 `()` 的行，检查多余内容与丢失的注释</summary>
        private void CheckBare(LineInfo l, bool allowEmptyParens, bool commentHasSlot)
        {
            var a = l.Args.Trim();
            if (a.Length != 0 && !(allowEmptyParens && a.Replace(" ", "") == "()"))
                Diag(l.LineNo, l.ArgsCol, $".{l.Kw} 后的内容被忽略：{a}");
            if (!commentHasSlot && l.Comment != null)
                Diag(l.LineNo, l.KwCol, $".{l.Kw} 行的注释无法保存，已忽略");
        }

        private bool OwnedBy(int p, int owner, out LineInfo l)
        {
            l = p < _l.Length ? _l[p] : null!;
            return p < _l.Length && IsCloser(l) && l.Owner == owner;
        }

        private LoopEnd EndInfo(LineInfo l) => new() { Pos = new SrcPos(l.LineNo, l.Indent), Mask = l.Mask, Comment = l.Comment };

        private Stmt ParseBlock(ref int p, List<int> chain)
        {
            int oi = p;
            var o = _l[p++];
            var pos = new SrcPos(o.LineNo, o.Indent);
            chain.Add(oi);
            try
            {
                switch (o.Kw)
                {
                    case "如果真":
                        {
                            var s = new IfTrueStmt { Pos = pos, Mask = o.Mask, Cond = ParseCond(o), Comment = o.Comment };
                            s.Body.AddRange(ParseSeq(ref p, chain));
                            if (OwnedBy(p, oi, out var e)) { CheckBare(e, false, false); p++; }
                            return s;
                        }
                    case "如果":
                        {
                            var s = new IfElseStmt { Pos = pos, Mask = o.Mask, Cond = ParseCond(o), Comment = o.Comment };
                            s.Then.AddRange(ParseSeq(ref p, chain));
                            if (OwnedBy(p, oi, out var e) && e.Kw == "否则")
                            {
                                CheckBare(e, false, false); p++;
                                s.Else.AddRange(ParseSeq(ref p, chain));
                            }
                            if (OwnedBy(p, oi, out e) && e.Kw == "如果结束") { CheckBare(e, false, false); p++; }
                            return s;
                        }
                    case "判断开始":
                        {
                            var s = new SwitchStmt { Pos = pos, Mask = o.Mask };
                            var c0 = new SwitchCase { Pos = pos, Mask = o.Mask, Cond = ParseCond(o), Comment = o.Comment };
                            c0.Body.AddRange(ParseSeq(ref p, chain));
                            s.Cases.Add(c0);
                            while (OwnedBy(p, oi, out var e))
                            {
                                p++;
                                if (e.Kw == "判断")
                                {
                                    var c = new SwitchCase { Pos = new SrcPos(e.LineNo, e.Indent), Mask = e.Mask, Cond = ParseCond(e), Comment = e.Comment };
                                    c.Body.AddRange(ParseSeq(ref p, chain));
                                    s.Cases.Add(c);
                                }
                                else if (e.Kw == "默认")
                                {
                                    CheckBare(e, false, false);
                                    s.HasDefault = true;
                                    s.Default.AddRange(ParseSeq(ref p, chain));
                                }
                                else { CheckBare(e, false, false); break; }   // 判断结束
                            }
                            return s;
                        }
                    case "判断循环首":
                        {
                            var s = new WhileStmt { Pos = pos, Mask = o.Mask, Cond = ParseCond(o), Comment = o.Comment };
                            s.Body.AddRange(ParseSeq(ref p, chain));
                            if (OwnedBy(p, oi, out var e)) { CheckBare(e, true, true); s.End = EndInfo(e); p++; }
                            return s;
                        }
                    case "循环判断首":
                        {
                            CheckBare(o, true, true);
                            var s = new DoWhileStmt { Pos = pos, Mask = o.Mask, Comment = o.Comment };
                            s.Body.AddRange(ParseSeq(ref p, chain));
                            if (OwnedBy(p, oi, out var e))
                            {
                                s.End = EndInfo(e);
                                s.Cond = ParseCond(e);
                                p++;
                            }
                            else
                            {
                                Diag(o.LineNo, o.KwCol, ".循环判断首 缺少 .循环判断尾，循环条件为空");
                                s.Cond = new Cond { Raw = "循环判断尾 ()" };
                            }
                            return s;
                        }
                    case "计次循环首":
                    case "变量循环首":
                        {
                            bool counter = o.Kw == "计次循环首";
                            var args = ParseLoopArgs(o, counter ? 2 : 4);
                            Stmt s;
                            List<Stmt> body;
                            if (counter)
                            {
                                var cs = new CounterStmt { Pos = pos, Mask = o.Mask, Raw = args == null ? o.HeadRaw : null, Comment = o.Comment };
                                if (args != null) cs.Args.AddRange(args);
                                s = cs; body = cs.Body;
                            }
                            else
                            {
                                var fs = new ForStmt { Pos = pos, Mask = o.Mask, Raw = args == null ? o.HeadRaw : null, Comment = o.Comment };
                                if (args != null) fs.Args.AddRange(args);
                                s = fs; body = fs.Body;
                            }
                            body.AddRange(ParseSeq(ref p, chain));
                            if (OwnedBy(p, oi, out var e))
                            {
                                CheckBare(e, true, true);
                                if (s is CounterStmt c2) c2.End = EndInfo(e); else ((ForStmt)s).End = EndInfo(e);
                                p++;
                            }
                            return s;
                        }
                    default:
                        return Raw(o, $"无法识别的流程控制 .{o.Kw}");
                }
            }
            finally
            {
                chain.RemoveAt(chain.Count - 1);
            }
        }
    }
}
