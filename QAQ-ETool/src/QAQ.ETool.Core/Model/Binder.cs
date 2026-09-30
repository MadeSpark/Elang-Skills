using OpenEpl.ELibInfo;
using QIQI.EProjectFile;
using QIQI.EProjectFile.Expressions;
using QIQI.EProjectFile.Statements;
using QAQ.ETool.Core.Text;
using QAQ.ETool.Core.Text.Ast;

namespace QAQ.ETool.Core.Model;

/// <summary>表达式的静态类型：数据类型 ID + 是否数组。仅用于成员/方法查找。</summary>
internal readonly record struct TRef(int Type, bool Array);

internal sealed class BindException : Exception
{
    public SrcPos Pos { get; }
    public BindException(string msg, SrcPos pos) : base(msg) { Pos = pos; }
}

/// <summary>
/// 代码绑定：Ast（CodeParser 产物）→ EProjectFile 语句/表达式。是 CodeRenderer 的逆过程。
/// 名称查找顺序见 docs/开发记录.md「Binder 设计」。绑定失败的语句按「未验证代码」写入（宽松模式记警告，严格模式记错误）。
/// </summary>
internal sealed class Binder
{
    private readonly Builder _b;
    private SMethod _m = null!;
    private SClass _c = null!;
    private SForm? _f;
    private readonly Dictionary<string, (int Id, TRef T)> _locals = new(StringComparer.Ordinal);

    public Binder(Builder b) { _b = b; }

    private short K => (short)_b.Krnln;

    // ================================================================ 入口
    public StatementBlock BindMethod(SMethod m, List<Stmt> body)
    {
        _m = m;
        _c = m.Class;
        _f = _c.Form;
        _locals.Clear();
        foreach (var p in m.Info.Parameters)
            if (!string.IsNullOrEmpty(p.Name)) _locals.TryAdd(p.Name, (p.Id, new TRef(p.DataType, p.ArrayParameter)));
        foreach (var v in m.Info.Variables)
            if (!string.IsNullOrEmpty(v.Name)) _locals.TryAdd(v.Name, (v.Id, new TRef(v.DataType, v.UBound.Length > 0)));

        var block = Block(body);
        if (block.Count == 0) block.Add(new ExpressionStatement());
        return block;
    }

    private string Where(SrcPos p) => $"{_c.T.File}:{p.Line}";

    private void Fail(SrcPos p, string msg)
    {
        if (_b.Lenient) _b.Warn(Where(p), msg + "（按未验证代码写入）");
        else _b.Error(Where(p), msg);
        _b.CountUnexamined();
    }

    // ================================================================ 语句
    private StatementBlock Block(List<Stmt> stmts)
    {
        var block = new StatementBlock();
        foreach (var s in stmts) block.Add(Statement(s));
        return block;
    }

