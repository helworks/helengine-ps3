# Task 4 full uncommitted source review package

Base HEAD ef27238ed85fe3218618a752c11d9eeede0a75a2; no commits. New Task4 files are untracked, so full source is the added-file diff. Reader baseline from accepted Task3 is included first for the shared-decoder extraction comparison. Requirements: task-4-brief.md; SDK evidence: sdk-format-evidence.md. Final logs: task4-final-pure.log46/46, task4-final-docker.log7/7. Full report: task-4-report.md.

## Accepted Task3 reader baseline

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


## Task4 current sources

### builder/shaders/Ps3CgDumpReader.cs
```csharp
using helengine;
using System.Globalization;

namespace helengine.ps3.builder;

/// <summary>Decodes shared Cg profile and variable declarations without interpreting RSX binary records.</summary>
internal static class Ps3CgDumpReader {
    /// <summary>Maps one exact Cg declaration type token to the pinned SDK parameter type.</summary>
    /// <param name="typeToken">Cg type token from a #var declaration.</param>
    /// <returns>The matching SDK type code, or Unknown when the token is outside the pinned subset.</returns>
    public static Ps3RsxDataType ParseDataType(string typeToken) {
        return typeToken.ToLowerInvariant() switch {
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

    /// <summary>Reads every variable declaration after confirming the dump profile matches the requested stage.</summary>
    /// <param name="stage">Expected vertex or pixel stage.</param>
    /// <param name="assembly">Unmodified cgcomp dump text.</param>
    /// <returns>All parsed declarations in dump order, including inactive declarations.</returns>
    public static IReadOnlyList<Ps3CgDumpVariable> Read(ShaderStage stage, string assembly) {
        if (stage != ShaderStage.Vertex && stage != ShaderStage.Pixel) throw new NotSupportedException($"Cg dump stage '{stage}' is unsupported.");
        if (string.IsNullOrWhiteSpace(assembly)) throw new InvalidDataException("cgcomp dump text is required to read declarations.");

        string expectedProfile = stage == ShaderStage.Vertex ? "vp40" : "fp40";
        bool foundProfile = false;
        List<Ps3CgDumpVariable> variables = new();
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
            string typeToken = declaration[..nameStart].Trim();
            string name = declaration[(nameStart + 1)..].Trim();
            if (typeToken.Length == 0 || name.Length == 0) throw new InvalidDataException($"Malformed cgcomp #var declaration: {trimmed}");
            if (!int.TryParse(fields[3].Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out int compilerParameterIndex)) {
                throw new InvalidDataException($"cgcomp #var declaration has an invalid compiler parameter index: {trimmed}");
            }
            if (!int.TryParse(fields[4].Trim(), NumberStyles.None, CultureInfo.InvariantCulture, out int activeFlag) || (activeFlag != 0 && activeFlag != 1)) {
                throw new InvalidDataException($"cgcomp #var declaration has an invalid activity flag: {trimmed}");
            }
            variables.Add(new Ps3CgDumpVariable(
                typeToken,
                name,
                fields[1].Trim(),
                fields[2].Trim(),
                compilerParameterIndex,
                activeFlag == 1));
        }

        if (!foundProfile) throw new InvalidDataException("cgcomp dump has no stage profile declaration.");
        return Array.AsReadOnly(variables.ToArray());
    }
}

```

### builder/shaders/Ps3CgDumpVariable.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Preserves one exact Cg dump variable declaration, including inactive optimized declarations.</summary>
internal sealed class Ps3CgDumpVariable {
    /// <summary>Creates a parsed Cg variable declaration.</summary>
    /// <param name="typeToken">Exact Cg type token.</param>
    /// <param name="name">Exact compiler parameter name.</param>
    /// <param name="semanticBinding">Primary Cg binding, such as $vin.ATTR0 or $vout.TEX0.</param>
    /// <param name="hardwareBinding">Secondary Cg binding, such as ATTR0, c[0], 4, or texunit 0.</param>
    /// <param name="compilerParameterIndex">Cg compiler parameter index from the dump.</param>
    /// <param name="isActive">Whether the compiler marks the declaration active.</param>
    public Ps3CgDumpVariable(string typeToken, string name, string semanticBinding, string hardwareBinding, int compilerParameterIndex, bool isActive) {
        TypeToken = typeToken;
        Name = name;
        SemanticBinding = semanticBinding;
        HardwareBinding = hardwareBinding;
        CompilerParameterIndex = compilerParameterIndex;
        IsActive = isActive;
    }

    /// <summary>Gets the exact compiler-reported Cg type spelling.</summary>
    public string TypeToken { get; }

    /// <summary>Gets the exact generated or built-in parameter name.</summary>
    public string Name { get; }

    /// <summary>Gets the primary semantic or resource binding field.</summary>
    public string SemanticBinding { get; }

    /// <summary>Gets the secondary hardware or constant-register binding field.</summary>
    public string HardwareBinding { get; }

    /// <summary>Gets the signed compiler parameter index stored in the Cg dump.</summary>
    public int CompilerParameterIndex { get; }

    /// <summary>Gets whether this declaration is active in the final Cg program.</summary>
    public bool IsActive { get; }
}

```

### builder/shaders/Ps3ShaderAttributeBinding.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Associates one authored vertex input with its final SDK attribute parameter and slot.</summary>
public sealed class Ps3ShaderAttributeBinding {
    /// <summary>Creates a mapping for one reflected vertex input and its actual SDK attribute record.</summary>
    /// <param name="sourceVariable">Original authored input reflection record.</param>
    /// <param name="rsxParameter">Validated SDK attribute parameter.</param>
    public Ps3ShaderAttributeBinding(Ps3ShaderInterfaceVariable sourceVariable, Ps3RsxParameter rsxParameter) {
        SourceVariable = sourceVariable ?? throw new ArgumentNullException(nameof(sourceVariable));
        RsxParameter = rsxParameter ?? throw new ArgumentNullException(nameof(rsxParameter));
        if (rsxParameter.Category != Ps3RsxParameterCategory.Attribute || !rsxParameter.HardwareIndex.HasValue) {
            throw new ArgumentException("Vertex attribute mapping requires an SDK attribute parameter with a hardware slot.", nameof(rsxParameter));
        }
        AuthoredSemantic = sourceVariable.Semantic;
        GeneratedName = sourceVariable.GeneratedName;
        HardwareSlot = rsxParameter.HardwareIndex.Value;
    }

    /// <summary>Gets the original authored input reflection record.</summary>
    public Ps3ShaderInterfaceVariable SourceVariable { get; }

    /// <summary>Gets the exact validated SDK attribute parameter.</summary>
    public Ps3RsxParameter RsxParameter { get; }

    /// <summary>Gets the authored HLSL input semantic.</summary>
    public string AuthoredSemantic { get; }

    /// <summary>Gets the exact compiler-generated Cg parameter name.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the raw SDK vertex attribute index.</summary>
    public uint HardwareSlot { get; }
}

```

### builder/shaders/Ps3ShaderBindingMap.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Contains validated RSX mappings for authored resources and inter-stage interfaces.</summary>
public sealed class Ps3ShaderBindingMap {
    /// <summary>Creates immutable snapshots of all validated source-to-RSX mappings.</summary>
    /// <param name="uniforms">Active authored uniform members with actual SDK constant records.</param>
    /// <param name="attributes">Authored vertex attributes with actual SDK attribute records.</param>
    /// <param name="samplers">Combined authored texture/sampler identities with actual SDK sampler records.</param>
    /// <param name="stageLinks">Validated active vertex-output to pixel-input links.</param>
    internal Ps3ShaderBindingMap(IEnumerable<Ps3ShaderUniformBinding> uniforms, IEnumerable<Ps3ShaderAttributeBinding> attributes,
        IEnumerable<Ps3ShaderSamplerMapEntry> samplers, IEnumerable<Ps3ShaderStageLink> stageLinks) {
        ArgumentNullException.ThrowIfNull(uniforms);
        ArgumentNullException.ThrowIfNull(attributes);
        ArgumentNullException.ThrowIfNull(samplers);
        ArgumentNullException.ThrowIfNull(stageLinks);
        Uniforms = Array.AsReadOnly(uniforms.ToArray());
        Attributes = Array.AsReadOnly(attributes.ToArray());
        Samplers = Array.AsReadOnly(samplers.ToArray());
        StageLinks = Array.AsReadOnly(stageLinks.ToArray());
    }

    /// <summary>Gets active authored uniform members mapped to their final SDK constant parameters.</summary>
    public IReadOnlyList<Ps3ShaderUniformBinding> Uniforms { get; }

    /// <summary>Gets authored vertex inputs mapped to their final SDK attribute parameters.</summary>
    public IReadOnlyList<Ps3ShaderAttributeBinding> Attributes { get; }

    /// <summary>Gets combined texture/sampler identities mapped to their final SDK sampler parameters.</summary>
    public IReadOnlyList<Ps3ShaderSamplerMapEntry> Samplers { get; }

    /// <summary>Gets active authored pixel inputs and their matching vertex-output channels.</summary>
    public IReadOnlyList<Ps3ShaderStageLink> StageLinks { get; }
}

```

### builder/shaders/Ps3ShaderBindingMapBuilder.cs
```csharp
using helengine;
using System.Globalization;

namespace helengine.ps3.builder;

/// <summary>Reconciles reflected authored stage interfaces with Cg's final RSX routing declarations.</summary>
public sealed class Ps3ShaderBindingMapBuilder {
    /// <summary>Builds validated inter-stage links from source reflection and the final stage dumps.</summary>
    /// <param name="vertexSource">Reflected vertex-stage HLSL translation.</param>
    /// <param name="vertexProgram">Validated vertex-stage SDK program and Cg dump.</param>
    /// <param name="pixelSource">Reflected pixel-stage HLSL translation.</param>
    /// <param name="pixelProgram">Validated pixel-stage SDK program and Cg dump.</param>
    /// <returns>A map with every active pixel input linked to a compatible vertex output.</returns>
    public Ps3ShaderBindingMap Build(Ps3ShaderTranslation vertexSource, Ps3RsxProgram vertexProgram,
        Ps3ShaderTranslation pixelSource, Ps3RsxProgram pixelProgram) {
        ArgumentNullException.ThrowIfNull(vertexSource);
        ArgumentNullException.ThrowIfNull(vertexProgram);
        ArgumentNullException.ThrowIfNull(pixelSource);
        ArgumentNullException.ThrowIfNull(pixelProgram);
        if (vertexSource.Stage != ShaderStage.Vertex || vertexProgram.Stage != ShaderStage.Vertex) {
            throw new InvalidOperationException("The first source translation and RSX program must both be vertex stages.");
        }
        if (pixelSource.Stage != ShaderStage.Pixel || pixelProgram.Stage != ShaderStage.Pixel) {
            throw new InvalidOperationException("The second source translation and RSX program must both be pixel stages.");
        }

        IReadOnlyList<Ps3CgDumpVariable> vertexVariables = Ps3CgDumpReader.Read(ShaderStage.Vertex, vertexProgram.Assembly);
        IReadOnlyList<Ps3CgDumpVariable> pixelVariables = Ps3CgDumpReader.Read(ShaderStage.Pixel, pixelProgram.Assembly);
        ValidateActiveDeclarations(ShaderStage.Vertex, vertexVariables);
        ValidateActiveDeclarations(ShaderStage.Pixel, pixelVariables);
        ValidateVertexPosition(vertexSource, vertexVariables);
        List<Ps3ShaderAttributeBinding> attributes = BuildAttributes(vertexSource, vertexProgram, vertexVariables);
        List<Ps3ShaderUniformBinding> uniforms = BuildUniforms(vertexSource, vertexProgram, vertexVariables);
        uniforms.AddRange(BuildUniforms(pixelSource, pixelProgram, pixelVariables));
        List<Ps3ShaderSamplerMapEntry> samplers = BuildSamplers(vertexSource, vertexProgram, vertexVariables);
        samplers.AddRange(BuildSamplers(pixelSource, pixelProgram, pixelVariables));
        Dictionary<string, Ps3CgDumpVariable> vertexOutputs = ReadVertexOutputs(vertexSource, vertexVariables);
        Dictionary<string, Ps3CgDumpVariable> pixelInputs = ReadPixelInputs(pixelSource, pixelVariables);
        List<Ps3ShaderStageLink> links = new();
        HashSet<string> linkedVertexChannels = new(StringComparer.Ordinal);
        HashSet<string> linkedPixelChannels = new(StringComparer.Ordinal);

        foreach (Ps3ShaderInterfaceVariable pixelInput in pixelSource.Inputs) {
            if (!pixelInputs.TryGetValue(pixelInput.Semantic, out Ps3CgDumpVariable pixelVariable)) continue;
            if (!vertexOutputs.TryGetValue(pixelInput.Semantic, out Ps3CgDumpVariable vertexVariable)) {
                throw new InvalidOperationException($"Pixel input semantic '{pixelInput.Semantic}' has no active vertex-output producer.");
            }
            Ps3ShaderInterfaceVariable vertexOutput = FindInterfaceByGeneratedName(vertexSource.Outputs, vertexVariable.Name);
            VerifyInterfaceShape(pixelInput, vertexOutput);
            string vertexChannel = GetVertexUserChannel(vertexVariable);
            string pixelChannel = GetPixelUserChannel(pixelInputs[pixelInput.Semantic]);
            if (!string.Equals(vertexChannel, pixelChannel, StringComparison.Ordinal)) {
                throw new InvalidOperationException($"Semantic '{pixelInput.Semantic}' routes to vertex channel '{vertexChannel}' and pixel channel '{pixelChannel}'.");
            }
            if (!linkedVertexChannels.Add(vertexChannel) || !linkedPixelChannels.Add(pixelChannel)) {
                throw new InvalidOperationException($"Active stage links overlap RSX channel '{vertexChannel}'.");
            }
            links.Add(new Ps3ShaderStageLink(
                pixelInput.Semantic,
                pixelInput.ScalarType,
                pixelInput.VectorWidth,
                pixelInput.Columns,
                pixelInput.ArrayLength,
                pixelInput.Interpolation,
                vertexChannel,
                pixelChannel,
                GetVertexSlot(vertexChannel),
                GetPixelSlot(pixelChannel)));
        }

        ValidateNonOverlappingBindings(uniforms, attributes, samplers);
        return new Ps3ShaderBindingMap(uniforms, attributes, samplers, links);
    }

