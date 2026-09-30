using System;
using System.IO;
using System.Text;
using QIQI.EProjectFile.Expressions;
using QIQI.EProjectFile.Internal;

namespace QIQI.EProjectFile.Statements
{
    /// <summary>
    /// 未验证代码语句
    /// </summary>
    public class UnexaminedStatement : Statement
    {
        private string unexaminedCode;
        public bool Mask { get; set; }
        /// <summary>[QAQ-ETool 补丁] 行尾注释（原实现写入时丢弃）</summary>
        public string Comment { get; set; }

        public string UnexaminedCode { get => unexaminedCode; set => unexaminedCode = value ?? throw new ArgumentNullException(nameof(UnexaminedCode)); }

        public UnexaminedStatement()
        {
        }

        public UnexaminedStatement(string unexaminedCode, bool mask)
        {
            UnexaminedCode = unexaminedCode;
            Mask = mask;
        }
        public override void ToTextCode(IdToNameMap nameMap, TextWriter writer, int indent = 0)
        {
            for (int i = 0; i < indent; i++)
                writer.Write("    ");
            if (Mask)
                writer.Write("' ");
            writer.Write(unexaminedCode);
        }
        internal void WriteTo(MethodCodeDataWriterArgs a, byte type, short libraryId, int methodId)
        {
            a.LineOffest.Write(a.Offest);
            a.ExpressionData.Write(type);
            a.ExpressionData.Write(methodId);
            a.ExpressionData.Write(libraryId);
            // [QAQ-ETool 补丁] 屏蔽位是 0x20（与 CallExpression 及解析器一致）；原实现写成 Mask ? 0 : 0x40，读回后屏蔽丢失
            a.ExpressionData.Write((short)(0x40 | (Mask ? 0x20 : 0)));
            a.ExpressionData.WriteBStr(a.Encoding, UnexaminedCode);
            a.ExpressionData.WriteBStr(a.Encoding, string.IsNullOrEmpty(Comment) ? null : Comment);
            a.ExpressionData.Write((byte)0x36);
            ParamListEnd.Instance.WriteTo(a);

        }
        internal override void WriteTo(MethodCodeDataWriterArgs a)
        {
            WriteTo(a, 0x6A, -1, 0);

        }
    }
}