    private Statement Statement(Stmt s)
    {
        switch (s)
        {
            case ExprStmt es:
                if (es.Expr == null) return new ExpressionStatement(null, es.Mask, es.Comment);
                try
                {
                    return new ExpressionStatement(StmtCall(es.Expr), es.Mask, es.Comment);
                }
                catch (BindException e)
                {
                    Fail(e.Pos, e.Message);
                    return new UnexaminedStatement(CodePrinter.Print(es.Expr), es.Mask) { Comment = es.Comment };
                }
            case RawStmt rs:
                _b.CountUnexamined();
                return new UnexaminedStatement(rs.Code, rs.Mask);
            case IfTrueStmt it:
            {
                var (cond, raw) = Condition(it.Cond, "如果真", it.Pos);
                return new IfStatement { Condition = cond, UnexaminedCode = raw, Block = Block(it.Body), Comment = it.Comment, Mask = it.Mask };
            }
            case IfElseStmt ie:
            {
                var (cond, raw) = Condition(ie.Cond, "如果", ie.Pos);
                return new IfElseStatement
                {
                    Condition = cond, UnexaminedCode = raw, Comment = ie.Comment, Mask = ie.Mask,
                    BlockOnTrue = Block(ie.Then), BlockOnFalse = Block(ie.Else),
                };
            }
            case SwitchStmt sw:
            {
                var r = new SwitchStatement();
                for (int i = 0; i < sw.Cases.Count; i++)
                {
                    var c = sw.Cases[i];
                    var (cond, raw) = Condition(c.Cond, "判断", c.Pos, i == 0 ? "判断开始" : null);
                    r.Case.Add(new SwitchStatement.CaseInfo { Condition = cond, UnexaminedCode = raw, Block = Block(c.Body), Comment = c.Comment, Mask = c.Mask });
                }
                r.DefaultBlock = Block(sw.Default);   // 缺 .默认 时为空块（IDE 惯例）
                return r;
            }
            case WhileStmt ws:
            {
                var (cond, raw) = Condition(ws.Cond, "判断循环首", ws.Pos);
                var r = new WhileStatement { Condition = cond, UnexaminedCode = raw, Block = Block(ws.Body) };
                Loop(r, ws.Mask, ws.Comment, ws.End);
                return r;
            }
            case DoWhileStmt dw:
            {
                var (cond, raw) = Condition(dw.Cond, "循环判断尾", dw.End.Pos);
                var r = new DoWhileStatement { Condition = cond, UnexaminedCode = raw, Block = Block(dw.Body) };
                Loop(r, dw.Mask, dw.Comment, dw.End);
                return r;
            }
            case CounterStmt cs:
            {
                var r = new CounterStatement { Block = Block(cs.Body) };
                Loop(r, cs.Mask, cs.Comment, cs.End);
                if (cs.Raw != null) { r.UnexaminedCode = cs.Raw; _b.CountUnexamined(); return r; }
                try
                {
                    if (cs.Args.Count != 2) throw new BindException($"计次循环首 需要 2 个参数，实际 {cs.Args.Count} 个", cs.Pos);
                    r.Count = Arg(cs.Args[0]);
                    r.Var = Arg(cs.Args[1]);
                }
                catch (BindException e)
                {
                    Fail(e.Pos, e.Message);
                    r.Count = r.Var = null;
                    r.UnexaminedCode = "计次循环首 (" + string.Join(", ", cs.Args.Select(CodePrinter.Print)) + ")";
                }
                return r;
            }
            case ForStmt fs:
            {
                var r = new ForStatement { Block = Block(fs.Body) };
                Loop(r, fs.Mask, fs.Comment, fs.End);
                if (fs.Raw != null) { r.UnexaminedCode = fs.Raw; _b.CountUnexamined(); return r; }
                try
                {
                    if (fs.Args.Count != 4) throw new BindException($"变量循环首 需要 4 个参数，实际 {fs.Args.Count} 个", fs.Pos);
                    r.Start = Arg(fs.Args[0]);
                    r.End = Arg(fs.Args[1]);
                    r.Step = Arg(fs.Args[2]);
                    r.Var = Arg(fs.Args[3]);
                }
                catch (BindException e)
                {
                    Fail(e.Pos, e.Message);
                    r.Start = r.End = r.Step = r.Var = null;
                    r.UnexaminedCode = "变量循环首 (" + string.Join(", ", fs.Args.Select(CodePrinter.Print)) + ")";
                }
                return r;
            }
            default:
                throw new NotSupportedException($"未支持的语句类型: {s.GetType().Name}");
        }
    }

    private static void Loop(LoopStatement r, bool mask, string? comment, LoopEnd end)
    {
        r.MaskOnStart = mask;
        r.CommentOnStart = comment;
        r.MaskOnEnd = end.Mask;
        r.CommentOnEnd = end.Comment;
    }

    /// <summary>
    /// 流程控制头条件 → (条件表达式, 未验证代码)。未验证代码不带前导 `.`；
    /// `.判断开始` 在 .e 里存作「判断」（CodeRenderer.Head 的逆）。
    /// </summary>
    private (Expression? Cond, string? Raw) Condition(Cond c, string kw, SrcPos pos, string? rawKw = null)
    {
        if (c.Raw != null)
        {
            _b.CountUnexamined();
            var raw = c.Raw;
            if (rawKw != null && raw.StartsWith(rawKw, StringComparison.Ordinal)) raw = kw + raw.Substring(rawKw.Length);
            return (null, raw);
        }
        if (c.Expr == null) return (DefaultValueExpression.Instance, null);
        try
        {
            return (Bind(c.Expr).E, null);
        }
        catch (BindException e)
        {
            Fail(e.Pos, e.Message);
            return (null, kw + " (" + CodePrinter.Print(c.Expr) + ")");
        }
    }