    /// <summary>Rejects active declarations that are outside the documented Cg and RSX binding subset.</summary>
    /// <param name="stage">Stage owning the declarations.</param>
    /// <param name="variables">Parsed stage declarations.</param>
    static void ValidateActiveDeclarations(ShaderStage stage, IReadOnlyList<Ps3CgDumpVariable> variables) {
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive) continue;
            if (Ps3CgDumpReader.ParseDataType(variable.TypeToken) == Ps3RsxDataType.Unknown) {
                throw new InvalidOperationException($"Active Cg parameter '{variable.Name}' uses unsupported type '{variable.TypeToken}'.");
            }
            if (variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) {
                if (stage == ShaderStage.Vertex) {
                    _ = GetVertexUserChannel(variable);
                } else {
                    ValidatePixelOutput(variable);
                }
                continue;
            }
            if (variable.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) {
                if (stage == ShaderStage.Vertex) {
                    _ = ParseVertexAttributeSlot(variable);
                } else {
                    _ = GetPixelUserChannel(variable);
                }
                continue;
            }
            if (variable.SemanticBinding.Length == 0 && variable.HardwareBinding.StartsWith("c[", StringComparison.Ordinal)) continue;
            if (variable.SemanticBinding.Length == 0 && variable.HardwareBinding.StartsWith("texunit ", StringComparison.Ordinal)) {
                _ = ParseTextureUnit(variable);
                continue;
            }
            throw new InvalidOperationException($"Active Cg parameter '{variable.Name}' uses unsupported binding '{variable.SemanticBinding}' / '{variable.HardwareBinding}'.");
        }
    }

    /// <summary>Maps every active reflected vertex input to its exact SDK attribute record.</summary>
    /// <param name="source">Reflected vertex translation.</param>
    /// <param name="program">Validated vertex RSX program.</param>
    /// <param name="variables">Parsed vertex Cg declarations.</param>
    /// <returns>Authored input mappings in source-reflection order.</returns>
    static List<Ps3ShaderAttributeBinding> BuildAttributes(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
        List<Ps3ShaderAttributeBinding> result = new();
        foreach (Ps3ShaderInterfaceVariable variable in source.Inputs) {
            Ps3CgDumpVariable declaration = FindActiveVariable(variables, variable.GeneratedName, "vertex attribute");
            uint slot = ParseVertexAttributeSlot(declaration);
            Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Attribute);
            if (parameter.HardwareIndex != slot) throw new InvalidOperationException($"Vertex input '{variable.Semantic}' Cg slot {slot} disagrees with SDK attribute index {parameter.HardwareIndex}.");
            ValidateParameterType(variable.ScalarType, variable.VectorWidth, variable.Columns, parameter, declaration);
            result.Add(new Ps3ShaderAttributeBinding(variable, parameter));
        }
        foreach (Ps3CgDumpVariable declaration in variables) {
            if (!declaration.IsActive || !declaration.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) continue;
            if (FindInterfaceByGeneratedName(source.Inputs, declaration.Name) == null) {
                throw new InvalidOperationException($"Active vertex input '{declaration.Name}' has no authored source reflection record.");
            }
        }
        return result;
    }

    /// <summary>Maps active Cg constants to their source uniform members without synthesizing optimized-out members.</summary>
    /// <param name="source">Reflected source translation for the stage.</param>
    /// <param name="program">Validated RSX program for the stage.</param>
    /// <param name="variables">Parsed stage Cg declarations.</param>
    /// <returns>Active authored member mappings in dump order.</returns>
    static List<Ps3ShaderUniformBinding> BuildUniforms(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
        List<Ps3ShaderUniformBinding> result = new();
        HashSet<string> mappedNames = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable declaration in variables) {
            if (!declaration.IsActive || declaration.SemanticBinding.Length != 0 || !declaration.HardwareBinding.StartsWith("c[", StringComparison.Ordinal)) continue;
            Ps3ShaderUniformMember member = FindUniformByGeneratedName(source.UniformMembers, declaration.Name);
            if (!mappedNames.Add(declaration.Name)) throw new InvalidOperationException($"Active uniform parameter '{declaration.Name}' is declared more than once.");
            Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Constant);
            ValidateParameterType(member.ScalarType, member.VectorWidth, member.Columns, parameter, declaration);
            uint expectedElementCount;
            try {
                expectedElementCount = checked(member.Columns * member.ArrayLength);
            } catch (OverflowException exception) {
                throw new InvalidOperationException($"Uniform '{member.MemberPath}' has a physical element count that overflows the RSX range.", exception);
            }
            if (expectedElementCount == 0 || parameter.ElementCount != expectedElementCount) {
                throw new InvalidOperationException($"Uniform '{member.MemberPath}' requires {expectedElementCount} RSX vectors but parameter '{declaration.Name}' contains {parameter.ElementCount}.");
            }
            result.Add(new Ps3ShaderUniformBinding(source.Stage, member, parameter));
        }
        return result;
    }

    /// <summary>Maps every active combined sampler identity to its distinct SDK texture-unit parameter.</summary>
    /// <param name="source">Reflected source translation for the stage.</param>
    /// <param name="program">Validated RSX program for the stage.</param>
    /// <param name="variables">Parsed stage Cg declarations.</param>
    /// <returns>Active combined-sampler mappings in dump order.</returns>
    static List<Ps3ShaderSamplerMapEntry> BuildSamplers(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
        List<Ps3ShaderSamplerMapEntry> result = new();
        HashSet<string> mappedNames = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable declaration in variables) {
            if (!declaration.IsActive || declaration.SemanticBinding.Length != 0 || !declaration.HardwareBinding.StartsWith("texunit ", StringComparison.Ordinal)) continue;
            Ps3ShaderSamplerBinding sourceSampler = FindSamplerByGeneratedName(source.Samplers, declaration.Name);
            if (!mappedNames.Add(declaration.Name)) throw new InvalidOperationException($"Active sampler parameter '{declaration.Name}' is declared more than once.");
            Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Sampler);
            Ps3RsxDataType expectedType = Ps3CgDumpReader.ParseDataType(GetSamplerTypeToken(sourceSampler.Dimension));
            if (expectedType == Ps3RsxDataType.Unknown || declaration.TypeToken != GetSamplerTypeToken(sourceSampler.Dimension)
                || parameter.DataType != expectedType || Ps3CgDumpReader.ParseDataType(declaration.TypeToken) != parameter.DataType) {
                throw new InvalidOperationException($"Sampler '{sourceSampler.TextureName}/{sourceSampler.SamplerName}' disagrees with Cg or SDK sampler type '{declaration.TypeToken}'.");
            }
            uint textureUnit = ParseTextureUnit(declaration);
            if (parameter.HardwareIndex != textureUnit) throw new InvalidOperationException($"Sampler '{declaration.Name}' Cg texture unit {textureUnit} disagrees with SDK unit {parameter.HardwareIndex}.");
            result.Add(new Ps3ShaderSamplerMapEntry(sourceSampler, parameter));
        }
        foreach (Ps3ShaderSamplerBinding sourceSampler in source.Samplers) {
            if (!mappedNames.Contains(sourceSampler.GeneratedName)) {
                throw new InvalidOperationException($"Active authored sampler '{sourceSampler.TextureName}/{sourceSampler.SamplerName}' has no active RSX sampler parameter.");
            }
        }
        return result;
    }

    /// <summary>Rejects duplicate active hardware slots and overlapping constant registers or relocation words.</summary>
    /// <param name="uniforms">Mapped active uniform parameters from both stages.</param>
    /// <param name="attributes">Mapped vertex input parameters.</param>
    /// <param name="samplers">Mapped combined sampler parameters.</param>
    static void ValidateNonOverlappingBindings(IReadOnlyList<Ps3ShaderUniformBinding> uniforms,
        IReadOnlyList<Ps3ShaderAttributeBinding> attributes, IReadOnlyList<Ps3ShaderSamplerMapEntry> samplers) {
        HashSet<uint> attributeSlots = new();
        foreach (Ps3ShaderAttributeBinding attribute in attributes) {
            if (!attributeSlots.Add(attribute.HardwareSlot)) throw new InvalidOperationException($"Multiple authored vertex inputs use SDK attribute slot {attribute.HardwareSlot}.");
        }
        HashSet<uint> textureUnits = new();
        foreach (Ps3ShaderSamplerMapEntry sampler in samplers) {
            if (!textureUnits.Add(sampler.TextureUnit)) throw new InvalidOperationException($"Multiple authored sampler identities use RSX texture unit {sampler.TextureUnit}.");
        }
        HashSet<string> constantReferences = new(StringComparer.Ordinal);
        foreach (Ps3ShaderUniformBinding uniform in uniforms) {
            foreach (Ps3RsxConstantElement element in uniform.RsxParameter.Elements) {
                if (uniform.Stage == ShaderStage.Vertex) {
                    if (!element.VertexRegister.HasValue) throw new InvalidOperationException($"Vertex uniform '{uniform.AuthoredMemberPath}' has no SDK constant register.");
                    string reference = "v:" + element.VertexRegister.Value.ToString(CultureInfo.InvariantCulture);
                    if (!constantReferences.Add(reference)) throw new InvalidOperationException($"Active vertex uniforms overlap SDK constant register {element.VertexRegister.Value}.");
                } else {
                    foreach (uint offset in element.FragmentRelocationOffsets) {
                        string reference = "f:" + offset.ToString(CultureInfo.InvariantCulture);
                        if (!constantReferences.Add(reference)) throw new InvalidOperationException($"Active pixel uniforms overlap fragment relocation offset {offset}.");
                    }
                }
            }
        }
    }

    /// <summary>Finds one active declaration by exact generated name and rejects missing or duplicate active records.</summary>
    /// <param name="variables">Parsed declarations from one stage.</param>
    /// <param name="name">Exact compiler-generated identity.</param>
    /// <param name="role">Resource role for diagnostics.</param>
    /// <returns>The unique active declaration.</returns>
    static Ps3CgDumpVariable FindActiveVariable(IReadOnlyList<Ps3CgDumpVariable> variables, string name, string role) {
        Ps3CgDumpVariable found = null;
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive || !string.Equals(variable.Name, name, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Active {role} '{name}' has more than one Cg declaration.");
            found = variable;
        }
        return found ?? throw new InvalidOperationException($"Active {role} '{name}' has no Cg declaration.");
    }

    /// <summary>Finds one uniquely named SDK parameter and verifies its expected resource category.</summary>
    /// <param name="program">Validated stage program.</param>
    /// <param name="name">Exact Cg parameter name.</param>
    /// <param name="category">Required SDK parameter category.</param>
    /// <returns>The matching parameter.</returns>
    static Ps3RsxParameter FindParameter(Ps3RsxProgram program, string name, Ps3RsxParameterCategory category) {
        Ps3RsxParameter found = null;
        foreach (Ps3RsxParameter parameter in program.Parameters) {
            if (!string.Equals(parameter.Name, name, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"SDK parameter '{name}' appears more than once.");
            found = parameter;
        }
        if (found == null) throw new InvalidOperationException($"Active Cg parameter '{name}' is absent from the SDK parameter tables.");
        if (found.Category != category) throw new InvalidOperationException($"Cg parameter '{name}' requires SDK category '{category}' but found '{found.Category}'.");
        return found;
    }

    /// <summary>Finds one uniform reflection record by its unique compiler-generated full member name.</summary>
    /// <param name="members">Source-reflected uniform members.</param>
    /// <param name="generatedName">Exact Cg parameter name.</param>
    /// <returns>The corresponding authored member.</returns>
    static Ps3ShaderUniformMember FindUniformByGeneratedName(IEnumerable<Ps3ShaderUniformMember> members, string generatedName) {
        Ps3ShaderUniformMember found = null;
        foreach (Ps3ShaderUniformMember member in members) {
            if (!string.Equals(member.GeneratedName, generatedName, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Generated uniform name '{generatedName}' is ambiguous.");
            found = member;
        }
        return found ?? throw new InvalidOperationException($"Active Cg constant '{generatedName}' has no authored uniform reflection record.");
    }

    /// <summary>Finds one combined sampler reflection record by its compiler-generated pair identity.</summary>
    /// <param name="samplers">Source-reflected combined samplers.</param>
    /// <param name="generatedName">Exact Cg parameter name.</param>
    /// <returns>The corresponding authored texture/sampler pair.</returns>
    static Ps3ShaderSamplerBinding FindSamplerByGeneratedName(IEnumerable<Ps3ShaderSamplerBinding> samplers, string generatedName) {
        Ps3ShaderSamplerBinding found = null;
        foreach (Ps3ShaderSamplerBinding sampler in samplers) {
            if (!string.Equals(sampler.GeneratedName, generatedName, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Generated sampler name '{generatedName}' is ambiguous.");
            found = sampler;
        }
        return found ?? throw new InvalidOperationException($"Active Cg sampler '{generatedName}' has no authored combined sampler reflection record.");
    }

    /// <summary>Compares Cg and SDK types against the explicit SPIR-V scalar type and physical matrix shape.</summary>
    /// <param name="scalarType">SPIRV-Cross scalar type text.</param>
    /// <param name="vectorWidth">Physical vector width.</param>
    /// <param name="columns">Physical matrix column count.</param>
    /// <param name="parameter">Final SDK parameter.</param>
    /// <param name="declaration">Final Cg declaration.</param>
    static void ValidateParameterType(string scalarType, uint vectorWidth, uint columns, Ps3RsxParameter parameter, Ps3CgDumpVariable declaration) {
        string expectedToken = GetFloatTypeToken(scalarType, vectorWidth, columns);
        Ps3RsxDataType expectedType = Ps3CgDumpReader.ParseDataType(expectedToken);
        Ps3RsxDataType dumpType = Ps3CgDumpReader.ParseDataType(declaration.TypeToken);
        if (expectedType == Ps3RsxDataType.Unknown || dumpType != expectedType || parameter.DataType != expectedType) {
            throw new InvalidOperationException($"Source metadata, Cg declaration '{declaration.TypeToken}', and SDK parameter '{declaration.Name}' do not agree on type '{expectedToken}'.");
        }
    }

    /// <summary>Translates supported FP3232 source reflection and physical shape into its exact Cg type token.</summary>
    /// <param name="scalarType">SPIRV-Cross scalar base type and bit width.</param>
    /// <param name="vectorWidth">Physical vector width.</param>
    /// <param name="columns">Physical matrix column count.</param>
    /// <returns>The Cg type token expected for this scalar and shape.</returns>
    static string GetFloatTypeToken(string scalarType, uint vectorWidth, uint columns) {
        if (!string.Equals(scalarType, "FP3232", StringComparison.Ordinal)) {
            throw new NotSupportedException($"Active PS3 source scalar type '{scalarType}' has no documented Cg/RSX mapping.");
        }
        if (vectorWidth == 0 || vectorWidth > 4 || columns == 0 || columns > 4) {
            throw new NotSupportedException($"Active PS3 source shape {columns} columns by vector width {vectorWidth} is outside the supported RSX range.");
        }
        if (columns > 1) return "float" + columns.ToString(CultureInfo.InvariantCulture) + "x" + vectorWidth.ToString(CultureInfo.InvariantCulture);
        return vectorWidth == 1 ? "float" : "float" + vectorWidth.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>Maps the reflected SPIRV-Cross image dimension to the pinned Cg sampler spelling.</summary>
    /// <param name="dimension">SPIRV-Cross dimension text.</param>
    /// <returns>The matching Cg sampler type token.</returns>
    static string GetSamplerTypeToken(string dimension) {
        return dimension switch {
            "Dim1D" => "sampler1D",
            "Dim2D" => "sampler2D",
            "Dim3D" => "sampler3D",
            "DimCube" or "Cube" => "samplerCUBE",
            "DimRect" or "Rect" => "samplerRECT",
            _ => throw new NotSupportedException($"Active PS3 sampler dimension '{dimension}' has no documented Cg mapping.")
        };
    }

    /// <summary>Validates the Cg $vin.ATTRn route and returns its numeric SDK attribute index.</summary>
    /// <param name="variable">Active vertex input declaration.</param>
    /// <returns>The parsed input attribute slot.</returns>
    static uint ParseVertexAttributeSlot(Ps3CgDumpVariable variable) {
        const string prefix = "$vin.ATTR";
        if (!variable.SemanticBinding.StartsWith(prefix, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active vertex input '{variable.Name}' has unsupported Cg binding '{variable.SemanticBinding}'.");
        }
        string suffix = variable.SemanticBinding[prefix.Length..];
        if (!uint.TryParse(suffix, NumberStyles.None, CultureInfo.InvariantCulture, out uint slot) || slot > 15) {
            throw new InvalidOperationException($"Active vertex input '{variable.Name}' has unsupported attribute slot '{variable.SemanticBinding}'.");
        }
        string expectedHardware = "ATTR" + slot.ToString(CultureInfo.InvariantCulture);
        if (!string.Equals(variable.HardwareBinding, expectedHardware, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active vertex input '{variable.Name}' has inconsistent semantic and hardware bindings.");
        }
        return slot;
    }

    /// <summary>Validates and parses the exact texunit n spelling of one active sampler declaration.</summary>
    /// <param name="variable">Active sampler declaration.</param>
    /// <returns>The SDK texture unit.</returns>
    static uint ParseTextureUnit(Ps3CgDumpVariable variable) {
        const string prefix = "texunit ";
        if (!variable.HardwareBinding.StartsWith(prefix, StringComparison.Ordinal)
            || !uint.TryParse(variable.HardwareBinding[prefix.Length..], NumberStyles.None, CultureInfo.InvariantCulture, out uint unit)) {
            throw new InvalidOperationException($"Active sampler '{variable.Name}' has unsupported hardware binding '{variable.HardwareBinding}'.");
        }
        return unit;
    }

    /// <summary>Validates the fixed-function fragment color output declarations supported by the Cg frontend.</summary>
    /// <param name="variable">Active fragment output declaration.</param>
    static void ValidatePixelOutput(Ps3CgDumpVariable variable) {
        if (variable.SemanticBinding == "$vout.COL" && variable.HardwareBinding == "COL") return;
        if ((variable.SemanticBinding == "$vout.COL0" || variable.SemanticBinding == "$vout.COL1")
            && variable.HardwareBinding == variable.SemanticBinding[6..]) return;
        throw new InvalidOperationException($"Active pixel output '{variable.Name}' uses unsupported Cg output route '{variable.SemanticBinding}' / '{variable.HardwareBinding}'.");
    }

    /// <summary>Requires the reflected Position built-in and its active Cg HPOS output route.</summary>
    /// <param name="vertexSource">Vertex-stage source reflection.</param>
    /// <param name="variables">Parsed vertex Cg declarations.</param>
    static void ValidateVertexPosition(Ps3ShaderTranslation vertexSource, IReadOnlyList<Ps3CgDumpVariable> variables) {
        if (!vertexSource.OutputBuiltIns.Contains("Position", StringComparer.Ordinal)) {
            throw new InvalidOperationException("Vertex translation does not declare the required Position built-in output.");
        }
        int matchingPositionCount = 0;
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive) continue;
            if (string.Equals(variable.Name, "gl_Position", StringComparison.Ordinal)) {
                if (!string.Equals(variable.SemanticBinding, "$vout.POSITION", StringComparison.Ordinal)
                    || !string.Equals(variable.HardwareBinding, "HPOS", StringComparison.Ordinal)) {
                    throw new InvalidOperationException("Active gl_Position does not route through the Cg POSITION/HPOS output channel.");
                }
                matchingPositionCount++;
            } else if (string.Equals(variable.SemanticBinding, "$vout.POSITION", StringComparison.Ordinal)
                || string.Equals(variable.SemanticBinding, "$vout.HPOS", StringComparison.Ordinal)) {
                throw new InvalidOperationException($"Active vertex position route is declared by unexpected parameter '{variable.Name}'.");
            }
        }
        if (matchingPositionCount != 1) throw new InvalidOperationException("Vertex Cg dump must contain one active gl_Position routed to HPOS.");
    }

    /// <summary>Matches active Cg user outputs to reflected authored vertex outputs.</summary>
    /// <param name="source">Reflected vertex translation.</param>
    /// <param name="variables">Parsed vertex declarations.</param>
    /// <returns>Active user outputs keyed by authored semantic.</returns>
    static Dictionary<string, Ps3CgDumpVariable> ReadVertexOutputs(Ps3ShaderTranslation source, IReadOnlyList<Ps3CgDumpVariable> variables) {
        Dictionary<string, Ps3ShaderInterfaceVariable> reflected = IndexInterfaces(source.Outputs, "vertex output");
        Dictionary<string, Ps3CgDumpVariable> result = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive || !variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) continue;
            if (variable.Name == "gl_Position") continue;
            if (variable.SemanticBinding == "$vout.COL" && variable.HardwareBinding == "COL") continue;
            string channel = GetVertexUserChannel(variable);
            if (channel == "TEMP") continue;
            Ps3ShaderInterfaceVariable authored = FindInterfaceByGeneratedName(source.Outputs, variable.Name);
            if (!reflected.ContainsKey(authored.Semantic)) throw new InvalidOperationException($"Active Cg output '{variable.Name}' has no matching reflected semantic.");
            ValidateInterfaceDeclaration(authored, variable);
            if (!result.TryAdd(authored.Semantic, variable)) throw new InvalidOperationException($"Vertex semantic '{authored.Semantic}' has multiple active Cg output routes.");
        }
        return result;
    }

    /// <summary>Matches active Cg pixel inputs to reflected authored pixel inputs.</summary>
    /// <param name="source">Reflected pixel translation.</param>
    /// <param name="variables">Parsed pixel declarations.</param>
    /// <returns>Active pixel inputs keyed by authored semantic.</returns>
    static Dictionary<string, Ps3CgDumpVariable> ReadPixelInputs(Ps3ShaderTranslation source, IReadOnlyList<Ps3CgDumpVariable> variables) {
        Dictionary<string, Ps3ShaderInterfaceVariable> reflected = IndexInterfaces(source.Inputs, "pixel input");
        Dictionary<string, Ps3CgDumpVariable> result = new(StringComparer.Ordinal);
        foreach (Ps3CgDumpVariable variable in variables) {
            if (!variable.IsActive || !variable.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) continue;
            string channel = GetPixelUserChannel(variable);
            if (channel == "WPOS") {
                if (!source.InputBuiltIns.Contains("FragCoord", StringComparer.Ordinal)) {
                    throw new InvalidOperationException("Active Cg WPOS input has no matching reflected FragCoord built-in.");
                }
                continue;
            }
            Ps3ShaderInterfaceVariable authored = FindInterfaceByGeneratedName(source.Inputs, variable.Name);
            if (!reflected.ContainsKey(authored.Semantic)) throw new InvalidOperationException($"Active Cg input '{variable.Name}' has no matching reflected semantic.");
            ValidateInterfaceDeclaration(authored, variable);
            if (!result.TryAdd(authored.Semantic, variable)) throw new InvalidOperationException($"Pixel semantic '{authored.Semantic}' has multiple active Cg input routes.");
        }
        return result;
    }

    /// <summary>Indexes authored interfaces and rejects repeated semantics that make Cg reconciliation ambiguous.</summary>
    /// <param name="variables">Reflected interface records.</param>
    /// <param name="role">Human-readable stage role for diagnostics.</param>
    /// <returns>Interface records keyed by exact authored semantic.</returns>
    static Dictionary<string, Ps3ShaderInterfaceVariable> IndexInterfaces(IEnumerable<Ps3ShaderInterfaceVariable> variables, string role) {
        Dictionary<string, Ps3ShaderInterfaceVariable> result = new(StringComparer.Ordinal);
        foreach (Ps3ShaderInterfaceVariable variable in variables) {
            if (!result.TryAdd(variable.Semantic, variable)) throw new InvalidOperationException($"{role} semantic '{variable.Semantic}' is ambiguous.");
        }
        return result;
    }

    /// <summary>Resolves a generated compiler name to exactly one authored interface record.</summary>
    /// <param name="variables">Reflected interface records.</param>
    /// <param name="generatedName">Exact generated GLSL/Cg identifier.</param>
    /// <returns>The matching reflected interface variable.</returns>
    static Ps3ShaderInterfaceVariable FindInterfaceByGeneratedName(IEnumerable<Ps3ShaderInterfaceVariable> variables, string generatedName) {
        Ps3ShaderInterfaceVariable found = null;
        foreach (Ps3ShaderInterfaceVariable variable in variables) {
            if (!string.Equals(variable.GeneratedName, generatedName, StringComparison.Ordinal)) continue;
            if (found != null) throw new InvalidOperationException($"Generated interface name '{generatedName}' is ambiguous.");
            found = variable;
        }
        return found ?? throw new InvalidOperationException($"Cg interface '{generatedName}' has no matching source reflection record.");
    }

    /// <summary>Checks that an active inter-stage source value is a supported scalar/vector and matches its Cg token.</summary>
    /// <param name="reflected">Source compiler reflection for the interface value.</param>
    /// <param name="declaration">The active Cg dump declaration for that value.</param>
    static void ValidateInterfaceDeclaration(Ps3ShaderInterfaceVariable reflected, Ps3CgDumpVariable declaration) {
        if (reflected.Columns != 1 || reflected.ArrayLength != 1) {
            throw new NotSupportedException($"Active inter-stage semantic '{reflected.Semantic}' uses unsupported matrix or array shape {reflected.Columns}x{reflected.VectorWidth}[{reflected.ArrayLength}].");
        }
        string expectedToken = GetFloatTypeToken(reflected.ScalarType, reflected.VectorWidth, reflected.Columns);
        Ps3RsxDataType expectedType = Ps3CgDumpReader.ParseDataType(expectedToken);
        Ps3RsxDataType declarationType = Ps3CgDumpReader.ParseDataType(declaration.TypeToken);
        if (expectedType == Ps3RsxDataType.Unknown || declarationType != expectedType) {
            throw new InvalidOperationException($"Active inter-stage semantic '{reflected.Semantic}' has source type '{expectedToken}' but Cg declares '{declaration.TypeToken}'.");
        }
    }

    /// <summary>Requires semantic, scalar type, physical shape and interpolation to agree between stages.</summary>
    /// <param name="pixelInput">Reflected pixel input.</param>
    /// <param name="vertexOutput">Matching reflected vertex output.</param>
    static void VerifyInterfaceShape(Ps3ShaderInterfaceVariable pixelInput, Ps3ShaderInterfaceVariable vertexOutput) {
        if (!string.Equals(pixelInput.Semantic, vertexOutput.Semantic, StringComparison.Ordinal)
            || !string.Equals(pixelInput.ScalarType, vertexOutput.ScalarType, StringComparison.Ordinal)
            || pixelInput.VectorWidth != vertexOutput.VectorWidth
            || pixelInput.Columns != vertexOutput.Columns
            || pixelInput.ArrayLength != vertexOutput.ArrayLength
            || !string.Equals(pixelInput.Interpolation, vertexOutput.Interpolation, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Vertex output and pixel input for semantic '{pixelInput.Semantic}' disagree in semantic, type, shape, or interpolation.");
        }
    }

    /// <summary>Validates and returns a vertex output's canonical user varying channel.</summary>
    /// <param name="variable">Active vertex Cg output declaration.</param>
    /// <returns>Canonical output channel name.</returns>
    static string GetVertexUserChannel(Ps3CgDumpVariable variable) {
        if (!variable.SemanticBinding.StartsWith("$vout.", StringComparison.Ordinal)) throw new InvalidOperationException($"Active vertex variable '{variable.Name}' has unsupported binding '{variable.SemanticBinding}'.");
        string semantic = variable.SemanticBinding[6..];
        string canonical = semantic switch {
            "POSITION" or "HPOS" => "POSITION",
            "COL0" => "COL0",
            "COL1" => "COL1",
            "BFC0" => "BFC0",
            "BFC1" => "BFC1",
            "FOGC" => "FOGC",
            "PSIZ" or "PSZ" => "PSIZ",
            "TEMP" => "TEMP",
            _ when IsNumberedChannel(semantic, "TEX", 7) => semantic,
            _ => throw new InvalidOperationException($"Active vertex output '{variable.Name}' uses unsupported Cg route '{variable.SemanticBinding}'.")
        };
        string expectedHardware = canonical switch {
            "POSITION" => "HPOS",
            "PSIZ" => "PSZ",
            _ => canonical
        };
        if (!string.Equals(variable.HardwareBinding, expectedHardware, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active vertex output '{variable.Name}' has inconsistent route '{variable.SemanticBinding}' and hardware binding '{variable.HardwareBinding}'.");
        }
        return canonical;
    }

    /// <summary>Validates and returns a pixel input's canonical user varying channel.</summary>
    /// <param name="variable">Active pixel Cg input declaration.</param>
    /// <returns>Canonical input channel name.</returns>
    static string GetPixelUserChannel(Ps3CgDumpVariable variable) {
        if (!variable.SemanticBinding.StartsWith("$vin.", StringComparison.Ordinal)) throw new InvalidOperationException($"Active pixel variable '{variable.Name}' has unsupported binding '{variable.SemanticBinding}'.");
        string semantic = variable.SemanticBinding[5..];
        string canonical = semantic switch {
            "WPOS" => "WPOS",
            "COL0" or "COLOR0" => "COL0",
            "COL1" or "COLOR1" => "COL1",
            "FOGC" => "FOGC",
            _ when IsNumberedChannel(semantic, "TEX", 7) => semantic,
            _ => throw new InvalidOperationException($"Active pixel input '{variable.Name}' uses unsupported Cg route '{variable.SemanticBinding}'.")
        };
        string expectedHardware = semantic.StartsWith("COLOR", StringComparison.Ordinal) ? canonical : semantic;
        if (!string.Equals(variable.HardwareBinding, expectedHardware, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Active pixel input '{variable.Name}' has inconsistent route '{variable.SemanticBinding}' and hardware binding '{variable.HardwareBinding}'.");
        }
        return canonical;
    }

    /// <summary>Maps a supported canonical vertex output to the SDK's stage-local numeric slot.</summary>
    /// <param name="channel">Canonical Cg/RSX channel name.</param>
    /// <returns>Vertex output slot index.</returns>
    static uint GetVertexSlot(string channel) {
        return channel switch {
            "POSITION" => 0,
            "COL0" => 1,
            "COL1" => 2,
            "BFC0" => 3,
            "BFC1" => 4,
            "FOGC" => 5,
            "PSIZ" => 6,
            _ when IsNumberedChannel(channel, "TEX", 7) => 7u + ParseChannelIndex(channel, "TEX"),
            _ => throw new InvalidOperationException($"Channel '{channel}' has no supported vertex output slot.")
        };
    }

    /// <summary>Maps a supported canonical pixel input to the SDK's stage-local numeric slot.</summary>
    /// <param name="channel">Canonical Cg/RSX channel name.</param>
    /// <returns>Pixel input slot index.</returns>
    static uint GetPixelSlot(string channel) {
        return channel switch {
            "WPOS" => 0,
            "COL0" => 1,
            "COL1" => 2,
            "FOGC" => 3,
            _ when IsNumberedChannel(channel, "TEX", 7) => 4u + ParseChannelIndex(channel, "TEX"),
            _ => throw new InvalidOperationException($"Channel '{channel}' has no supported pixel input slot.")
        };
    }

    /// <summary>Checks a pinned named channel's prefix and supported decimal range.</summary>
    /// <param name="channel">Cg channel spelling.</param>
    /// <param name="prefix">Required channel prefix.</param>
    /// <param name="maximumIndex">Largest supported zero-based channel index.</param>
    /// <returns>True only for a fully numeric supported channel.</returns>
    static bool IsNumberedChannel(string channel, string prefix, uint maximumIndex) {
        if (!channel.StartsWith(prefix, StringComparison.Ordinal)) return false;
        string indexText = channel[prefix.Length..];
        return uint.TryParse(indexText, NumberStyles.None, CultureInfo.InvariantCulture, out uint index) && index <= maximumIndex;
    }

    /// <summary>Parses the already-validated numeric suffix of a canonical channel name.</summary>
    /// <param name="channel">Cg channel spelling.</param>
    /// <param name="prefix">Channel prefix to remove.</param>
    /// <returns>Zero-based channel index.</returns>
    static uint ParseChannelIndex(string channel, string prefix) {
        return uint.Parse(channel[prefix.Length..], NumberStyles.None, CultureInfo.InvariantCulture);
    }
}

```

### builder/shaders/Ps3ShaderPairCompiler.cs
```csharp
using helengine;

namespace helengine.ps3.builder;

/// <summary>Translates, compiles, and reconciles a compatible PS3 vertex/pixel shader pair.</summary>
public sealed class Ps3ShaderPairCompiler {
    /// <summary>Translates authored HLSL into PS3 GLSL and reflection.</summary>
    readonly Ps3ShaderSourceCompiler SourceCompiler;

    /// <summary>Runs each translated stage through the pinned RSX compiler.</summary>
    readonly IPs3RsxCompiler RsxCompiler;

    /// <summary>Reconciles the stage source metadata with final Cg and SDK declarations.</summary>
    readonly Ps3ShaderBindingMapBuilder BindingMapBuilder;

    /// <summary>Creates a pair compiler with its source translator and pinned SDK executor.</summary>
    /// <param name="sourceCompiler">PS3 HLSL source translator.</param>
    /// <param name="rsxCompiler">Pinned Docker RSX compiler.</param>
    public Ps3ShaderPairCompiler(Ps3ShaderSourceCompiler sourceCompiler, IPs3RsxCompiler rsxCompiler) {
        SourceCompiler = sourceCompiler ?? throw new ArgumentNullException(nameof(sourceCompiler));
        RsxCompiler = rsxCompiler ?? throw new ArgumentNullException(nameof(rsxCompiler));
        BindingMapBuilder = new Ps3ShaderBindingMapBuilder();
    }

    /// <summary>Compiles both requests under unique stage roots and returns only a fully reconciled pair.</summary>
    /// <param name="vertexRequest">Authored vertex-stage request.</param>
    /// <param name="pixelRequest">Authored pixel-stage request.</param>
    /// <param name="includeResolver">Shared source include resolver and compiler preprocessor callback.</param>
    /// <param name="workingRoot">Explicit build-owned directory for this pair's compiler jobs.</param>
    /// <param name="cancellationToken">Token forwarded to each SDK stage job.</param>
    /// <returns>A complete pair with both SDK programs, translations and validated bindings.</returns>
    public Ps3ShaderProgramPair Compile(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest,
        IShaderIncludeResolver includeResolver, string workingRoot, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(vertexRequest);
        ArgumentNullException.ThrowIfNull(pixelRequest);
        ArgumentNullException.ThrowIfNull(includeResolver);
        if (string.IsNullOrWhiteSpace(workingRoot)) throw new ArgumentException("Explicit pair working root must be provided.", nameof(workingRoot));
        ValidateCompatibleRequests(vertexRequest, pixelRequest);
        cancellationToken.ThrowIfCancellationRequested();

        string pairRoot = CreateUniquePairDirectory(workingRoot);
        string vertexRoot = Path.Combine(pairRoot, "vertex");
        string pixelRoot = Path.Combine(pairRoot, "pixel");
        Directory.CreateDirectory(vertexRoot);
        Directory.CreateDirectory(pixelRoot);

        Ps3ShaderTranslation vertexTranslation = SourceCompiler.Translate(vertexRequest, includeResolver);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3RsxProgram vertexProgram = RsxCompiler.Compile(vertexTranslation, vertexRoot, cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3ShaderTranslation pixelTranslation = SourceCompiler.Translate(pixelRequest, includeResolver);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3RsxProgram pixelProgram = RsxCompiler.Compile(pixelTranslation, pixelRoot, cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3ShaderBindingMap bindings = BindingMapBuilder.Build(vertexTranslation, vertexProgram, pixelTranslation, pixelProgram);
        return new Ps3ShaderProgramPair(vertexProgram, pixelProgram, vertexTranslation, pixelTranslation, bindings);
    }

    /// <summary>Rejects incompatible target, stage, program, variant, defines, shader model or shared options before work starts.</summary>
    /// <param name="vertexRequest">Vertex request candidate.</param>
    /// <param name="pixelRequest">Pixel request candidate.</param>
    static void ValidateCompatibleRequests(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest) {
        if (vertexRequest.Target != ShaderCompileTarget.Ps3 || pixelRequest.Target != ShaderCompileTarget.Ps3) {
            throw new InvalidOperationException("Both shader pair requests must target PS3.");
        }
        if (vertexRequest.Stage != ShaderStage.Vertex || pixelRequest.Stage != ShaderStage.Pixel) {
            throw new InvalidOperationException("Shader pair requests must be ordered as vertex then pixel stages.");
        }
        if (!string.Equals(vertexRequest.ProgramName, pixelRequest.ProgramName, StringComparison.Ordinal)
            || !string.Equals(vertexRequest.Variant, pixelRequest.Variant, StringComparison.Ordinal)) {
            throw new InvalidOperationException("Vertex and pixel requests must share program and variant identities.");
        }
        if (vertexRequest.ShaderModel.Major != pixelRequest.ShaderModel.Major || vertexRequest.ShaderModel.Minor != pixelRequest.ShaderModel.Minor) {
            throw new InvalidOperationException("Vertex and pixel requests must use the same shader model.");
        }
        if (!HaveSameDefines(vertexRequest.Defines, pixelRequest.Defines)) {
            throw new InvalidOperationException("Vertex and pixel requests must use the same ordered preprocessor definitions.");
        }
        if (!HaveSameOptions(vertexRequest.Options, pixelRequest.Options)) {
            throw new InvalidOperationException("Vertex and pixel requests must use the same shader compilation options and binding policy.");
        }
    }

    /// <summary>Compares ordered define names and values without normalizing duplicate preprocessor definitions.</summary>
    /// <param name="vertexDefines">Vertex request definitions.</param>
    /// <param name="pixelDefines">Pixel request definitions.</param>
    /// <returns>True when both requests have identical ordered definitions.</returns>
    static bool HaveSameDefines(IReadOnlyList<ShaderDefine> vertexDefines, IReadOnlyList<ShaderDefine> pixelDefines) {
        if (vertexDefines.Count != pixelDefines.Count) return false;
        for (int index = 0; index < vertexDefines.Count; index++) {
            if (!string.Equals(vertexDefines[index].Name, pixelDefines[index].Name, StringComparison.Ordinal)
                || !string.Equals(vertexDefines[index].Value, pixelDefines[index].Value, StringComparison.Ordinal)) return false;
        }
        return true;
    }

    /// <summary>Compares every shared compilation option and binding-policy field.</summary>
    /// <param name="vertexOptions">Vertex request options.</param>
    /// <param name="pixelOptions">Pixel request options.</param>
    /// <returns>True when both options records are equivalent.</returns>
    static bool HaveSameOptions(ShaderCompileOptions vertexOptions, ShaderCompileOptions pixelOptions) {
        ShaderBindingPolicy vertexPolicy = vertexOptions.BindingPolicy;
        ShaderBindingPolicy pixelPolicy = pixelOptions.BindingPolicy;
        return vertexOptions.GenerateDebugInfo == pixelOptions.GenerateDebugInfo
            && vertexOptions.Optimize == pixelOptions.Optimize
            && vertexOptions.TreatWarningsAsErrors == pixelOptions.TreatWarningsAsErrors
            && vertexPolicy.DefaultSpace == pixelPolicy.DefaultSpace
            && vertexPolicy.ConstantBufferShift == pixelPolicy.ConstantBufferShift
            && vertexPolicy.TextureShift == pixelPolicy.TextureShift
            && vertexPolicy.SamplerShift == pixelPolicy.SamplerShift
            && vertexPolicy.StorageShift == pixelPolicy.StorageShift;
    }

    /// <summary>Creates a unique explicit pair directory below the caller's build-owned work root.</summary>
    /// <param name="workingRoot">Caller-supplied pair directory parent.</param>
    /// <returns>The newly created unique pair directory.</returns>
    static string CreateUniquePairDirectory(string workingRoot) {
        string fullWorkingRoot = Path.GetFullPath(workingRoot);
        Directory.CreateDirectory(fullWorkingRoot);
        while (true) {
            string pairRoot = Path.Combine(fullWorkingRoot, "ps3-shader-pair-" + Guid.NewGuid().ToString("N"));
            try {
                Directory.CreateDirectory(pairRoot);
                return pairRoot;
            } catch (IOException) when (Directory.Exists(pairRoot)) {
                continue;
            }
        }
    }
}

```

### builder/shaders/Ps3ShaderProgramPair.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Contains both validated SDK stages, their source reflection, and the reconciled binding map.</summary>
public sealed class Ps3ShaderProgramPair {
    /// <summary>Creates a complete immutable pair after both source translations and SDK programs validate.</summary>
    /// <param name="vertex">Validated vertex SDK program.</param>
    /// <param name="pixel">Validated pixel SDK program.</param>
    /// <param name="vertexTranslation">Vertex source translation and reflection.</param>
    /// <param name="pixelTranslation">Pixel source translation and reflection.</param>
    /// <param name="bindings">Validated authored-to-RSX binding map.</param>
    internal Ps3ShaderProgramPair(Ps3RsxProgram vertex, Ps3RsxProgram pixel, Ps3ShaderTranslation vertexTranslation,
        Ps3ShaderTranslation pixelTranslation, Ps3ShaderBindingMap bindings) {
        Vertex = vertex ?? throw new ArgumentNullException(nameof(vertex));
        Pixel = pixel ?? throw new ArgumentNullException(nameof(pixel));
        VertexTranslation = vertexTranslation ?? throw new ArgumentNullException(nameof(vertexTranslation));
        PixelTranslation = pixelTranslation ?? throw new ArgumentNullException(nameof(pixelTranslation));
        Bindings = bindings ?? throw new ArgumentNullException(nameof(bindings));
        if (Vertex.Stage != helengine.ShaderStage.Vertex || Pixel.Stage != helengine.ShaderStage.Pixel
            || VertexTranslation.Stage != helengine.ShaderStage.Vertex || PixelTranslation.Stage != helengine.ShaderStage.Pixel) {
            throw new ArgumentException("A PS3 shader program pair requires matching vertex and pixel stages.");
        }
    }

    /// <summary>Gets the validated RSX vertex program.</summary>
    public Ps3RsxProgram Vertex { get; }

    /// <summary>Gets the validated RSX pixel program.</summary>
    public Ps3RsxProgram Pixel { get; }

    /// <summary>Gets the vertex source translation and authored reflection.</summary>
    public Ps3ShaderTranslation VertexTranslation { get; }

    /// <summary>Gets the pixel source translation and authored reflection.</summary>
    public Ps3ShaderTranslation PixelTranslation { get; }

    /// <summary>Gets the validated map between authored resources and final RSX parameters/channels.</summary>
    public Ps3ShaderBindingMap Bindings { get; }
}

```

### builder/shaders/Ps3ShaderSamplerMapEntry.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Associates one authored texture/sampler pair with its actual RSX sampler parameter and texture unit.</summary>
public sealed class Ps3ShaderSamplerMapEntry {
    /// <summary>Creates a mapping for one reflected combined sampler and its SDK sampler record.</summary>
    /// <param name="sourceSampler">Original authored texture and sampler identities.</param>
    /// <param name="rsxParameter">Validated SDK sampler parameter.</param>
    public Ps3ShaderSamplerMapEntry(Ps3ShaderSamplerBinding sourceSampler, Ps3RsxParameter rsxParameter) {
        SourceSampler = sourceSampler ?? throw new ArgumentNullException(nameof(sourceSampler));
        RsxParameter = rsxParameter ?? throw new ArgumentNullException(nameof(rsxParameter));
        if (rsxParameter.Category != Ps3RsxParameterCategory.Sampler || !rsxParameter.HardwareIndex.HasValue) {
            throw new ArgumentException("Combined sampler mapping requires an SDK sampler parameter with a texture unit.", nameof(rsxParameter));
        }
        TextureName = sourceSampler.TextureName;
        TextureBinding = sourceSampler.TextureBinding;
        SamplerName = sourceSampler.SamplerName;
        SamplerBinding = sourceSampler.SamplerBinding;
        GeneratedName = sourceSampler.GeneratedName;
        Dimension = sourceSampler.Dimension;
        TextureUnit = rsxParameter.HardwareIndex.Value;
    }

    /// <summary>Gets the original authored combined texture/sampler reflection record.</summary>
    public Ps3ShaderSamplerBinding SourceSampler { get; }

    /// <summary>Gets the exact validated SDK sampler parameter.</summary>
    public Ps3RsxParameter RsxParameter { get; }

    /// <summary>Gets the authored texture resource name.</summary>
    public string TextureName { get; }

    /// <summary>Gets the source binding assigned to the authored texture resource.</summary>
    public uint TextureBinding { get; }

    /// <summary>Gets the authored sampler resource name.</summary>
    public string SamplerName { get; }

    /// <summary>Gets the source binding assigned to the authored sampler resource.</summary>
    public uint SamplerBinding { get; }

    /// <summary>Gets the exact unique generated combined sampler identifier.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the source compiler's image dimensionality name.</summary>
    public string Dimension { get; }

    /// <summary>Gets the raw SDK texture unit occupied by this combined sampler.</summary>
    public uint TextureUnit { get; }
}

```

### builder/shaders/Ps3ShaderStageLink.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Associates one authored fragment input with its matching vertex output and stage-local RSX channels.</summary>
public sealed class Ps3ShaderStageLink {
    /// <summary>Creates a validated immutable link between matching authored interface variables.</summary>
    /// <param name="authoredSemantic">Shared HLSL semantic, such as TEXCOORD0.</param>
    /// <param name="scalarType">SPIR-V scalar base type and bit width.</param>
    /// <param name="vectorWidth">Number of scalar components in the vector.</param>
    /// <param name="columns">Number of physical matrix columns.</param>
    /// <param name="arrayLength">Number of literal array elements.</param>
    /// <param name="interpolation">Compiler-reported interpolation mode and modifiers.</param>
    /// <param name="vertexChannel">Canonical RSX vertex-output channel.</param>
    /// <param name="pixelChannel">Canonical RSX pixel-input channel.</param>
    /// <param name="vertexSlot">Pinned SDK's stage-local vertex-output slot index.</param>
    /// <param name="pixelSlot">Pinned SDK's stage-local pixel-input slot index.</param>
    public Ps3ShaderStageLink(string authoredSemantic, string scalarType, uint vectorWidth, uint columns, uint arrayLength, string interpolation,
        string vertexChannel, string pixelChannel, uint vertexSlot, uint pixelSlot) {
        if (string.IsNullOrWhiteSpace(authoredSemantic)) throw new ArgumentException("Authored semantic is required.", nameof(authoredSemantic));
        if (string.IsNullOrWhiteSpace(scalarType)) throw new ArgumentException("Scalar type is required.", nameof(scalarType));
        if (string.IsNullOrWhiteSpace(interpolation)) throw new ArgumentException("Interpolation metadata is required.", nameof(interpolation));
        if (string.IsNullOrWhiteSpace(vertexChannel)) throw new ArgumentException("Vertex channel is required.", nameof(vertexChannel));
        if (string.IsNullOrWhiteSpace(pixelChannel)) throw new ArgumentException("Pixel channel is required.", nameof(pixelChannel));
        AuthoredSemantic = authoredSemantic;
        ScalarType = scalarType;
        VectorWidth = vectorWidth;
        Columns = columns;
        ArrayLength = arrayLength;
        Interpolation = interpolation;
        VertexChannel = vertexChannel;
        PixelChannel = pixelChannel;
        VertexSlot = vertexSlot;
        PixelSlot = pixelSlot;
    }

    /// <summary>Gets the authored HLSL semantic shared by both stages.</summary>
    public string AuthoredSemantic { get; }

    /// <summary>Gets the scalar base type and bit width reflected from SPIR-V.</summary>
    public string ScalarType { get; }

    /// <summary>Gets the number of scalar components in each vector.</summary>
    public uint VectorWidth { get; }

    /// <summary>Gets the physical matrix column count, or one for non-matrix values.</summary>
    public uint Columns { get; }

    /// <summary>Gets the literal array length, or one for non-array values.</summary>
    public uint ArrayLength { get; }

    /// <summary>Gets the compiler-reported interpolation mode and modifiers.</summary>
    public string Interpolation { get; }

    /// <summary>Gets the canonical Cg/RSX vertex-output route name.</summary>
    public string VertexChannel { get; }

    /// <summary>Gets the canonical Cg/RSX pixel-input route name.</summary>
    public string PixelChannel { get; }

    /// <summary>Gets the raw stage-local RSX vertex-output slot index.</summary>
    public uint VertexSlot { get; }

    /// <summary>Gets the raw stage-local RSX pixel-input slot index.</summary>
    public uint PixelSlot { get; }
}

```

### builder/shaders/Ps3ShaderUniformBinding.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Associates one authored constant-buffer member with its validated RSX constant parameter.</summary>
public sealed class Ps3ShaderUniformBinding {
    /// <summary>Creates a mapping for one reflected uniform member and its actual SDK parameter.</summary>
    /// <param name="sourceMember">Original authored buffer member reflection record.</param>
    /// <param name="rsxParameter">Validated constant parameter containing vertex registers or fragment relocations.</param>
    public Ps3ShaderUniformBinding(helengine.ShaderStage stage, Ps3ShaderUniformMember sourceMember, Ps3RsxParameter rsxParameter) {
        Stage = stage;
        SourceMember = sourceMember ?? throw new ArgumentNullException(nameof(sourceMember));
        RsxParameter = rsxParameter ?? throw new ArgumentNullException(nameof(rsxParameter));
        AuthoredBufferName = sourceMember.BufferName;
        AuthoredBufferBinding = sourceMember.BufferBinding;
        AuthoredMemberPath = sourceMember.MemberPath;
        GeneratedName = sourceMember.GeneratedName;
    }

    /// <summary>Gets the source reflection record with the member's declared physical layout.</summary>
    public Ps3ShaderUniformMember SourceMember { get; }

    /// <summary>Gets the stage whose binary constant references are carried by the SDK parameter.</summary>
    public helengine.ShaderStage Stage { get; }

    /// <summary>Gets the exact validated SDK constant parameter.</summary>
    public Ps3RsxParameter RsxParameter { get; }

    /// <summary>Gets the authored constant-buffer name.</summary>
    public string AuthoredBufferName { get; }

    /// <summary>Gets the authored constant-buffer binding.</summary>
    public uint AuthoredBufferBinding { get; }

    /// <summary>Gets the complete member path within the authored buffer.</summary>
    public string AuthoredMemberPath { get; }

    /// <summary>Gets the exact SPIRV-Cross-generated Cg parameter identity.</summary>
    public string GeneratedName { get; }
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

```

### builder.tests/Ps3ShaderPairCompilerTests.cs
```csharp
using helengine;
using helengine.ps3.builder;
using System.Runtime.CompilerServices;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies paired PS3 compilation validates request identity and forwards cancellation to both stage jobs.</summary>
public sealed class Ps3ShaderPairCompilerTests {
    /// <summary>HLSL source with one active vertex attribute and built-in position output.</summary>
    const string VertexSource = "float4 VS(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }";

    /// <summary>HLSL source with one fixed-function fragment color output.</summary>
    const string PixelSource = "float4 PS() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }";

    /// <summary>Proves variant mismatches are rejected before either source translation or RSX compilation begins.</summary>
    [Fact]
    public void RejectsIncompatibleVariantBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "wireframe", ShaderCompileTarget.Ps3);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects non-PS3 targets before invoking the source or RSX compiler.</summary>
    [Fact]
    public void RejectsNonPs3TargetBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.DirectX11);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects requests that are not ordered as vertex followed by pixel before starting jobs.</summary>
    [Fact]
    public void RejectsIncorrectStageOrderBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects differing preprocessor define sets as incompatible pair variants before starting jobs.</summary>
    [Fact]
    public void RejectsDifferentDefineSetsBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3,
            new[] { new ShaderDefine("QUALITY", "high") });
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, Array.Empty<ShaderDefine>());

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects requests that target different shader models before starting either stage job.</summary>
    [Fact]
    public void RejectsDifferentShaderModelsBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, model: new ShaderModel(4, 0));

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects requests with different optimization options before starting either stage job.</summary>
    [Fact]
    public void RejectsDifferentCompileOptionsBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileOptions unoptimized = new(ShaderBindingPolicies.Default, true, false, false);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, options: unoptimized);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Proves a cancellation raised by the pixel job is propagated after the vertex job and carries the caller token.</summary>
    [Fact]
    public void ForwardsCancellationTokenThroughBothStageJobs() {
        using CancellationTokenSource cancellationSource = new();
        Ps3ShaderPairCompilerStub rsxCompiler = new(2);
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request(VertexSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request(PixelSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);

        Assert.Throws<OperationCanceledException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), cancellationSource.Token)));

        Assert.Equal(2, rsxCompiler.CallCount);
        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[0]);
        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[1]);
        Assert.NotEqual(rsxCompiler.WorkingRoots[0], rsxCompiler.WorkingRoots[1]);
    }

    /// <summary>Compiles the modern cbuffer, paired-texture and two-varying fixture with the pinned Docker SDK.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesModernTextureCbufferPairAndLinksBothVaryingsWithPinnedSdk() {
        ShaderCompileRequest vertex = Request(ModernPairSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request(ModernPairSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), new Ps3DockerShaderCompiler(Ps3ShaderToolchain.Load(RequiredRepositoryRoot())));

        Ps3ShaderProgramPair pair = compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(),
            Path.Combine(RequiredArtifactsRoot(), "modern-texture-cbuffer-pair"), CancellationToken.None);

        Assert.Equal(ShaderStage.Vertex, pair.Vertex.Stage);
        Assert.Equal(ShaderStage.Pixel, pair.Pixel.Stage);
        Assert.Equal(ShaderStage.Vertex, pair.VertexTranslation.Stage);
        Assert.Equal(ShaderStage.Pixel, pair.PixelTranslation.Stage);
        Assert.Equal(3, pair.Bindings.Attributes.Count);
        Assert.Equal(2, pair.Bindings.StageLinks.Count);
        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "COLOR0" && link.VertexChannel == "TEX0" && link.PixelChannel == "TEX0");
        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "TEXCOORD0" && link.VertexChannel == "TEX1" && link.PixelChannel == "TEX1");
        Assert.All(pair.Bindings.StageLinks, link => Assert.Equal(link.VertexChannel, link.PixelChannel));
        Assert.Equal(2, pair.Bindings.Samplers.Count);
        Assert.Equal(2, pair.Bindings.Samplers.Select(binding => binding.TextureUnit).Distinct().Count());
        Assert.Contains(pair.Bindings.Uniforms, binding => binding.AuthoredMemberPath == "Primary.Tint");
        Ps3ShaderUniformBinding pixelTint = Assert.Single(pair.Bindings.Uniforms,
            binding => binding.Stage == ShaderStage.Pixel && binding.AuthoredMemberPath == "Primary.Tint");
        Assert.Contains(pixelTint.RsxParameter.Elements, element => element.FragmentRelocationOffsets.Count > 0);
    }

    /// <summary>Builds a request with an explicit target, entry point and variant for validation tests.</summary>
    /// <param name="source">HLSL source text.</param>
    /// <param name="stage">Requested pipeline stage.</param>
    /// <param name="variant">Variant identity.</param>
    /// <param name="target">Compilation target.</param>
    /// <returns>The requested immutable compile request.</returns>
    static ShaderCompileRequest Request(string source, ShaderStage stage, string variant, ShaderCompileTarget target,
        IReadOnlyList<ShaderDefine> defines = null, ShaderModel model = null, ShaderCompileOptions options = null) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("pair-test.hlsl", source),
            "PairTest",
            stage == ShaderStage.Vertex ? "VS" : "PS",
            stage,
            target,
            model ?? new ShaderModel(3, 0),
            variant,
            defines ?? Array.Empty<ShaderDefine>(),
            options ?? new ShaderCompileOptions(ShaderBindingPolicies.Default, true, true, false));
    }

    /// <summary>Returns a unique child root under the explicitly configured build-owned shader-test directory.</summary>
    /// <returns>A build-owned pair job root.</returns>
    static string WorkingRoot() {
        string configuredRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(configuredRoot), "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to a build-owned Task 4 test root.");
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "Set HELENGINE_PS3_REPOSITORY_ROOT to this checkout.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(configuredRoot!, repositoryRoot!);
        return Path.Combine(validatedRoot, "pair-unit-tests", Guid.NewGuid().ToString("N"));
    }

    /// <summary>Builds and validates the explicit build-owned artifact root for pinned SDK tests.</summary>
    /// <returns>Canonical test output root below sibling builds/helengine-ps3.</returns>
    static string RequiredArtifactsRoot() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(outputRoot), "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to a build-owned Task 4 test root.");
        return Ps3Task3TestPaths.ValidateArtifactsRoot(outputRoot!, RequiredRepositoryRoot());
    }

    /// <summary>Returns the explicit repository root required by pinned SDK integration tests.</summary>
    /// <returns>Repository root containing toolchain-lock.json.</returns>
    static string RequiredRepositoryRoot() {
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "Set HELENGINE_PS3_REPOSITORY_ROOT to this checkout.");
        return repositoryRoot!;
    }

    /// <summary>HLSL fixture that exercises reordered varyings, nested constants, and two samplers on the same texture.</summary>
    const string ModernPairSource = """
        cbuffer Frame : register(b0)
        {
            float3 Direction;
            float Intensity;
            column_major float4x4 Transform;
            row_major float3x4 RowMajorTransform;
        };
        struct SharedTint { float4 Tint; };
        cbuffer MaterialPrimary : register(b1) { SharedTint Primary; };
        cbuffer MaterialSecondary : register(b2) { SharedTint Secondary; };
        Texture2D Albedo : register(t0);
        SamplerState LinearSampler : register(s0);
        SamplerState PointSampler : register(s1);
        struct VertexInput
        {
            float3 Position : POSITION;
            float2 Uv : TEXCOORD0;
            float4 Color : COLOR0;
        };
        struct VertexOutput
        {
            float4 Color : COLOR0;
            float2 Uv : TEXCOORD0;
            float4 Position : SV_Position;
        };
        VertexOutput VS(VertexInput input)
        {
            VertexOutput output;
            output.Color = input.Color * Primary.Tint * Secondary.Tint;
            output.Uv = input.Uv;
            float3 position = mul(Transform, float4(input.Position, 1)).xyz
                + mul(RowMajorTransform, float4(input.Position, 1))
                + Direction * Intensity;
            output.Position = float4(position, 1);
            return output;
        }
        float4 PS(VertexOutput input) : SV_Target
        {
            float4 linearSample = Albedo.Sample(LinearSampler, input.Uv);
            float4 pointSample = Albedo.Sample(PointSampler, input.Uv);
            return (linearSample + pointSample) * input.Color * Primary.Tint;
        }
        """;
}

