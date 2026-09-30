namespace QAQ.ETool.Core.Text.Ast;

// =====================================================================================
// t2e 代码语法树（契约文件）。
// 由 CodeParser 从 *.e.txt 子程序体文本产生，由 Model/Binder 绑定成 EProjectFile 表达式。
// 这里只描述「文本上写了什么」，不做任何名称解析；名称一律保留原文。
// 行号 Line 均为所在文件的 1 基行号，Col 为该行内 0 基字符偏移（仅用于报错）。
// =====================================================================================

/// <summary>源位置</summary>
public readonly record struct SrcPos(int Line, int Col);

// ------------------------------------------------------------------ 表达式
public abstract class Expr
{
    public SrcPos Pos { get; init; }
    /// <summary>文本里被显式括号包住（`(a ＋ b)`）。括号内的同运算符不与外层扁平合并。</summary>
    public bool Parenthesized { get; set; }
}

/// <summary>数值字面量。Raw 为原文（如 `-1`、`1.5e+017`），Value 为解析值。</summary>
public sealed class NumberLit : Expr { public double Value { get; init; } public string Raw { get; init; } = ""; }

/// <summary>文本字面量 `“…”`（无转义，内容原样）</summary>
public sealed class StringLit : Expr { public string Value { get; init; } = ""; }

/// <summary>`真` / `假`</summary>
public sealed class BoolLit : Expr { public bool Value { get; init; } }

/// <summary>日期时间字面量 `[2020年1月2日3时4分5秒]`</summary>
public sealed class DateLit : Expr { public DateTime Value { get; init; } }

/// <summary>
/// 常量引用：`#名称` → Name=名称, Member=null；`#类型.成员`（枚举/库类型常量）→ Name=类型, Member=成员。
/// </summary>
public sealed class ConstRef : Expr { public string Name { get; init; } = ""; public string? Member { get; init; } }

/// <summary>子程序指针 `&amp;名称`</summary>
public sealed class MethodPtr : Expr { public string Name { get; init; } = ""; }

/// <summary>裸标识符（变量 / 参数 / 组件 / 窗口名 / 类名 …，由 Binder 决定）</summary>
public sealed class NameRef : Expr { public string Name { get; init; } = ""; }

/// <summary>
/// 数组字面量 `{ a, b }`；`{ }` 为空数组。
/// 元素为 null 表示该位置留空（`{ 1, , 3 }`）。
/// </summary>
public sealed class ArrayLit : Expr { public List<Expr?> Items { get; } = new(); }

/// <summary>
/// 调用 `名称 (参数, …)` 或 `目标.名称 (参数, …)`。
/// Args 中 null 表示省略的参数（`f (, x)` → [null, x]）。
/// `f ()` → Args 为空列表（0 个参数）。注意 `f ( )` 同样为空列表。
/// </summary>
public sealed class CallExpr : Expr
{
    public Expr? Target { get; init; }
    public string Name { get; init; } = "";
    public List<Expr?> Args { get; } = new();
}

/// <summary>成员访问 `目标.名称`（不带括号）</summary>
public sealed class MemberExpr : Expr { public Expr Target { get; init; } = null!; public string Name { get; init; } = ""; }

/// <summary>数组下标 `目标 [下标]`</summary>
public sealed class IndexExpr : Expr { public Expr Target { get; init; } = null!; public Expr Index { get; init; } = null!; }

/// <summary>
/// 运算符表达式（含赋值）。Cmd = krnln 命令号：
///   15 ×  16 ÷  17 ＼  18 ％  19 ＋  20 －(减)  21 －(负，一元)
///   38 ＝(相等)  39 ≠  40 ＜  41 ＞  42 ≤  43 ≥  44 ≈  45 且  46 或
///   52 ＝(赋值，仅语句顶层第一个 ＝)
/// 可多元（扁平化）的：15 16 17 18 19 20 45 46 —— 同一 Cmd 连写且未被括号隔开时合并为一个多操作数节点，
/// 左结合：`a － b － c` → Op(20,[a,b,c])；`a × b ÷ c` → Op(16,[Op(15,[a,b]), c])。
/// 比较（38–44）与赋值（52）恒为 2 个操作数；负号（21）恒为 1 个。
/// </summary>
public sealed class OpExpr : Expr
{
    public int Cmd { get; init; }
    public List<Expr> Operands { get; } = new();
}

// ------------------------------------------------------------------ 语句
public abstract class Stmt
{
    public SrcPos Pos { get; init; }
    /// <summary>被 IDE 屏蔽（文本以 `' ` 开头的流程控制行）</summary>
    public bool Mask { get; set; }
}

