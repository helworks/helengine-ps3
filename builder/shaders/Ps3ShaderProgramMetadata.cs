using helengine;
using System.Globalization;

namespace helengine.ps3.builder;

/// <summary>Maps source-derived PS3 reflection into shared logical bindings and stage signatures.</summary>
internal static class Ps3ShaderProgramMetadata {
    /// <summary>Creates shared metadata directly from immutable compiler reflection without rewriting that reflection.</summary>
    /// <param name="request">Original request carrying program identity, variant, defines and binding policy.</param>
    /// <param name="translation">Source compiler output whose physical reflection remains authoritative.</param>
    /// <returns>Shared logical metadata for one requested stage.</returns>
    internal static ShaderProgramDefinition Create(ShaderCompileRequest request, Ps3ShaderTranslation translation) {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(translation);
        if (request.Target != ShaderCompileTarget.Ps3) {
            throw new NotSupportedException("PS3 shared metadata requires a PS3 compilation request.");
        }
        if (request.Stage != translation.Stage) {
            throw new InvalidOperationException("The PS3 source translation stage does not match the shared request.");
        }

        ShaderBindingPolicy policy = request.Options.BindingPolicy;
        if (policy.DefaultSpace != 0) {
            throw new NotSupportedException("PS3 shared metadata supports descriptor space 0 only.");
        }

        List<ShaderBinding> bindings = new();
        AddUniformBufferBindings(translation, policy, bindings);
        AddSampledResourceBindings(translation, policy, bindings);

        ShaderVertexElement[] inputs = CreateInterfaceElements(translation.Inputs, "input");
        ShaderVertexElement[] outputs = CreateInterfaceElements(translation.Outputs, "output");
        string[] defines = request.Defines
            .Select(define => define.Value.Length == 0 ? define.Name : define.Name + "=" + define.Value)
            .ToArray();
        ShaderVariant[] variants = [new ShaderVariant(request.Variant, defines)];
        return new ShaderProgramDefinition(
            request.ProgramName,
            request.Stage,
            request.EntryPoint,
            bindings.ToArray(),
            inputs,
            outputs,
            variants);
    }

    /// <summary>Creates one shared constant-buffer binding for every reflected declared buffer.</summary>
    /// <param name="translation">Physical source translation.</param>
    /// <param name="policy">Shared resource-class shifts.</param>
    /// <param name="bindings">Destination logical binding collection.</param>
    static void AddUniformBufferBindings(Ps3ShaderTranslation translation, ShaderBindingPolicy policy,
        List<ShaderBinding> bindings) {
        Dictionary<string, uint> registersByName = new(StringComparer.Ordinal);
        Dictionary<uint, string> namesByRegister = new();
        foreach (Ps3ShaderUniformBuffer buffer in translation.UniformBuffers) {
            if (buffer == null) {
                throw new InvalidOperationException("PS3 uniform-buffer reflection contains a null buffer record.");
            }
            RegisterLogicalResource(buffer.BufferName, buffer.BufferBinding, "constant buffer",
                registersByName, namesByRegister, false);

            int bufferSize = checked((int)buffer.ByteSize);
            int slot = CheckedSlot(policy, ShaderResourceType.ConstantBuffer, buffer.BufferBinding,
                "constant buffer '" + buffer.BufferName + "'");
            List<ShaderConstantMember> members = new();
            foreach (Ps3ShaderUniformMember member in translation.UniformMembers) {
                if (member.BufferBinding == buffer.BufferBinding && member.BufferName == buffer.BufferName) {
                    int offset = checked((int)member.ByteOffset);
                    int size = checked((int)member.ByteSize);
                    if (size <= 0) {
                        throw new InvalidOperationException(
                            $"PS3 constant-buffer member '{member.MemberPath}' in '{buffer.BufferName}' has no positive declared size.");
                    }
                    int end = checked(offset + size);
                    if (end > bufferSize) {
                        throw new InvalidOperationException(
                            $"PS3 constant-buffer member '{member.MemberPath}' in '{buffer.BufferName}' extends past its declared {bufferSize}-byte structure.");
                    }
                    string type = GetFloatType(member.ScalarType, member.VectorWidth, member.Columns,
                        "constant-buffer member '" + member.MemberPath + "' in '" + buffer.BufferName + "'");
                    members.Add(new ShaderConstantMember(member.MemberPath, type, offset, size));
                }
            }

            bindings.Add(new ShaderBinding(buffer.BufferName, ShaderResourceType.ConstantBuffer,
                0, slot, bufferSize, members.ToArray()));
        }

        foreach (Ps3ShaderUniformMember member in translation.UniformMembers) {
            bool hasBuffer = translation.UniformBuffers.Any(buffer =>
                buffer.BufferName == member.BufferName && buffer.BufferBinding == member.BufferBinding);
            if (!hasBuffer) {
                throw new InvalidOperationException(
                    $"PS3 uniform member '{member.MemberPath}' refers to missing buffer '{member.BufferName}' at binding {member.BufferBinding}.");
            }
        }
    }

