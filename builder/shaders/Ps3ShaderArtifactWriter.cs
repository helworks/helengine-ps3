using System.Buffers.Binary;
using System.IO;
using System.Text;
using helengine;

namespace helengine.ps3.builder;

/// <summary>Serializes a validated shader pair into the deterministic, bounded P3SP version 1 format.</summary>
public static class Ps3ShaderArtifactWriter {
    /// <summary>Maximum number of bytes in an encoded artifact string.</summary>
    public const int MaximumStringBytes = 65536;

    /// <summary>Maximum number of bytes copied from either SDK stage program.</summary>
    public const int MaximumStageBytes = 1024 * 1024;

    /// <summary>Maximum number of records in any serialized metadata list.</summary>
    public const int MaximumListCount = 4096;

    /// <summary>Maximum number of bytes in a complete version 1 artifact.</summary>
    public const int MaximumArtifactBytes = 16 * 1024 * 1024;

    /// <summary>Validates and serializes a vertex and pixel program pair without writing to the filesystem.</summary>
    /// <param name="identity">Required authored shader identity.</param>
    /// <param name="sourceHash">Required source content hash recorded in the body.</param>
    /// <param name="vertexEntryPoint">Required vertex entry point.</param>
    /// <param name="pixelEntryPoint">Required pixel entry point.</param>
    /// <param name="pair">Validated immutable programs, translations and binding map.</param>
    /// <returns>A new byte array containing one complete P3SP version 1 artifact.</returns>
    public static byte[] Write(Ps3ShaderArtifactIdentity identity, string sourceHash, string vertexEntryPoint,
        string pixelEntryPoint, Ps3ShaderProgramPair pair) {
        ArgumentNullException.ThrowIfNull(identity);
        ArgumentNullException.ThrowIfNull(pair);
        ValidateRequired(sourceHash, "sourceHash");
        ValidateRequired(vertexEntryPoint, "vertexEntryPoint");
        ValidateRequired(pixelEntryPoint, "pixelEntryPoint");
        ValidatePair(identity, pair);

        using MemoryStream stream = new();
        WriteBytes(stream, [0x50, 0x33, 0x53, 0x50]);
        WriteU32(stream, 1);
        WriteU32(stream, 0);
        WriteText(stream, identity, identity.ShaderAssetId, "shaderAssetId");
        WriteText(stream, identity, identity.VertexProgramName, "vertexProgramName");
        WriteText(stream, identity, identity.PixelProgramName, "pixelProgramName");
        WriteText(stream, identity, identity.VariantName, "variantName");
        WriteText(stream, identity, sourceHash, "sourceHash");
        WriteText(stream, identity, pair.Vertex.ToolchainIdentity, "toolchainIdentity");
        WriteStage(stream, identity, pair, ShaderStage.Vertex, vertexEntryPoint);
        WriteStage(stream, identity, pair, ShaderStage.Pixel, pixelEntryPoint);
        WriteU32(stream, Count(pair.Bindings.Attributes.Count, identity, "attributes"));
        foreach (Ps3ShaderAttributeBinding attribute in pair.Bindings.Attributes) WriteAttribute(stream, identity, attribute);
        WriteU32(stream, Count(pair.Bindings.Samplers.Count, identity, "pixel samplers"));
        foreach (Ps3ShaderSamplerMapEntry sampler in pair.Bindings.Samplers) WriteSampler(stream, identity, sampler);

        uint totalLength = checked((uint)stream.Length);
        BinaryPrimitives.WriteUInt32BigEndian(stream.GetBuffer().AsSpan(8, 4), totalLength);
        return stream.ToArray();
    }

