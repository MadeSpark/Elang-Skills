using System.Globalization;

namespace QAQ.ETool.Core.Text;

/// <summary>代码行词法单元种类</summary>
public enum CodeTokenKind
{
    Ident, Number, String, Date, ConstRef, MethodPtr, Operator,
    LParen, RParen, LBracket, RBracket, LBrace, RBrace, Comma, Dot,
    End, Error,
}

/// <summary>
/// 词法单元。Col/EndCol 为所在文件行内的 0 基列（[Col, EndCol)）。
/// Operator：Cmd = krnln 命令号（半角/全角 ＝ 都给 38，是否可作赋值看 <see cref="IsAssign"/>）。
/// String：Text 为引号内的内容；ConstRef：Text=名称、Member=成员；Error：Text 为错误信息。
/// </summary>
public readonly record struct CodeToken(CodeTokenKind Kind, string Text, int Col, int EndCol)
{
    public int Cmd { get; init; }
    public string? Member { get; init; }
    public DateTime Date { get; init; }
    /// <summary>原文是单个 `＝` 或 `=`（语句顶层第一个即赋值）</summary>
    public bool IsAssign => Kind == CodeTokenKind.Operator && (Text == "＝" || Text == "=");
}

/// <summary>
/// 子程序体代码行（已去掉缩进、屏蔽前缀与行尾注释）的词法分析器。
/// 不抛异常：遇到非法字符时产生一个 Error 单元并结束。
/// </summary>
public static class CodeLexer
{
    private const string NonIdentChars = "＋－×÷＼％＝≠＜＞≤≥≈“”（），、；：";

    /// <summary>标识符字符：字母、数字、_，以及除全角运算符/全角标点外的非 ASCII 字符</summary>
    public static bool IsIdentChar(char c) =>
        c == '_' || (c < 128 ? char.IsAsciiLetterOrDigit(c) : !char.IsWhiteSpace(c) && NonIdentChars.IndexOf(c) < 0);

    /// <summary>在 `“…”` 之外找第一个 `'`（行尾注释起点），找不到返回 -1</summary>
    public static int FindCommentStart(string s, int start = 0)
    {
        bool inStr = false;
        for (int i = start; i < s.Length; i++)
        {
            char c = s[i];
            if (inStr) { if (c == '”') inStr = false; }
            else if (c == '“') inStr = true;
            else if (c == '\'') return i;
        }
        return -1;
    }

    private static bool EndsOperand(List<CodeToken> ts)
    {
        if (ts.Count == 0) return false;
        switch (ts[^1].Kind)
        {
            case CodeTokenKind.Ident: case CodeTokenKind.Number: case CodeTokenKind.String:
            case CodeTokenKind.Date: case CodeTokenKind.ConstRef: case CodeTokenKind.MethodPtr:
            case CodeTokenKind.RParen: case CodeTokenKind.RBracket: case CodeTokenKind.RBrace:
                return true;
            default:
                return false;
        }
    }

    private static int FullWidthOp(char c) => c switch
    {
        '＋' => 19, '－' => 20, '×' => 15, '÷' => 16, '＼' => 17, '％' => 18,
        '＝' => 38, '≠' => 39, '＜' => 40, '＞' => 41, '≤' => 42, '≥' => 43, '≈' => 44,
        _ => 0,
    };

