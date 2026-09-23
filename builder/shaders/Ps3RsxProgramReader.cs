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
        uint activeVariableCount = 0;
        HashSet<string> matchedNames = new(StringComparer.Ordinal);
        IReadOnlyList<Ps3CgDumpVariable> variables = Ps3CgDumpReader.Read(stage, assembly);
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive) continue;
            activeVariableCount++;
            if (variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) continue;
            Ps3RsxParameter parameter = FindNamedParameter(parameters, variable.Name);
            if (parameter == null) throw new InvalidDataException($"Active cgcomp parameter '{variable.Name}' is missing from the SDK parameter tables.");
            if (!matchedNames.Add(variable.Name)) throw new InvalidDataException($"cgcomp dump repeats active parameter '{variable.Name}'.");
            Ps3RsxDataType dumpType = Ps3CgDumpReader.ParseDataType(variable.TypeToken);
            if (parameter.DataType != dumpType) throw new InvalidDataException($"cgcomp parameter '{variable.Name}' type '{variable.TypeToken}' disagrees with SDK type '{parameter.DataType}'.");
            ValidateDumpBinding(stage, parameter, variable.SemanticBinding, variable.HardwareBinding);
        }

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