    private Expression Arg(Expr? e) => e == null ? DefaultValueExpression.Instance : Bind(e).E;

    /// <summary>语句顶层：必须是调用或运算（含赋值）</summary>
    private CallExpression StmtCall(Expr e)
    {
        var (x, _) = Bind(e);
        return x as CallExpression ?? throw new BindException("语句必须是子程序/命令调用或赋值", e.Pos);
    }

    // ================================================================ 表达式
    private static readonly TRef TInt = new(EplSystemId.DataType_Int, false);
    private static readonly TRef TDouble = new(EplSystemId.DataType_Double, false);
    private static readonly TRef TBool = new(EplSystemId.DataType_Bool, false);
    private static readonly TRef TString = new(EplSystemId.DataType_String, false);
    private static readonly TRef TDate = new(EplSystemId.DataType_DateTime, false);
    private static readonly TRef TVoid = new(0, false);

    private (Expression E, TRef? T) Bind(Expr e)
    {
        switch (e)
        {
            case NumberLit n:
                return (new NumberLiteral(n.Value), n.Value == Math.Floor(n.Value) && Math.Abs(n.Value) <= int.MaxValue ? TInt : TDouble);
            case StringLit s:
                return (new StringLiteral(s.Value), TString);
            case BoolLit b:
                return (BoolLiteral.ValueOf(b.Value), TBool);
            case DateLit d:
                return (new DateTimeLiteral(d.Value), TDate);
            case ConstRef cr:
                return Const(cr);
            case MethodPtr mp:
            {
                var m = FindUserMethod(mp.Name) ?? throw new BindException($"未定义的子程序 {mp.Name}", mp.Pos);
                return (new MethodPtrExpression(m.Info.Id), new TRef(EplSystemId.DataType_MethodPtr, false));
            }
            case NameRef nr:
                return Name(nr.Name, nr.Pos) ?? throw new BindException($"未定义名称 {nr.Name}", nr.Pos);
            case ArrayLit al:
            {
                var a = new ArrayLiteralExpression();
                foreach (var x in al.Items) a.Add(Arg(x));
                return (a, null);
            }
            case CallExpr ce:
                return Call(ce);
            case MemberExpr me:
            {
                var (t, tt) = Bind(me.Target);
                return Member(t, tt, me.Name, me.Pos);
            }
            case IndexExpr ie:
            {
                var (t, tt) = Bind(ie.Target);
                var (i, _) = Bind(ie.Index);
                return (new AccessArrayExpression(t, i), tt is { } r ? r with { Array = false } : null);
            }
            case OpExpr op:
            {
                var c = new CallExpression(K, op.Cmd, Params(op.Operands));
                TRef? t = op.Cmd switch
                {
                    >= 38 and <= 46 => TBool,
                    52 => TVoid,
                    _ => null,
                };
                return (c, t);
            }
            default:
                throw new BindException($"未支持的表达式 {e.GetType().Name}", e.Pos);
        }
    }

    private ParamListExpression Params(IEnumerable<Expr?> args)
    {
        var p = new ParamListExpression();
        foreach (var a in args) p.Add(Arg(a));
        return p;
    }

    // ---------------------------------------------------------------- 常量
    private (Expression, TRef?) Const(ConstRef cr)
    {
        if (cr.Member == null)
        {
            foreach (var sc in ScopeOrder())
                if (sc.Consts.TryGetValue(cr.Name, out var ci)) return (new ConstantExpression(ci.Id), ConstType(ci));
            if (_b.LibConsts.TryGetValue(cr.Name, out var lc))
            {
                var info = _b.Libs[lc.Lib]!.Constants[lc.Idx];
                return (new ConstantExpression((short)lc.Lib, lc.Idx), new TRef(_b.LibDataType(lc.Lib, info.DataType), false));
            }
            throw new BindException($"未定义的常量 #{cr.Name}", cr.Pos);
        }
        if (_b.LibTypes.TryGetValue(cr.Name, out var lt))
        {
            var t = _b.LibType(lt.Lib, lt.Type)!;
            for (int i = 0; i < t.Members.Length; i++)
                if (t.Members[i].Name == cr.Member)
                    return (new EmnuConstantExpression((short)lt.Type, (short)lt.Lib, i),
                        new TRef(EplSystemId.MakeLibDataTypeId((short)lt.Lib, (short)lt.Type), false));
            throw new BindException($"{cr.Name} 没有成员 {cr.Member}", cr.Pos);
        }
        throw new BindException($"未定义的常量类型 #{cr.Name}", cr.Pos);
    }