/// <summary>
/// 表达式语句 / 空语句 / 整行注释。
///   Expr != null：普通语句（Expr 通常是 CallExpr，或 Cmd=52 的赋值 OpExpr）
///   Expr == null &amp;&amp; Comment == null：空语句（纯缩进空格行）
///   Expr == null &amp;&amp; Comment != null：整行注释
/// Comment 为 `' ` 之后的原文（只去掉 `'` 后面固定的那一个空格，其余前导空格保留）。
/// </summary>
public sealed class ExprStmt : Stmt
{
    public Expr? Expr { get; init; }
    public string? Comment { get; init; }
}

/// <summary>
/// 无法解析的语句行（宽松模式的兜底）。Code 为去掉缩进（及 `' ` 屏蔽前缀）后的原文，
/// 将写成 EProjectFile 的 UnexaminedStatement（IDE 显示为红色未验证代码）。
/// </summary>
public sealed class RawStmt : Stmt { public string Code { get; init; } = ""; public string Error { get; init; } = ""; }

/// <summary>
/// 流程控制头的条件。Expr 解析成功时非 null；失败时 Raw 保存原文（整个头，去掉前导 `.`，
/// 例如 `如果真 (a ＝ ` ），将写成 UnexaminedCode。
/// </summary>
public sealed class Cond
{
    public Expr? Expr { get; init; }
    public string? Raw { get; init; }
}

/// <summary>`.如果真 (c)` … `.如果真结束`</summary>
public sealed class IfTrueStmt : Stmt
{
    public Cond Cond { get; init; } = new();
    public string? Comment { get; init; }
    public List<Stmt> Body { get; } = new();
}

/// <summary>`.如果 (c)` … `.否则` … `.如果结束`</summary>
public sealed class IfElseStmt : Stmt
{
    public Cond Cond { get; init; } = new();
    public string? Comment { get; init; }
    public List<Stmt> Then { get; } = new();
    public List<Stmt> Else { get; } = new();
}

public sealed class SwitchCase
{
    public SrcPos Pos { get; init; }
    public bool Mask { get; init; }
    public Cond Cond { get; init; } = new();
    public string? Comment { get; init; }
    public List<Stmt> Body { get; } = new();
}

/// <summary>
/// `.判断开始 (c1)` … [`.判断 (c2)` …]* `.默认` … `.判断结束`。
/// Cases[0] 来自 `.判断开始`。Stmt.Mask = Cases[0].Mask。
/// 缺 `.默认` 时 HasDefault=false、Default 为空（Binder 按 IDE 惯例补空的默认分支）。
/// </summary>
public sealed class SwitchStmt : Stmt
{
    public List<SwitchCase> Cases { get; } = new();
    public bool HasDefault { get; set; }
    public List<Stmt> Default { get; } = new();
}

/// <summary>循环尾部信息（屏蔽 / 行尾注释）</summary>
public sealed class LoopEnd
{
    public SrcPos Pos { get; init; }
    public bool Mask { get; init; }
    public string? Comment { get; init; }
}

/// <summary>`.判断循环首 (c)` … `.判断循环尾 ()`</summary>
public sealed class WhileStmt : Stmt
{
    public Cond Cond { get; init; } = new();
    public string? Comment { get; init; }
    public List<Stmt> Body { get; } = new();
    public LoopEnd End { get; set; } = new();
}

/// <summary>`.循环判断首 ()` … `.循环判断尾 (c)`；Cond/EndComment 在尾部</summary>
public sealed class DoWhileStmt : Stmt
{
    public string? Comment { get; init; }
    public List<Stmt> Body { get; } = new();
    public LoopEnd End { get; set; } = new();
    public Cond Cond { get; set; } = new();
}

/// <summary>
/// `.计次循环首 (次数, 变量)` … `.计次循环尾 ()`。Args 为头部参数（可含 null = 省略），
/// 解析失败时 Raw 保存原文（去掉前导 `.`）。
/// </summary>
public sealed class CounterStmt : Stmt
{
    public List<Expr?> Args { get; } = new();
    public string? Raw { get; init; }
    public string? Comment { get; init; }
    public List<Stmt> Body { get; } = new();
    public LoopEnd End { get; set; } = new();
}

/// <summary>`.变量循环首 (起, 止, 步长, 变量)` … `.变量循环尾 ()`，其余同 CounterStmt</summary>
public sealed class ForStmt : Stmt
{
    public List<Expr?> Args { get; } = new();
    public string? Raw { get; init; }
    public string? Comment { get; init; }
    public List<Stmt> Body { get; } = new();
    public LoopEnd End { get; set; } = new();
}

/// <summary>解析诊断</summary>
public sealed record CodeDiagnostic(string File, int Line, int Col, string Message)
{
    public override string ToString() => $"{File}:{Line}:{Col + 1}: {Message}";
}