    /// <summary>Checks the immutable pair's stages, toolchain, resources, mappings and runtime subset.</summary>
    /// <param name="identity">Identity used to contextualize diagnostics.</param>
    /// <param name="pair">Pair to validate.</param>
    static void ValidatePair(Ps3ShaderArtifactIdentity identity, Ps3ShaderProgramPair pair) {
        if (pair.Vertex is null || pair.Pixel is null || pair.VertexTranslation is null || pair.PixelTranslation is null || pair.Bindings is null) {
            throw Invalid(identity, "pair", "contains a missing program, translation, or binding map");
        }
        if (pair.Vertex.Stage != ShaderStage.Vertex || pair.Pixel.Stage != ShaderStage.Pixel
            || pair.VertexTranslation.Stage != ShaderStage.Vertex || pair.PixelTranslation.Stage != ShaderStage.Pixel) {
            throw Invalid(identity, "pair", "program and translation stages do not match the required vertex/pixel order");
        }
        ValidateResourceText(identity, pair.Vertex.ToolchainIdentity, "vertex toolchain identity");
        ValidateResourceText(identity, pair.Pixel.ToolchainIdentity, "pixel toolchain identity");
        if (!string.Equals(pair.Vertex.ToolchainIdentity, pair.Pixel.ToolchainIdentity, StringComparison.Ordinal)) {
            throw Invalid(identity, "toolchain", "vertex and pixel programs were produced by different toolchains");
        }
        ValidateCount(pair.Vertex.Parameters.Count, identity, "vertex SDK parameters");
        ValidateCount(pair.Pixel.Parameters.Count, identity, "pixel SDK parameters");
        ValidateCount(pair.VertexTranslation.UniformBuffers.Count, identity, "vertex buffers");
        ValidateCount(pair.PixelTranslation.UniformBuffers.Count, identity, "pixel buffers");
        ValidateCount(pair.VertexTranslation.Samplers.Count, identity, "vertex source samplers");
        ValidateCount(pair.PixelTranslation.Samplers.Count, identity, "pixel source samplers");
        ValidateCount(pair.Bindings.Uniforms.Count, identity, "uniform mappings");
        ValidateCount(pair.Bindings.Attributes.Count, identity, "attributes");
        ValidateCount(pair.Bindings.Samplers.Count, identity, "pixel samplers");
        foreach (Ps3ShaderUniformBinding binding in pair.Bindings.Uniforms) {
            if (binding is null) throw Invalid(identity, "uniform mapping", "is null");
            if (binding.Stage != ShaderStage.Vertex && binding.Stage != ShaderStage.Pixel) throw Invalid(identity, "uniform mapping", "has an unknown shader stage");
        }

        byte[] vertexBytes = pair.Vertex.Bytecode;
        byte[] pixelBytes = pair.Pixel.Bytecode;
        ValidateStageBlob(identity, "vertex", vertexBytes);
        ValidateStageBlob(identity, "pixel", pixelBytes);
        ValidateBuffers(identity, "vertex", pair.VertexTranslation.UniformBuffers);
        ValidateBuffers(identity, "pixel", pair.PixelTranslation.UniformBuffers);
        ValidateUniforms(identity, pair, ShaderStage.Vertex, pair.VertexTranslation.UniformBuffers);
        ValidateUniforms(identity, pair, ShaderStage.Pixel, pair.PixelTranslation.UniformBuffers);
        if (pair.VertexTranslation.Samplers.Count != 0) throw Invalid(identity, "vertex samplers", "are not supported by artifact version 1");
        ValidateAttributes(identity, pair.Bindings.Attributes);
        ValidateSamplers(identity, pair.Bindings.Samplers);
    }

    /// <summary>Validates stage uniform buffers and rejects duplicated authored names or bindings.</summary>
    /// <param name="identity">Identity used in diagnostics.</param>
    /// <param name="stageName">Human-readable stage name.</param>
    /// <param name="buffers">Declared buffers for the stage.</param>
    static void ValidateBuffers(Ps3ShaderArtifactIdentity identity, string stageName, IReadOnlyList<Ps3ShaderUniformBuffer> buffers) {
        HashSet<string> names = new(StringComparer.Ordinal);
        HashSet<uint> bindings = [];
        foreach (Ps3ShaderUniformBuffer buffer in buffers) {
            if (buffer is null) throw Invalid(identity, stageName + " buffer", "is null");
            ValidateResourceText(identity, buffer.BufferName, stageName + " buffer name");
            if (buffer.ByteSize == 0) throw Invalid(identity, stageName + " buffer " + buffer.BufferName, "has zero declared size");
            if (!names.Add(buffer.BufferName)) throw Invalid(identity, stageName + " buffer " + buffer.BufferName, "duplicates a buffer name");
            if (!bindings.Add(buffer.BufferBinding)) throw Invalid(identity, stageName + " buffer " + buffer.BufferName, "duplicates a buffer binding");
        }
    }