    /// <summary>Deduplicates authored textures and samplers while retaining every combined pair in the translation.</summary>
    /// <param name="translation">Physical source translation retaining combined sampler records.</param>
    /// <param name="policy">Shared resource-class shifts.</param>
    /// <param name="bindings">Destination logical binding collection.</param>
    static void AddSampledResourceBindings(Ps3ShaderTranslation translation, ShaderBindingPolicy policy,
        List<ShaderBinding> bindings) {
        Dictionary<string, uint> textureRegistersByName = new(StringComparer.Ordinal);
        Dictionary<uint, string> textureNamesByRegister = new();
        Dictionary<string, ShaderResourceType> textureTypesByName = new(StringComparer.Ordinal);
        Dictionary<string, uint> samplerRegistersByName = new(StringComparer.Ordinal);
        Dictionary<uint, string> samplerNamesByRegister = new();

        foreach (Ps3ShaderSamplerBinding sampler in translation.Samplers) {
            if (sampler == null) {
                throw new InvalidOperationException("PS3 sampler reflection contains a null combined-sampler record.");
            }
            ShaderResourceType textureType = GetTextureType(sampler.TextureName, sampler.Dimension);
            bool newTexture = RegisterLogicalResource(sampler.TextureName, sampler.TextureBinding, "texture",
                textureRegistersByName, textureNamesByRegister, true);
            if (textureTypesByName.TryGetValue(sampler.TextureName, out ShaderResourceType previousType)
                && previousType != textureType) {
                throw new InvalidOperationException(
                    $"PS3 texture '{sampler.TextureName}' at binding {sampler.TextureBinding} has conflicting reflected dimensions.");
            }
            textureTypesByName[sampler.TextureName] = textureType;
            if (newTexture) {
                int textureSlot = CheckedSlot(policy, textureType, sampler.TextureBinding,
                    "texture '" + sampler.TextureName + "'");
                bindings.Add(new ShaderBinding(sampler.TextureName, textureType, 0, textureSlot, 0,
                    Array.Empty<ShaderConstantMember>()));
            }

            bool newSampler = RegisterLogicalResource(sampler.SamplerName, sampler.SamplerBinding, "sampler",
                samplerRegistersByName, samplerNamesByRegister, true);
            if (newSampler) {
                int samplerSlot = CheckedSlot(policy, ShaderResourceType.Sampler, sampler.SamplerBinding,
                    "sampler '" + sampler.SamplerName + "'");
                bindings.Add(new ShaderBinding(sampler.SamplerName, ShaderResourceType.Sampler, 0,
                    samplerSlot, 0, Array.Empty<ShaderConstantMember>()));
            }
        }
    }

    /// <summary>Registers one resource identity and rejects name/register ambiguity in its own register class.</summary>
    /// <param name="name">Compiler-preserved authored resource name.</param>
    /// <param name="register">Physical source register.</param>
    /// <param name="resourceKind">Resource class used in diagnostics.</param>
    /// <param name="registersByName">Existing register for each authored name.</param>
    /// <param name="namesByRegister">Existing authored name for each register.</param>
    /// <param name="deduplicateExactIdentity">Whether an identical logical resource may recur in pair reflection.</param>
    /// <returns>True when this is the first occurrence of the identity.</returns>
    static bool RegisterLogicalResource(string name, uint register, string resourceKind,
        Dictionary<string, uint> registersByName, Dictionary<uint, string> namesByRegister,
        bool deduplicateExactIdentity) {
        if (registersByName.TryGetValue(name, out uint previousRegister)) {
            if (previousRegister != register) {
                throw new InvalidOperationException(
                    $"PS3 {resourceKind} '{name}' is reflected at conflicting registers {previousRegister} and {register}.");
            }
            if (!deduplicateExactIdentity) {
                throw new InvalidOperationException(
                    $"PS3 {resourceKind} '{name}' at register {register} is reflected more than once.");
            }
            return false;
        }
        if (namesByRegister.TryGetValue(register, out string previousName)
            && !string.Equals(previousName, name, StringComparison.Ordinal)) {
            throw new InvalidOperationException(
                $"PS3 {resourceKind}s '{previousName}' and '{name}' claim the same register {register}.");
        }
        registersByName.Add(name, register);
        namesByRegister[register] = name;
        return true;
    }

    /// <summary>Converts a supported SPIR-V image dimension into a shared texture resource type.</summary>
    /// <param name="textureName">Authored texture identity for diagnostics.</param>
    /// <param name="dimension">SPIRV-Cross physical image-dimension token.</param>
    /// <returns>Shared two-dimensional or cube texture type.</returns>
    static ShaderResourceType GetTextureType(string textureName, string dimension) {
        return dimension switch {
            "Dim2D" => ShaderResourceType.Texture2D,
            "DimCube" => ShaderResourceType.TextureCube,
            _ => throw new NotSupportedException(
                $"PS3 texture '{textureName}' has unsupported reflected dimension '{dimension}'.")
        };
    }