    /// <summary>分词；colBase 为 text[0] 在文件行内的列。结果总以 End（或 Error）结尾。</summary>
    public static List<CodeToken> Tokenize(string text, int colBase = 0)
    {
        var ts = new List<CodeToken>();
        int n = text.Length, i = 0;

        void Add(CodeTokenKind k, string t, int s, int e, int cmd = 0) =>
            ts.Add(new CodeToken(k, t, colBase + s, colBase + e) { Cmd = cmd });

        List<CodeToken> Fail(string msg, int at)
        {
            ts.Add(new CodeToken(CodeTokenKind.Error, msg, colBase + at, colBase + at));
            return ts;
        }

        while (i < n)
        {
            char c = text[i];
            if (char.IsWhiteSpace(c)) { i++; continue; }
            int s = i;

            // ---- 数值（含 `.5`）
            if (char.IsAsciiDigit(c) || (c == '.' && i + 1 < n && char.IsAsciiDigit(text[i + 1]) && !EndsOperand(ts)))
            {
                while (i < n && char.IsAsciiDigit(text[i])) i++;
                if (i < n && text[i] == '.' && i + 1 < n && char.IsAsciiDigit(text[i + 1]))
                {
                    i++;
                    while (i < n && char.IsAsciiDigit(text[i])) i++;
                }
                if (i < n && (text[i] == 'e' || text[i] == 'E'))
                {
                    int k = i + 1;
                    if (k < n && (text[k] == '+' || text[k] == '-')) k++;
                    if (k < n && char.IsAsciiDigit(text[k]))
                    {
                        i = k;
                        while (i < n && char.IsAsciiDigit(text[i])) i++;
                    }
                }
                Add(CodeTokenKind.Number, text[s..i], s, i);
                continue;
            }

            // ---- 文本 “…”
            if (c == '“')
            {
                int e = text.IndexOf('”', i + 1);
                if (e < 0) return Fail("文本常量缺少结束引号 ”", s);
                Add(CodeTokenKind.String, text.Substring(i + 1, e - i - 1), s, e + 1);
                i = e + 1;
                continue;
            }

            // ---- 标识符 / 且 / 或
            if (IsIdentChar(c))
            {
                while (i < n && IsIdentChar(text[i])) i++;
                var id = text[s..i];
                bool afterDot = ts.Count > 0 && ts[^1].Kind == CodeTokenKind.Dot;
                if (!afterDot && id == "且") Add(CodeTokenKind.Operator, id, s, i, 45);
                else if (!afterDot && id == "或") Add(CodeTokenKind.Operator, id, s, i, 46);
                else Add(CodeTokenKind.Ident, id, s, i);
                continue;
            }

            int fw = FullWidthOp(c);
            if (fw != 0) { i++; Add(CodeTokenKind.Operator, c.ToString(), s, i, fw); continue; }

            char nx = i + 1 < n ? text[i + 1] : '\0';
            switch (c)
            {
                case '(': i++; Add(CodeTokenKind.LParen, "(", s, i); continue;
                case ')': i++; Add(CodeTokenKind.RParen, ")", s, i); continue;
                case '{': i++; Add(CodeTokenKind.LBrace, "{", s, i); continue;
                case '}': i++; Add(CodeTokenKind.RBrace, "}", s, i); continue;
                case ',': i++; Add(CodeTokenKind.Comma, ",", s, i); continue;
                case ']': i++; Add(CodeTokenKind.RBracket, "]", s, i); continue;
                case '.': i++; Add(CodeTokenKind.Dot, ".", s, i); continue;
                case '[':
                    if (EndsOperand(ts)) { i++; Add(CodeTokenKind.LBracket, "[", s, i); continue; }
                    {
                        // 操作数位置：日期时间字面量
                        int e = text.IndexOf(']', i + 1);
                        if (e < 0) return Fail("日期时间常量缺少 ]", s);
                        var d = CodeParser.ParseDate(text.Substring(i, e - i + 1));
                        if (d == null) return Fail("无法识别的日期时间常量 " + text.Substring(i, e - i + 1), s);
                        ts.Add(new CodeToken(CodeTokenKind.Date, text.Substring(i, e - i + 1), colBase + s, colBase + e + 1) { Date = d.Value });
                        i = e + 1;
                        continue;
                    }
                case '#':
                    {
                        i++;
                        int ns = i;
                        while (i < n && IsIdentChar(text[i])) i++;
                        if (i == ns) return Fail("# 后缺少常量名", s);
                        var name = text[ns..i];
                        string? member = null;
                        if (i + 1 < n && text[i] == '.' && IsIdentChar(text[i + 1]))
                        {
                            int ms = ++i;
                            while (i < n && IsIdentChar(text[i])) i++;
                            member = text[ms..i];
                        }
                        ts.Add(new CodeToken(CodeTokenKind.ConstRef, name, colBase + s, colBase + i) { Member = member });
                        continue;
                    }
                case '&':
                    if (nx == '&') { i += 2; Add(CodeTokenKind.Operator, "&&", s, i, 45); continue; }
                    {
                        i++;
                        int ns = i;
                        while (i < n && IsIdentChar(text[i])) i++;
                        if (i == ns) return Fail("& 后缺少子程序名", s);
                        Add(CodeTokenKind.MethodPtr, text[ns..i], s, i);
                        continue;
                    }
                case '|':
                    if (nx == '|') { i += 2; Add(CodeTokenKind.Operator, "||", s, i, 46); continue; }
                    return Fail("无法识别的字符 |", s);
                case '+': i++; Add(CodeTokenKind.Operator, "+", s, i, 19); continue;
                case '-': i++; Add(CodeTokenKind.Operator, "-", s, i, 20); continue;
                case '*': i++; Add(CodeTokenKind.Operator, "*", s, i, 15); continue;
                case '/': i++; Add(CodeTokenKind.Operator, "/", s, i, 16); continue;
                case '\\': i++; Add(CodeTokenKind.Operator, "\\", s, i, 17); continue;
                case '%': i++; Add(CodeTokenKind.Operator, "%", s, i, 18); continue;
                case '=':
                    if (nx == '=') { i += 2; Add(CodeTokenKind.Operator, "==", s, i, 38); continue; }
                    i++; Add(CodeTokenKind.Operator, "=", s, i, 38); continue;
                case '!':
                    if (nx == '=') { i += 2; Add(CodeTokenKind.Operator, "!=", s, i, 39); continue; }
                    return Fail("无法识别的字符 !", s);
                case '<':
                    if (nx == '?') return Fail("未解析的占位符 <?…?>", s);
                    if (nx == '>') { i += 2; Add(CodeTokenKind.Operator, "<>", s, i, 39); continue; }
                    if (nx == '=') { i += 2; Add(CodeTokenKind.Operator, "<=", s, i, 42); continue; }
                    i++; Add(CodeTokenKind.Operator, "<", s, i, 40); continue;
                case '>':
                    if (nx == '=') { i += 2; Add(CodeTokenKind.Operator, ">=", s, i, 43); continue; }
                    i++; Add(CodeTokenKind.Operator, ">", s, i, 41); continue;
                default:
                    return Fail("无法识别的字符 " + c.ToString(CultureInfo.InvariantCulture), s);
            }
        }
        ts.Add(new CodeToken(CodeTokenKind.End, "", colBase + n, colBase + n));
        return ts;
    }
}