    /// <summary>Validates uniform-to-buffer references, physical ranges, shape metadata and RSX constant identities.</summary>
    /// <param name="identity">Identity used in diagnostics.</param>
    /// <param name="pair">Pair that owns uniform mappings.</param>
    /// <param name="stage">Stage whose uniform mappings are validated.</param>
    /// <param name="buffers">Declared buffers for that stage.</param>
    static void ValidateUniforms(Ps3ShaderArtifactIdentity identity, Ps3ShaderProgramPair pair, ShaderStage stage,
        IReadOnlyList<Ps3ShaderUniformBuffer> buffers) {
        string stageName = StageName(stage);
        HashSet<string> paths = new(StringComparer.Ordinal);
        List<Ps3ShaderUniformBinding> selected = pair.Bindings.Uniforms.Where(item => item.Stage == stage).ToList();
        ValidateCount(selected.Count, identity, stageName + " uniforms");
        foreach (Ps3ShaderUniformBinding binding in selected) {
            if (binding is null || binding.SourceMember is null || binding.RsxParameter is null) throw Invalid(identity, stageName + " uniform", "has a missing mapping component");
            Ps3ShaderUniformMember member = binding.SourceMember;
            string resource = stageName + " uniform " + member.MemberPath;
            ValidateResourceText(identity, member.BufferName, resource + " bufferName");
            ValidateResourceText(identity, member.MemberPath, resource + " memberPath");
            ValidateResourceText(identity, member.GeneratedName, resource + " generatedName");
            ValidateResourceText(identity, member.ScalarType, resource + " scalarType");
            if (member.ByteSize == 0 || member.VectorWidth == 0 || member.Columns == 0 || member.ArrayLength == 0) {
                throw Invalid(identity, resource, "has zero size or shape metadata");
            }
            Ps3ShaderUniformBuffer[] matchingBuffers = buffers.Where(buffer =>
                string.Equals(buffer.BufferName, member.BufferName, StringComparison.Ordinal) && buffer.BufferBinding == member.BufferBinding).ToArray();
            if (matchingBuffers.Length != 1) throw Invalid(identity, resource, "does not reference exactly one declared buffer by name and binding");
            uint end;
            try { end = checked(member.ByteOffset + member.ByteSize); }
            catch (OverflowException exception) { throw Invalid(identity, resource, "has an overflowing byte range", exception); }
            if (end > matchingBuffers[0].ByteSize) throw Invalid(identity, resource, "extends beyond its declared buffer size");
            if (!paths.Add(member.BufferName + "\0" + member.MemberPath)) throw Invalid(identity, resource, "duplicates a member path in its buffer and stage");
            Ps3RsxParameter parameter = binding.RsxParameter;
            if (parameter.Category != Ps3RsxParameterCategory.Constant) throw Invalid(identity, resource, "does not map to an SDK constant");
            ValidateResourceText(identity, parameter.Name, resource + " SDK name");
            if (!string.Equals(member.GeneratedName, parameter.Name, StringComparison.Ordinal)
                || !string.Equals(binding.GeneratedName, parameter.Name, StringComparison.Ordinal)) {
                throw Invalid(identity, resource, "generated name does not match its SDK parameter");
            }
        }
    }

    /// <summary>Validates mapped vertex attributes and uniqueness of their actual physical slots.</summary>
    /// <param name="identity">Identity used in diagnostics.</param>
    /// <param name="attributes">Mapped source attributes.</param>
    static void ValidateAttributes(Ps3ShaderArtifactIdentity identity, IReadOnlyList<Ps3ShaderAttributeBinding> attributes) {
        HashSet<uint> slots = [];
        foreach (Ps3ShaderAttributeBinding attribute in attributes) {
            if (attribute is null || attribute.SourceVariable is null || attribute.RsxParameter is null) throw Invalid(identity, "attribute", "has a missing mapping component");
            Ps3ShaderInterfaceVariable source = attribute.SourceVariable;
            Ps3RsxParameter parameter = attribute.RsxParameter;
            string resource = "attribute " + source.Semantic;
            ValidateResourceText(identity, source.Semantic, resource + " semantic");
            ValidateResourceText(identity, source.GeneratedName, resource + " generatedName");
            ValidateResourceText(identity, source.ScalarType, resource + " scalarType");
            ValidateResourceText(identity, parameter.Name, resource + " SDK name");
            if (source.VectorWidth == 0 || source.Columns == 0 || source.ArrayLength == 0) throw Invalid(identity, resource, "has zero shape metadata");
            if (parameter.Category != Ps3RsxParameterCategory.Attribute || !parameter.HardwareIndex.HasValue) throw Invalid(identity, resource, "does not map to an SDK attribute");
            if (attribute.HardwareSlot != parameter.HardwareIndex.Value || attribute.GeneratedName != parameter.Name || source.GeneratedName != parameter.Name) {
                throw Invalid(identity, resource, "generated name or hardware slot does not match its SDK parameter");
            }
            if (!slots.Add(attribute.HardwareSlot)) throw Invalid(identity, resource, "duplicates a physical hardware slot");
        }
    }