    private static TRef? ConstType(ConstantInfo c) => c.Value switch
    {
        string => TString,
        double => TDouble,
        bool => TBool,
        DateTime => TDate,
        byte[] => new TRef(EplSystemId.DataType_Bin, false),
        _ => null,
    };

    // ---------------------------------------------------------------- 名称
    /// <summary>本模块 → 主工程 → 其他模块</summary>
    private IEnumerable<SScope> ScopeOrder()
    {
        int mod = _c.Mod;
        if (mod >= 0) yield return _b.ScopeOf(mod);
        yield return _b.Main;
        for (int i = 0; i < _b.Scopes.Count - 1; i++)
            if (i != mod) yield return _b.ScopeOf(i);
    }

    private IEnumerable<SClass> ClassChain(SClass? c)
    {
        var seen = new HashSet<SClass>(ReferenceEqualityComparer.Instance);
        for (; c != null && seen.Add(c); c = c.Base) yield return c;
    }

    private static TRef VarType(AbstractVariableInfo v) => new(v.DataType, v.UBound.Length > 0);

    private (Expression, TRef?)? Name(string name, SrcPos pos)
    {
        if (_locals.TryGetValue(name, out var l)) return (new VariableExpression(l.Id), l.T);
        foreach (var c in ClassChain(_c))
            if (c.Vars.TryGetValue(name, out var cv)) return (new VariableExpression(cv.Id), VarType(cv));
        if (_f != null)
        {
            if (_f.Elems.TryGetValue(name, out var el)) return (new VariableExpression(el.Id), new TRef(el.DataType, false));
            // 窗口程序集里写窗口名指代自身：原件存的是 Var(窗口 ID)，不是 FormSelf（大项目 `下拉.可视 ＝ 假` 实测）
            if (_f.Name == name) return (new VariableExpression(_f.Info.Id), new TRef(_f.Info.Id, false));
        }
        foreach (var sc in ScopeOrder())
            if (sc.Globals.TryGetValue(name, out var g)) return (new VariableExpression(g.Id), VarType(g));
        if (_b.FormByName.TryGetValue(name, out var f)) return (new VariableExpression(f.Info.Id), new TRef(f.Info.Id, false));
        return null;
    }

