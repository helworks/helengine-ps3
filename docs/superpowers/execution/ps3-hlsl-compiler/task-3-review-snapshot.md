# Task 3 full uncommitted source snapshot

All task files are new/untracked; this package contains their full source. Base HEAD ef27238ed85fe3218618a752c11d9eeede0a75a2, no commits. Includes current explicit-container-removal cancellation fix. Requirements are task-3-brief.md and sdk-format-evidence.md. Test report pending at snapshot creation; verify report states no later product changes or regenerate.

### builder/shaders/IPs3RsxCompiler.cs
```csharp
using helengine;

namespace helengine.ps3.builder;

/// <summary>Compiles translated GLSL through the pinned PS3 SDK and validates the resulting RSX program.</summary>
public interface IPs3RsxCompiler {
    /// <summary>Compiles one translated shader inside the supplied build-owned working root.</summary>
    /// <param name="translation">Task2 GLSL translation and authored stage metadata.</param>
    /// <param name="workingRoot">Explicit root under which a unique compiler job directory is created.</param>
    /// <param name="cancellationToken">Token that stops this job and its uniquely named Docker container.</param>
    /// <returns>A validated immutable snapshot of the SDK program and dump.</returns>
    Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken);
}
```
### builder/shaders/Ps3RsxConstantElement.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Preserves one vertex constant register or one fragment constant relocation element.</summary>
public sealed class Ps3RsxConstantElement {
    /// <summary>Creates a copied element record for one compiler constant vector.</summary>
    /// <param name="elementOrdinal">Zero-based position within the authored array or matrix parameter.</param>
    /// <param name="vertexRegister">Vertex constant register, or null for fragment-stage constants.</param>
    /// <param name="fragmentRelocationOffsets">Copied microcode-relative fragment patch references.</param>
    /// <param name="internalValues">Copied 32-bit SDK values associated with this constant vector.</param>
    internal Ps3RsxConstantElement(uint elementOrdinal, uint? vertexRegister, IEnumerable<uint> fragmentRelocationOffsets, IEnumerable<uint> internalValues) {
        ArgumentNullException.ThrowIfNull(fragmentRelocationOffsets);
        ArgumentNullException.ThrowIfNull(internalValues);
        ElementOrdinal = elementOrdinal;
        VertexRegister = vertexRegister;
        FragmentRelocationOffsets = Array.AsReadOnly(fragmentRelocationOffsets.ToArray());
        InternalValues = Array.AsReadOnly(internalValues.ToArray());
    }

    /// <summary>Gets this vector's zero-based ordinal within its parameter.</summary>
    public uint ElementOrdinal { get; }

    /// <summary>Gets its vertex constant register, or null when fragment relocation records apply.</summary>
    public uint? VertexRegister { get; }

    /// <summary>Gets copied fragment microcode-relative relocation offsets; an empty list means no references.</summary>
    public IReadOnlyList<uint> FragmentRelocationOffsets { get; }

    /// <summary>Gets the four copied big-endian SDK constant words without numeric reinterpretation.</summary>
    public IReadOnlyList<uint> InternalValues { get; }
}
```
### builder/shaders/Ps3RsxDataType.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Names the exact byte values used by the pinned SDK's RSX parameter records.</summary>
public enum Ps3RsxDataType : byte {
    /// <summary>SDK BOOL type code 0.</summary>
    Bool = 0,

    /// <summary>SDK BOOL1 type code 1.</summary>
    Bool1 = 1,

    /// <summary>SDK BOOL2 type code 2.</summary>
    Bool2 = 2,

    /// <summary>SDK BOOL3 type code 3.</summary>
    Bool3 = 3,

    /// <summary>SDK BOOL4 type code 4.</summary>
    Bool4 = 4,

    /// <summary>SDK scalar FLOAT type code 5.</summary>
    Float = 5,

    /// <summary>SDK FLOAT1 type code 6, distinct from scalar FLOAT.</summary>
    Float1 = 6,

    /// <summary>SDK FLOAT2 type code 7.</summary>
    Float2 = 7,

    /// <summary>SDK FLOAT3 type code 8.</summary>
    Float3 = 8,

    /// <summary>SDK FLOAT4 type code 9.</summary>
    Float4 = 9,

    /// <summary>SDK FLOAT3x4 type code 10.</summary>
    Float3x4 = 10,

    /// <summary>SDK FLOAT4x4 type code 11.</summary>
    Float4x4 = 11,

    /// <summary>SDK FLOAT3x3 type code 12.</summary>
    Float3x3 = 12,

    /// <summary>SDK FLOAT4x3 type code 13.</summary>
    Float4x3 = 13,

    /// <summary>SDK SAMPLER1D type code 14.</summary>
    Sampler1D = 14,

    /// <summary>SDK SAMPLER2D type code 15.</summary>
    Sampler2D = 15,

    /// <summary>SDK SAMPLER3D type code 16.</summary>
    Sampler3D = 16,

    /// <summary>SDK SAMPLERCUBE type code 17.</summary>
    SamplerCube = 17,

    /// <summary>SDK SAMPLERRECT type code 18.</summary>
    SamplerRect = 18,

    /// <summary>SDK UNKNOWN type code 255.</summary>
    Unknown = 255
}
```
### builder/shaders/Ps3RsxParameter.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Describes one named or SDK-internal RSX attribute, sampler or constant parameter.</summary>
public sealed class Ps3RsxParameter {
    /// <summary>Creates a validated parameter with copied constant elements.</summary>
    /// <param name="name">Exact SDK compiler name, or null for a valid unnamed internal constant.</param>
    /// <param name="category">SDK resource category.</param>
    /// <param name="hardwareIndex">Attribute or texture-unit index, or null for constants.</param>
    /// <param name="elementCount">Number of physical constant vectors represented by this parameter.</param>
    /// <param name="dataType">Exact SDK type code.</param>
    /// <param name="elements">Copied per-vector constant elements.</param>
    internal Ps3RsxParameter(string name, Ps3RsxParameterCategory category, uint? hardwareIndex, uint elementCount, Ps3RsxDataType dataType, IEnumerable<Ps3RsxConstantElement> elements) {
        ArgumentNullException.ThrowIfNull(elements);
        Ps3RsxConstantElement[] copiedElements = elements.ToArray();
        if (elementCount == 0) throw new ArgumentOutOfRangeException(nameof(elementCount), "RSX parameter element count must be nonzero.");
        if (category == Ps3RsxParameterCategory.Constant && (hardwareIndex.HasValue || copiedElements.Length != elementCount)) {
            throw new ArgumentException("Constant parameters require one copied element per vector and no aggregate hardware index.", nameof(elements));
        }
        if (category != Ps3RsxParameterCategory.Constant && (!hardwareIndex.HasValue || copiedElements.Length != 0 || elementCount != 1)) {
            throw new ArgumentException("Attribute and sampler parameters require one hardware index and no constant elements.", nameof(elements));
        }
        Name = name;
        Category = category;
        HardwareIndex = hardwareIndex;
        ElementCount = elementCount;
        DataType = dataType;
        Elements = Array.AsReadOnly(copiedElements);
    }

    /// <summary>Gets the exact compiler-provided parameter name, or null for unnamed SDK-internal constants.</summary>
    public string Name { get; }

    /// <summary>Gets whether this record is an input attribute, sampler or constant.</summary>
    public Ps3RsxParameterCategory Category { get; }

    /// <summary>Gets the attribute or texture-unit index, or null for constants.</summary>
    public uint? HardwareIndex { get; }

    /// <summary>Gets the number of vector elements in this constant parameter, or one for attributes and samplers.</summary>
    public uint ElementCount { get; }

    /// <summary>Gets the exact SDK parameter data type code.</summary>
    public Ps3RsxDataType DataType { get; }