    /// <summary>Validates mapped pixel samplers, their dimensions, SDK identities and unique texture units.</summary>
    /// <param name="identity">Identity used in diagnostics.</param>
    /// <param name="samplers">Mapped pixel samplers.</param>
    static void ValidateSamplers(Ps3ShaderArtifactIdentity identity, IReadOnlyList<Ps3ShaderSamplerMapEntry> samplers) {
        HashSet<uint> units = [];
        foreach (Ps3ShaderSamplerMapEntry sampler in samplers) {
            if (sampler is null || sampler.SourceSampler is null || sampler.RsxParameter is null) throw Invalid(identity, "pixel sampler", "has a missing mapping component");
            Ps3ShaderSamplerBinding source = sampler.SourceSampler;
            Ps3RsxParameter parameter = sampler.RsxParameter;
            string resource = "pixel sampler " + source.TextureName + "/" + source.SamplerName;
            ValidateResourceText(identity, source.TextureName, resource + " textureName");
            ValidateResourceText(identity, source.SamplerName, resource + " samplerName");
            ValidateResourceText(identity, source.GeneratedName, resource + " generatedName");
            ValidateResourceText(identity, source.Dimension, resource + " dimension");
            ValidateResourceText(identity, parameter.Name, resource + " SDK name");
            if (parameter.Category != Ps3RsxParameterCategory.Sampler || !parameter.HardwareIndex.HasValue) throw Invalid(identity, resource, "does not map to an SDK sampler");
            if (source.Dimension != "Dim2D" && source.Dimension != "DimCube") {
                throw Invalid(identity, resource, "dimension " + source.Dimension + " is not supported by artifact version 1");
            }
            if (sampler.GeneratedName != parameter.Name || source.GeneratedName != parameter.Name || sampler.TextureUnit != parameter.HardwareIndex.Value) {
                throw Invalid(identity, resource, "generated name or texture unit does not match its SDK parameter");
            }
            if (!units.Add(sampler.TextureUnit)) throw Invalid(identity, resource, "duplicates a pixel texture unit");
        }
    }

    /// <summary>Writes one stage and its declared buffers and mapped uniforms in preserved source order.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="identity">Artifact identity for errors.</param>
    /// <param name="pair">Pair containing the selected stage data.</param>
    /// <param name="stage">Stage to write.</param>
    /// <param name="entryPoint">Stage entry point.</param>
    static void WriteStage(MemoryStream stream, Ps3ShaderArtifactIdentity identity, Ps3ShaderProgramPair pair,
        ShaderStage stage, string entryPoint) {
        bool vertex = stage == ShaderStage.Vertex;
        Ps3RsxProgram program = vertex ? pair.Vertex : pair.Pixel;
        Ps3ShaderTranslation translation = vertex ? pair.VertexTranslation : pair.PixelTranslation;
        string stageName = StageName(stage);
        WriteU32(stream, vertex ? 0u : 1u);
        WriteText(stream, identity, entryPoint, stageName + " entryPoint");
        byte[] bytecode = program.Bytecode;
        ValidateStageBlob(identity, stageName, bytecode);
        WriteBlob(stream, bytecode, identity, stageName + " blob");
        WriteU32(stream, Count(translation.UniformBuffers.Count, identity, stageName + " buffers"));
        foreach (Ps3ShaderUniformBuffer buffer in translation.UniformBuffers) {
            WriteText(stream, identity, buffer.BufferName, stageName + " buffer name");
            WriteU32(stream, buffer.BufferBinding);
            WriteU32(stream, buffer.ByteSize);
        }
        List<Ps3ShaderUniformBinding> uniforms = pair.Bindings.Uniforms.Where(binding => binding.Stage == stage).ToList();
        WriteU32(stream, Count(uniforms.Count, identity, stageName + " uniforms"));
        foreach (Ps3ShaderUniformBinding binding in uniforms) WriteUniform(stream, identity, stageName, binding);
    }