    // ---------------------------------------------------------------- 成员访问
    private (Expression, TRef?) Member(Expression target, TRef? tt, string name, SrcPos pos)
    {
        if (tt is not { } t) throw new BindException($"无法确定 .{name} 左侧的类型", pos);
        if (t.Array) throw new BindException($"数组没有成员 {name}", pos);
        int type = t.Type;
        int kind = EplSystemId.GetType(type);

        if (kind == EplSystemId.Type_Struct && _b.StructMembers.TryGetValue(type, out var sm))
        {
            if (sm.TryGetValue(name, out var mi)) return (new AccessMemberExpression(target, type, mi.Id), VarType(mi));
            throw new BindException($"数据类型 {_b.StructById[type].Name} 没有成员 {name}", pos);
        }
        if (kind == EplSystemId.Type_Form && _b.FormById.TryGetValue(type, out var f))
        {
            if (f.Elems.TryGetValue(name, out var el)) return (new AccessMemberExpression(target, type, el.Id), new TRef(el.DataType, false));
            var w = _b.LibType(_b.Krnln, _b.WindowTypeIndex);
            if (w != null)
                for (int i = 0; i < w.Members.Length; i++)
                    if (w.Members[i].Name == name)
                        // 注：本窗口自引用在原件里有时存 FormSelf、有时存 Var(窗口 ID)，文本无法区分，统一用后者（归旁路）
                        return (new AccessMemberExpression(target, type, i + 1), new TRef(_b.LibDataType(_b.Krnln, w.Members[i].DataType), false));
            // `窗口名.程序集变量`（大项目 `设置窗口.CQ.数据初始化 ()` 实测）
            if (f.Class != null && f.Class.Vars.TryGetValue(name, out var cv))
                return (new AccessMemberExpression(target, type, cv.Id), VarType(cv));
            throw new BindException($"窗口 {f.Name} 没有组件或属性 {name}", pos);
        }
        if (EplSystemId.IsLibDataType(type))
        {
            EplSystemId.DecomposeLibDataTypeId(type, out var lib, out var ti);
            var lt = _b.LibType(lib, ti) ?? throw new BindException($"支持库类型 0x{type:X8} 未加载", pos);
            for (int i = 0; i < lt.Members.Length; i++)
                if (lt.Members[i].Name == name)
                    return (new AccessMemberExpression(target, (short)lib, ti, i), new TRef(_b.LibDataType(lib, lt.Members[i].DataType), false));
            if (lt.Kind == ELibDataTypeKind.Component)
            {
                var w = _b.LibType(_b.Krnln, _b.WindowTypeIndex);
                if (w != null)
                    for (int i = 0; i < w.Members.Length; i++)
                        if (w.Members[i].Name == name)
                            return (new AccessMemberExpression(target, K, _b.WindowTypeIndex, i), new TRef(_b.LibDataType(_b.Krnln, w.Members[i].DataType), false));
            }
            throw new BindException($"{lt.Name} 没有成员 {name}", pos);
        }
        if (kind == EplSystemId.Type_Class) throw new BindException($"类不能直接访问成员 {name}（请用方法）", pos);
        throw new BindException($"类型 0x{type:X8} 没有成员 {name}", pos);
    }

    // ---------------------------------------------------------------- 调用
    private SMethod? FindUserMethod(string name)
    {
        foreach (var c in ClassChain(_c))
            if (c.MethodByName.TryGetValue(name, out var m)) return m;
        foreach (var sc in ScopeOrder())
            if (sc.StaticMethods.TryGetValue(name, out var m)) return m;
        foreach (var sc in ScopeOrder())
            if (sc.FormMethods.TryGetValue(name, out var m)) return m;
        return null;
    }

    /// <summary>
    /// 只有 1 个参数的支持库命令写成 `f ()` 时，IDE 存的是 1 个省略参数（`返回 ()`、`销毁 ()` 实测）；
    /// 用户子程序的 `f ()` 存 0 个参数（大项目 `TimeStamp ()` 347 处实测），不补。
    /// 2 个及以上的省略在文本里会带逗号，不存在歧义。
    /// </summary>
    private static void PadSingle(ParamListExpression ps, int declared)
    {
        if (ps.Count == 0 && declared == 1) ps.Add(DefaultValueExpression.Instance);
    }

    private (Expression, TRef?) UserCall(SMethod m, ParamListExpression ps, Expression? target = null, bool special = false)
    {
        return (new CallExpression(-2, m.Info.Id, ps) { Target = target, InvokeSpecial = special }, new TRef(m.Info.ReturnDataType, false));
    }

    private (Expression, TRef?) LibCall(int lib, int cmd, ParamListExpression ps, Expression? target = null)
    {
        var info = _b.LibCmd(lib, cmd)!;
        PadSingle(ps, info.Parameters.Length);
        return (new CallExpression((short)lib, cmd, ps) { Target = target }, new TRef(_b.LibDataType(lib, info.ReturnDataType), info.ReturnArray));
    }

    private int FindTypeMethod(int lib, int type, string name)
    {
        var t = _b.LibType(lib, type);
        if (t == null) return -1;
        foreach (var c in t.Methods)
            if (_b.LibCmd(lib, c)?.Name == name) return c;
        return -1;
    }

