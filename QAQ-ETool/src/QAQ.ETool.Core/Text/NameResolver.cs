using QIQI.EProjectFile;
using QAQ.ETool.Core.Model;

namespace QAQ.ETool.Core.Text;

/// <summary>
/// ID → 文本名称。任何解析失败都计入 Failures（判据①：零占位符），并返回可见占位。
/// </summary>
public sealed class NameResolver
{
    private readonly EProject _p;
    public List<string> Failures { get; } = new();

    public NameResolver(EProject p) { _p = p; }

    private string Fail(string what)
    {
        Failures.Add(what);
        return $"<?{what}?>";
    }

    public string User(int id)
    {
        if (_p.Names.TryGetValue(id, out var n)) return n;
        return Fail($"未知名称:0x{id:X8}");
    }

    private static readonly Dictionary<int, string> SystemTypes = new()
    {
        { EplSystemId.DataType_Bin, "字节集" },
        { EplSystemId.DataType_Bool, "逻辑型" },
        { EplSystemId.DataType_Byte, "字节型" },
        { EplSystemId.DataType_DateTime, "日期时间型" },
        { EplSystemId.DataType_Double, "双精度小数型" },
        { EplSystemId.DataType_Float, "小数型" },
        { EplSystemId.DataType_Int, "整数型" },
        { EplSystemId.DataType_Long, "长整数型" },
        { EplSystemId.DataType_MethodPtr, "子程序指针" },
        { EplSystemId.DataType_Short, "短整数型" },
        { EplSystemId.DataType_String, "文本型" },
        { EplSystemId.DataType_Any, "通用型" },
        { EplSystemId.DataType_Lambda, "条件语句型" },
        { EplSystemId.DataType_Void, "" },
    };

    /// <summary>声明里的数据类型名（系统类型 / 支持库类型 / 自定义类型 / 类 / 窗口）</summary>
    public string DataType(int id, ISet<int>? usedLibs = null)
    {
        if (SystemTypes.TryGetValue(id, out var s)) return s;
        if (EplSystemId.IsLibDataType(id))
        {
            EplSystemId.DecomposeLibDataTypeId(id, out var lib, out var type);
            return LibTypeName(lib, type, usedLibs);
        }
        if (_p.RenamedTypes.TryGetValue(id, out var rn)) return rn;
        if (_p.Names.TryGetValue(id, out var n)) return n;
        if (EplSystemId.GetType(id) == EplSystemId.Type_Struct || EplSystemId.GetType(id) == EplSystemId.Type_Class)
        {
            // 模块内部未公开的数据类型：.e 里只存了 ID，没有定义与名称（e2txt 显示为 <?未知本地类型?>）。
            // 带上 ID 输出，t2e 原样还原，保证无损；计为提示而非错误。
            ModulePrivateTypes.Add(id);
            return $"<?未知本地类型:0x{id:X8}?>";
        }
        return Fail($"未知类型:0x{id:X8}");
    }

    /// <summary>模块私有（未导出）的数据类型 ID：信息不在 .e 中，无法命名</summary>
    public SortedSet<int> ModulePrivateTypes { get; } = new();

    public string LibCmd(int lib, int cmd)
    {
        var l = Lib(lib);
        if (l != null && cmd >= 0 && cmd < l.Cmds.Length) return l.Cmds[cmd].Name;
        return Fail($"未知命令:{LibKey(lib)}#{cmd}");
    }

    public string LibConstant(int lib, int idx, ISet<int>? usedLibs = null)
    {
        if (lib > 0) usedLibs?.Add(lib);
        var l = Lib(lib);
        if (l != null && idx >= 0 && idx < l.Constants.Length) return l.Constants[idx].Name;
        return Fail($"未知常量:{LibKey(lib)}#{idx}");
    }

    public string LibTypeName(int lib, int type, ISet<int>? usedLibs = null)
    {
        if (lib > 0) usedLibs?.Add(lib);
        var t = _p.LibType(lib, type);
        if (t != null) return t.Name;
        return Fail($"未知库类型:{LibKey(lib)}#{type}");
    }

    public string LibTypeMember(int lib, int type, int member)
    {
        var t = _p.LibType(lib, type);
        if (t != null && member >= 0 && member < t.Members.Length) return t.Members[member].Name;
        return Fail($"未知库成员:{LibKey(lib)}#{type}.{member}");
    }

    private OpenEpl.ELibInfo.ELibManifest? Lib(int lib) =>
        lib >= 0 && lib < _p.Libs.Length ? _p.Libs[lib] : null;

    private string LibKey(int lib) =>
        lib >= 0 && lib < _p.Code.Libraries.Length ? _p.Code.Libraries[lib].FileName : $"lib{lib}";
}