    /// <summary>Writes one uniform record using its source physical metadata and actual SDK constant metadata.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="identity">Artifact identity for errors.</param>
    /// <param name="stageName">Stage name for diagnostics.</param>
    /// <param name="binding">Validated uniform mapping.</param>
    static void WriteUniform(MemoryStream stream, Ps3ShaderArtifactIdentity identity, string stageName, Ps3ShaderUniformBinding binding) {
        Ps3ShaderUniformMember member = binding.SourceMember;
        Ps3RsxParameter parameter = binding.RsxParameter;
        string resource = stageName + " uniform " + member.MemberPath;
        WriteText(stream, identity, member.BufferName, resource + " bufferName");
        WriteU32(stream, member.BufferBinding);
        WriteText(stream, identity, member.MemberPath, resource + " memberPath");
        WriteU32(stream, member.ByteOffset);
        WriteU32(stream, member.ByteSize);
        WriteU32(stream, member.ArrayStride);
        WriteU32(stream, member.MatrixStride);
        WriteU32(stream, member.RowMajor ? 1u : 0u);
        WriteText(stream, identity, member.ScalarType, resource + " scalarType");
        WriteU32(stream, member.VectorWidth);
        WriteU32(stream, member.Columns);
        WriteU32(stream, member.ArrayLength);
        WriteText(stream, identity, member.GeneratedName, resource + " generatedName");
        WriteU32(stream, (uint)parameter.DataType);
        WriteU32(stream, parameter.ElementCount);
    }

    /// <summary>Writes one source vertex attribute and its actual SDK slot and type code.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="identity">Artifact identity for errors.</param>
    /// <param name="attribute">Validated mapped attribute.</param>
    static void WriteAttribute(MemoryStream stream, Ps3ShaderArtifactIdentity identity, Ps3ShaderAttributeBinding attribute) {
        Ps3ShaderInterfaceVariable source = attribute.SourceVariable;
        string resource = "attribute " + source.Semantic;
        WriteText(stream, identity, source.Semantic, resource + " semantic");
        WriteText(stream, identity, attribute.GeneratedName, resource + " generatedName");
        WriteText(stream, identity, source.ScalarType, resource + " scalarType");
        WriteU32(stream, source.VectorWidth);
        WriteU32(stream, source.Columns);
        WriteU32(stream, source.ArrayLength);
        WriteU32(stream, attribute.HardwareSlot);
        WriteU32(stream, (uint)attribute.RsxParameter.DataType);
    }

    /// <summary>Writes one mapped pixel texture/sampler and its actual SDK unit and type code.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="identity">Artifact identity for errors.</param>
    /// <param name="sampler">Validated mapped pixel sampler.</param>
    static void WriteSampler(MemoryStream stream, Ps3ShaderArtifactIdentity identity, Ps3ShaderSamplerMapEntry sampler) {
        Ps3ShaderSamplerBinding source = sampler.SourceSampler;
        string resource = "pixel sampler " + source.TextureName + "/" + source.SamplerName;
        WriteText(stream, identity, source.TextureName, resource + " textureName");
        WriteU32(stream, source.TextureBinding);
        WriteText(stream, identity, source.SamplerName, resource + " samplerName");
        WriteU32(stream, source.SamplerBinding);
        WriteText(stream, identity, sampler.GeneratedName, resource + " generatedName");
        WriteText(stream, identity, source.Dimension, resource + " dimension");
        WriteU32(stream, sampler.TextureUnit);
        WriteU32(stream, (uint)sampler.RsxParameter.DataType);
    }

    /// <summary>Writes a required strict UTF-8 string as a byte-counted record.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="value">String to serialize.</param>
    /// <param name="fieldName">Field context for diagnostics.</param>
    static void WriteText(MemoryStream stream, Ps3ShaderArtifactIdentity identity, string value, string fieldName) {
        ValidateResourceText(identity, value, fieldName);
        byte[] encoded = new UTF8Encoding(false, true).GetBytes(value);
        WriteU32(stream, checked((uint)encoded.Length));
        WriteBytes(stream, encoded);
    }

    /// <summary>Writes a length-prefixed opaque byte array after checking the stage size bound.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="bytes">Opaque SDK program copy.</param>
    /// <param name="identity">Artifact identity for diagnostics.</param>
    /// <param name="resource">Stage context for diagnostics.</param>
    static void WriteBlob(MemoryStream stream, byte[] bytes, Ps3ShaderArtifactIdentity identity, string resource) {
        if (bytes.Length == 0 || bytes.Length > MaximumStageBytes) throw Invalid(identity, resource, "must contain 1 through 1048576 bytes");
        WriteU32(stream, checked((uint)bytes.Length));
        WriteBytes(stream, bytes);
    }