    /// <summary>Validates scalar and physical dimensions and names a supported floating-point type.</summary>
    /// <param name="scalarType">SPIR-V scalar base type and width.</param>
    /// <param name="vectorWidth">Physical scalar components per vector.</param>
    /// <param name="columns">Physical SPIR-V matrix column count.</param>
    /// <param name="identity">Reflected resource identity for diagnostics.</param>
    /// <returns>Shared float, vector or physical matrix type name.</returns>
    static string GetFloatType(string scalarType, uint vectorWidth, uint columns, string identity) {
        if (!string.Equals(scalarType, "FP3232", StringComparison.Ordinal)) {
            throw new NotSupportedException(
                $"PS3 {identity} uses unsupported scalar type '{scalarType}'; only FP3232 is supported.");
        }
        if (vectorWidth < 1 || vectorWidth > 4 || columns < 1 || columns > 4) {
            throw new NotSupportedException(
                $"PS3 {identity} has unsupported physical float dimensions {vectorWidth}x{columns}; widths and columns must be from 1 through 4.");
        }
        if (columns > 1) {
            return "float" + vectorWidth.ToString(CultureInfo.InvariantCulture)
                + "x" + columns.ToString(CultureInfo.InvariantCulture);
        }
        return vectorWidth == 1
            ? "float"
            : "float" + vectorWidth.ToString(CultureInfo.InvariantCulture);
    }

    /// <summary>Builds shared stage signature elements from reflected authored user interfaces only.</summary>
    /// <param name="variables">Compiler-reflected active stage inputs or outputs.</param>
    /// <param name="direction">Interface direction used in diagnostics.</param>
    /// <returns>Shared signature elements in compiler reflection order.</returns>
    static ShaderVertexElement[] CreateInterfaceElements(IReadOnlyList<Ps3ShaderInterfaceVariable> variables,
        string direction) {
        ShaderVertexElement[] elements = new ShaderVertexElement[variables.Count];
        for (int index = 0; index < variables.Count; index++) {
            Ps3ShaderInterfaceVariable variable = variables[index];
            string identity = direction + " semantic '" + variable.Semantic + "'";
            if (variable.Columns != 1 || variable.ArrayLength != 1) {
                throw new NotSupportedException(
                    $"PS3 {identity} must be a non-array scalar or vector with one physical column.");
            }
            string type = GetFloatType(variable.ScalarType, variable.VectorWidth, variable.Columns, identity);
            SplitSemanticIndex(variable.Semantic, out string semantic, out int semanticIndex);
            elements[index] = new ShaderVertexElement(semantic, semanticIndex, type);
        }
        return elements;
    }

    /// <summary>Splits a trailing decimal semantic suffix while retaining unsuffixed identity at index zero.</summary>
    /// <param name="semantic">Compiler-preserved authored HLSL semantic.</param>
    /// <param name="semanticName">Semantic prefix without a trailing decimal suffix.</param>
    /// <param name="semanticIndex">Checked suffix value, or zero when the semantic is unsuffixed.</param>
    static void SplitSemanticIndex(string semantic, out string semanticName, out int semanticIndex) {
        int digitStart = semantic.Length;
        while (digitStart > 0 && semantic[digitStart - 1] >= '0' && semantic[digitStart - 1] <= '9') {
            digitStart--;
        }
        if (digitStart == semantic.Length) {
            semanticName = semantic;
            semanticIndex = 0;
            return;
        }
        if (digitStart == 0 || !int.TryParse(semantic.AsSpan(digitStart), NumberStyles.None,
                CultureInfo.InvariantCulture, out semanticIndex)) {
            throw new NotSupportedException($"PS3 interface semantic '{semantic}' has an invalid or overflowing decimal index.");
        }
        semanticName = semantic[..digitStart];
    }

    /// <summary>Checks both the register conversion and shifted slot before the shared unchecked policy method.</summary>
    /// <param name="policy">Validated shared binding policy.</param>
    /// <param name="type">Shared type used to select the resource-class shift.</param>
    /// <param name="sourceBinding">Unsigned physical SPIR-V binding.</param>
    /// <param name="identity">Resource identity included in overflow diagnostics.</param>
    /// <returns>The policy's logical slot after checked arithmetic proves the sum is representable.</returns>
    static int CheckedSlot(ShaderBindingPolicy policy, ShaderResourceType type, uint sourceBinding,
        string identity) {
        int register = checked((int)sourceBinding);
        int shift = type switch {
            ShaderResourceType.ConstantBuffer => policy.ConstantBufferShift,
            ShaderResourceType.Texture2D or ShaderResourceType.TextureCube => policy.TextureShift,
            ShaderResourceType.Sampler => policy.SamplerShift,
            _ => throw new ArgumentOutOfRangeException(nameof(type), "Unsupported PS3 logical resource class.")
        };
        try {
            _ = checked(register + shift);
        } catch (OverflowException exception) {
            throw new OverflowException($"PS3 binding for {identity} overflows after its {shift}-slot shift.", exception);
        }
        return policy.GetSlot(type, register);
    }
}