```

### builder.tests/Ps3ShaderPairCompilerStub.cs
```csharp
using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Records pair-compiler job calls and can raise cancellation at a selected stage boundary.</summary>
internal sealed class Ps3ShaderPairCompilerStub : IPs3RsxCompiler {
    /// <summary>Job call count at which the stub raises an operation-canceled exception, or zero to compile both stages.</summary>
    readonly int CancelOnCall;

    /// <summary>Records the working roots passed to each stage job.</summary>
    readonly List<string> Roots = new();

    /// <summary>Records the caller cancellation token passed to each stage job.</summary>
    readonly List<CancellationToken> Tokens = new();

    /// <summary>Creates an RSX compiler stub with optional cancellation at a particular call number.</summary>
    /// <param name="cancelOnCall">One-based call number to cancel, or zero for no cancellation.</param>
    public Ps3ShaderPairCompilerStub(int cancelOnCall = 0) {
        CancelOnCall = cancelOnCall;
    }

    /// <summary>Gets the number of stage jobs started.</summary>
    public int CallCount { get; private set; }

    /// <summary>Gets all explicit per-stage working roots.</summary>
    public IReadOnlyList<string> WorkingRoots => Roots;

    /// <summary>Gets the exact cancellation tokens received by each stage job.</summary>
    public IReadOnlyList<CancellationToken> CancellationTokens => Tokens;

