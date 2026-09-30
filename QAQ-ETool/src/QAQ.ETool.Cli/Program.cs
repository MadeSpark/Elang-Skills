using System.Diagnostics;
using System.Text;
using QAQ.ETool.Core.Model;
using QAQ.ETool.Core.Text;

// QAQ-ETool CLI —— 与 e2txt 参数习惯兼容：
//   QAQ-ETool -mode e2t -src <x.e> -dst <目录> [-ns 2] [-level 2] [-enc UTF-8]
//   QAQ-ETool -mode t2e -src <目录> -dst <x.e>
// 输出约定与 e2txt 相同（成功行 SUCC:<路径> 在 stdout，致命错误 ERROR:<原因> 在 stderr），
// 但退出码有意义：0 成功，1 失败，2 成功但有无法解析的内容（判据①未达成）。
Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
Console.OutputEncoding = new UTF8Encoding(false);

var opts = ParseArgs(args);
if (opts == null || !opts.TryGetValue("mode", out var mode))
{
    Console.Error.WriteLine("ERROR:请输入有效的命令行参数");
    Console.Error.WriteLine("用法: QAQ-ETool -mode e2t -src <文件.e> -dst <目录> [-ns 2] [-level 2] [-nosidecar]");
    Console.Error.WriteLine("      QAQ-ETool -mode t2e -src <目录> -dst <文件.e> [-strict] [-nosidecar]");
    return 1;
}
opts.TryGetValue("src", out var src);
opts.TryGetValue("dst", out var dst);
if (string.IsNullOrEmpty(src) || string.IsNullOrEmpty(dst))
{
    Console.Error.WriteLine("ERROR:缺少 -src 或 -dst");
    return 1;
}

try
{
    switch (mode)
    {
        case "e2t": return E2T(Path.GetFullPath(src), Path.GetFullPath(dst), opts);
        case "t2e": return T2E(Path.GetFullPath(src), Path.GetFullPath(dst), opts);
        default:
            Console.Error.WriteLine($"ERROR:不支持的模式 {mode}");
            return 1;
    }
}
catch (Exception ex)
{
    Console.Error.WriteLine("ERROR:" + ex.Message);
    if (opts.ContainsKey("debug")) Console.Error.WriteLine(ex);
    return 1;
}

static int E2T(string src, string dst, Dictionary<string, string> opts)
{
    if (!File.Exists(src))
    {
        Console.Error.WriteLine("ERROR:来源文件不存在");
        return 1;
    }
    var sw = Stopwatch.StartNew();
    var project = EProject.Load(src);
    for (int i = 0; i < project.Libs.Length; i++)
        if (project.Libs[i] == null)
            Console.Error.WriteLine($"[警告] 支持库未能加载: {project.Code.Libraries[i].FileName} {project.Code.Libraries[i].Name}");

    if (Directory.Exists(dst) && Directory.EnumerateFileSystemEntries(dst).Any())
    {
        // 安全闸：只清空「看起来是文本工程」的目录，绝不误删用户其它目录
        if (!Layout.IsTextProjectDir(dst))
        {
            Console.Error.WriteLine("ERROR:目标目录非空且不是文本工程目录（缺少 项目.qtprj / project.qtprj），拒绝覆盖");
            return 1;
        }
        ClearDir(dst);
    }
    var result = new Exporter(project, new ExportOptions
    {
        Layout = Layout.FromNs(opts.GetValueOrDefault("ns")),
        Level = int.TryParse(opts.GetValueOrDefault("level"), out var lv) ? lv : 2,
        SourcePath = src,
    }).Export(dst);
    // 旁路文件：文本表达不了的原件信息（ID、隐藏标志、原始代码字节、编辑状态段…），t2e 时用来无损回写
    if (!opts.ContainsKey("nosidecar")) Sidecar.Capture(project).Save(dst);

    foreach (var w in result.Warnings) Console.Error.WriteLine("[警告] " + w);
    foreach (var f in result.Failures.Distinct()) Console.Error.WriteLine("[错误] " + f);
    Console.WriteLine($"[信息] {result.FileCount} 个文件, {result.TotalBytes:N0} 字节, 无法解析 {result.Failures.Count} 处, 耗时 {sw.ElapsedMilliseconds} ms");
    Console.WriteLine("SUCC:" + dst);
    return result.Failures.Count == 0 ? 0 : 2;
}

static int T2E(string src, string dst, Dictionary<string, string> opts)
{
    if (!Directory.Exists(src))
    {
        Console.Error.WriteLine("ERROR:来源目录不存在");
        return 1;
    }
    var sw = Stopwatch.StartNew();
    var text = Importer.Load(src);
    // 默认宽松：绑定失败的语句以「未验证代码」写入并以退出码 2 提示；-strict 时任何错误都不产出文件
    bool strict = opts.ContainsKey("strict");
    Sidecar? sc = null;
    if (!opts.ContainsKey("nosidecar"))
    {
        sc = Sidecar.TryLoad(src, out var problem);
        if (problem != null) Console.Error.WriteLine($"[警告] 旁路文件 {Sidecar.PathOf(src)} 无法使用（{problem}），按纯文本组包");
    }
    var result = Builder.Build(text, new ImportOptions { Lenient = !strict, Sidecar = sc });
    if (result.Sidecar != null) Console.WriteLine("[信息] 旁路: " + result.Sidecar);

    foreach (var w in result.Warnings) Console.Error.WriteLine("[警告] " + w);
    foreach (var e in result.Errors) Console.Error.WriteLine("[错误] " + e);
    if (result.Doc == null || (strict && result.Errors.Count > 0))
    {
        Console.Error.WriteLine($"ERROR:组包失败，{result.Errors.Count} 个错误");
        return 1;
    }

    // 先写临时文件再替换，避免失败时留下半截 .e
    var dir = Path.GetDirectoryName(dst);
    if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
    var tmp = dst + ".tmp";
    using (var fs = File.Create(tmp)) result.Doc.Save(fs);
    File.Move(tmp, dst, overwrite: true);

    Console.WriteLine($"[信息] 错误 {result.Errors.Count} 处, 警告 {result.Warnings.Count} 处, 未验证代码 {result.Unexamined} 处, 耗时 {sw.ElapsedMilliseconds} ms");
    Console.WriteLine("SUCC:" + dst);
    return result.Errors.Count == 0 && result.Unexamined == 0 ? 0 : 2;
}

/// <summary>清空输出目录的内容（保留目录本身，避免残留旧文件造成误判）</summary>
static void ClearDir(string dir)
{
    foreach (var f in Directory.GetFiles(dir)) File.Delete(f);
    foreach (var d in Directory.GetDirectories(dir)) Directory.Delete(d, true);
}

static Dictionary<string, string>? ParseArgs(string[] a)
{
    var d = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    for (int i = 0; i < a.Length; i++)
    {
        if (!a[i].StartsWith('-')) return null;
        var k = a[i].TrimStart('-');
        if (i + 1 < a.Length && !a[i + 1].StartsWith('-')) d[k] = a[++i];
        else d[k] = "";
    }
    return d;
}
