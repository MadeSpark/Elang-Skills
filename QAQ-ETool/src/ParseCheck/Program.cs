using System.Diagnostics;
using System.Text;
using QAQ.ETool.Core.Text;
using QAQ.ETool.Core.Text.Ast;

// 自检：对 e2t 产物的所有子程序体做「解析 → CodePrinter 打印」，要求与原文逐行一致；外加手工单元用例。
Console.OutputEncoding = Encoding.UTF8;

int unitFail = UnitTests.Run();

var root = args.Length > 0 ? args[0] : FindRepoRoot();
var dirs = new[]
{
    Path.Combine(root, "tests", "out", "大项目测试", "en"),
    Path.Combine(root, "tests", "out", "新增 文字文件", "en"),
    Path.Combine(root, "tests", "out", "窗口属性无法解析", "en"),
};

var sw = Stopwatch.StartNew();
long parseTicks = 0;
int files = 0, filesWithSubs = 0, subs = 0, stmtLines = 0, raws = 0, mismatches = 0;
var diags = new List<CodeDiagnostic>();
var samples = new List<string>();
var rawSamples = new List<string>();
var kinds = new SortedDictionary<string, int>(StringComparer.Ordinal);
int candidateLines = 0;

foreach (var dir in dirs)
{
    if (!Directory.Exists(dir)) { Console.WriteLine($"[跳过] 目录不存在：{dir}"); continue; }
    foreach (var path in Directory.EnumerateFiles(dir, "*.e.txt", SearchOption.AllDirectories).OrderBy(x => x, StringComparer.Ordinal))
    {
        files++;
        var text = File.ReadAllText(path, new UTF8Encoding(false));   // 自动去 BOM
        var lines = text.Split("\r\n");                                // 只按 CRLF 分行（文本常量里可能有裸 CR）
        var rel = Path.GetRelativePath(root, path);
        bool any = false;
        if (lines.Any(l => l.StartsWith(".子程序 ", StringComparison.Ordinal)))
            candidateLines += lines.Count(l => l.Length > 0 && l[0] == ' '
                && !l.StartsWith("    .参数 ", StringComparison.Ordinal) && !l.StartsWith("    .局部变量 ", StringComparison.Ordinal));
        for (int i = 0; i < lines.Length; i++)
        {
            if (!lines[i].StartsWith(".子程序 ", StringComparison.Ordinal)) continue;
            any = true;
            subs++;
            int j = i + 1;
            bool decl = false;
            while (j < lines.Length)
            {
                var l = lines[j];
                if (l.StartsWith("    .参数 ", StringComparison.Ordinal) || l.StartsWith("    .局部变量 ", StringComparison.Ordinal)) { j++; decl = true; continue; }
                // 连续空行：除最后一个外都是无名局部变量
                if (l.Length == 0 && j + 1 < lines.Length && (lines[j + 1].Length == 0
                        || lines[j + 1].StartsWith("    .参数 ", StringComparison.Ordinal)
                        || lines[j + 1].StartsWith("    .局部变量 ", StringComparison.Ordinal)))
                { j++; decl = true; continue; }
                break;
            }
            if (decl && j < lines.Length && lines[j].Length == 0) j++;
            int start = j;
            while (j < lines.Length && lines[j].Length > 0 && lines[j][0] == ' ') j++;
            var body = lines[start..j];
            if (body.Length == 0) continue;
            stmtLines += body.Length;

            var d0 = diags.Count;
            var t0 = Stopwatch.GetTimestamp();
            var ast = CodeParser.ParseBody(body, start + 1, rel, diags);
            var printed = CodePrinter.PrintBody(ast);
            parseTicks += Stopwatch.GetTimestamp() - t0;

            raws += CountRaw(ast, rawSamples, rel);
            Stats.Walk(ast, kinds);
            int n = Math.Max(body.Length, printed.Count);
            for (int k = 0; k < n; k++)
            {
                var a = k < body.Length ? body[k] : "<无>";
                var b = k < printed.Count ? printed[k] : "<无>";
                if (a == b) continue;
                mismatches++;
                if (samples.Count < 30) samples.Add($"{rel}:{start + 1 + k}\n    原文: [{a}]\n    打印: [{b}]");
            }
            i = j - 1;
        }
        if (any) filesWithSubs++;
    }
}
sw.Stop();

Console.WriteLine();
Console.WriteLine("===== 恒等自检 =====");
Console.WriteLine($"文件数: {files}（含子程序 {filesWithSubs}）  子程序数: {subs}  语句行数: {stmtLines}");
Console.WriteLine($"RawStmt: {raws}  diag: {diags.Count}  恒等失败行: {mismatches}");
Console.WriteLine($"覆盖核对: 含子程序文件中以空格开头的非声明行 {candidateLines}，提取到的体行 {stmtLines}");
Console.WriteLine("节点统计: " + string.Join(", ", kinds.Select(kv => $"{kv.Key}={kv.Value}")));
Console.WriteLine($"总耗时: {sw.ElapsedMilliseconds} ms（其中解析+打印 {parseTicks * 1000 / Stopwatch.Frequency} ms）");
foreach (var d in diags.Take(30)) Console.WriteLine("  diag " + d);
foreach (var r in rawSamples.Take(30)) Console.WriteLine("  raw  " + r);
foreach (var s in samples) Console.WriteLine("  " + s);

Console.WriteLine();
Console.WriteLine(unitFail == 0 ? "单元用例: 全部通过" : $"单元用例: 失败 {unitFail} 个");
return unitFail == 0 && raws == 0 && diags.Count == 0 && mismatches == 0 ? 0 : 1;

static int CountRaw(IEnumerable<Stmt> body, List<string> samples, string file)
{
    int n = 0;
    foreach (var s in body)
    {
        switch (s)
        {
            case RawStmt r:
                n++;
                if (samples.Count < 30) samples.Add($"{file}:{r.Pos.Line} {r.Error} | {r.Code}");
                break;
            case IfTrueStmt it: n += CountRaw(it.Body, samples, file); break;
            case IfElseStmt ie: n += CountRaw(ie.Then, samples, file) + CountRaw(ie.Else, samples, file); break;
            case SwitchStmt sw:
                foreach (var c in sw.Cases) n += CountRaw(c.Body, samples, file);
                n += CountRaw(sw.Default, samples, file);
                break;
            case WhileStmt ws: n += CountRaw(ws.Body, samples, file); break;
            case DoWhileStmt dw: n += CountRaw(dw.Body, samples, file); break;
            case CounterStmt cs: n += CountRaw(cs.Body, samples, file); break;
            case ForStmt fs: n += CountRaw(fs.Body, samples, file); break;
        }
    }
    return n;
}

static string FindRepoRoot()
{
    var d = new DirectoryInfo(AppContext.BaseDirectory);
    while (d != null && !Directory.Exists(Path.Combine(d.FullName, "tests", "out"))) d = d.Parent;
    return d?.FullName ?? Directory.GetCurrentDirectory();
}