    private (Expression, TRef?) Call(CallExpr ce)
    {
        var ps = Params(ce.Args);
        if (ce.Target == null)
        {
            foreach (var c in ClassChain(_c))
                if (c.MethodByName.TryGetValue(ce.Name, out var m)) return UserCall(m, ps);
            foreach (var sc in ScopeOrder())
            {
                if (sc.StaticMethods.TryGetValue(ce.Name, out var m)) return UserCall(m, ps);
                if (sc.Dlls.TryGetValue(ce.Name, out var d))
                    return (new CallExpression(-3, d.Id, ps), new TRef(d.ReturnDataType, false));
            }
            foreach (var sc in ScopeOrder())
                if (sc.FormMethods.TryGetValue(ce.Name, out var m)) return UserCall(m, ps);
            if (_b.LibGlobalCmds.TryGetValue(ce.Name, out var lc)) return LibCall(lc.Lib, lc.Cmd, ps);
            if (_f != null)
            {
                // 窗口程序集里直接调用窗口自身的方法（`销毁 ()`）
                var wc = FindTypeMethod(_b.Krnln, _b.WindowTypeIndex, ce.Name);
                if (wc >= 0) return LibCall(_b.Krnln, wc, ps);
            }
            throw new BindException($"未定义的子程序或命令 {ce.Name}", ce.Pos);
        }

        // `类名.方法 ()`：调用基类方法（InvokeSpecial）
        if (ce.Target is NameRef nr && Name(nr.Name, nr.Pos) == null)
        {
            foreach (var sc in ScopeOrder())
                if (sc.Classes.TryGetValue(nr.Name, out var cls))
                {
                    foreach (var c in ClassChain(cls))
                        if (c.MethodByName.TryGetValue(ce.Name, out var m)) return UserCall(m, ps, null, true);
                    throw new BindException($"程序集 {nr.Name} 没有方法 {ce.Name}", ce.Pos);
                }
            throw new BindException($"未定义名称 {nr.Name}", nr.Pos);
        }

        var (target, tt) = Bind(ce.Target);
        if (tt is not { } t) throw new BindException($"无法确定 .{ce.Name} 左侧的类型", ce.Pos);
        if (t.Array) throw new BindException($"数组没有方法 {ce.Name}", ce.Pos);
        int kind = EplSystemId.GetType(t.Type);
        if (kind == EplSystemId.Type_Class && _b.ClassById.TryGetValue(t.Type, out var uc))
        {
            foreach (var c in ClassChain(uc))
                if (c.MethodByName.TryGetValue(ce.Name, out var m)) return UserCall(m, ps, target);
            throw new BindException($"类 {uc.T.Name} 没有方法 {ce.Name}", ce.Pos);
        }
        if (kind == EplSystemId.Type_Form)
        {
            var wc = FindTypeMethod(_b.Krnln, _b.WindowTypeIndex, ce.Name);
            if (wc >= 0) return LibCall(_b.Krnln, wc, ps, target);
            // `窗口名.窗口程序集子程序 ()`（大项目 `下拉.下拉_置托盘图标 ()` 实测：Call(-2, m, tgt=Var(窗口 ID))）
            if (_b.FormById.TryGetValue(t.Type, out var tf) && tf.Class != null
                && tf.Class.MethodByName.TryGetValue(ce.Name, out var fm))
                return UserCall(fm, ps, target);
            throw new BindException($"窗口没有方法 {ce.Name}", ce.Pos);
        }
        if (EplSystemId.IsLibDataType(t.Type))
        {
            EplSystemId.DecomposeLibDataTypeId(t.Type, out var lib, out var ti);
            var lt = _b.LibType(lib, ti) ?? throw new BindException($"支持库类型 0x{t.Type:X8} 未加载", ce.Pos);
            var mc = FindTypeMethod(lib, ti, ce.Name);
            if (mc >= 0) return LibCall(lib, mc, ps, target);
            if (lt.Kind == ELibDataTypeKind.Component)
            {
                var wc = FindTypeMethod(_b.Krnln, _b.WindowTypeIndex, ce.Name);
                if (wc >= 0) return LibCall(_b.Krnln, wc, ps, target);
            }
            throw new BindException($"{lt.Name} 没有方法 {ce.Name}", ce.Pos);
        }
        throw new BindException($"类型 0x{t.Type:X8} 没有方法 {ce.Name}", ce.Pos);
    }
}
