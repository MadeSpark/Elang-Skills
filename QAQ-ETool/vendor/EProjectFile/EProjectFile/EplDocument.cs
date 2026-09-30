using QIQI.EProjectFile.Context;
using QIQI.EProjectFile.Encryption;
using QIQI.EProjectFile.Sections;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace QIQI.EProjectFile
{
    public class EplDocument
    {
        public List<ISection> Sections { get; } = new List<ISection>();
        /// <summary>
        /// [QAQ-ETool 补丁] 段的「可选」标志以文件实际值为准（部分 IDE 版本与库内默认值不同）
        /// </summary>
        public Dictionary<ISection, bool> OptionalOverrides { get; } = new Dictionary<ISection, bool>(new RefComparer());
        private sealed class RefComparer : IEqualityComparer<ISection>
        {
            public bool Equals(ISection x, ISection y) => ReferenceEquals(x, y);
            public int GetHashCode(ISection obj) => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(obj);
        }
        /// <summary>
        /// [QAQ-ETool 补丁] 结束段名称按密钥异或写出（e2txt 产物如此；IDE 写全 0）
        /// </summary>
        public bool EofNameXored { get; set; }
        public Encoding DetermineEncoding()
        {
            return GetOrNull(ESystemInfoSection.Key)?.DetermineEncoding() ?? Encoding.GetEncoding("gbk");
        }

        public TSection GetOrNull<TSection>(ISectionKey<TSection> key) where TSection : ISection
        {
            if (Sections.FirstOrDefault(x => x.SectionKey == key.SectionKey) is TSection it)
            {
                return it;
            }
            else
            {
                return default;
            }
        }

        public TSection Get<TSection>(ISectionKey<TSection> key) where TSection : ISection
        {
            return (TSection)Sections.First(x => x.SectionKey == key.SectionKey);
        }

        public void Load(Stream stream, ProjectFileReader.OnInputPassword inputPassword = null)
        {
            var encoding = Encoding.GetEncoding("gbk");
            Sections.Clear();
            OptionalOverrides.Clear();
            using (var reader = new ProjectFileReader(stream, inputPassword))
            {
                while (!reader.IsFinish)
                {
                    var rawSection = reader.ReadSection();
                    ISection section;
                    if (PredefinedSections.Keys.TryGetValue(rawSection.Key, out var sectionKey))
                    {
                        section = sectionKey.Parse(new BlockParserContext(rawSection.Data, encoding, reader.CryptEC));
                    }
                    else
                    {
                        section = new GeneralSection(rawSection);
                    }
                    if (section is ESystemInfoSection systemInfo)
                    {
                        encoding = systemInfo.DetermineEncoding();
                    }
                    if (section is EndOfFileSection) EofNameXored = reader.EofNameXored;
                    if (!(section is EndOfFileSection))
                    {
                        Sections.Add(section);
                        // [QAQ-ETool 补丁] 记下文件里的真实「可选」标志，Save 时优先使用
                        if (section.IsOptional != rawSection.IsOptional)
                        {
                            OptionalOverrides[section] = rawSection.IsOptional;
                        }
                    }
                }
            }
        }
        public void Save(Stream stream) => Save(stream, null);
        public void Save(Stream stream, EplEncryptionOptions encryptionOptions)
        {
            var encoding = Encoding.GetEncoding("gbk");
            using (var writer = new ProjectFileWriter(stream, encryptionOptions) { XorEofName = EofNameXored })
            {
                var context = new BlockByteifierContext(encoding, writer.CryptEC);
                foreach (var section in Sections)
                {
                    if (section is ESystemInfoSection systemInfo)
                    {
                        encoding = systemInfo.DetermineEncoding();
                    }
                    var isOptional = OptionalOverrides.TryGetValue(section, out var o) ? o : section.IsOptional;
                    writer.WriteSection(new RawSectionInfo(section.SectionKey, section.SectionName, isOptional, section.ToBytes(context)));
                }
                {
                    var section = EndOfFileSection.Instance;
                    writer.WriteSection(new RawSectionInfo(section.SectionKey, section.SectionName, section.IsOptional, section.ToBytes(context)));
                }
            }
        }
    }
}