    /// <summary>Returns a minimal fixed-function-only program or raises cancellation on the configured call.</summary>
    /// <param name="translation">Source translation for the stage.</param>
    /// <param name="workingRoot">Unique explicit stage job root.</param>
    /// <param name="cancellationToken">Caller cancellation token.</param>
    /// <returns>A minimal RSX program with a stage profile and fixed-function output.</returns>
    public Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken) {
        CallCount++;
        Roots.Add(workingRoot);
        Tokens.Add(cancellationToken);
        if (CallCount == CancelOnCall) throw new OperationCanceledException(cancellationToken);
        string dump = translation.Stage == ShaderStage.Vertex
            ? "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            : "#profile fp40\n#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n";
        return new Ps3RsxProgram(translation.Stage, Array.Empty<byte>(), dump, "test-sdk", Array.Empty<Ps3RsxParameter>());
    }
}

```

### builder.tests/Ps3ShaderBindingMapTests.cs
```csharp
using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies authored shader metadata resolves to the actual stage-local RSX routing slots.</summary>
public sealed class Ps3ShaderBindingMapTests {
    /// <summary>Links matching TEX0 channels while retaining their different vertex and pixel slot indices.</summary>
    [Fact]
    public void LinksTexcoordZeroAcrossDifferentStageLocalIndices() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX0 : TEX0 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX0 : TEX0 : -1 : 1\n";
        Ps3ShaderTranslation vertexSource = new(
            ShaderStage.Vertex,
            "vertex source",
            Array.Empty<string>(),
            new[] { "Position" },
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            new[] { new Ps3ShaderInterfaceVariable("TEXCOORD0", 0, "FP3232", 2, 1, 1, "smooth", "vertexUv") },
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>());
        Ps3ShaderTranslation pixelSource = new(
            ShaderStage.Pixel,
            "pixel source",
            Array.Empty<string>(),
            Array.Empty<string>(),
            new[] { new Ps3ShaderInterfaceVariable("TEXCOORD0", 0, "FP3232", 2, 1, 1, "smooth", "pixelUv") },
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>());
        Ps3RsxProgram vertexProgram = new(ShaderStage.Vertex, Array.Empty<byte>(), vertexDump, "test-sdk", Array.Empty<Ps3RsxParameter>());
        Ps3RsxProgram pixelProgram = new(ShaderStage.Pixel, Array.Empty<byte>(), pixelDump, "test-sdk", Array.Empty<Ps3RsxParameter>());
        Ps3ShaderBindingMapBuilder builder = new();