    /// <summary>Writes one unsigned 32-bit integer in big-endian order.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="value">Unsigned value to write.</param>
    static void WriteU32(MemoryStream stream, uint value) {
        Span<byte> bytes = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(bytes, value);
        WriteBytes(stream, bytes);
    }

    /// <summary>Appends bytes only if the resulting artifact remains within its configured bound.</summary>
    /// <param name="stream">Bounded destination stream.</param>
    /// <param name="bytes">Bytes to append.</param>
    static void WriteBytes(MemoryStream stream, ReadOnlySpan<byte> bytes) {
        long newLength = checked(stream.Length + bytes.Length);
        if (newLength > MaximumArtifactBytes) throw new InvalidDataException($"PS3 shader artifact exceeds {MaximumArtifactBytes} bytes.");
        stream.Write(bytes);
    }

    /// <summary>Validates one stage blob's nonempty and maximum-size requirements.</summary>
    /// <param name="identity">Artifact identity for diagnostics.</param>
    /// <param name="stageName">Human-readable stage name.</param>
    /// <param name="bytecode">Copied SDK program bytes.</param>
    static void ValidateStageBlob(Ps3ShaderArtifactIdentity identity, string stageName, byte[] bytecode) {
        if (bytecode.Length == 0 || bytecode.Length > MaximumStageBytes) throw Invalid(identity, stageName + " blob", "must contain 1 through 1048576 bytes");
    }

    /// <summary>Validates required text while retaining an exception type suitable for caller arguments.</summary>
    /// <param name="value">Text to validate.</param>
    /// <param name="fieldName">Field name included in the exception.</param>
    static void ValidateResourceText(Ps3ShaderArtifactIdentity identity, string value, string resource) {
        try {
            Ps3ShaderArtifactIdentity.ValidateText(value, resource);
        } catch (ArgumentException exception) {
            throw Invalid(identity, resource, exception.Message, exception);
        }
    }

    /// <summary>Validates required caller-supplied text and preserves argument validation semantics.</summary>
    /// <param name="value">Text to validate.</param>
    /// <param name="fieldName">Argument name used in diagnostics.</param>
    static void ValidateRequired(string value, string fieldName) => Ps3ShaderArtifactIdentity.ValidateText(value, fieldName);

    /// <summary>Checks that a serialized list count is within version 1 limits.</summary>
    /// <param name="count">Record count.</param>
    /// <param name="identity">Artifact identity for diagnostics.</param>
    /// <param name="resource">List context.</param>
    /// <returns>The count as an unsigned wire value.</returns>
    static uint Count(int count, Ps3ShaderArtifactIdentity identity, string resource) {
        ValidateCount(count, identity, resource);
        return checked((uint)count);
    }

    /// <summary>Validates a record count against the version 1 list limit.</summary>
    /// <param name="count">Record count.</param>
    /// <param name="identity">Artifact identity for diagnostics.</param>
    /// <param name="resource">List context.</param>
    static void ValidateCount(int count, Ps3ShaderArtifactIdentity identity, string resource) {
        if (count < 0 || count > MaximumListCount) throw Invalid(identity, resource, $"record count {count} exceeds {MaximumListCount}");
    }

    /// <summary>Returns the explicit stage label used by the wire format diagnostics.</summary>
    /// <param name="stage">Validated vertex or pixel stage.</param>
    /// <returns>Stable lowercase stage label.</returns>
    static string StageName(ShaderStage stage) => stage == ShaderStage.Vertex ? "vertex" : "pixel";

    /// <summary>Creates an identity- and resource-scoped validation exception.</summary>
    /// <param name="identity">Artifact identity involved in the failure.</param>
    /// <param name="resource">Resource or field involved in the failure.</param>
    /// <param name="message">Specific reason validation failed.</param>
    /// <param name="innerException">Optional arithmetic failure.</param>
    /// <returns>An explicit malformed-artifact exception.</returns>
    static InvalidDataException Invalid(Ps3ShaderArtifactIdentity identity, string resource, string message, Exception innerException = null) =>
        new($"Shader artifact '{identity.RelativePath}' {resource}: {message}.", innerException);
}
