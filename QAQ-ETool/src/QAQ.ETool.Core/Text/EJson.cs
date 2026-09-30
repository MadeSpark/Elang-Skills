using System.Globalization;
using System.Text;

namespace QAQ.ETool.Core.Text;

/// <summary>
/// e2txt 风格 JSON 序列化器。
/// 规则（docs/e2txt-format-spec.md §3.1，对全部样本逐字节验证过）：
///   4 空格缩进、键按码点升序、数组保持顺序、空数组 [] 空对象 {}、
///   非 ASCII 原样输出、换行 CRLF、文件末尾无换行。
/// 值类型：null / bool / 整数(long/int) / string / JObject / List&lt;object?&gt;
/// </summary>
public sealed class JObject : Dictionary<string, object?>
{
    public JObject() : base(StringComparer.Ordinal) { }
}

public static class EJson
{
    public static string Serialize(object? value)
    {
        var sb = new StringBuilder();
        Write(sb, value, 0);
        return sb.ToString();
    }

    /// <summary>UTF-8 + BOM，e2txt 的 *.json 文件格式</summary>
    public static byte[] ToUtf8Bom(object? value) => TextFiles.Utf8Bom(Serialize(value));

    private static void Indent(StringBuilder sb, int level) => sb.Append(' ', level * 4);

    private static void Write(StringBuilder sb, object? value, int level)
    {
        switch (value)
        {
            case null: sb.Append("null"); break;
            case bool b: sb.Append(b ? "true" : "false"); break;
            case string s: WriteString(sb, s); break;
            case int i: sb.Append(i.ToString(CultureInfo.InvariantCulture)); break;
            case long l: sb.Append(l.ToString(CultureInfo.InvariantCulture)); break;
            case uint ui: sb.Append(ui.ToString(CultureInfo.InvariantCulture)); break;
            case short sh: sb.Append(sh.ToString(CultureInfo.InvariantCulture)); break;
            case JObject obj:
                if (obj.Count == 0) { sb.Append("{}"); break; }
                sb.Append('{').Append("\r\n");
                var keys = obj.Keys.ToList();
                keys.Sort(CompareCodePoint);
                for (int k = 0; k < keys.Count; k++)
                {
                    Indent(sb, level + 1);
                    WriteString(sb, keys[k]);
                    sb.Append(": ");
                    Write(sb, obj[keys[k]], level + 1);
                    if (k != keys.Count - 1) sb.Append(',');
                    sb.Append("\r\n");
                }
                Indent(sb, level);
                sb.Append('}');
                break;
            case System.Collections.IEnumerable list:
                var items = list.Cast<object?>().ToList();
                if (items.Count == 0) { sb.Append("[]"); break; }
                sb.Append('[').Append("\r\n");
                for (int k = 0; k < items.Count; k++)
                {
                    Indent(sb, level + 1);
                    Write(sb, items[k], level + 1);
                    if (k != items.Count - 1) sb.Append(',');
                    sb.Append("\r\n");
                }
                Indent(sb, level);
                sb.Append(']');
                break;
            default:
                throw new ArgumentException($"EJson 不支持的类型: {value.GetType()}");
        }
    }

    /// <summary>与 Python json.dumps(ensure_ascii=False) 相同的转义规则</summary>
    private static void WriteString(StringBuilder sb, string s)
    {
        sb.Append('"');
        foreach (var c in s)
        {
            switch (c)
            {
                case '"': sb.Append("\\\""); break;
                case '\\': sb.Append("\\\\"); break;
                case '\n': sb.Append("\\n"); break;
                case '\r': sb.Append("\\r"); break;
                case '\t': sb.Append("\\t"); break;
                case '\b': sb.Append("\\b"); break;
                case '\f': sb.Append("\\f"); break;
                default:
                    if (c < 0x20) sb.Append("\\u").Append(((int)c).ToString("x4"));
                    else sb.Append(c);
                    break;
            }
        }
        sb.Append('"');
    }

    /// <summary>按 Unicode 码点比较（Python 的 str 排序语义）</summary>
    public static int CompareCodePoint(string a, string b)
    {
        int i = 0, j = 0;
        while (i < a.Length && j < b.Length)
        {
            int ca = CodePointAt(a, i, out int la);
            int cb = CodePointAt(b, j, out int lb);
            if (ca != cb) return ca.CompareTo(cb);
            i += la; j += lb;
        }
        return (a.Length - i).CompareTo(b.Length - j);
    }

    private static int CodePointAt(string s, int index, out int length)
    {
        if (char.IsHighSurrogate(s[index]) && index + 1 < s.Length && char.IsLowSurrogate(s[index + 1]))
        {
            length = 2;
            return char.ConvertToUtf32(s[index], s[index + 1]);
        }
        length = 1;
        return s[index];
    }

    // ---------- 解析（t2e 用） ----------
    public static object? Parse(string text)
    {
        using var doc = System.Text.Json.JsonDocument.Parse(text, new System.Text.Json.JsonDocumentOptions
        {
            AllowTrailingCommas = true,
            CommentHandling = System.Text.Json.JsonCommentHandling.Skip,
        });
        return Convert(doc.RootElement);
    }

    private static object? Convert(System.Text.Json.JsonElement e) => e.ValueKind switch
    {
        System.Text.Json.JsonValueKind.Object => ToObj(e),
        System.Text.Json.JsonValueKind.Array => e.EnumerateArray().Select(Convert).ToList(),
        System.Text.Json.JsonValueKind.String => e.GetString(),
        System.Text.Json.JsonValueKind.Number => e.TryGetInt64(out var l) ? l : (object)e.GetDouble(),
        System.Text.Json.JsonValueKind.True => true,
        System.Text.Json.JsonValueKind.False => false,
        _ => null,
    };

    private static JObject ToObj(System.Text.Json.JsonElement e)
    {
        var o = new JObject();
        foreach (var p in e.EnumerateObject()) o[p.Name] = Convert(p.Value);
        return o;
    }
}