        Ps3ShaderBindingMap map = builder.Build(vertexSource, vertexProgram, pixelSource, pixelProgram);

        Ps3ShaderStageLink link = Assert.Single(map.StageLinks);
        Assert.Equal("TEXCOORD0", link.AuthoredSemantic);
        Assert.Equal("TEX0", link.VertexChannel);
        Assert.Equal("TEX0", link.PixelChannel);
        Assert.Equal(7u, link.VertexSlot);
        Assert.Equal(4u, link.PixelSlot);
    }

    /// <summary>Confirms the binary reader still accepts a fixed-function position output after dump parsing is shared.</summary>
    [Fact]
    public void ReaderRetainsBinaryValidationForPositionOnlyVertexProgram() {
        byte[] bytecode = CreatePositionOnlyVertexProgram();
        const string dump = "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n";

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytecode, dump, "test-sdk");

        Assert.Equal(ShaderStage.Vertex, program.Stage);
        Assert.Equal(bytecode, program.Bytecode);
        Assert.Empty(program.Parameters);
    }

    /// <summary>Preserves exact active and inactive Cg declaration fields for both reconciliation and SDK validation.</summary>
    [Fact]
    public void SharedDumpReaderPreservesExactFieldsAndInactiveDeclarations() {
        const string dump = "#profile vp40\n"
            + "#var float2 activeUv : $vout.TEX0 : TEX0 : -1 : 1\n"
            + "#var float4 optimizedColor :  : c[7] : 12 : 0\n";

        IReadOnlyList<Ps3CgDumpVariable> variables = Ps3CgDumpReader.Read(ShaderStage.Vertex, dump);

        Assert.Equal(2, variables.Count);
        Assert.Equal("float2", variables[0].TypeToken);
        Assert.Equal("activeUv", variables[0].Name);
        Assert.Equal("$vout.TEX0", variables[0].SemanticBinding);
        Assert.Equal("TEX0", variables[0].HardwareBinding);
        Assert.Equal(-1, variables[0].CompilerParameterIndex);
        Assert.True(variables[0].IsActive);
        Assert.Equal("optimizedColor", variables[1].Name);
        Assert.Equal("c[7]", variables[1].HardwareBinding);
        Assert.Equal(12, variables[1].CompilerParameterIndex);
        Assert.False(variables[1].IsActive);
    }

    /// <summary>Matches interfaces by authored semantic when struct fields are reordered and leaves unused outputs unlinked.</summary>
    [Fact]
    public void LinksReorderedColorAndUvFieldsAndAllowsUnusedVertexOutput() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float4 vertexColor : $vout.TEX0 : TEX0 : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX1 : TEX1 : -1 : 1\n"
            + "#var float3 unusedNormal : $vout.TEX2 : TEX2 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX1 : TEX1 : -1 : 1\n"
            + "#var float4 pixelColor : $vin.TEX0 : TEX0 : -1 : 1\n";
        Ps3ShaderInterfaceVariable vertexColor = Interface("COLOR0", "vertexColor", 4);
        Ps3ShaderInterfaceVariable vertexUv = Interface("TEXCOORD0", "vertexUv", 2);
        Ps3ShaderInterfaceVariable unusedNormal = Interface("NORMAL0", "unusedNormal", 3);
        Ps3ShaderInterfaceVariable pixelUv = Interface("TEXCOORD0", "pixelUv", 2);
        Ps3ShaderInterfaceVariable pixelColor = Interface("COLOR0", "pixelColor", 4);

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { vertexColor, vertexUv, unusedNormal }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, new[] { pixelUv, pixelColor }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump));

        Assert.Equal(new[] { "TEXCOORD0", "COLOR0" }, map.StageLinks.Select(link => link.AuthoredSemantic));
        Assert.Equal(new[] { "TEX1", "TEX0" }, map.StageLinks.Select(link => link.VertexChannel));
        Assert.DoesNotContain(map.StageLinks, link => link.AuthoredSemantic == "NORMAL0");
    }

    /// <summary>Rejects equal numeric stage-local indices when their canonical RSX channels differ.</summary>
    [Fact]
    public void RejectsEqualRawSlotsWhenCanonicalChannelsDiffer() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX0 : TEX0 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX3 : TEX3 : -1 : 1\n";

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, new[] { Interface("TEXCOORD0", "pixelUv", 2) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump)));
    }

    /// <summary>Rejects a fragment input with no vertex producer for its authored semantic.</summary>
    [Fact]
    public void RejectsPixelInputWithoutVertexProducer() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX0 : TEX0 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float4 pixelColor : $vin.TEX0 : TEX0 : -1 : 1\n";

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, new[] { Interface("COLOR0", "pixelColor", 4) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump)));
    }

    /// <summary>Rejects an inter-stage scalar type outside the supported FP3232 varying subset.</summary>
    [Fact]
    public void RejectsUnsupportedPixelVaryingScalarType() {
        Assert.Throws<NotSupportedException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { new Ps3ShaderInterfaceVariable("TEXCOORD0", 0, "SINT32", 2, 1, 1, "smooth", "pixelUv") }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float2 pixelUv"))));
    }

    /// <summary>Rejects source reflection whose scalar type cannot be reconciled with the actual float Cg declaration.</summary>
    [Fact]
    public void RejectsUnsupportedScalarTypeEvenWhenBothStagesAgree() {
        Ps3ShaderInterfaceVariable vertexOutput = new("TEXCOORD0", 0, "SINT32", 2, 1, 1, "smooth", "vertexUv");
        Ps3ShaderInterfaceVariable pixelInput = new("TEXCOORD0", 0, "SINT32", 2, 1, 1, "smooth", "pixelUv");

        Assert.Throws<NotSupportedException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { vertexOutput }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { pixelInput }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float2 pixelUv"))));
    }

    /// <summary>Rejects a varying whose vector width differs between vertex and pixel stages.</summary>
    [Fact]
    public void RejectsVaryingWidthMismatch() {
        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { Interface("TEXCOORD0", "pixelUv", 3) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float3 pixelUv"))));
    }

    /// <summary>Rejects a varying whose interpolation mode differs between vertex and pixel stages.</summary>
    [Fact]
    public void RejectsVaryingInterpolationMismatch() {
        Ps3ShaderInterfaceVariable flatPixelUv = new("TEXCOORD0", 0, "FP3232", 2, 1, 1, "flat", "pixelUv");

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { flatPixelUv }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float2 pixelUv"))));
    }

    /// <summary>Rejects matching authored semantics routed to different hardware channels with different raw indices.</summary>
    [Fact]
    public void RejectsMatchingSemanticWithMismatchedHardwareChannels() {
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX1 : TEX1 : -1 : 1\n";

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { Interface("TEXCOORD0", "pixelUv", 2) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump)));
    }

    /// <summary>Does not demand an authored varying producer for the compiler's fixed-function WPOS input.</summary>
    [Fact]
    public void DoesNotLinkPixelWposBuiltinAsUserVarying() {
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float4 fragment.position : $vin.WPOS : WPOS : -1 : 1\n";
        Ps3ShaderTranslation pixelSource = new(
            ShaderStage.Pixel,
            "pixel source",
            new[] { "FragCoord" },
            Array.Empty<string>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>());

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, VertexPositionDump()),
            pixelSource,
            Program(ShaderStage.Pixel, pixelDump));

        Assert.Empty(map.StageLinks);
    }

    /// <summary>Maps active vertex attributes through their generated names to the actual SDK attribute parameters.</summary>
    [Fact]
    public void MapsAuthoredVertexAttributeToItsSdkParameter() {
        Ps3ShaderInterfaceVariable position = new("POSITION", 0, "FP3232", 3, 1, 1, "smooth", "positionInput");
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float3 positionInput : $vin.ATTR0 : ATTR0 : -1 : 1\n";
        Ps3RsxParameter parameter = new("positionInput", Ps3RsxParameterCategory.Attribute, 0, 1, Ps3RsxDataType.Float3, Array.Empty<Ps3RsxConstantElement>());

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, new[] { position }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, vertexDump, parameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Ps3ShaderAttributeBinding binding = Assert.Single(map.Attributes);
        Assert.Equal("POSITION", binding.AuthoredSemantic);
        Assert.Equal(0u, binding.HardwareSlot);
        Assert.Same(parameter, binding.RsxParameter);
    }

    /// <summary>Maps duplicate authored member paths by their separate buffer identities and exact FP3232 metadata.</summary>
    [Fact]
    public void MapsSameNamedUniformMembersFromDifferentBuffersSeparately() {
        Ps3ShaderUniformMember primary = Uniform("Primary", 1, "Tint", "primaryTint");
        Ps3ShaderUniformMember secondary = Uniform("Secondary", 2, "Tint", "secondaryTint");
        Ps3RsxParameter primaryParameter = VertexConstant("primaryTint", Ps3RsxDataType.Float4, 1, 2);
        Ps3RsxParameter secondaryParameter = VertexConstant("secondaryTint", Ps3RsxDataType.Float4, 1, 3);
        string vertexDump = VertexPositionLine + "#var float4 primaryTint :  : c[2] : -1 : 1\n"
            + "#var float4 secondaryTint :  : c[3] : -1 : 1\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { primary, secondary }),
            Program(ShaderStage.Vertex, vertexDump, primaryParameter, secondaryParameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Assert.Equal(2, map.Uniforms.Count);
        Assert.Equal(new[] { "Primary", "Secondary" }, map.Uniforms.Select(binding => binding.AuthoredBufferName));
        Assert.All(map.Uniforms, binding => Assert.False(string.IsNullOrWhiteSpace(binding.AuthoredMemberPath)));
        Assert.Equal(new[] { primaryParameter, secondaryParameter }, map.Uniforms.Select(binding => binding.RsxParameter));
    }

    /// <summary>Uses physical SPIR-V matrix columns and vector width to reconcile the SDK constant-vector count.</summary>
    [Fact]
    public void MapsPhysicalFloat3x4MatrixToThreeVertexConstantRegisters() {
        Ps3ShaderUniformMember matrix = new("Frame", 0, "RowMajorTransform", 80, 48, 0, 16, false,
            "physicalMatrix", "FP3232", 4, 3, 1);
        Ps3RsxParameter parameter = VertexConstant("physicalMatrix", Ps3RsxDataType.Float3x4, 3, 4);
        string vertexDump = VertexPositionLine + "#var float3x4 physicalMatrix :  : c[4], 3 : -1 : 1\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { matrix }),
            Program(ShaderStage.Vertex, vertexDump, parameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Ps3ShaderUniformBinding binding = Assert.Single(map.Uniforms);
        Assert.Equal("FP3232", binding.SourceMember.ScalarType);
        Assert.Equal(3u, binding.SourceMember.Columns);
        Assert.Equal(4u, binding.SourceMember.VectorWidth);
        Assert.Equal(new uint?[] { 4, 5, 6 }, binding.RsxParameter.Elements.Select(element => element.VertexRegister));
    }

    /// <summary>Allows a translation member to disappear when Cg marks the corresponding declaration inactive.</summary>
    [Fact]
    public void OmitsOptimizedOutUniformMemberWithoutInventingAParameter() {
        Ps3ShaderUniformMember tint = Uniform("Material", 1, "Tint", "inactiveTint");
        string vertexDump = VertexPositionLine + "#var float4 inactiveTint :  : c[0] : -1 : 0\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { tint }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Assert.Empty(map.Uniforms);
    }

    /// <summary>Maps two combined samplers that share a texture but use distinct authored sampler identities and RSX units.</summary>
    [Fact]
    public void MapsTextureSamplerPairsToDistinctSdkTextureUnits() {
        Ps3ShaderSamplerBinding linear = new("Albedo", 0, "LinearSampler", 0, "sampleLinear", "Dim2D");
        Ps3ShaderSamplerBinding point = new("Albedo", 0, "PointSampler", 1, "samplePoint", "Dim2D");
        Ps3RsxParameter linearParameter = new("sampleLinear", Ps3RsxParameterCategory.Sampler, 0, 1, Ps3RsxDataType.Sampler2D, Array.Empty<Ps3RsxConstantElement>());
        Ps3RsxParameter pointParameter = new("samplePoint", Ps3RsxParameterCategory.Sampler, 1, 1, Ps3RsxDataType.Sampler2D, Array.Empty<Ps3RsxConstantElement>());
        string pixelDump = PixelOutputLine
            + "#var sampler2D sampleLinear :  : texunit 0 : -1 : 1\n"
            + "#var sampler2D samplePoint :  : texunit 1 : -1 : 1\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, VertexPositionDump()),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), samplers: new[] { linear, point }),
            Program(ShaderStage.Pixel, pixelDump, linearParameter, pointParameter));

        Assert.Equal(2, map.Samplers.Count);
        Assert.Equal(new[] { "LinearSampler", "PointSampler" }, map.Samplers.Select(binding => binding.SamplerName));
        Assert.Equal(new uint[] { 0, 1 }, map.Samplers.Select(binding => binding.TextureUnit));
        Assert.Equal(new[] { linearParameter, pointParameter }, map.Samplers.Select(binding => binding.RsxParameter));
    }

    /// <summary>Rejects unsupported active output routes instead of silently omitting them from the validated map.</summary>
    [Fact]
    public void RejectsUnknownActiveOutputRoute() {
        string vertexDump = VertexPositionLine + "#var float2 outputUv : $vout.MYSTERY : MYSTERY : -1 : 1\n";
        Ps3ShaderTranslation vertex = Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(),
            new[] { Interface("TEXCOORD0", "outputUv", 2) });

        Assert.Throws<InvalidOperationException>(() => Map(
            vertex,
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump())));
    }

    /// <summary>Rejects a vertex source translation that omits its required built-in Position output.</summary>
    [Fact]
    public void RequiresReflectedPositionBuiltin() {
        Ps3ShaderTranslation vertexSource = new(
            ShaderStage.Vertex,
            "vertex source",
            Array.Empty<string>(),
            Array.Empty<string>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>());

        Assert.Throws<InvalidOperationException>(() => Map(
            vertexSource,
            Program(ShaderStage.Vertex, VertexPositionDump()),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump())));
    }

    /// <summary>Creates a standard authored interface record for a 32-bit floating-point vector.</summary>
    /// <param name="semantic">HLSL semantic.</param>
    /// <param name="generatedName">Compiler-generated identifier used in the Cg dump.</param>
    /// <param name="width">Number of reflected scalar components.</param>
    /// <returns>The requested interface metadata.</returns>
    static Ps3ShaderInterfaceVariable Interface(string semantic, string generatedName, uint width) {
        return new Ps3ShaderInterfaceVariable(semantic, 0, "FP3232", width, 1, 1, "smooth", generatedName);
    }

    /// <summary>Creates a translation with explicit interface, uniform and sampler metadata.</summary>
    /// <param name="stage">Stage represented by the translation.</param>
    /// <param name="inputs">Reflected active stage inputs.</param>
    /// <param name="outputs">Reflected active stage outputs.</param>
    /// <param name="uniforms">Optional reflected active uniform members.</param>
    /// <param name="samplers">Optional reflected active combined samplers.</param>
    /// <returns>An immutable source translation fixture.</returns>
    static Ps3ShaderTranslation Translation(ShaderStage stage, Ps3ShaderInterfaceVariable[] inputs, Ps3ShaderInterfaceVariable[] outputs,
        Ps3ShaderUniformMember[] uniforms = null, Ps3ShaderSamplerBinding[] samplers = null) {
        return new Ps3ShaderTranslation(stage, "shader source", Array.Empty<string>(), stage == ShaderStage.Vertex ? new[] { "Position" } : Array.Empty<string>(),
            inputs, outputs, uniforms ?? Array.Empty<Ps3ShaderUniformMember>(), samplers ?? Array.Empty<Ps3ShaderSamplerBinding>(), Array.Empty<Ps3ShaderSourceDependency>());
    }

    /// <summary>Creates a parsed-program fixture around explicit Cg dump and RSX parameter data.</summary>
    /// <param name="stage">Expected RSX program stage.</param>
    /// <param name="dump">Cg profile and variable declarations.</param>
    /// <param name="parameters">Explicit SDK parameter records.</param>
    /// <returns>The requested stage program fixture.</returns>
    static Ps3RsxProgram Program(ShaderStage stage, string dump, params Ps3RsxParameter[] parameters) {
        return new Ps3RsxProgram(stage, Array.Empty<byte>(), dump, "test-sdk", parameters);
    }

    /// <summary>Builds the authored-interface reconciliation result for explicit stage fixtures.</summary>
    /// <param name="vertexSource">Vertex source metadata.</param>
    /// <param name="vertexProgram">Vertex SDK and dump fixture.</param>
    /// <param name="pixelSource">Pixel source metadata.</param>
    /// <param name="pixelProgram">Pixel SDK and dump fixture.</param>
    /// <returns>The validated binding map.</returns>
    static Ps3ShaderBindingMap Map(Ps3ShaderTranslation vertexSource, Ps3RsxProgram vertexProgram,
        Ps3ShaderTranslation pixelSource, Ps3RsxProgram pixelProgram) {
        return new Ps3ShaderBindingMapBuilder().Build(vertexSource, vertexProgram, pixelSource, pixelProgram);
    }

    /// <summary>Creates one float4 uniform metadata record with an exact generated Cg identifier.</summary>
    /// <param name="bufferName">Authored constant-buffer name.</param>
    /// <param name="bufferBinding">Authored constant-buffer binding.</param>
    /// <param name="memberPath">Authored member path.</param>
    /// <param name="generatedName">Compiler-generated Cg parameter name.</param>
    /// <returns>The requested uniform member metadata.</returns>
    static Ps3ShaderUniformMember Uniform(string bufferName, uint bufferBinding, string memberPath, string generatedName) {
        return new Ps3ShaderUniformMember(bufferName, bufferBinding, memberPath, 0, 16, 0, 0, false, generatedName, "FP3232", 4, 1, 1);
    }

    /// <summary>Creates one named vertex constant parameter with its physical register list.</summary>
    /// <param name="name">Exact Cg parameter name.</param>
    /// <param name="dataType">Pinned SDK constant type.</param>
    /// <param name="elementCount">Physical constant vector count.</param>
    /// <param name="firstRegister">First vertex constant register.</param>
    /// <returns>The requested SDK constant parameter.</returns>
    static Ps3RsxParameter VertexConstant(string name, Ps3RsxDataType dataType, uint elementCount, uint firstRegister) {
        List<Ps3RsxConstantElement> elements = new();
        for (uint index = 0; index < elementCount; index++) {
            elements.Add(new Ps3RsxConstantElement(index, firstRegister + index, Array.Empty<uint>(), new uint[4]));
        }
        return new Ps3RsxParameter(name, Ps3RsxParameterCategory.Constant, null, elementCount, dataType, elements);
    }

    /// <summary>Creates a vertex dump with a required position route and one TEX0 output.</summary>
    /// <param name="outputDeclaration">Full float varying declaration prefix and name.</param>
    /// <returns>A complete minimal vp40 dump.</returns>
    static string VertexTex0Dump(string outputDeclaration) {
        return VertexPositionLine + $"#var {outputDeclaration} : $vout.TEX0 : TEX0 : -1 : 1\n";
    }

    /// <summary>Creates a pixel dump with its fixed-function color output and one TEX0 input.</summary>
    /// <param name="inputDeclaration">Full float varying declaration prefix and name.</param>
    /// <returns>A complete minimal fp40 dump.</returns>
    static string PixelTex0Dump(string inputDeclaration) {
        return PixelOutputLine + $"#var {inputDeclaration} : $vin.TEX0 : TEX0 : -1 : 1\n";
    }

    /// <summary>Creates a valid vertex dump containing only fixed-function position.</summary>
    /// <returns>A minimal vp40 dump.</returns>
    static string VertexPositionDump() {
        return VertexPositionLine;
    }

    /// <summary>Creates a valid pixel dump containing only the fixed-function color output.</summary>
    /// <returns>A minimal fp40 dump.</returns>
    static string PixelOutputDump() {
        return PixelOutputLine;
    }

    /// <summary>Gets the pinned SDK's active vertex position declaration line.</summary>
    static string VertexPositionLine => "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n";

    /// <summary>Gets the pinned SDK's active fragment color output declaration line.</summary>
    static string PixelOutputLine => "#profile fp40\n#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n";

    /// <summary>Builds the smallest SDK vertex container with a valid aligned microcode slot.</summary>
    /// <returns>A structurally valid one-instruction program with no parameter tables.</returns>
    static byte[] CreatePositionOnlyVertexProgram() {
        byte[] bytecode = new byte[64];
        bytecode[0] = 0x56;
        bytecode[1] = 0x50;
        bytecode[10] = 0;
        bytecode[11] = 1;
        bytecode[15] = 36;
        bytecode[19] = 36;
        bytecode[23] = 48;
        return bytecode;
    }
}

```