    /// <summary>Gets copied per-vector constant register or relocation data; empty for non-constant resources.</summary>
    public IReadOnlyList<Ps3RsxConstantElement> Elements { get; }
}
```
### builder/shaders/Ps3RsxParameterCategory.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Identifies how an RSX parameter participates in a shader stage.</summary>
public enum Ps3RsxParameterCategory {
    /// <summary>Vertex input or fragment-stage input attribute record from the SDK attribute table.</summary>
    Attribute,

    /// <summary>Texture sampler whose SDK attribute index is a texture-unit number.</summary>
    Sampler,

    /// <summary>Constant-table parameter whose stage-specific element records carry its references.</summary>
    Constant
}
```
### builder/shaders/Ps3RsxProgram.cs
```csharp
using helengine;

namespace helengine.ps3.builder;

/// <summary>Contains validated SDK bytecode, its unmodified compiler dump and parameter metadata.</summary>
public sealed class Ps3RsxProgram {
    /// <summary>Stores copied program data after binary and compiler-dump validation.</summary>
    /// <param name="stage">SDK vertex or pixel stage.</param>
    /// <param name="bytecode">Generated SDK bytecode to snapshot.</param>
    /// <param name="assembly">Unmodified cgcomp dump text.</param>
    /// <param name="toolchainIdentity">Identity of the pinned Docker image, compiler and Cg library.</param>
    /// <param name="parameters">Validated parameter records to snapshot.</param>
    internal Ps3RsxProgram(ShaderStage stage, byte[] bytecode, string assembly, string toolchainIdentity, IEnumerable<Ps3RsxParameter> parameters) {
        ArgumentNullException.ThrowIfNull(bytecode);
        ArgumentNullException.ThrowIfNull(parameters);
        if (string.IsNullOrWhiteSpace(assembly)) throw new ArgumentException("Compiler dump must be provided.", nameof(assembly));
        if (string.IsNullOrWhiteSpace(toolchainIdentity)) throw new ArgumentException("Toolchain identity must be provided.", nameof(toolchainIdentity));
        Stage = stage;
        BytecodeData = (byte[])bytecode.Clone();
        Assembly = assembly;
        ToolchainIdentity = toolchainIdentity;
        Parameters = Array.AsReadOnly(parameters.ToArray());
    }

    /// <summary>Gets the stage encoded by the SDK program header.</summary>
    public ShaderStage Stage { get; }

    /// <summary>Gets a fresh byte-for-byte copy of the unmodified SDK program.</summary>
    public byte[] Bytecode => (byte[])BytecodeData.Clone();

    /// <summary>Gets the exact compiler-produced dump text without postprocessing.</summary>
    public string Assembly { get; }

    /// <summary>Gets the validated identity of the Docker image, cgcomp path and mounted Cg library.</summary>
    public string ToolchainIdentity { get; }

    /// <summary>Gets the copied validated SDK parameter records.</summary>
    public IReadOnlyList<Ps3RsxParameter> Parameters { get; }

    /// <summary>Stores the private immutable snapshot used by the defensive bytecode property.</summary>
    byte[] BytecodeData { get; }
}
```
### builder/shaders/Ps3RsxProgramReader.cs
```csharp
using helengine;
using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace helengine.ps3.builder;

/// <summary>Validates pinned SDK RSX program bytes and reconciles shared metadata with the unmodified cgcomp dump.</summary>
public static class Ps3RsxProgramReader {
    /// <summary>The pinned SDK program header size shared by vertex and fragment stages.</summary>
    const int HeaderSize = 36;

    /// <summary>The pinned SDK attribute-table record size.</summary>
    const int AttributeRecordSize = 12;

    /// <summary>The pinned SDK constant-table record size.</summary>
    const int ConstantRecordSize = 28;

    /// <summary>The pinned SDK microcode instruction slot size.</summary>
    const int InstructionSize = 16;

    /// <summary>The fragment constant record's no-relocation sentinel.</summary>
    const uint NoFragmentRelocations = uint.MaxValue;

    /// <summary>Decodes one generated RSX binary only after validating every referenced range and its compiler dump.</summary>
    /// <param name="stage">Expected Task2 shader stage.</param>
    /// <param name="bytecode">Generated SDK binary bytes, preserved exactly.</param>
    /// <param name="assembly">Unmodified cgcomp dump associated with the binary.</param>
    /// <param name="toolchainIdentity">Validated pinned Docker toolchain identity.</param>
    /// <returns>A copied program snapshot with stage-specific parameter references.</returns>
    public static Ps3RsxProgram Read(ShaderStage stage, byte[] bytecode, string assembly, string toolchainIdentity) {
        ArgumentNullException.ThrowIfNull(bytecode);
        if (stage != ShaderStage.Vertex && stage != ShaderStage.Pixel) throw new NotSupportedException($"RSX program stage '{stage}' is unsupported.");
        if (bytecode.Length < HeaderSize) throw new InvalidDataException($"RSX program header is truncated: expected {HeaderSize} bytes, found {bytecode.Length}.");
        if (string.IsNullOrWhiteSpace(assembly)) throw new InvalidDataException("cgcomp dump text is required to validate the RSX program.");
        if (string.IsNullOrWhiteSpace(toolchainIdentity)) throw new InvalidDataException("Pinned toolchain identity is required to validate the RSX program.");

        ushort magic = ReadUInt16(bytecode, 0);
        ushort expectedMagic = stage == ShaderStage.Vertex ? (ushort)0x5650 : (ushort)0x4650;
        if (magic != expectedMagic) throw new InvalidDataException($"RSX program magic 0x{magic:X4} does not match requested {stage} stage.");

        uint attributeCount = ReadUInt16(bytecode, 6);
        uint constantCount = ReadUInt16(bytecode, 8);
        uint instructionCount = ReadUInt16(bytecode, 10);
        uint attributeOffset = ReadUInt32(bytecode, 12);
        uint constantOffset = ReadUInt32(bytecode, 16);
        uint microcodeOffset = ReadUInt32(bytecode, 20);
        if (instructionCount == 0) throw new InvalidDataException("RSX program has no instruction slots.");
        if (microcodeOffset % InstructionSize != 0) throw new InvalidDataException("RSX microcode offset is not 16-byte aligned.");

        ulong attributeEnd = ValidateTableRange(bytecode, attributeOffset, attributeCount, AttributeRecordSize, "attribute");
        ulong constantEnd = ValidateTableRange(bytecode, constantOffset, constantCount, ConstantRecordSize, "constant");
        ulong microcodeLength = (ulong)instructionCount * InstructionSize;
        ulong microcodeEnd = (ulong)microcodeOffset + microcodeLength;
        if (microcodeOffset < HeaderSize || microcodeEnd > (ulong)bytecode.Length) {
            throw new InvalidDataException("RSX microcode range extends outside the program file.");
        }
        if (attributeEnd > constantOffset || constantEnd > microcodeOffset) {
            throw new InvalidDataException("RSX attribute, constant and microcode sections overlap or are out of order.");
        }

        List<Ps3RsxParameter> parameters = ReadAttributeParameters(stage, bytecode, attributeOffset, attributeCount);
        parameters.AddRange(ReadConstantParameters(stage, bytecode, constantOffset, constantCount, attributeOffset, attributeEnd, microcodeOffset, microcodeLength));
        ValidateUniqueParameterNames(parameters);
        ValidateAssembly(stage, assembly, parameters);
        return new Ps3RsxProgram(stage, bytecode, assembly, toolchainIdentity, parameters);
    }

    /// <summary>Reads and bounds-checks one SDK table before any table record is accessed.</summary>
    /// <param name="bytecode">Complete program byte array.</param>
    /// <param name="offset">Big-endian table offset from the SDK header.</param>
    /// <param name="count">Header record count.</param>
    /// <param name="recordSize">Pinned SDK record size.</param>
    /// <param name="tableName">Diagnostic table name.</param>
    /// <returns>The exclusive end offset as unsigned wide arithmetic.</returns>
    static ulong ValidateTableRange(byte[] bytecode, uint offset, uint count, int recordSize, string tableName) {
        ulong end = (ulong)offset + (ulong)count * (uint)recordSize;
        if (offset < HeaderSize || end > (ulong)bytecode.Length) {
            throw new InvalidDataException($"RSX {tableName} table range [{offset}, {end}) is outside the program file.");
        }
        return end;
    }

    /// <summary>Decodes SDK attribute records and separates texture samplers from input attributes by SDK type code.</summary>
    /// <param name="stage">Program stage used to retain the shared SDK attribute-table category.</param>
    /// <param name="bytecode">Validated program byte array.</param>
    /// <param name="attributeOffset">Validated attribute table offset.</param>
    /// <param name="attributeCount">Validated attribute record count.</param>
    /// <returns>Attribute and sampler records in SDK table order.</returns>
    static List<Ps3RsxParameter> ReadAttributeParameters(ShaderStage stage, byte[] bytecode, uint attributeOffset, uint attributeCount) {
        List<Ps3RsxParameter> parameters = new();
        for (uint ordinal = 0; ordinal < attributeCount; ordinal++) {
            int recordOffset = checked((int)((ulong)attributeOffset + (ulong)ordinal * AttributeRecordSize));
            string name = ReadName(bytecode, ReadUInt32(bytecode, recordOffset), "attribute");
            if (string.IsNullOrWhiteSpace(name)) throw new InvalidDataException("SDK attribute record must have a nonempty compiler name.");
            uint index = ReadUInt32(bytecode, recordOffset + 4);
            Ps3RsxDataType dataType = (Ps3RsxDataType)bytecode[recordOffset + 8];
            Ps3RsxParameterCategory category = IsSampler(dataType) ? Ps3RsxParameterCategory.Sampler : Ps3RsxParameterCategory.Attribute;
            parameters.Add(new Ps3RsxParameter(name, category, index, 1, dataType, Array.Empty<Ps3RsxConstantElement>()));
        }
        return parameters;
    }

    /// <summary>Groups named and unnamed constant table continuation records into array or matrix parameters.</summary>
    /// <param name="stage">Program stage that determines whether each element contains a register or relocations.</param>
    /// <param name="bytecode">Validated program byte array.</param>
    /// <param name="constantOffset">Validated constant table offset.</param>
    /// <param name="constantRecordCount">Validated constant record count.</param>
    /// <param name="attributeOffset">Attribute table offset used to reject relocation-table overlap.</param>
    /// <param name="attributeEnd">Exclusive attribute table end.</param>
    /// <param name="microcodeOffset">Validated microcode start.</param>
    /// <param name="microcodeLength">Validated microcode byte length.</param>
    /// <returns>Constant parameters with separate element boundaries.</returns>
    static List<Ps3RsxParameter> ReadConstantParameters(ShaderStage stage, byte[] bytecode, uint constantOffset, uint constantRecordCount, uint attributeOffset, ulong attributeEnd, uint microcodeOffset, ulong microcodeLength) {
        List<Ps3RsxParameter> parameters = new();
        uint recordOrdinal = 0;
        while (recordOrdinal < constantRecordCount) {
            int recordOffset = checked((int)((ulong)constantOffset + (ulong)recordOrdinal * ConstantRecordSize));
            uint nameOffset = ReadUInt32(bytecode, recordOffset);
            string name = ReadName(bytecode, nameOffset, "constant");
            byte typeCode = bytecode[recordOffset + 8];
            uint elementCount = bytecode[recordOffset + 10];
            if (elementCount == 0) throw new InvalidDataException($"Constant record {recordOrdinal} has no element count and is not attached to a preceding parameter.");
            if (elementCount > constantRecordCount - recordOrdinal) throw new InvalidDataException($"Constant parameter '{name}' continues beyond the SDK constant table.");

            List<Ps3RsxConstantElement> elements = new(checked((int)elementCount));
            for (uint elementOrdinal = 0; elementOrdinal < elementCount; elementOrdinal++) {
                uint currentOrdinal = recordOrdinal + elementOrdinal;
                int currentOffset = checked((int)((ulong)constantOffset + (ulong)currentOrdinal * ConstantRecordSize));
                string continuationName = ReadName(bytecode, ReadUInt32(bytecode, currentOffset), "constant continuation");
                if (elementOrdinal > 0 && continuationName != null && !string.Equals(name, continuationName, StringComparison.Ordinal)) {
                    throw new InvalidDataException($"Constant continuation record {currentOrdinal} introduces an unrelated compiler name.");
                }

                uint index = ReadUInt32(bytecode, currentOffset + 4);
                uint? vertexRegister = stage == ShaderStage.Vertex ? index : null;
                IReadOnlyList<uint> relocationOffsets = stage == ShaderStage.Pixel
                    ? ReadFragmentRelocations(bytecode, index, attributeOffset, attributeEnd, constantOffset, (ulong)constantOffset + (ulong)constantRecordCount * ConstantRecordSize, microcodeOffset, microcodeLength)
                    : Array.Empty<uint>();
                uint[] internalValues = [
                    ReadUInt32(bytecode, currentOffset + 12),
                    ReadUInt32(bytecode, currentOffset + 16),
                    ReadUInt32(bytecode, currentOffset + 20),
                    ReadUInt32(bytecode, currentOffset + 24)
                ];
                elements.Add(new Ps3RsxConstantElement(elementOrdinal, vertexRegister, relocationOffsets, internalValues));
            }

            parameters.Add(new Ps3RsxParameter(name, Ps3RsxParameterCategory.Constant, null, elementCount, (Ps3RsxDataType)typeCode, elements));
            recordOrdinal += elementCount;
        }
        return parameters;
    }

    /// <summary>Reads a fragment relocation table and proves every word reference lies inside fragment microcode.</summary>
    /// <param name="bytecode">Validated program byte array.</param>
    /// <param name="tableOffset">File offset stored in the fragment constant record.</param>
    /// <param name="attributeOffset">Attribute table start.</param>
    /// <param name="attributeEnd">Attribute table exclusive end.</param>
    /// <param name="constantOffset">Constant table start.</param>
    /// <param name="constantEnd">Constant table exclusive end.</param>
    /// <param name="microcodeOffset">Microcode start.</param>
    /// <param name="microcodeLength">Microcode byte length.</param>
    /// <returns>Copied microcode-relative relocation offsets, or an empty list for the SDK sentinel.</returns>
    static IReadOnlyList<uint> ReadFragmentRelocations(byte[] bytecode, uint tableOffset, uint attributeOffset, ulong attributeEnd, uint constantOffset, ulong constantEnd, uint microcodeOffset, ulong microcodeLength) {
        if (tableOffset == NoFragmentRelocations) return Array.Empty<uint>();
        if (tableOffset < HeaderSize || (ulong)tableOffset + sizeof(uint) > (ulong)bytecode.Length) {
            throw new InvalidDataException($"Fragment relocation table offset {tableOffset} is outside the program file.");
        }
        uint relocationCount = ReadUInt32(bytecode, checked((int)tableOffset));
        ulong tableEnd = (ulong)tableOffset + sizeof(uint) + (ulong)relocationCount * sizeof(uint);
        if (tableEnd > (ulong)bytecode.Length) throw new InvalidDataException("Fragment relocation table extends outside the program file.");
        if (RangesOverlap(tableOffset, tableEnd, attributeOffset, attributeEnd)
            || RangesOverlap(tableOffset, tableEnd, constantOffset, constantEnd)
            || RangesOverlap(tableOffset, tableEnd, microcodeOffset, (ulong)microcodeOffset + microcodeLength)) {
            throw new InvalidDataException("Fragment relocation table overlaps an SDK table or microcode section.");
        }

        List<uint> offsets = new(checked((int)relocationCount));
        for (uint ordinal = 0; ordinal < relocationCount; ordinal++) {
            int offsetPosition = checked((int)((ulong)tableOffset + sizeof(uint) + (ulong)ordinal * sizeof(uint)));
            uint microcodeRelativeOffset = ReadUInt32(bytecode, offsetPosition);
            if (microcodeRelativeOffset % sizeof(uint) != 0 || (ulong)microcodeRelativeOffset + sizeof(uint) > microcodeLength) {
                throw new InvalidDataException($"Fragment relocation offset {microcodeRelativeOffset} does not reference a complete aligned word in microcode.");
            }
            offsets.Add(microcodeRelativeOffset);
        }
        return Array.AsReadOnly(offsets.ToArray());
    }

    /// <summary>Checks that nonempty SDK ranges do not overlap.</summary>
    /// <param name="firstStart">First range start.</param>
    /// <param name="firstEnd">First range exclusive end.</param>
    /// <param name="secondStart">Second range start.</param>
    /// <param name="secondEnd">Second range exclusive end.</param>
    /// <returns>True when both ranges contain bytes and intersect.</returns>
    static bool RangesOverlap(uint firstStart, ulong firstEnd, uint secondStart, ulong secondEnd) {
        return (ulong)firstStart < firstEnd && (ulong)secondStart < secondEnd && (ulong)firstStart < secondEnd && (ulong)secondStart < firstEnd;
    }

    /// <summary>Reads one null-terminated SDK name after validating its pointer and UTF-8 contents.</summary>
    /// <param name="bytecode">Program byte array.</param>
    /// <param name="nameOffset">SDK file offset, where zero denotes a valid unnamed continuation/internal value.</param>
    /// <param name="recordName">Diagnostic record description.</param>
    /// <returns>The exact UTF-8 compiler name, or null for an SDK zero-name field.</returns>
    static string ReadName(byte[] bytecode, uint nameOffset, string recordName) {
        if (nameOffset == 0) return null;
        if (nameOffset >= bytecode.Length) throw new InvalidDataException($"{recordName} name offset {nameOffset} is outside the program file.");
        int start = checked((int)nameOffset);
        int end = start;
        while (end < bytecode.Length && bytecode[end] != 0) end++;
        if (end == bytecode.Length) throw new InvalidDataException($"{recordName} name at offset {nameOffset} is not null-terminated.");
        if (end == start) throw new InvalidDataException($"{recordName} name at offset {nameOffset} is empty.");
        try {
            return new UTF8Encoding(false, true).GetString(bytecode, start, end - start);
        } catch (DecoderFallbackException error) {
            throw new InvalidDataException($"{recordName} name at offset {nameOffset} is not valid UTF-8.", error);
        }
    }

    /// <summary>Rejects duplicate nonempty SDK parameter names that would make dump reconciliation ambiguous.</summary>
    /// <param name="parameters">Decoded parameters.</param>
    static void ValidateUniqueParameterNames(IEnumerable<Ps3RsxParameter> parameters) {
        HashSet<string> names = new(StringComparer.Ordinal);
        foreach (Ps3RsxParameter parameter in parameters) {
            if (parameter.Name != null && !names.Add(parameter.Name)) throw new InvalidDataException($"RSX parameter name '{parameter.Name}' appears more than once.");
        }
    }

    /// <summary>Requires a matching stage profile and reconciles active shared dump fields with binary parameters.</summary>
    /// <param name="stage">SDK binary stage.</param>
    /// <param name="assembly">Unmodified cgcomp dump text.</param>
    /// <param name="parameters">Decoded binary parameters.</param>
    static void ValidateAssembly(ShaderStage stage, string assembly, IReadOnlyList<Ps3RsxParameter> parameters) {
        string expectedProfile = stage == ShaderStage.Vertex ? "vp40" : "fp40";
        bool foundProfile = false;
        uint activeVariableCount = 0;
        HashSet<string> matchedNames = new(StringComparer.Ordinal);
        using StringReader reader = new(assembly);
        string line;
        while ((line = reader.ReadLine()) != null) {
            string trimmed = line.Trim();
            if (trimmed.StartsWith("#profile ", StringComparison.Ordinal)) {
                if (foundProfile) throw new InvalidDataException("cgcomp dump contains more than one profile declaration.");
                string profile = trimmed[9..].Trim();
                if (!string.Equals(profile, expectedProfile, StringComparison.Ordinal)) {
                    throw new InvalidDataException($"cgcomp dump profile '{profile}' does not match SDK stage '{stage}'.");
                }
                foundProfile = true;
                continue;
            }
            if (!trimmed.StartsWith("#var ", StringComparison.Ordinal)) continue;
            string[] fields = trimmed[5..].Split(':');
            if (fields.Length != 5) throw new InvalidDataException($"Malformed cgcomp #var declaration: {trimmed}");
            string declaration = fields[0].Trim();
            int nameStart = declaration.IndexOfAny([' ', '\t']);
            if (nameStart <= 0 || nameStart == declaration.Length - 1) throw new InvalidDataException($"Malformed cgcomp #var declaration: {trimmed}");
            string typeName = declaration[..nameStart].Trim();
            string parameterName = declaration[(nameStart + 1)..].Trim();
            if (!int.TryParse(fields[4].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out int activeFlag) || (activeFlag != 0 && activeFlag != 1)) {
                throw new InvalidDataException($"cgcomp #var declaration has an invalid activity flag: {trimmed}");
            }
            if (activeFlag == 0) continue;
            activeVariableCount++;

            string firstBinding = fields[1].Trim();
            string secondBinding = fields[2].Trim();
            if (firstBinding.StartsWith("$vout.", StringComparison.Ordinal)) continue;
            Ps3RsxParameter parameter = FindNamedParameter(parameters, parameterName);
            if (parameter == null) throw new InvalidDataException($"Active cgcomp parameter '{parameterName}' is missing from the SDK parameter tables.");
            if (!matchedNames.Add(parameterName)) throw new InvalidDataException($"cgcomp dump repeats active parameter '{parameterName}'.");
            Ps3RsxDataType dumpType = ParseDumpDataType(typeName);
            if (parameter.DataType != dumpType) throw new InvalidDataException($"cgcomp parameter '{parameterName}' type '{typeName}' disagrees with SDK type '{parameter.DataType}'.");
            ValidateDumpBinding(stage, parameter, firstBinding, secondBinding);
        }

        if (!foundProfile) throw new InvalidDataException("cgcomp dump has no stage profile declaration.");
        if (activeVariableCount == 0) throw new InvalidDataException("cgcomp dump has no active #var declarations.");
        foreach (Ps3RsxParameter parameter in parameters) {
            if (parameter.Name != null && !matchedNames.Contains(parameter.Name)) {
                throw new InvalidDataException($"Named SDK parameter '{parameter.Name}' has no active matching cgcomp #var declaration.");
            }
        }
    }

    /// <summary>Finds a uniquely named decoded SDK parameter.</summary>
    /// <param name="parameters">Decoded parameter list.</param>
    /// <param name="name">Exact cgcomp identifier.</param>
    /// <returns>The matching record, or null when no SDK record uses the name.</returns>
    static Ps3RsxParameter FindNamedParameter(IReadOnlyList<Ps3RsxParameter> parameters, string name) {
        foreach (Ps3RsxParameter parameter in parameters) {
            if (string.Equals(parameter.Name, name, StringComparison.Ordinal)) return parameter;
        }
        return null;
    }

    /// <summary>Checks dump binding category and shared numeric slots without confusing fragment relocation indices with registers.</summary>
    /// <param name="stage">SDK shader stage.</param>
    /// <param name="parameter">Matching decoded SDK parameter.</param>
    /// <param name="firstBinding">Cg primary binding field.</param>
    /// <param name="secondBinding">Cg resource or constant binding field.</param>
    static void ValidateDumpBinding(ShaderStage stage, Ps3RsxParameter parameter, string firstBinding, string secondBinding) {
        if (parameter.Category == Ps3RsxParameterCategory.Attribute) {
            if (!firstBinding.StartsWith("$vin.", StringComparison.Ordinal)) throw new InvalidDataException($"Input parameter '{parameter.Name}' has no cgcomp $vin binding.");
            string semanticSlot = firstBinding[5..];
            string expectedAlias = CanonicalizeInputSlotAlias(semanticSlot, parameter.Name);
            if (!string.Equals(secondBinding, expectedAlias, StringComparison.Ordinal)) throw new InvalidDataException($"Input parameter '{parameter.Name}' has inconsistent cgcomp binding fields.");
            if (stage == ShaderStage.Vertex) {
                uint slot = ParseNumericSlot(expectedAlias, "ATTR", parameter.Name);
                if (slot != parameter.HardwareIndex) throw new InvalidDataException($"Vertex attribute '{parameter.Name}' dump slot {slot} disagrees with SDK index {parameter.HardwareIndex}.");
            } else {
                if (expectedAlias.StartsWith("TEX", StringComparison.Ordinal)) _ = ParseNumericSlot(expectedAlias, "TEX", parameter.Name);
                else if (expectedAlias.StartsWith("COL", StringComparison.Ordinal)) _ = ParseNumericSlot(expectedAlias, "COL", parameter.Name);
                else throw new InvalidDataException($"Fragment input '{parameter.Name}' has unsupported cgcomp slot '{expectedAlias}'.");
            }
            return;
        }
        if (parameter.Category == Ps3RsxParameterCategory.Sampler) {
            if (firstBinding.Length != 0 || !secondBinding.StartsWith("texunit ", StringComparison.Ordinal)) {
                throw new InvalidDataException($"Sampler '{parameter.Name}' has no cgcomp texunit binding.");
            }
            uint textureUnit = ParseUnsigned(secondBinding[8..].Trim(), parameter.Name);
            if (textureUnit != parameter.HardwareIndex) throw new InvalidDataException($"Sampler '{parameter.Name}' dump texunit {textureUnit} disagrees with SDK index {parameter.HardwareIndex}.");
            return;
        }
        if (parameter.Category == Ps3RsxParameterCategory.Constant) {
            if (firstBinding.Length != 0) throw new InvalidDataException($"Constant '{parameter.Name}' has an unexpected primary cgcomp binding.");
            ParseConstantBinding(secondBinding, parameter.Name, out uint register, out uint count);
            if (count != parameter.ElementCount) throw new InvalidDataException($"Constant '{parameter.Name}' dump count {count} disagrees with SDK element count {parameter.ElementCount}.");
            if (stage == ShaderStage.Vertex && AssertVertexConstantRegister(parameter) != register) {
                throw new InvalidDataException($"Vertex constant '{parameter.Name}' dump register {register} disagrees with SDK register {AssertVertexConstantRegister(parameter)}.");
            }
            return;
        }
        throw new InvalidDataException($"RSX parameter '{parameter.Name}' has an unsupported resource category.");
    }

    /// <summary>Returns the first physical vertex constant register for a named dump comparison.</summary>
    /// <param name="parameter">Decoded vertex constant parameter.</param>
    /// <returns>First SDK vertex constant register.</returns>
    static uint AssertVertexConstantRegister(Ps3RsxParameter parameter) {
        if (parameter.Elements.Count == 0 || !parameter.Elements[0].VertexRegister.HasValue) {
            throw new InvalidDataException($"Vertex constant '{parameter.Name}' has no SDK register element.");
        }
        return parameter.Elements[0].VertexRegister.Value;
    }

    /// <summary>Parses one SDK input slot suffix such as ATTR2 or TEX1 and validates its decimal index.</summary>
    /// <param name="slotText">Cg slot name.</param>
    /// <param name="prefix">Expected stage slot prefix.</param>
    /// <param name="parameterName">Parameter name for diagnostics.</param>
    /// <returns>Parsed nonnegative slot number.</returns>
    static uint ParseNumericSlot(string slotText, string prefix, string parameterName) {
        if (!slotText.StartsWith(prefix, StringComparison.Ordinal)) throw new InvalidDataException($"Input parameter '{parameterName}' has unexpected slot '{slotText}'.");
        return ParseUnsigned(slotText[prefix.Length..], parameterName);
    }

    /// <summary>Normalizes the Cg COLORn semantic spelling to its documented COLn binding alias.</summary>
    /// <param name="semanticSlot">Cg stage input semantic after $vin.</param>
    /// <param name="parameterName">Parameter name for diagnostics.</param>
    /// <returns>The exact secondary binding spelling expected for this semantic.</returns>
    static string CanonicalizeInputSlotAlias(string semanticSlot, string parameterName) {
        if (!semanticSlot.StartsWith("COLOR", StringComparison.Ordinal)) return semanticSlot;
        uint colorIndex = ParseUnsigned(semanticSlot[5..], parameterName);
        return "COL" + colorIndex.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>Parses the c[register] and optional element count used by cgcomp constant bindings.</summary>
    /// <param name="binding">Cg constant binding text.</param>
    /// <param name="parameterName">Parameter name for diagnostics.</param>
    /// <returns>The dump register and declared element count.</returns>
    static void ParseConstantBinding(string binding, string parameterName, out uint register, out uint count) {
        if (!binding.StartsWith("c[", StringComparison.Ordinal)) throw new InvalidDataException($"Constant '{parameterName}' has no cgcomp c-register binding.");
        int closeBracket = binding.IndexOf(']', 2);
        if (closeBracket < 0) throw new InvalidDataException($"Constant '{parameterName}' has malformed cgcomp c-register binding '{binding}'.");
        register = ParseUnsigned(binding[2..closeBracket], parameterName);
        string suffix = binding[(closeBracket + 1)..].Trim();
        if (suffix.Length == 0) {
            count = 1;
            return;
        }
        if (suffix[0] != ',') throw new InvalidDataException($"Constant '{parameterName}' has malformed cgcomp element count '{binding}'.");
        count = ParseUnsigned(suffix[1..].Trim(), parameterName);
        if (count == 0) throw new InvalidDataException($"Constant '{parameterName}' has zero cgcomp elements.");
    }

    /// <summary>Parses an unsigned decimal SDK or cgcomp index using invariant syntax.</summary>
    /// <param name="text">Decimal number text.</param>
    /// <param name="parameterName">Parameter name for the error message.</param>
    /// <returns>Parsed unsigned index.</returns>
    static uint ParseUnsigned(string text, string parameterName) {
        if (!uint.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out uint value)) {
            throw new InvalidDataException($"Parameter '{parameterName}' has invalid numeric binding '{text}'.");
        }
        return value;
    }

    /// <summary>Maps the exact Cg dump type spelling to the pinned SDK type code, preserving scalar FLOAT and FLOAT1.</summary>
    /// <param name="typeName">Type token from an active #var declaration.</param>
    /// <returns>The corresponding SDK type code or UNKNOWN for an unrecognized token.</returns>
    static Ps3RsxDataType ParseDumpDataType(string typeName) {
        return typeName.ToLowerInvariant() switch {
            "bool" => Ps3RsxDataType.Bool,
            "bool1" => Ps3RsxDataType.Bool1,
            "bool2" => Ps3RsxDataType.Bool2,
            "bool3" => Ps3RsxDataType.Bool3,
            "bool4" => Ps3RsxDataType.Bool4,
            "float" => Ps3RsxDataType.Float,
            "float1" => Ps3RsxDataType.Float1,
            "float2" => Ps3RsxDataType.Float2,
            "float3" => Ps3RsxDataType.Float3,
            "float4" => Ps3RsxDataType.Float4,
            "float3x4" => Ps3RsxDataType.Float3x4,
            "float4x4" => Ps3RsxDataType.Float4x4,
            "float3x3" => Ps3RsxDataType.Float3x3,
            "float4x3" => Ps3RsxDataType.Float4x3,
            "sampler1d" => Ps3RsxDataType.Sampler1D,
            "sampler2d" => Ps3RsxDataType.Sampler2D,
            "sampler3d" => Ps3RsxDataType.Sampler3D,
            "samplercube" => Ps3RsxDataType.SamplerCube,
            "samplerrect" => Ps3RsxDataType.SamplerRect,
            _ => Ps3RsxDataType.Unknown
        };
    }

    /// <summary>Determines whether an SDK type code represents a texture sampler.</summary>
    /// <param name="dataType">Exact SDK parameter code.</param>
    /// <returns>True for the contiguous SDK sampler type codes 14 through 18.</returns>
    static bool IsSampler(Ps3RsxDataType dataType) {
        return (byte)dataType >= (byte)Ps3RsxDataType.Sampler1D && (byte)dataType <= (byte)Ps3RsxDataType.SamplerRect;
    }

    /// <summary>Reads one pinned SDK big-endian 16-bit integer after its range has been validated.</summary>
    /// <param name="bytes">Program byte array.</param>
    /// <param name="offset">Byte offset.</param>
    /// <returns>Big-endian unsigned integer.</returns>
    static ushort ReadUInt16(byte[] bytes, int offset) {
        return BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(offset, sizeof(ushort)));
    }

    /// <summary>Reads one pinned SDK big-endian 32-bit integer after its range has been validated.</summary>
    /// <param name="bytes">Program byte array.</param>
    /// <param name="offset">Byte offset.</param>
    /// <returns>Big-endian unsigned integer.</returns>
    static uint ReadUInt32(byte[] bytes, int offset) {
        return BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, sizeof(uint)));
    }
}
```
### builder/shaders/Ps3ShaderToolchain.cs
```csharp
using System.Security.Cryptography;
using System.Text.Json;

namespace helengine.ps3.builder;

/// <summary>Loads and validates the repository-locked Docker image and read-only NVIDIA Cg mount.</summary>
public sealed class Ps3ShaderToolchain {
    /// <summary>Creates an immutable toolchain description after its lock and mounted Cg library are validated.</summary>
    /// <param name="imageId">Immutable Docker image identifier.</param>
    /// <param name="cgMountSource">Resolved host directory containing the Cg toolkit.</param>
    /// <param name="cgMountTarget">Locked absolute path inside the Docker image.</param>
    /// <param name="compilerPath">Locked cgcomp executable path inside the Docker image.</param>
    /// <param name="cgLibraryDirectory">Locked Cg library directory inside the image.</param>
    /// <param name="identity">Stable identity formed from image, compiler and Cg library digest.</param>
    Ps3ShaderToolchain(string imageId, string cgMountSource, string cgMountTarget, string compilerPath, string cgLibraryDirectory, string identity) {
        ImageId = imageId;
        CgMountSource = cgMountSource;
        CgMountTarget = cgMountTarget;
        CompilerPath = compilerPath;
        CgLibraryDirectory = cgLibraryDirectory;
        Identity = identity;
    }

    /// <summary>Gets the content-addressed Docker image ID from toolchain-lock.json.</summary>
    public string ImageId { get; }

    /// <summary>Gets the resolved host path mounted read-only for cgcomp's Cg shared library.</summary>
    public string CgMountSource { get; }

    /// <summary>Gets the locked absolute container target of the read-only Cg mount.</summary>
    public string CgMountTarget { get; }

    /// <summary>Gets the cgcomp executable path inside the pinned SDK image.</summary>
    public string CompilerPath { get; }

    /// <summary>Gets the locked Cg library directory used for the compiler process environment.</summary>
    internal string CgLibraryDirectory { get; }

    /// <summary>Gets the stable image, compiler and Cg digest identity attached to each compiled program.</summary>
    public string Identity { get; }

    /// <summary>Loads the repository lock and verifies the exact mounted Cg library before returning a toolchain.</summary>
    /// <param name="repositoryRoot">Explicit repository root containing toolchain-lock.json.</param>
    /// <returns>A validated immutable toolchain configuration.</returns>
    public static Ps3ShaderToolchain Load(string repositoryRoot) {
        if (string.IsNullOrWhiteSpace(repositoryRoot)) throw new ArgumentException("Repository root must be provided.", nameof(repositoryRoot));
        string fullRepositoryRoot = Path.GetFullPath(repositoryRoot);
        string lockPath = Path.Combine(fullRepositoryRoot, "toolchain-lock.json");
        if (!File.Exists(lockPath)) throw new FileNotFoundException("Pinned PS3 toolchain lock was not found.", lockPath);

        using JsonDocument lockDocument = JsonDocument.Parse(File.ReadAllText(lockPath));
        JsonElement root = lockDocument.RootElement;
        string imageId = ReadRequiredString(root, "ResolvedImage", "ImageId");
        string compilerPath = ReadRequiredString(root, "ResolvedImage", "CgcompPath");
        string mountSourceRelative = ReadRequiredString(root, "ShaderToolkit", "MountSource");
        string mountTarget = ReadRequiredString(root, "ShaderToolkit", "MountTarget");
        string libraryPath = ReadRequiredString(root, "ShaderToolkit", "LibraryPath");
        string expectedDigest = ReadRequiredString(root, "ShaderToolkit", "LibCgSha256");
        ValidateImageId(imageId);
        ValidateContainerPath(compilerPath, nameof(compilerPath));
        ValidateContainerPath(mountTarget, nameof(mountTarget));
        ValidateDigest(expectedDigest);

        if (Path.IsPathRooted(mountSourceRelative)) throw new InvalidDataException("Locked Cg mount source must be relative to the repository root.");
        string cgMountSource = Path.GetFullPath(Path.Combine(fullRepositoryRoot, mountSourceRelative));
        EnsureContained(fullRepositoryRoot, cgMountSource, "Locked Cg mount source escapes the repository root.");
        if (!Directory.Exists(cgMountSource)) throw new DirectoryNotFoundException($"Locked Cg mount source does not exist: {cgMountSource}");

        string libraryRelative = libraryPath.Replace('/', Path.DirectorySeparatorChar);
        if (Path.IsPathRooted(libraryRelative)) throw new InvalidDataException("Locked Cg library path must be relative to its mount.");
        string libraryFullPath = Path.GetFullPath(Path.Combine(cgMountSource, libraryRelative));
        EnsureContained(cgMountSource, libraryFullPath, "Locked Cg library path escapes its mount source.");
        if (!File.Exists(libraryFullPath)) throw new FileNotFoundException("Locked Cg shared library is missing.", libraryFullPath);
        string actualDigest;
        using (FileStream libraryStream = File.OpenRead(libraryFullPath)) actualDigest = Convert.ToHexString(SHA256.HashData(libraryStream));
        if (!string.Equals(actualDigest, expectedDigest, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidDataException($"Mounted Cg library SHA-256 mismatch: expected {expectedDigest}, found {actualDigest}.");
        }

        string containerLibraryDirectory = GetContainerDirectory(mountTarget, libraryPath);
        string identity = $"{imageId}|{compilerPath}|{actualDigest}";
        return new Ps3ShaderToolchain(imageId, cgMountSource, mountTarget, compilerPath, containerLibraryDirectory, identity);
    }

    /// <summary>Reads one required nonempty string from a nested toolchain-lock.json object.</summary>
    /// <param name="root">Parsed lock root.</param>
    /// <param name="objectName">Parent property name.</param>
    /// <param name="propertyName">Required string property name.</param>
    /// <returns>The required lock value.</returns>
    static string ReadRequiredString(JsonElement root, string objectName, string propertyName) {
        if (!root.TryGetProperty(objectName, out JsonElement parent) || parent.ValueKind != JsonValueKind.Object) {
            throw new InvalidDataException($"Toolchain lock is missing object '{objectName}'.");
        }
        if (!parent.TryGetProperty(propertyName, out JsonElement property) || property.ValueKind != JsonValueKind.String) {
            throw new InvalidDataException($"Toolchain lock is missing string '{objectName}.{propertyName}'.");
        }
        string value = property.GetString();
        if (string.IsNullOrWhiteSpace(value)) throw new InvalidDataException($"Toolchain lock value '{objectName}.{propertyName}' must not be empty.");
        return value;
    }

    /// <summary>Validates that a Docker image identity is a full immutable SHA-256 image ID.</summary>
    /// <param name="imageId">Locked Docker image ID.</param>
    static void ValidateImageId(string imageId) {
        const string Prefix = "sha256:";
        if (!imageId.StartsWith(Prefix, StringComparison.Ordinal) || imageId.Length != Prefix.Length + 64 || !IsHex(imageId.AsSpan(Prefix.Length))) {
            throw new InvalidDataException("Locked Docker ImageId must be sha256 followed by exactly 64 hexadecimal characters.");
        }
    }

    /// <summary>Validates that a Cg library digest is exactly 64 hexadecimal characters.</summary>
    /// <param name="digest">Locked SHA-256 value.</param>
    static void ValidateDigest(string digest) {
        if (digest.Length != 64 || !IsHex(digest.AsSpan())) throw new InvalidDataException("Locked Cg LibCgSha256 must contain exactly 64 hexadecimal characters.");
    }

    /// <summary>Determines whether every character in a digest span is hexadecimal.</summary>
    /// <param name="value">Candidate hexadecimal text.</param>
    /// <returns>True when all characters are ASCII hexadecimal digits.</returns>
    static bool IsHex(ReadOnlySpan<char> value) {
        foreach (char character in value) {
            if (!(character >= '0' && character <= '9') && !(character >= 'a' && character <= 'f') && !(character >= 'A' && character <= 'F')) return false;
        }
        return true;
    }

    /// <summary>Validates an absolute POSIX path that is passed as a container executable or mount target.</summary>
    /// <param name="path">Locked container path.</param>
    /// <param name="name">Field name for the diagnostic.</param>
    static void ValidateContainerPath(string path, string name) {
        if (path.Length == 0 || path[0] != '/' || path.Contains(',') || path.Contains('\0')) {
            throw new InvalidDataException($"Locked container path '{name}' must be an absolute POSIX path without mount delimiters.");
        }
    }

    /// <summary>Ensures a normalized path remains at or below its designated root.</summary>
    /// <param name="root">Allowed normalized root.</param>
    /// <param name="path">Candidate normalized path.</param>
    /// <param name="error">Diagnostic used when the path escapes.</param>
    static void EnsureContained(string root, string path, string error) {
        string relative = Path.GetRelativePath(root, path);
        if (Path.IsPathRooted(relative) || relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)) {
            throw new InvalidDataException(error);
        }
    }

    /// <summary>Combines the locked mount target and Cg library's relative parent directory using POSIX separators.</summary>
    /// <param name="mountTarget">Locked Cg mount target.</param>
    /// <param name="libraryPath">Locked Cg library path below the mount.</param>
    /// <returns>Container directory containing libCg.so.</returns>
    static string GetContainerDirectory(string mountTarget, string libraryPath) {
        string normalizedLibraryPath = libraryPath.Replace('\\', '/');
        int separator = normalizedLibraryPath.LastIndexOf('/');
        string libraryDirectory = separator < 0 ? string.Empty : normalizedLibraryPath[..separator];
        string mount = mountTarget.TrimEnd('/');
        return libraryDirectory.Length == 0 ? mount : $"{mount}/{libraryDirectory}";
    }
}
```
### builder/shaders/Ps3DockerShaderCommand.cs
```csharp
using helengine;
using System.Diagnostics;

namespace helengine.ps3.builder;

/// <summary>Builds a shell-free Docker invocation for one uniquely owned locked-SDK shader job.</summary>
public sealed class Ps3DockerShaderCommand {
    /// <summary>Initializes a Docker command using explicit validated toolchain and job paths.</summary>
    /// <param name="toolchain">Validated image and Cg mount configuration.</param>
    /// <param name="stage">Vertex or pixel shader stage.</param>
    /// <param name="containerName">Unique Docker container identity owned by this job.</param>
    /// <param name="jobDirectory">Absolute host directory mounted at /work for this isolated job.</param>
    public Ps3DockerShaderCommand(Ps3ShaderToolchain toolchain, ShaderStage stage, string containerName, string jobDirectory) {
        ArgumentNullException.ThrowIfNull(toolchain);
        if (stage != ShaderStage.Vertex && stage != ShaderStage.Pixel) throw new NotSupportedException($"RSX compiler stage '{stage}' is unsupported.");
        if (string.IsNullOrWhiteSpace(containerName) || containerName.Any(character => !char.IsAsciiLetterOrDigit(character) && character != '-' && character != '_')) {
            throw new ArgumentException("Docker container name must contain only ASCII letters, digits, dash and underscore.", nameof(containerName));
        }
        if (string.IsNullOrWhiteSpace(jobDirectory) || !Path.IsPathRooted(jobDirectory)) {
            throw new ArgumentException("An explicit absolute job directory is required.", nameof(jobDirectory));
        }
        Toolchain = toolchain;
        Stage = stage;
        ContainerName = containerName;
        JobDirectory = Path.GetFullPath(jobDirectory);
    }

    /// <summary>Gets the exact unique container identity used for cancellation and test observability.</summary>
    public string ContainerName { get; }

    /// <summary>Gets the isolated absolute host job directory mounted at /work.</summary>
    public string JobDirectory { get; }

    /// <summary>Creates a Docker process start description with every argument kept as a distinct value.</summary>
    /// <returns>Configured shell-free Docker process start information.</returns>
    public ProcessStartInfo CreateStartInfo() {
        ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = JobDirectory
        };
        Add(startInfo, "run");
        Add(startInfo, "--pull=never");
        Add(startInfo, "--rm");
        Add(startInfo, "--name");
        Add(startInfo, ContainerName);
        Add(startInfo, "--network");
        Add(startInfo, "none");
        Add(startInfo, "--mount");
        Add(startInfo, $"type=bind,source={Toolchain.CgMountSource},target={Toolchain.CgMountTarget},readonly");
        Add(startInfo, "--mount");
        Add(startInfo, $"type=bind,source={JobDirectory},target=/work");
        Add(startInfo, "--env");
        Add(startInfo, $"LD_LIBRARY_PATH={Toolchain.CgLibraryDirectory}");
        Add(startInfo, Toolchain.ImageId);
        Add(startInfo, Toolchain.CompilerPath);
        Add(startInfo, Stage == ShaderStage.Vertex ? "-v" : "-f");
        Add(startInfo, "-d");
        Add(startInfo, "-Wcg,-oglsl");
        Add(startInfo, "/work/source.glsl");
        Add(startInfo, "/work/program.bin");
        return startInfo;
    }

    /// <summary>Adds one literal argument without joining or shell-escaping it.</summary>
    /// <param name="startInfo">Docker process start description.</param>
    /// <param name="argument">One complete Docker CLI argument.</param>
    static void Add(ProcessStartInfo startInfo, string argument) {
        startInfo.ArgumentList.Add(argument);
    }

    /// <summary>Stores the validated toolchain used to construct the immutable invocation arguments.</summary>
    Ps3ShaderToolchain Toolchain { get; }

    /// <summary>Stores the requested vertex or pixel stage.</summary>
    ShaderStage Stage { get; }
}
```
### builder/shaders/Ps3DockerShaderCompiler.cs
```csharp
using helengine;
using System.Diagnostics;
using System.Text;

namespace helengine.ps3.builder;

/// <summary>Runs cgcomp in an isolated, network-disabled container from the repository's locked SDK image.</summary>
public sealed class Ps3DockerShaderCompiler : IPs3RsxCompiler {
    /// <summary>Initializes the compiler with a previously validated locked toolchain and Cg mount.</summary>
    /// <param name="toolchain">Validated SDK image, compiler path and Cg shared library.</param>
    public Ps3DockerShaderCompiler(Ps3ShaderToolchain toolchain) {
        ArgumentNullException.ThrowIfNull(toolchain);
        Toolchain = toolchain;
    }

    /// <summary>Compiles one Task2 translation under a caller-supplied root and validates its SDK binary and dump.</summary>
    /// <param name="translation">Translated GLSL and stage metadata.</param>
    /// <param name="workingRoot">Explicit build-owned parent directory for a unique job.</param>
    /// <param name="cancellationToken">Token that terminates this job's owned container and Docker process.</param>
    /// <returns>Validated program data copied from the SDK output.</returns>
    public Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(translation);
        return CompileJobAsync(translation, workingRoot, cancellationToken).GetAwaiter().GetResult();
    }

    /// <summary>Creates a unique host job directory, invokes Docker, stores logs and validates the generated outputs.</summary>
    /// <param name="translation">Translated GLSL and stage metadata.</param>
    /// <param name="workingRoot">Explicit absolute build-owned parent directory.</param>
    /// <param name="cancellationToken">Caller cancellation token.</param>
    /// <returns>Validated SDK program.</returns>
    async Task<Ps3RsxProgram> CompileJobAsync(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken) {
        cancellationToken.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(workingRoot) || !Path.IsPathRooted(workingRoot)) {
            throw new ArgumentException("An explicit absolute shader compiler working root is required.", nameof(workingRoot));
        }
        if (translation.Stage != ShaderStage.Vertex && translation.Stage != ShaderStage.Pixel) {
            throw new NotSupportedException($"RSX compiler stage '{translation.Stage}' is unsupported.");
        }

        string fullWorkingRoot = Path.GetFullPath(workingRoot);
        Directory.CreateDirectory(fullWorkingRoot);
        string jobDirectory = CreateUniqueJobDirectory(fullWorkingRoot);
        string containerName = Path.GetFileName(jobDirectory);
        string standardOutput = string.Empty;
        string standardError = string.Empty;
        File.WriteAllText(Path.Combine(jobDirectory, "source.glsl"), translation.GlslSource, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(jobDirectory, "container-name.txt"), containerName, new UTF8Encoding(false));
        Ps3DockerShaderCommand command = new(Toolchain, translation.Stage, containerName, jobDirectory);

        try {
            using Process process = new() { StartInfo = command.CreateStartInfo() };
            if (!process.Start()) throw new InvalidOperationException("Failed to start the Docker CLI for the locked PS3 shader toolchain.");
            Task<string> standardOutputTask = process.StandardOutput.ReadToEndAsync();
            Task<string> standardErrorTask = process.StandardError.ReadToEndAsync();
            try {
                await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            } catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) {
                Exception cleanupFailure = await StopOwnedContainerAsync(containerName, process).ConfigureAwait(false);
                standardOutput = await standardOutputTask.ConfigureAwait(false);
                standardError = await standardErrorTask.ConfigureAwait(false);
                SaveProcessLogs(jobDirectory, standardOutput, standardError);
                if (cleanupFailure != null) {
                    throw new OperationCanceledException($"Cancellation cleanup failed for owned Docker container '{containerName}'.", cleanupFailure, cancellationToken);
                }
                throw new OperationCanceledException($"PS3 shader Docker job '{containerName}' was cancelled.", cancellationToken);
            }
            standardOutput = await standardOutputTask.ConfigureAwait(false);
            standardError = await standardErrorTask.ConfigureAwait(false);
            SaveProcessLogs(jobDirectory, standardOutput, standardError);

            if (process.ExitCode != 0) {
                throw new InvalidOperationException($"cgcomp in pinned Docker image exited with code {process.ExitCode}. See '{jobDirectory}'.{Environment.NewLine}{standardError}");
            }
            string bytecodePath = Path.Combine(jobDirectory, "program.bin");
            if (!File.Exists(bytecodePath)) throw new InvalidOperationException($"cgcomp succeeded without producing '{bytecodePath}'. See '{jobDirectory}'.");
            byte[] bytecode = File.ReadAllBytes(bytecodePath);
            if (bytecode.Length == 0) throw new InvalidOperationException($"cgcomp produced an empty RSX program at '{bytecodePath}'. See '{jobDirectory}'.");
            string dumpPath = bytecodePath + ".dump";
            if (!File.Exists(dumpPath)) throw new InvalidOperationException($"cgcomp succeeded without producing its required dump '{dumpPath}'. See '{jobDirectory}'.");
            string assembly = File.ReadAllText(dumpPath);
            if (string.IsNullOrWhiteSpace(assembly)) throw new InvalidOperationException($"cgcomp produced an empty compiler dump at '{dumpPath}'. See '{jobDirectory}'.");
            return Ps3RsxProgramReader.Read(translation.Stage, bytecode, assembly, Toolchain.Identity);
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception error) {
            if (standardError.Length == 0) standardError = error.ToString();
            SaveProcessLogs(jobDirectory, standardOutput, standardError);
            throw;
        }
    }

    /// <summary>Creates one collision-resistant child directory beneath the exact caller-supplied root.</summary>
    /// <param name="workingRoot">Normalized caller-supplied root.</param>
    /// <returns>Newly created isolated job directory.</returns>
    static string CreateUniqueJobDirectory(string workingRoot) {
        while (true) {
            string jobDirectory = Path.Combine(workingRoot, "ps3-shader-" + Guid.NewGuid().ToString("N"));
            if (Directory.Exists(jobDirectory) || File.Exists(jobDirectory)) continue;
            Directory.CreateDirectory(jobDirectory);
            return jobDirectory;
        }
    }

    /// <summary>Stores the Docker CLI's unmodified output streams inside its isolated job for inspection.</summary>
    /// <param name="jobDirectory">Owned compiler job directory.</param>
    /// <param name="standardOutput">Captured Docker stdout.</param>
    /// <param name="standardError">Captured Docker stderr.</param>
    static void SaveProcessLogs(string jobDirectory, string standardOutput, string standardError) {
        File.WriteAllText(Path.Combine(jobDirectory, "compiler.stdout.log"), standardOutput, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(jobDirectory, "compiler.stderr.log"), standardError, new UTF8Encoding(false));
    }

    /// <summary>Stops only the unique container and Docker CLI process owned by the cancelled shader job.</summary>
    /// <param name="containerName">Unique job container name.</param>
    /// <param name="dockerRunProcess">Docker CLI process created for this job.</param>
    /// <returns>A cleanup failure to preserve alongside cancellation, or null when shutdown completed.</returns>
    static async Task<Exception> StopOwnedContainerAsync(string containerName, Process dockerRunProcess) {
        Exception cleanupFailure = await RemoveOwnedContainerAsync(containerName).ConfigureAwait(false);

        try {
            using CancellationTokenSource waitTimeout = new(TimeSpan.FromSeconds(10));
            await dockerRunProcess.WaitForExitAsync(waitTimeout.Token).ConfigureAwait(false);
        } catch (OperationCanceledException) {
            try {
                if (!dockerRunProcess.HasExited) dockerRunProcess.Kill(true);
                await dockerRunProcess.WaitForExitAsync().ConfigureAwait(false);
            } catch (Exception error) {
                cleanupFailure ??= error;
            }
        } catch (Exception error) {
            cleanupFailure ??= error;
        }

        try {
            if (await ContainerExistsAsync(containerName).ConfigureAwait(false)) {
                cleanupFailure ??= new InvalidOperationException($"Owned Docker container '{containerName}' still exists after cancellation cleanup.");
            }
        } catch (Exception error) {
            cleanupFailure ??= error;
        }
        return cleanupFailure;
    }

    /// <summary>Force-removes only the named job container when Docker kill cannot stop it cleanly.</summary>
    /// <param name="containerName">Unique job container name.</param>
    /// <returns>A cleanup error when removal fails, or null when removal succeeds.</returns>
    static async Task<Exception> RemoveOwnedContainerAsync(string containerName) {
        try {
            using Process removeProcess = new() { StartInfo = CreateDockerControlStartInfo("rm", "-f", containerName) };
            if (!removeProcess.Start()) return new InvalidOperationException("Failed to start docker rm for the owned shader container.");
            Task<string> outputTask = removeProcess.StandardOutput.ReadToEndAsync();
            Task<string> errorTask = removeProcess.StandardError.ReadToEndAsync();
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
            await removeProcess.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
            _ = await outputTask.ConfigureAwait(false);
            string error = await errorTask.ConfigureAwait(false);
            if (removeProcess.ExitCode == 0) return null;
            if (!await ContainerExistsAsync(containerName).ConfigureAwait(false)) return null;
            return new InvalidOperationException($"docker rm failed for owned container '{containerName}': {error}");
        } catch (Exception error) {
            return error;
        }
    }

    /// <summary>Checks exact container existence and distinguishes a missing name from Docker daemon errors.</summary>
    /// <param name="containerName">Unique container identity owned by the cancelled compiler job.</param>
    /// <returns>True when Docker reports the exact container; false only when Docker reports no such object.</returns>
    static async Task<bool> ContainerExistsAsync(string containerName) {
        using Process inspectProcess = new() { StartInfo = CreateDockerControlStartInfo("inspect", "--format", "{{.Id}}", containerName) };
        if (!inspectProcess.Start()) throw new InvalidOperationException("Failed to start docker inspect for the owned shader container.");
        Task<string> outputTask = inspectProcess.StandardOutput.ReadToEndAsync();
        Task<string> errorTask = inspectProcess.StandardError.ReadToEndAsync();
        await inspectProcess.WaitForExitAsync().ConfigureAwait(false);
        string output = await outputTask.ConfigureAwait(false);
        string error = await errorTask.ConfigureAwait(false);
        if (inspectProcess.ExitCode == 0) return output.Trim().Length > 0;
        if (error.Contains("no such object", StringComparison.OrdinalIgnoreCase) || error.Contains("no such container", StringComparison.OrdinalIgnoreCase)) return false;
        throw new InvalidOperationException($"docker inspect failed for owned container '{containerName}': {error}");
    }

    /// <summary>Creates a direct Docker control command with separate arguments and redirected output.</summary>
    /// <param name="arguments">Docker subcommand and literal arguments.</param>
    /// <returns>Configured Docker CLI process start description.</returns>
    static ProcessStartInfo CreateDockerControlStartInfo(params string[] arguments) {
        ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (string argument in arguments) startInfo.ArgumentList.Add(argument);
        return startInfo;
    }

    /// <summary>Stores the validated toolchain used by all invocations from this compiler instance.</summary>
    Ps3ShaderToolchain Toolchain { get; }
}
```
### builder.tests/Ps3RsxProgramReaderTests.cs
```csharp
using helengine;
using helengine.ps3.builder;
using System.Buffers.Binary;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies strict parsing of pinned SDK RSX program headers and parameter records.</summary>
public sealed class Ps3RsxProgramReaderTests {
    /// <summary>The byte size of one SDK attribute record.</summary>
    const int AttributeRecordSize = 12;

    /// <summary>The byte size of one SDK constant record.</summary>
    const int ConstantRecordSize = 28;

    /// <summary>The byte size of one SDK program header.</summary>
    const int HeaderSize = 36;

    /// <summary>A valid compiler dump with shared vertex attributes and a register-bound constant.</summary>
    const string VertexAssembly = "#profile vp40\n#var float3 POSITION : $vin.ATTR0 : ATTR0 : -1 : 1\n#var float4 tint :  : c[2] : -1 : 1\n#var float4 inactive :  : c[8] : -1 : 0\n";

    /// <summary>A valid compiler dump with a fragment constant but no fabricated constant register.</summary>
    const string FragmentAssembly = "#profile fp40\n#var float4 tint :  : c[0] : -1 : 1\n";

    /// <summary>A valid vertex fixture exposes attributes and one named vector constant.</summary>
    [Fact]
    public void ReadsVertexAttributesAndConstantRegisters() {
        byte[] bytes = CreateVertexProgram();

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk-id");

        Ps3RsxParameter attribute = Assert.Single(program.Parameters, parameter => parameter.Name == "POSITION");
        Ps3RsxParameter constant = Assert.Single(program.Parameters, parameter => parameter.Name == "tint");
        Assert.Equal(ShaderStage.Vertex, program.Stage);
        Assert.Equal(Ps3RsxParameterCategory.Attribute, attribute.Category);
        Assert.Equal(0u, attribute.HardwareIndex);
        Assert.Equal(Ps3RsxDataType.Float3, attribute.DataType);
        Assert.Equal(Ps3RsxParameterCategory.Constant, constant.Category);
        Assert.Null(constant.HardwareIndex);
        Assert.Equal(1u, constant.ElementCount);
        Assert.Equal(2u, Assert.Single(constant.Elements).VertexRegister);
        Assert.Equal(new uint[] { 0x3F800000, 0x40000000, 0x40400000, 0x40800000 }, Assert.Single(constant.Elements).InternalValues);
        Assert.DoesNotContain(program.Parameters, parameter => parameter.Name == "inactive");
    }

    /// <summary>Groups a matrix's unnamed continuation rows beneath its named first constant record.</summary>
    [Fact]
    public void PreservesMatrixConstantElementBoundaries() {
        byte[] bytes = CreateVertexMatrixProgram();
        string assembly = "#profile vp40\n#var float4x4 transform :  : c[0], 4 : -1 : 1\n";

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, assembly, "sdk-id");

        Ps3RsxParameter matrix = Assert.Single(program.Parameters);
        Assert.Equal("transform", matrix.Name);
        Assert.Equal(4u, matrix.ElementCount);
        Assert.Equal(new uint?[] { 0, 1, 2, 3 }, matrix.Elements.Select(element => element.VertexRegister));
        Assert.Equal(new uint[] { 0, 1, 2, 3 }, matrix.Elements.Select(element => element.ElementOrdinal));
    }

    /// <summary>Program construction copies the SDK bytes and retains the compiler dump and toolchain identity.</summary>
    [Fact]
    public void PreservesCopiedCompilerOutput() {
        byte[] bytes = CreateVertexProgram();
        byte[] expected = (byte[])bytes.Clone();
        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "pinned-sdk");
        bytes[0] = 0;

        Assert.Equal(expected, program.Bytecode);
        Assert.Equal(VertexAssembly, program.Assembly);
        Assert.Equal("pinned-sdk", program.ToolchainIdentity);
    }

    /// <summary>Rejects a program whose stage magic is not a pinned SDK vertex or fragment signature.</summary>
    [Fact]
    public void RejectsWrongMagic() {
        byte[] bytes = CreateVertexProgram();
        bytes[0] = 0;

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects an SDK program when the requested shader stage differs from its encoded stage.</summary>
    [Fact]
    public void RejectsStageMismatch() {
        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Pixel, CreateVertexProgram(), VertexAssembly, "sdk"));
    }

    /// <summary>Rejects headers shorter than the pinned SDK's complete 36-byte common header.</summary>
    [Fact]
    public void RejectsTruncatedHeader() {
        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, new byte[HeaderSize - 1], VertexAssembly, "sdk"));
    }

    /// <summary>Rejects overflowing offset arithmetic before reading an attribute table.</summary>
    [Fact]
    public void RejectsOverflowingTableRange() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, 12, uint.MaxValue - 3);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects a name offset that does not point to a terminated string within the program.</summary>
    [Fact]
    public void RejectsOutOfBoundsName() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, HeaderSize, uint.MaxValue - 1);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects instruction bytes extending beyond the SDK program file.</summary>
    [Fact]
    public void RejectsOutOfBoundsMicrocode() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, 20, 0xFFFFFFF0);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects a microcode start that violates the SDK's 16-byte instruction alignment.</summary>
    [Fact]
    public void RejectsInvalidMicrocodeAlignment() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt32(bytes, 20, 81);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>Rejects SDK headers that declare no instructions instead of returning an empty program.</summary>
    [Fact]
    public void RejectsEmptyMicrocode() {
        byte[] bytes = CreateVertexProgram();
        WriteUInt16(bytes, 10, 0);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytes, VertexAssembly, "sdk"));
    }

    /// <summary>A fragment relocation record is resolved as a microcode offset rather than a hardware register.</summary>
    [Fact]
    public void ReadsFragmentRelocationsWithoutInventingRegisterIndices() {
        byte[] bytes = CreateFragmentProgramWithRelocation();

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, FragmentAssembly, "sdk-id");

        Ps3RsxParameter constant = Assert.Single(program.Parameters, parameter => parameter.Category == Ps3RsxParameterCategory.Constant);
        Ps3RsxConstantElement element = Assert.Single(constant.Elements);
        Assert.Null(constant.HardwareIndex);
        Assert.Null(element.VertexRegister);
        Assert.Equal(new uint[] { 0 }, element.FragmentRelocationOffsets);
    }

    /// <summary>An empty fragment relocation list remains empty instead of becoming register zero.</summary>
    [Fact]
    public void PreservesFragmentConstantWithoutRelocations() {
        byte[] bytes = CreateFragmentProgramWithoutRelocation();

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, FragmentAssembly, "sdk-id");

        Ps3RsxConstantElement element = Assert.Single(Assert.Single(program.Parameters).Elements);
        Assert.Empty(element.FragmentRelocationOffsets);
        Assert.Null(element.VertexRegister);
    }

    /// <summary>Accepts Cg's distinct COLOR0 and COL0 spellings while preserving the fragment attribute's raw SDK index.</summary>
    [Fact]
    public void ReadsFragmentColorInputAlias() {
        byte[] bytes = CreateFragmentColorInputProgram();
        string assembly = "#profile fp40\n#var float4 gl_Color : $vin.COLOR0 : COL0 : -1 : 1\n";

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, assembly, "sdk-id");

        Ps3RsxParameter color = Assert.Single(program.Parameters);
        Assert.Equal("gl_Color", color.Name);
        Assert.Equal(Ps3RsxParameterCategory.Attribute, color.Category);
        Assert.Equal(0u, color.HardwareIndex);
        Assert.Equal(Ps3RsxDataType.Float4, color.DataType);
    }

    /// <summary>Rejects relocation references that fall outside the fragment microcode region.</summary>
    [Fact]
    public void RejectsOutOfBoundsFragmentRelocation() {
        byte[] bytes = CreateFragmentProgramWithRelocation();
        WriteUInt32(bytes, 68, 32);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Pixel, bytes, FragmentAssembly, "sdk"));
    }

    /// <summary>Rejects compiler dump input whose parameter name disagrees with the SDK binary name.</summary>
    [Fact]
    public void RejectsDumpNameMismatch() {
        string assembly = VertexAssembly.Replace("POSITION", "NORMAL", StringComparison.Ordinal);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Rejects compiler dump input whose attribute slot disagrees with the SDK hardware index.</summary>
    [Fact]
    public void RejectsDumpAttributeSlotMismatch() {
        string assembly = VertexAssembly.Replace("ATTR0", "ATTR1", StringComparison.Ordinal);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Rejects compiler dump input whose parameter type disagrees with the SDK type code.</summary>
    [Fact]
    public void RejectsDumpTypeMismatch() {
        string assembly = VertexAssembly.Replace("float3 POSITION", "float4 POSITION", StringComparison.Ordinal);

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Rejects a stage profile that conflicts with the stage encoded in the SDK binary.</summary>
    [Fact]
    public void RejectsAssemblyProfileMismatch() {
        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateVertexProgram(), FragmentAssembly, "sdk"));
    }

    /// <summary>Rejects profile-only text instead of treating an arbitrary empty dump as compiler evidence.</summary>
    [Fact]
    public void RejectsDumpWithoutVariableDeclarations() {
        string assembly = "#profile vp40\n";

        Assert.Throws<InvalidDataException>(() => Ps3RsxProgramReader.Read(ShaderStage.Vertex, CreateEmptyVertexProgram(), assembly, "sdk"));
    }

    /// <summary>Creates a vertex program fixture with exact SDK header, table and name fields.</summary>
    /// <returns>A valid one-instruction vertex program byte sequence.</returns>
    static byte[] CreateVertexProgram() {
        byte[] bytes = new byte[116];
        WriteUInt16(bytes, 0, 0x5650);
        WriteUInt16(bytes, 4, 1);
        WriteUInt16(bytes, 6, 1);
        WriteUInt16(bytes, 8, 1);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize + AttributeRecordSize);
        WriteUInt32(bytes, 20, 80);
        WriteUInt32(bytes, 24, 1);
        WriteUInt32(bytes, 28, 0);
        WriteUInt16(bytes, 32, 0);
        WriteUInt16(bytes, 34, 0);
        WriteUInt32(bytes, HeaderSize, 96);
        WriteUInt32(bytes, HeaderSize + 4, 0);
        bytes[HeaderSize + 8] = 8;
        int constantOffset = HeaderSize + AttributeRecordSize;
        WriteUInt32(bytes, constantOffset, 106);
        WriteUInt32(bytes, constantOffset + 4, 2);
        bytes[constantOffset + 8] = 9;
        bytes[constantOffset + 10] = 1;
        WriteUInt32(bytes, constantOffset + 12, 0x3F800000);
        WriteUInt32(bytes, constantOffset + 16, 0x40000000);
        WriteUInt32(bytes, constantOffset + 20, 0x40400000);
        WriteUInt32(bytes, constantOffset + 24, 0x40800000);
        WriteName(bytes, 96, "POSITION");
        WriteName(bytes, 106, "tint");
        return bytes;
    }

    /// <summary>Creates one FLOAT4x4 parameter with four physical vertex constant records.</summary>
    /// <returns>A valid one-instruction vertex program with three unnamed continuation rows.</returns>
    static byte[] CreateVertexMatrixProgram() {
        byte[] bytes = new byte[186];
        WriteUInt16(bytes, 0, 0x5650);
        WriteUInt16(bytes, 4, 4);
        WriteUInt16(bytes, 6, 0);
        WriteUInt16(bytes, 8, 4);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize);
        WriteUInt32(bytes, 20, 160);
        for (int element = 0; element < 4; element++) {
            int recordOffset = HeaderSize + element * ConstantRecordSize;
            if (element == 0) WriteUInt32(bytes, recordOffset, 176);
            WriteUInt32(bytes, recordOffset + 4, (uint)element);
            bytes[recordOffset + 8] = 11;
            bytes[recordOffset + 10] = element == 0 ? (byte)4 : (byte)0;
        }
        WriteName(bytes, 176, "transform");
        return bytes;
    }

    /// <summary>Creates a valid one-instruction vertex program with no SDK parameter records.</summary>
    /// <returns>A header and aligned microcode block suitable for dump validation.</returns>
    static byte[] CreateEmptyVertexProgram() {
        byte[] bytes = new byte[64];
        WriteUInt16(bytes, 0, 0x5650);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize);
        WriteUInt32(bytes, 20, 48);
        return bytes;
    }

    /// <summary>Creates a fragment program fixture with one relocation table and one referenced instruction word.</summary>
    /// <returns>A valid fragment program byte sequence.</returns>
    static byte[] CreateFragmentProgramWithRelocation() {
        byte[] bytes = CreateFragmentHeaderAndConstant(64);
        WriteUInt32(bytes, 64, 1);
        WriteUInt32(bytes, 68, 0);
        return bytes;
    }

    /// <summary>Creates a fragment program fixture whose constant has no relocation table.</summary>
    /// <returns>A valid fragment program byte sequence.</returns>
    static byte[] CreateFragmentProgramWithoutRelocation() {
        byte[] bytes = CreateFragmentHeaderAndConstant(0xFFFFFFFF);
        return bytes;
    }

    /// <summary>Creates a fragment program whose COLOR0 input uses the SDK's raw attribute index zero.</summary>
    /// <returns>A valid one-instruction fragment input program.</returns>
    static byte[] CreateFragmentColorInputProgram() {
        byte[] bytes = new byte[90];
        WriteUInt16(bytes, 0, 0x4650);
        WriteUInt16(bytes, 4, 1);
        WriteUInt16(bytes, 6, 1);
        WriteUInt16(bytes, 8, 0);
        WriteUInt16(bytes, 10, 1);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize + AttributeRecordSize);
        WriteUInt32(bytes, 20, 64);
        WriteUInt32(bytes, HeaderSize, 80);
        WriteUInt32(bytes, HeaderSize + 4, 0);
        bytes[HeaderSize + 8] = 9;
        WriteName(bytes, 80, "gl_Color");
        return bytes;
    }

    /// <summary>Creates an explicitly laid out fragment header, constant and two-instruction microcode area.</summary>
    /// <param name="relocationTableOffset">File offset to the relocation table, or the SDK no-relocations sentinel.</param>
    /// <returns>A fragment program fixture with all ranges sized for one relocation entry.</returns>
    static byte[] CreateFragmentHeaderAndConstant(uint relocationTableOffset) {
        byte[] bytes = new byte[128];
        WriteUInt16(bytes, 0, 0x4650);
        WriteUInt16(bytes, 4, 2);
        WriteUInt16(bytes, 6, 0);
        WriteUInt16(bytes, 8, 1);
        WriteUInt16(bytes, 10, 2);
        WriteUInt32(bytes, 12, HeaderSize);
        WriteUInt32(bytes, 16, HeaderSize);
        WriteUInt32(bytes, 20, 80);
        WriteUInt32(bytes, 24, 0x40);
        WriteUInt16(bytes, 28, 0);
        WriteUInt16(bytes, 30, 0);
        WriteUInt16(bytes, 32, 0);
        WriteUInt16(bytes, 34, 0);
        WriteUInt32(bytes, HeaderSize, 120);
        WriteUInt32(bytes, HeaderSize + 4, relocationTableOffset);
        bytes[HeaderSize + 8] = 9;
        bytes[HeaderSize + 10] = 1;
        WriteUInt32(bytes, HeaderSize + 12, 0x3F800000);
        WriteUInt32(bytes, 80, 0x12345678);
        WriteUInt32(bytes, 84, 0x90ABCDEF);
        WriteName(bytes, 120, "tint");
        return bytes;
    }

    /// <summary>Writes an SDK big-endian 16-bit header field.</summary>
    /// <param name="bytes">Fixture byte array.</param>
    /// <param name="offset">Field byte offset.</param>
    /// <param name="value">Field value.</param>
    static void WriteUInt16(byte[] bytes, int offset, ushort value) {
        BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(offset, sizeof(ushort)), value);
    }

    /// <summary>Writes an SDK big-endian 32-bit field.</summary>
    /// <param name="bytes">Fixture byte array.</param>
    /// <param name="offset">Field byte offset.</param>
    /// <param name="value">Field value.</param>
    static void WriteUInt32(byte[] bytes, int offset, uint value) {
        BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(offset, sizeof(uint)), value);
    }

    /// <summary>Writes a null-terminated UTF-8 fixture name at an exact file offset.</summary>
    /// <param name="bytes">Fixture byte array.</param>
    /// <param name="offset">String byte offset.</param>
    /// <param name="value">String contents.</param>
    static void WriteName(byte[] bytes, int offset, string value) {
        byte[] encoded = System.Text.Encoding.UTF8.GetBytes(value);
        encoded.CopyTo(bytes, offset);
        bytes[offset + encoded.Length] = 0;
    }
}
```
### builder.tests/Ps3DockerShaderCompilerTests.cs
```csharp
using helengine.ps3.builder;
using System.Security.Cryptography;
using System.Text.Json.Nodes;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies locked Cg toolchain configuration and Docker compiler execution.</summary>
public sealed class Ps3DockerShaderCompilerTests {
    /// <summary>A one-instruction fragment shader used for real SDK smoke compilation.</summary>
    const string FragmentSource = "#version 120\nvoid main() { gl_FragColor = vec4(1.0, 0.25, 0.5, 1.0); }\n";

    /// <summary>A fragment shader with one active uniform whose binary reference must become a relocation.</summary>
    const string ConstantFragmentSource = "#version 120\nuniform vec4 tint;\nvoid main() { gl_FragColor = gl_Color * tint; }\n";

    /// <summary>A vertex shader whose mat4 and homogeneous position require four RSX constants and a literal value.</summary>
    const string VertexMatrixSource = "#version 120\nuniform mat4 MVP;\nattribute vec3 position;\nvoid main() { gl_Position = MVP * vec4(position, 1.0) + vec4(1.0, 2.0, 3.0, 4.0); }\n";

    /// <summary>Loads a fully hashed test toolchain and exposes the expected mounted compiler identity.</summary>
    [Fact]
    public void LoadsPinnedToolchainConfiguration() {
        string root = CreateValidConfigurationRoot(out string imageId, out string compilerPath, out string mountSource, out string mountTarget, out string digest);

        Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(root);

        Assert.Equal(imageId, toolchain.ImageId);
        Assert.Equal(compilerPath, toolchain.CompilerPath);
        Assert.Equal(mountSource, toolchain.CgMountSource);
        Assert.Equal(mountTarget, toolchain.CgMountTarget);
        Assert.Contains(digest, toolchain.Identity, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Rejects a missing immutable Docker image identity from the toolchain lock.</summary>
    [Fact]
    public void RejectsEmptyLockedImageIdentity() {
        string root = CreateValidConfigurationRoot(out _, out _, out _, out _, out _);
        JsonObject lockFile = ReadTestLock(root);
        lockFile["ResolvedImage"]!["ImageId"] = " ";
        WriteTestLock(root, lockFile);

        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(root));
    }

    /// <summary>Rejects an invalid or mismatched Cg shared-library SHA-256 from the toolchain lock.</summary>
    [Fact]
    public void RejectsInvalidCgLibraryDigest() {
        string root = CreateValidConfigurationRoot(out _, out _, out _, out _, out _);
        JsonObject lockFile = ReadTestLock(root);
        lockFile["ShaderToolkit"]!["LibCgSha256"] = new string('0', 64);
        WriteTestLock(root, lockFile);

        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(root));
    }

    /// <summary>Rejects relative, temporary and out-of-tree artifact roots before creating directories.</summary>
    [Fact]
    public void RejectsUnsafeTask3ArtifactRoots() {
        string repositoryRoot = RequiredRepositoryRoot();
        string repositoryParent = Directory.GetParent(repositoryRoot)!.FullName;
        string validRoot = Path.Combine(repositoryParent, "builds", "helengine-ps3", "task-3-test");

        Assert.Equal(Path.GetFullPath(validRoot), Ps3Task3TestPaths.ValidateArtifactsRoot(validRoot, repositoryRoot));
        Assert.Throws<ArgumentException>(() => Ps3Task3TestPaths.ValidateArtifactsRoot("relative-artifacts", repositoryRoot));
        Assert.Throws<ArgumentException>(() => Ps3Task3TestPaths.ValidateArtifactsRoot(Path.Combine(repositoryParent, "other-output"), repositoryRoot));
        Assert.Throws<ArgumentException>(() => Ps3Task3TestPaths.ValidateArtifactsRoot(Path.Combine(Path.GetTempPath(), "task-3-test"), repositoryRoot));
    }

    /// <summary>Builds Docker arguments as isolated values and keeps a host path containing spaces intact.</summary>
    [Fact]
    public void BuildsDockerArgumentsWithoutShellOrPathSplitting() {
        string root = CreateValidConfigurationRoot(out string imageId, out string compilerPath, out string mountSource, out string mountTarget, out _);
        Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(root);
        string jobDirectory = Path.Combine(RequiredArtifactsRoot(), "Docker Command With Spaces", "job-1");
        Ps3DockerShaderCommand command = new(toolchain, helengine.ShaderStage.Pixel, "helengine-shader-test-1", jobDirectory);

        System.Diagnostics.ProcessStartInfo startInfo = command.CreateStartInfo();

        Assert.False(startInfo.UseShellExecute);
        Assert.Equal("docker", startInfo.FileName);
        Assert.Equal("run", startInfo.ArgumentList[0]);
        Assert.Equal("--pull=never", startInfo.ArgumentList[1]);
        Assert.Equal("type=bind,source=" + mountSource + ",target=" + mountTarget + ",readonly", startInfo.ArgumentList[8]);
        Assert.Equal("type=bind,source=" + jobDirectory + ",target=/work", startInfo.ArgumentList[10]);
        Assert.Equal(imageId, startInfo.ArgumentList[13]);
        Assert.Equal(compilerPath, startInfo.ArgumentList[14]);
        Assert.Equal("-f", startInfo.ArgumentList[15]);
        Assert.Equal("-Wcg,-oglsl", startInfo.ArgumentList[17]);
        Assert.Equal("/work/source.glsl", startInfo.ArgumentList[18]);
        Assert.Equal("/work/program.bin", startInfo.ArgumentList[19]);
    }

    /// <summary>Compiles a shader through the pinned SDK using a working path that contains spaces.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesRealFragmentShaderUnderPathWithSpaces() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Docker Jobs", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        Ps3ShaderTranslation translation = CreateTranslation(FragmentSource);

        Ps3RsxProgram program = compiler.Compile(translation, workingRoot, CancellationToken.None);

        Assert.Equal(helengine.ShaderStage.Pixel, program.Stage);
        Assert.True(program.Bytecode.Length > 36);
        Assert.Contains("!!ARBfp1.0", program.Assembly, StringComparison.Ordinal);
        Assert.NotEmpty(program.ToolchainIdentity);
    }

    /// <summary>A real SDK-produced fragment constant preserves its relocation offsets and has no fabricated register.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesConstantBearingFragmentProgram() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Fragment Constant", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));

        Ps3RsxProgram program = compiler.Compile(CreateTranslation(ConstantFragmentSource), workingRoot, CancellationToken.None);

        Ps3RsxParameter constant = Assert.Single(program.Parameters, parameter => parameter.Category == Ps3RsxParameterCategory.Constant);
        Assert.Null(constant.HardwareIndex);
        Assert.NotEmpty(Assert.Single(constant.Elements).FragmentRelocationOffsets);
    }

    /// <summary>A real SDK vertex conversion preserves matrix continuation registers and any unnamed internal constants.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesVertexMatrixAndInternalConstants() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Vertex Matrix", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));

        Ps3RsxProgram program = compiler.Compile(CreateTranslation(helengine.ShaderStage.Vertex, VertexMatrixSource), workingRoot, CancellationToken.None);

        Assert.Equal(helengine.ShaderStage.Vertex, program.Stage);
        Assert.Contains(program.Parameters, parameter => parameter.Name == "position" && parameter.Category == Ps3RsxParameterCategory.Attribute);
        Ps3RsxParameter matrix = Assert.Single(program.Parameters, parameter => parameter.Name == "MVP");
        Assert.Equal(Ps3RsxDataType.Float4x4, matrix.DataType);
        Assert.Equal(4u, matrix.ElementCount);
        Assert.Equal(new uint?[] { 0, 1, 2, 3 }, matrix.Elements.Select(element => element.VertexRegister));
        Ps3RsxParameter unnamed = Assert.Single(program.Parameters, parameter => parameter.Category == Ps3RsxParameterCategory.Constant && parameter.Name == null);
        Assert.Single(unnamed.Elements);
        Assert.Equal(Ps3RsxDataType.Float4, unnamed.DataType);
        Assert.Equal(new uint[] { 0x3F800000, 0x40000000, 0x40400000, 0x40800000 }, unnamed.Elements[0].InternalValues);
    }

    /// <summary>Rejects malformed generated GLSL and retains the failed isolated job's diagnostic files.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void RejectsMalformedSourceAndRetainsJobEvidence() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Malformed Shader", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        Ps3ShaderTranslation translation = CreateTranslation("#version 120\nthis is not GLSL\n");

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => compiler.Compile(translation, workingRoot, CancellationToken.None));

        Assert.Contains("cgcomp", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(Directory.GetDirectories(workingRoot), job => File.Exists(Path.Combine(job, "compiler.stderr.log")));
    }

    /// <summary>Two concurrent Docker jobs return their own generated programs without sharing output files.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public async Task RunsConcurrentJobsWithoutOutputCollisions() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Concurrent Jobs", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(workingRoot);
        string unrelatedOutput = Path.Combine(workingRoot, "program.bin");
        byte[] sentinel = [9, 8, 7, 6];
        File.WriteAllBytes(unrelatedOutput, sentinel);
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        Ps3ShaderTranslation firstTranslation = CreateTranslation(FragmentSource);
        Ps3ShaderTranslation secondTranslation = CreateTranslation(FragmentSource.Replace("0.25", "0.75", StringComparison.Ordinal));

        Task<Ps3RsxProgram> firstTask = Task.Run(() => compiler.Compile(firstTranslation, workingRoot, CancellationToken.None));
        Task<Ps3RsxProgram> secondTask = Task.Run(() => compiler.Compile(secondTranslation, workingRoot, CancellationToken.None));
        Ps3RsxProgram[] programs = await Task.WhenAll(firstTask, secondTask);

        Assert.All(programs, program => Assert.True(program.Bytecode.Length > 36));
        Assert.False(programs[0].Bytecode.SequenceEqual(programs[1].Bytecode));
        Assert.NotEqual(programs[0].Assembly, programs[1].Assembly);
        byte[][] jobOutputs = Directory.GetDirectories(workingRoot)
            .Select(job => File.ReadAllBytes(Path.Combine(job, "program.bin")))
            .ToArray();
        Assert.Equal(2, jobOutputs.Length);
        Assert.All(programs, program => Assert.Contains(jobOutputs, output => output.SequenceEqual(program.Bytecode)));
        Assert.Equal(sentinel, File.ReadAllBytes(unrelatedOutput));
        Assert.Equal(2, Directory.GetDirectories(workingRoot).Length);
    }

    /// <summary>Cancellation interrupts an active Docker compiler process and reports cancellation to its caller.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public async Task CancelsActiveDockerJob() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Cancelled Job", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        string expensiveSource = CreateExpensiveFragmentSource();
        using CancellationTokenSource cancellation = new();
        Task<Ps3RsxProgram> compileTask = Task.Run(() => compiler.Compile(CreateTranslation(expensiveSource), workingRoot, cancellation.Token));
        string containerName = null;
        try {
            containerName = await WaitForRunningContainerName(compileTask, workingRoot);
            cancellation.Cancel();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => compileTask);
            Assert.False(await ContainerExists(containerName));
        } finally {
            cancellation.Cancel();
            try {
                await compileTask.WaitAsync(TimeSpan.FromSeconds(15));
            } catch (OperationCanceledException) {
            } catch (TimeoutException) {
                if (!string.IsNullOrWhiteSpace(containerName)) await KillOwnedContainer(containerName);
                throw new InvalidOperationException("Cancelled compiler job did not exit and was force-stopped by its unique container name.");
            } catch (InvalidOperationException) {
            }
        }
    }

    /// <summary>Creates a temporary valid lock and a matching Cg library under the explicit artifact root.</summary>
    /// <param name="imageId">Resolved test image identity.</param>
    /// <param name="compilerPath">Locked compiler path.</param>
    /// <param name="mountSource">Resolved test Cg source mount.</param>
    /// <param name="mountTarget">Locked container mount target.</param>
    /// <param name="digest">SHA-256 digest written into the test lock.</param>
    /// <returns>The isolated repository root containing the lock and Cg library.</returns>
    static string CreateValidConfigurationRoot(out string imageId, out string compilerPath, out string mountSource, out string mountTarget, out string digest) {
        string repositoryRoot = RequiredRepositoryRoot();
        string root = Path.Combine(RequiredArtifactsRoot(), "Toolchain Config", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        JsonObject lockFile = JsonNode.Parse(File.ReadAllText(Path.Combine(repositoryRoot, "toolchain-lock.json")))!.AsObject();
        imageId = "sha256:" + new string('a', 64);
        compilerPath = "/opt/ps3dev/bin/cgcomp";
        mountSource = Path.Combine(root, "cg");
        mountTarget = "/opt/cg";
        byte[] library = [1, 7, 3, 9, 2, 8];
        string libraryPath = Path.Combine(mountSource, "lib64", "libCg.so");
        Directory.CreateDirectory(Path.GetDirectoryName(libraryPath)!);
        File.WriteAllBytes(libraryPath, library);
        digest = Convert.ToHexString(SHA256.HashData(library)).ToLowerInvariant();
        lockFile["ShaderToolkit"]!["MountSource"] = "cg";
        lockFile["ShaderToolkit"]!["MountTarget"] = mountTarget;
        lockFile["ShaderToolkit"]!["LibraryPath"] = "lib64/libCg.so";
        lockFile["ShaderToolkit"]!["LibCgSha256"] = digest;
        lockFile["ResolvedImage"]!["ImageId"] = imageId;
        lockFile["ResolvedImage"]!["CgcompPath"] = compilerPath;
        WriteTestLock(root, lockFile);
        return root;
    }

    /// <summary>Reads a test lock without changing the repository's pinned lock.</summary>
    /// <param name="root">Isolated configuration root.</param>
    /// <returns>The editable test JSON object.</returns>
    static JsonObject ReadTestLock(string root) {
        return JsonNode.Parse(File.ReadAllText(Path.Combine(root, "toolchain-lock.json")))!.AsObject();
    }

    /// <summary>Writes an isolated test lock under the build-owned artifact root.</summary>
    /// <param name="root">Isolated configuration root.</param>
    /// <param name="lockFile">Lock JSON to write.</param>
    static void WriteTestLock(string root, JsonObject lockFile) {
        File.WriteAllText(Path.Combine(root, "toolchain-lock.json"), lockFile.ToJsonString());
    }

    /// <summary>Builds the existing Task2 translation DTO for a supplied generated GLSL string.</summary>
    /// <param name="source">GLSL text sent to the pinned SDK compiler.</param>
    /// <returns>A pixel translation with no fabricated reflection resources.</returns>
    static Ps3ShaderTranslation CreateTranslation(string source) {
        return CreateTranslation(helengine.ShaderStage.Pixel, source);
    }

    /// <summary>Builds a Task2 translation for an explicit vertex or pixel source string.</summary>
    /// <param name="stage">Translation stage.</param>
    /// <param name="source">Compiler-generated GLSL for the test.</param>
    /// <returns>A translation carrying the exact stage and no fabricated reflection records.</returns>
    static Ps3ShaderTranslation CreateTranslation(helengine.ShaderStage stage, string source) {
        return new Ps3ShaderTranslation(stage, source, [], [], [], [], [], [], []);
    }

    /// <summary>Creates a large unrolled fragment expression to keep a real Docker job active for cancellation.</summary>
    /// <returns>Valid generated GLSL with substantial compile work.</returns>
    static string CreateExpensiveFragmentSource() {
        System.Text.StringBuilder source = new("#version 120\nvoid main() {\nfloat x = 0.25;\n");
        for (int index = 0; index < 12000; index++) source.Append("x = x * 1.00001 + 0.00001;\n");
        source.Append("gl_FragColor = vec4(x);\n}\n");
        return source.ToString();
    }

    /// <summary>Waits until this compiler job's named container is running, proving cancellation targets active work.</summary>
    /// <param name="compileTask">Active compiler task.</param>
    /// <param name="workingRoot">Compiler working root containing its unique child job directory.</param>
    /// <returns>The exact running container name owned by the compiler job.</returns>
    static async Task<string> WaitForRunningContainerName(Task<Ps3RsxProgram> compileTask, string workingRoot) {
        DateTime deadline = DateTime.UtcNow.AddSeconds(45);
        while (DateTime.UtcNow < deadline && !compileTask.IsCompleted) {
            foreach (string job in Directory.Exists(workingRoot) ? Directory.GetDirectories(workingRoot) : []) {
                string namePath = Path.Combine(job, "container-name.txt");
                if (!File.Exists(namePath)) continue;
                string name = File.ReadAllText(namePath).Trim();
                if (name.Length > 0 && await ContainerIsRunning(name)) return name;
            }
            await Task.Delay(100);
        }
        throw new TimeoutException("The real Docker compiler job did not become active before its compile task ended or timed out.");
    }

    /// <summary>Checks whether a Docker container with the exact task-owned identity exists.</summary>
    /// <param name="containerName">Compiler-generated unique container name.</param>
    /// <returns>True when Docker inspect succeeds.</returns>
    static async Task<bool> ContainerExists(string containerName) {
        using System.Diagnostics.Process process = StartDockerInspect(containerName, false);
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string output = await standardOutput;
        _ = await standardError;
        return process.ExitCode == 0 && output.Trim().Length > 0;
    }

    /// <summary>Checks whether a Docker container with the exact task-owned identity is running.</summary>
    /// <param name="containerName">Compiler-generated unique container name.</param>
    /// <returns>True only when the inspected container state is running.</returns>
    static async Task<bool> ContainerIsRunning(string containerName) {
        using System.Diagnostics.Process process = StartDockerInspect(containerName, true);
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string result = await standardOutput;
        _ = await standardError;
        return process.ExitCode == 0 && string.Equals(result.Trim(), "true", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Starts a Docker inspect command for the exact task-owned container without invoking a shell.</summary>
    /// <param name="containerName">Compiler-generated unique container name.</param>
    /// <param name="includeState">Whether to request the running-state value.</param>
    /// <returns>The started Docker CLI process.</returns>
    static System.Diagnostics.Process StartDockerInspect(string containerName, bool includeState) {
        System.Diagnostics.ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("inspect");
        startInfo.ArgumentList.Add("--format");
        startInfo.ArgumentList.Add(includeState ? "{{.State.Running}}" : "{{.Id}}");
        startInfo.ArgumentList.Add(containerName);
        System.Diagnostics.Process process = new() { StartInfo = startInfo };
        if (!process.Start()) throw new InvalidOperationException("Failed to start Docker inspect for the owned compiler container.");
        return process;
    }

    /// <summary>Force-stops only the compiler container named by this test's own job marker.</summary>
    /// <param name="containerName">Unique container name written by the compiler job.</param>
    static async Task KillOwnedContainer(string containerName) {
        System.Diagnostics.ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("kill");
        startInfo.ArgumentList.Add(containerName);
        using System.Diagnostics.Process process = new() { StartInfo = startInfo };
        if (!process.Start()) throw new InvalidOperationException("Failed to start Docker kill for the unique test container.");
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        _ = await standardOutput;
        _ = await standardError;
    }

    /// <summary>Returns the explicitly configured repository root required by integration tests.</summary>
    /// <returns>Repository root containing toolchain-lock.json.</returns>
    static string RequiredRepositoryRoot() {
        string root = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(root), "Set HELENGINE_PS3_REPOSITORY_ROOT to the checkout root for Task3 tests.");
        return root!;
    }

    /// <summary>Returns the explicit artifact root for all Task3 test files and logs.</summary>
    /// <returns>Build-owned artifact directory.</returns>
    static string RequiredArtifactsRoot() {
        string root = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(root), "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to the Task3 artifact root.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(root!, RequiredRepositoryRoot());
        Directory.CreateDirectory(validatedRoot);
        return validatedRoot;
    }
}
```
### builder.tests/Ps3Task3TestPaths.cs
```csharp
namespace helengine.ps3.builder.tests;

/// <summary>Validates that Task3 test artifacts stay in the designated sibling build tree.</summary>
internal static class Ps3Task3TestPaths {
    /// <summary>Resolves a Task3 artifact root only when it is absolute, build-owned and outside system temporary paths.</summary>
    /// <param name="candidate">Explicit artifact-root path supplied to the test process.</param>
    /// <param name="repositoryRoot">Repository checkout root used to locate its sibling build directory.</param>
    /// <returns>The normalized artifact-root path.</returns>
    internal static string ValidateArtifactsRoot(string candidate, string repositoryRoot) {
        if (string.IsNullOrWhiteSpace(candidate)) throw new ArgumentException("An explicit Task3 artifact root is required.", nameof(candidate));
        if (!Path.IsPathRooted(candidate)) throw new ArgumentException("Task3 artifact root must be an absolute path.", nameof(candidate));
        if (string.IsNullOrWhiteSpace(repositoryRoot) || !Path.IsPathRooted(repositoryRoot)) throw new ArgumentException("Repository root must be absolute.", nameof(repositoryRoot));

        string fullRoot = Path.GetFullPath(candidate);
        string fullRepositoryRoot = Path.GetFullPath(repositoryRoot);
        DirectoryInfo repositoryParent = Directory.GetParent(fullRepositoryRoot)
            ?? throw new ArgumentException("Repository root must have a parent directory.", nameof(repositoryRoot));
        string expectedBuildRoot = Path.GetFullPath(Path.Combine(repositoryParent.FullName, "builds", "helengine-ps3"));
        if (!IsStrictDescendant(fullRoot, expectedBuildRoot)) {
            throw new ArgumentException("Task3 artifacts must be below the checkout's sibling builds/helengine-ps3 directory.", nameof(candidate));
        }

        string systemTemporaryRoot = Path.GetFullPath(Path.GetTempPath());
        if (IsSameOrDescendant(fullRoot, systemTemporaryRoot)) {
            throw new ArgumentException("Task3 artifacts cannot be written under a system temporary directory.", nameof(candidate));
        }
        return fullRoot;
    }

    /// <summary>Determines whether a normalized path is strictly below a normalized parent path.</summary>
    /// <param name="path">Candidate child path.</param>
    /// <param name="parent">Expected parent path.</param>
    /// <returns>True only when the candidate is a strict descendant.</returns>
    static bool IsStrictDescendant(string path, string parent) {
        if (string.Equals(path, parent, StringComparison.OrdinalIgnoreCase)) return false;
        return IsSameOrDescendant(path, parent);
    }

    /// <summary>Determines whether a normalized path is equal to or below another normalized path.</summary>
    /// <param name="path">Candidate path.</param>
    /// <param name="parent">Potential parent path.</param>
    /// <returns>True when the candidate resolves within the parent directory.</returns>
    static bool IsSameOrDescendant(string path, string parent) {
        string relative = Path.GetRelativePath(parent, path);
        return !Path.IsPathRooted(relative)
            && relative != ".."
            && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            && !relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal);
    }
}
```
