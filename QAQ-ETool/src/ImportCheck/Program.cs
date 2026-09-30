using System.Text;
using ImportCheck;
using QAQ.ETool.Core.Text;

Console.OutputEncoding = Encoding.UTF8;
Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

int fails = ManualCases.Run();

var baseDir = args.Length > 0 ? args[0] : @"C:\Users\MadeSpark\Desktop\测试\QAQ-ETool\tests\out";
foreach (var proj in new[] { "大项目测试", "新增 文字文件", "窗口属性无法解析" })
    foreach (var lay in new[] { "en", "cn" })
        fails += DirCheck.Run(Path.Combine(baseDir, proj, lay));

Console.WriteLine();
Console.WriteLine(fails == 0 ? "== 全部通过 ==" : $"== 失败项：{fails} ==");
return fails == 0 ? 0 : 1;

static class DirCheck
{
    public static int Run(string root)
    {
        Console.WriteLine();
        Console.WriteLine($"################ {root}");
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var p = Importer.Load(root);
        sw.Stop();

        var allClasses = p.Classes.Concat(p.Modules.SelectMany(m => m.Classes)).ToList();
        var methods = allClasses.SelectMany(c => c.Methods).ToList();
        var locals = methods.SelectMany(m => m.Locals).ToList();
        var sets = new[] { p.Main }.Concat(p.Modules.Select(m => m.Decls)).ToList();
        Console.WriteLine($"布局={(p.Layout == Layout.Chinese ? "中文" : "英文")} 耗时={sw.ElapsedMilliseconds}ms etprj={(p.Etprj != null ? "有" : "无")} 支持库={p.Libs.Count} EcInit={p.EcInit.Count} Epack={p.EpackFileNames.Count} 图标={p.Icon.Length}B");
        Console.WriteLine($"程序集={p.Classes.Count}(主)+{allClasses.Count - p.Classes.Count}(模块) 子程序={methods.Count} 参数={methods.Sum(m => m.Params.Count)} " +
                          $"局部变量={locals.Count}(无名 {locals.Count(v => v.Name.Length == 0)}) 程序集变量={allClasses.Sum(c => c.Vars.Count)}(无名 {allClasses.Sum(c => c.Vars.Count(v => v.Name.Length == 0))}) " +
                          $"体行={methods.Sum(m => m.Body.Count)} 空体={methods.Count(m => m.Body.Count == 0)}");
        Console.WriteLine($"常量={sets.Sum(s => s.Consts.Count)}(无名 {sets.Sum(s => s.Consts.Count(c => c.Name.Length == 0))}) 全局={sets.Sum(s => s.Globals.Count)} 数据类型={sets.Sum(s => s.Structs.Count)} " +
                          $"DLL={sets.Sum(s => s.Dlls.Count)} 窗口={p.Forms.Count} 资源={p.Resources.Count}({string.Join(",", p.Resources.GroupBy(r => r.Kind).Select(g => g.Key + ":" + g.Count()))}) " +
                          $"模块={p.Modules.Count} 分组={p.FolderOrders.Count} 展开={p.ExpandedDirs.Count}");
        Console.WriteLine($"Errors={p.Errors.Count} Warnings={p.Warnings.Count}");
        foreach (var e in p.Errors.Take(10)) Console.WriteLine("  E " + e);
        foreach (var w in p.Warnings.Take(10)) Console.WriteLine("  W " + w);

        // ---- 回写比对
        var pathProblems = new List<string>();
        var files = Rewriter.Rewrite(p, pathProblems);
        int same = 0, diff = 0, shown = 0;
        var covered = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var (rel, data) in files)
        {
            covered.Add(rel);
            var full = Path.Combine(root, rel);
            if (!File.Exists(full))
            {
                diff++;
                if (shown++ < 10) Console.WriteLine($"  D {rel}: 原文件不存在（回写 {data.Length}B）");
                continue;
            }
            var orig = File.ReadAllBytes(full);
            if (orig.AsSpan().SequenceEqual(data)) { same++; continue; }
            diff++;
            if (shown++ < 10) Console.WriteLine("  D " + FirstDiff(rel, orig, data));
        }
        // 原样 JSON 回写（窗口、system/user、etprj）与资源字节
        int jsonSame = 0, jsonDiff = 0;
        void J(string rel, byte[] data)
        {
            var o = File.ReadAllBytes(Path.Combine(root, rel));
            if (o.AsSpan().SequenceEqual(data)) jsonSame++;
            else { jsonDiff++; Console.WriteLine($"  J {rel}: JSON 回写不一致"); }
        }
        foreach (var fm in p.Forms) J(fm.File, EJson.ToUtf8Bom(fm.Json));
        if (p.SystemConfig != null) J(p.Layout.Config + "/" + p.Layout.SystemConfig, EJson.ToUtf8Bom(p.SystemConfig));
        if (p.UserConfig != null) J(p.Layout.Config + "/" + p.Layout.UserConfig, EJson.ToUtf8Bom(p.UserConfig));
        if (p.Etprj != null) J(p.Layout.Project, TextFiles.Gbk.GetBytes(EJson.Serialize(p.Etprj)));
        foreach (var r in p.Resources) J(r.File, r.Data);
        Console.WriteLine($"JSON/资源原样回写：{jsonSame}/{jsonSame + jsonDiff} 一致");
        Console.WriteLine("程序集顺序（前 8）：" + string.Join(", ", p.Classes.Take(8).Select(c => (c.Folder.Length > 0 ? c.Folder + "/" : "") + c.Name)));
        Console.WriteLine("窗口顺序：" + string.Join(", ", p.Forms.Select(f => f.File)));
        diff += jsonDiff;

        // 覆盖范围：原目录里所有 *.e.txt 与排序表都应被回写
        var l = p.Layout;
        var missing = Directory.GetFiles(root, "*", SearchOption.AllDirectories)
            .Select(f => Path.GetRelativePath(root, f).Replace('\\', '/'))
            .Where(f => f.EndsWith(".e.txt", StringComparison.Ordinal) || Path.GetFileName(f) == l.OrderList)
            .Where(f => !covered.Contains(f)).ToList();

        Console.WriteLine($"回写比对：{same}/{files.Count} 逐字节一致，不一致 {diff}；原目录中未被覆盖的文本文件 {missing.Count}；路径推导不符 {pathProblems.Count}");
        foreach (var m in missing.Take(10)) Console.WriteLine("  M " + m);
        foreach (var m in pathProblems.Take(10)) Console.WriteLine("  P " + m);
        return (diff > 0 ? 1 : 0) + (missing.Count > 0 ? 1 : 0) + (pathProblems.Count > 0 ? 1 : 0) + (p.Errors.Count > 0 ? 1 : 0);
    }

    private static string FirstDiff(string rel, byte[] a, byte[] b)
    {
        string sa, sb;
        try { sa = TextFiles.ReadUtf8(a); sb = TextFiles.ReadUtf8(b); }
        catch { return $"{rel}: 字节不同（长度 {a.Length} vs {b.Length}）"; }
        var la = sa.Split("\r\n");
        var lb = sb.Split("\r\n");
        for (int i = 0; i < Math.Max(la.Length, lb.Length); i++)
        {
            var x = i < la.Length ? la[i] : "<EOF>";
            var y = i < lb.Length ? lb[i] : "<EOF>";
            if (x != y) return $"{rel}:{i + 1} 原文 {Rewriter.Show(x)} vs 回写 {Rewriter.Show(y)}";
        }
        return $"{rel}: 文本相同但字节不同（BOM？长度 {a.Length} vs {b.Length}）";
    }
}
