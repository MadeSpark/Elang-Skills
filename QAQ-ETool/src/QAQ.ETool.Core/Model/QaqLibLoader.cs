using System.Diagnostics;
using Microsoft.Win32;
using OpenEpl.ELibInfo;
using OpenEpl.ELibInfo.Loader;

namespace QAQ.ETool.Core.Model;

/// <summary>
/// 支持库元数据加载器。
/// 用自研的 qaq-libinfo.exe（32 位，SDK 布局 + 访问违例保护）把 .fne 导出为 ELibInfo v2 JSON，
/// 放进缓存目录后交给 ELibInfoCacheableLoader 反序列化。
/// 不用 ELibInfo 自带的 ExportELibInfoV2.exe：它在 e2ee.fne 等库上会崩溃（0xC0000005）。
/// 已验证：15 个本机库与官方导出器逐项一致（tools/libinfo_check.py）。
/// </summary>
public sealed class QaqLibLoader : IELibInfoLoader
{
    public string CachePath { get; }
    public string? LibDir { get; }
    public string ExporterPath { get; }
    private readonly ELibInfoCacheableLoader _cache;
    private readonly Dictionary<string, ELibManifest?> _memo = new(StringComparer.OrdinalIgnoreCase);

    public QaqLibLoader(string cachePath, string? libDir, string exporterPath)
    {
        CachePath = cachePath;
        LibDir = libDir;
        ExporterPath = exporterPath;
        Directory.CreateDirectory(cachePath);
        _cache = new ELibInfoCacheableLoader(cachePath, null);
    }

    public ELibManifest Load(Guid guid, string fileName, Version version)
    {
        var memoKey = $"{guid:N}|{fileName}|{version}";
        if (_memo.TryGetValue(memoKey, out var hit)) return hit!;
        var r = LoadCore(guid, fileName, version);
        _memo[memoKey] = r;
        return r!;
    }

    private ELibManifest? LoadCore(Guid guid, string fileName, Version version)
    {
        // 1. 缓存命中（且 .fne 未变）
        var fne = LibDir == null ? null : Path.Combine(LibDir, fileName + ".fne");
        var dir = Path.Combine(CachePath, $"{guid:N}_{fileName}");
        var file = Path.Combine(dir, $"{version.Major}.{version.Minor}.v2.json");
        var stamp = Path.Combine(dir, $"{version.Major}.{version.Minor}.stamp");
        var fneStamp = fne != null && File.Exists(fne) ? $"{new FileInfo(fne).Length}|{File.GetLastWriteTimeUtc(fne).Ticks}" : null;
        if (File.Exists(file) && (fneStamp == null || (File.Exists(stamp) && File.ReadAllText(stamp) == fneStamp)))
        {
            var m = TryCache(guid, fileName, version);
            if (m != null) return m;
        }

        // 2. 导出
        if (fne == null || !File.Exists(fne) || !File.Exists(ExporterPath)) return null;
        Directory.CreateDirectory(dir);
        var tmp = Path.Combine(dir, $"export.{Environment.ProcessId}.json");
        var psi = new ProcessStartInfo(ExporterPath)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
        };
        psi.ArgumentList.Add(fne);
        psi.ArgumentList.Add(tmp);
        using (var p = Process.Start(psi)!)
        {
            p.StandardOutput.ReadToEndAsync();
            p.StandardError.ReadToEndAsync();
            if (!p.WaitForExit(120_000))
            {
                try { p.Kill(); } catch { }
                return null;
            }
            if (p.ExitCode != 0 || !File.Exists(tmp)) return null;
        }
        // 导出结果的 Version 是实际安装版本；按工程要求的版本名存放，便于缓存命中
        File.Copy(tmp, file, true);
        File.Delete(tmp);
        if (fneStamp != null) File.WriteAllText(stamp, fneStamp);
        return TryCache(guid, fileName, version);
    }

    private ELibManifest? TryCache(Guid guid, string fileName, Version version)
    {
        try { return _cache.Load(guid, fileName, version); }
        catch { return null; }
    }

    /// <summary>易语言支持库目录：环境变量 E_LIBDIR → 注册表 FlySky\E\Install\Path → 常见路径</summary>
    public static string? FindLibDir()
    {
        var env = Environment.GetEnvironmentVariable("E_LIBDIR");
        if (!string.IsNullOrEmpty(env) && Directory.Exists(env)) return env;
        if (OperatingSystem.IsWindows())
        {
            foreach (var root in new[] { Registry.CurrentUser, Registry.LocalMachine })
            {
                try
                {
                    using var k = root.OpenSubKey(@"Software\FlySky\E\Install");
                    if (k?.GetValue("Path") is string p && !string.IsNullOrEmpty(p))
                    {
                        // 注册表里的值可能是安装目录，也可能已经是 lib 目录
                        var cand = p.TrimEnd('\\');
                        if (File.Exists(Path.Combine(cand, "krnln.fne"))) return cand;
                        var lib = Path.Combine(cand, "lib");
                        if (File.Exists(Path.Combine(lib, "krnln.fne"))) return lib;
                    }
                }
                catch { }
            }
        }
        foreach (var d in new[] { @"D:\ides\e\lib", @"C:\Program Files (x86)\易语言\lib", @"C:\Program Files\易语言\lib", @"D:\易语言\lib", @"C:\易语言\lib" })
            if (File.Exists(Path.Combine(d, "krnln.fne"))) return d;
        return null;
    }
}
