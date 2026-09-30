using System.Text;

namespace QAQ.ETool.Core.Text;

/// <summary>文本文件编码约定（e2txt 兼容）</summary>
public static class TextFiles
{
    public static readonly byte[] Bom = { 0xEF, 0xBB, 0xBF };
    public const string CRLF = "\r\n";

    static TextFiles()
    {
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);
    }

    public static Encoding Gbk => Encoding.GetEncoding("gbk");

    /// <summary>UTF-8 带 BOM（*.e.txt / *.json / order.list.txt）</summary>
    public static byte[] Utf8Bom(string text)
    {
        var body = new UTF8Encoding(false).GetBytes(text);
        var r = new byte[Bom.Length + body.Length];
        Bom.CopyTo(r, 0);
        body.CopyTo(r, Bom.Length);
        return r;
    }

    /// <summary>读 UTF-8（有无 BOM 均可），不做任何换行转换</summary>
    public static string ReadUtf8(byte[] data)
    {
        int start = data.Length >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF ? 3 : 0;
        return new UTF8Encoding(false, true).GetString(data, start, data.Length - start);
    }

    /// <summary>
    /// 按 CRLF 分行。**只认 \r\n**：常量值里可能出现裸 CR / TAB（spec §1）。
    /// 结尾的 CRLF 产生最后一个空串，调用方自行处理。
    /// </summary>
    public static string[] SplitCrlf(string text) => text.Split(CRLF);
}
