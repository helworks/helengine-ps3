using System.Buffers.Binary;
using System.Globalization;
using System.Text;
using System.Text.Json;
using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies deterministic PS3 shader identity and artifact wire behavior without invoking the SDK compiler.</summary>
public sealed class Ps3ShaderArtifactTests {
    /// <summary>Verifies length-delimited identity components cannot alias at delimiter boundaries.</summary>
    [Fact]
    public void IdentityPathDoesNotAliasDelimiterBoundaries() {
        Ps3ShaderArtifactIdentity first = new("a|b", "c", "d", "v");
        Ps3ShaderArtifactIdentity second = new("a", "b|c", "d", "v");
        Assert.NotEqual(first.RelativePath, second.RelativePath);
        Assert.StartsWith("shaders/", first.RelativePath, StringComparison.Ordinal);
        Assert.EndsWith(".ps3sp", first.RelativePath, StringComparison.Ordinal);
    }

    /// <summary>Verifies UTF-8 identity hashing and rejects missing, NUL, malformed and oversized text.</summary>
    [Fact]
    public void IdentityUsesUtf8AndRejectsInvalidText() {
        Ps3ShaderArtifactIdentity identity = new("雪/asset", "頂點", "pixel", "é");
        Assert.Equal("shaders/" + Convert.ToHexString(SHA256LengthPrefixed("雪/asset", "頂點", "pixel", "é")).ToLowerInvariant() + ".ps3sp", identity.RelativePath);
        Assert.Throws<ArgumentException>(() => new Ps3ShaderArtifactIdentity(" ", "v", "p", "x"));
        Assert.Throws<ArgumentException>(() => new Ps3ShaderArtifactIdentity("a\0b", "v", "p", "x"));
        Assert.Throws<ArgumentException>(() => new Ps3ShaderArtifactIdentity("\ud800", "v", "p", "x"));
        Assert.Throws<ArgumentException>(() => new Ps3ShaderArtifactIdentity(new string('x', 65537), "v", "p", "x"));
    }

    /// <summary>Verifies exact bytes for every version 1 record using an independent bounded decoder.</summary>
    [Fact]
    public void WritesCompleteDeterministicArtifactAndPreservesInputs() {
        Ps3ShaderArtifactIdentity identity = new("asset/雪", "Vertex", "Pixel", "Default");
        Ps3ShaderProgramPair pair = CreatePair();
        byte[] vertexBefore = pair.Vertex.Bytecode;
        byte[] pixelBefore = pair.Pixel.Bytecode;
        string pairSnapshot = JsonSerializer.Serialize(pair);

        byte[] bytes = Ps3ShaderArtifactWriter.Write(identity, "source-hash", "VS", "PS", pair);
        byte[] second = Ps3ShaderArtifactWriter.Write(identity, "source-hash", "VS", "PS", pair);

        Assert.Equal(bytes, second);
        Decoder decoder = new(bytes);
        Assert.Equal(new byte[] { 0x50, 0x33, 0x53, 0x50 }, decoder.ReadBytes(4));
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal((uint)bytes.Length, decoder.ReadU32());
        Assert.Equal("asset/雪", decoder.ReadText());
        Assert.Equal("Vertex", decoder.ReadText());
        Assert.Equal("Pixel", decoder.ReadText());
        Assert.Equal("Default", decoder.ReadText());
        Assert.Equal("source-hash", decoder.ReadText());
        Assert.Equal("toolchain", decoder.ReadText());
        Assert.Equal(0u, decoder.ReadU32());
        Assert.Equal("VS", decoder.ReadText());
        Assert.Equal(new byte[] { 0x00, 0x81, 0xFE }, decoder.ReadBlob());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("Frame", decoder.ReadText());
        Assert.Equal(2u, decoder.ReadU32());
        Assert.Equal(128u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("Frame", decoder.ReadText());
        Assert.Equal(2u, decoder.ReadU32());
        Assert.Equal("Transform", decoder.ReadText());
        Assert.Equal(16u, decoder.ReadU32());
        Assert.Equal(48u, decoder.ReadU32());
        Assert.Equal(32u, decoder.ReadU32());
        Assert.Equal(16u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("float32", decoder.ReadText());
        Assert.Equal(3u, decoder.ReadU32());
        Assert.Equal(2u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("Frame.Transform", decoder.ReadText());
        Assert.Equal((uint)Ps3RsxDataType.Float3x4, decoder.ReadU32());
        Assert.Equal(2u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("PS", decoder.ReadText());
        Assert.Equal(new byte[] { 0xA0, 0xB1, 0xC2 }, decoder.ReadBlob());
        Assert.Equal(0u, decoder.ReadU32());
        Assert.Equal(0u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("POSITION0", decoder.ReadText());
        Assert.Equal("vertex_position", decoder.ReadText());
        Assert.Equal("float32", decoder.ReadText());
        Assert.Equal(3u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal(7u, decoder.ReadU32());
        Assert.Equal((uint)Ps3RsxDataType.Float3, decoder.ReadU32());
        Assert.Equal(1u, decoder.ReadU32());
        Assert.Equal("Albedo", decoder.ReadText());
        Assert.Equal(4u, decoder.ReadU32());
        Assert.Equal("Linear", decoder.ReadText());
        Assert.Equal(5u, decoder.ReadU32());
        Assert.Equal("sample_albedo", decoder.ReadText());
        Assert.Equal("Dim2D", decoder.ReadText());
        Assert.Equal(3u, decoder.ReadU32());
        Assert.Equal((uint)Ps3RsxDataType.Sampler2D, decoder.ReadU32());
        Assert.Equal(bytes.Length, decoder.Position);
        bytes[0] ^= 0xFF;
        Assert.Equal(vertexBefore, pair.Vertex.Bytecode);
        Assert.Equal(pixelBefore, pair.Pixel.Bytecode);
        Assert.Equal(pairSnapshot, JsonSerializer.Serialize(pair));
    }

    /// <summary>Verifies the writer fails visibly for missing required text and mismatched toolchains.</summary>
    [Fact]
    public void RejectsMissingTextAndDifferentToolchains() {
        Ps3ShaderArtifactIdentity identity = Identity();
        Assert.Throws<ArgumentException>(() => Ps3ShaderArtifactWriter.Write(identity, " ", "VS", "PS", CreatePair()));
        Assert.Throws<InvalidDataException>(() => Ps3ShaderArtifactWriter.Write(identity, "hash", "VS", "PS", CreatePair(pixelToolchain: "other")));
    }

    /// <summary>Verifies runtime subset and mapping validation reject unsupported or inconsistent resources.</summary>
    [Fact]
    public void RejectsUnsupportedSamplersAndInvalidResourceMappings() {
        Ps3ShaderArtifactIdentity identity = Identity();
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(vertexSampler: true)));
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(pixelDimension: "Dim3D")));
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(pixelDimension: "Dim1D")));
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(duplicateBuffer: true)));
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(duplicateSlot: true, attributeCount: 2)));
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(memberOffset: 81)));
    }

    /// <summary>Verifies cube samplers are accepted and their exact reflected dimension and SDK type are preserved.</summary>
    [Fact]
    public void AcceptsCubePixelSamplerAndPreservesDimension() {
        byte[] bytes = Write(Identity(), CreatePair(pixelDimension: "DimCube"));
        AssertSamplerRecord(bytes, "DimCube", Ps3RsxDataType.SamplerCube);
    }

    /// <summary>Verifies serialized list and stage blob limits are enforced before returning bytes.</summary>
    [Fact]
    public void RejectsMetadataAndStageBlobLimitViolations() {
        Ps3ShaderArtifactIdentity identity = Identity();
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(attributeCount: 4097)));
        Assert.Throws<InvalidDataException>(() => Write(identity, CreatePair(stageByteCount: 1024 * 1024 + 1)));
    }

    /// <summary>Verifies individually valid strings, lists and stage blobs cannot exceed the aggregate artifact limit.</summary>
    [Fact]
    public void RejectsAggregateArtifactSizeOver16MiB() {
        InvalidDataException exception = Assert.Throws<InvalidDataException>(() =>
            Write(Identity(), CreatePair(attributeCount: 280, attributeTextBytes: 30000)));

        Assert.Contains("exceeds 16777216 bytes", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Writes one pair using fixed source hashes and entry points for negative assertions.</summary>
    /// <param name="identity">Artifact identity.</param>
    /// <param name="pair">Synthetic pair to serialize.</param>
    static byte[] Write(Ps3ShaderArtifactIdentity identity, Ps3ShaderProgramPair pair) =>
        Ps3ShaderArtifactWriter.Write(identity, "hash", "VS", "PS", pair);

    /// <summary>Creates a deterministic identity for validation-focused tests.</summary>
    /// <returns>A valid identity.</returns>
    static Ps3ShaderArtifactIdentity Identity() => new("asset", "Vertex", "Pixel", "Default");

    /// <summary>Builds immutable synthetic DTOs with optional invalid metadata for pure writer tests.</summary>
    /// <param name="pixelToolchain">Optional pixel toolchain override.</param>
    /// <param name="vertexSampler">Whether vertex source reflection includes an unsupported sampler.</param>
    /// <param name="pixelDimension">Pixel sampler dimension string.</param>
    /// <param name="duplicateBuffer">Whether the vertex translation repeats a buffer identity.</param>
    /// <param name="duplicateSlot">Whether a second attribute reuses the first physical slot.</param>
    /// <param name="memberOffset">Physical uniform byte offset.</param>
    /// <param name="attributeCount">Number of mapped attributes.</param>
    /// <param name="stageByteCount">Length of each opaque SDK stage blob.</param>
    /// <param name="attributeTextBytes">Optional ASCII length for each large semantic and generated name.</param>
    /// <returns>A synthetic pair that has never passed through the SDK compiler.</returns>
    static Ps3ShaderProgramPair CreatePair(string pixelToolchain = "toolchain", bool vertexSampler = false,
        string pixelDimension = "Dim2D", bool duplicateBuffer = false, bool duplicateSlot = false, uint memberOffset = 16,
        int attributeCount = 1, int stageByteCount = 3, int attributeTextBytes = 0) {
        Ps3ShaderUniformMember member = new("Frame", 2, "Transform", memberOffset, 48, 32, 16, true,
            "Frame.Transform", "float32", 3, 2, 1);
        Ps3RsxParameter constant = new("Frame.Transform", Ps3RsxParameterCategory.Constant, null, 2,
            Ps3RsxDataType.Float3x4,
            [new Ps3RsxConstantElement(0, 4, Array.Empty<uint>(), Array.Empty<uint>()),
             new Ps3RsxConstantElement(1, 5, Array.Empty<uint>(), Array.Empty<uint>())]);
        Ps3ShaderUniformBinding uniform = new(ShaderStage.Vertex, member, constant);
        Ps3ShaderInterfaceVariable sourceAttribute = new("POSITION0", 0, "float32", 3, 1, 1, "smooth", "vertex_position");
        List<Ps3ShaderAttributeBinding> attributes = [];
        for (int index = 0; index < attributeCount; index++) {
            string name = attributeTextBytes == 0
                ? (index == 0 ? "vertex_position" : "vertex_position_" + index)
                : new string('g', attributeTextBytes - 6) + index.ToString("D6", CultureInfo.InvariantCulture);
            string semantic = attributeTextBytes == 0
                ? (index == 0 ? "POSITION0" : "POSITION" + index)
                : new string('s', attributeTextBytes - 6) + index.ToString("D6", CultureInfo.InvariantCulture);
            Ps3ShaderInterfaceVariable source = attributeTextBytes == 0 && index == 0
                ? sourceAttribute
                : new(semantic, (uint)index, "float32", 3, 1, 1, "smooth", name);
            Ps3RsxParameter parameter = new(name, Ps3RsxParameterCategory.Attribute, (uint)(duplicateSlot ? 7 : 7 + index),
                1, Ps3RsxDataType.Float3, Array.Empty<Ps3RsxConstantElement>());
            attributes.Add(new Ps3ShaderAttributeBinding(source, parameter));
        }
        Ps3ShaderSamplerBinding samplerSource = new("Albedo", 4, "Linear", 5, "sample_albedo", pixelDimension);
        Ps3RsxDataType samplerType = pixelDimension switch {
            "Dim1D" => Ps3RsxDataType.Sampler1D,
            "Dim3D" => Ps3RsxDataType.Sampler3D,
            "DimCube" => Ps3RsxDataType.SamplerCube,
            _ => Ps3RsxDataType.Sampler2D
        };
        Ps3RsxParameter samplerParameter = new("sample_albedo", Ps3RsxParameterCategory.Sampler, 3, 1,
            samplerType, Array.Empty<Ps3RsxConstantElement>());
        Ps3ShaderSamplerMapEntry sampler = new(samplerSource, samplerParameter);
        List<Ps3ShaderUniformBuffer> buffers = [new("Frame", 2, 128)];
        if (duplicateBuffer) buffers.Add(new("Frame", 2, 128));
        Ps3ShaderSamplerBinding[] vertexSamplers = vertexSampler ? [new("VertexTexture", 0, "VertexSampler", 0, "vs_sample", "Dim2D")] : [];
        Ps3ShaderTranslation vertexTranslation = CreateTranslation(ShaderStage.Vertex, buffers, vertexSamplers);
        Ps3ShaderTranslation pixelTranslation = CreateTranslation(ShaderStage.Pixel, Array.Empty<Ps3ShaderUniformBuffer>(), []);
        Ps3ShaderBindingMap bindings = new([uniform], attributes, [sampler], Array.Empty<Ps3ShaderStageLink>());
        byte[] vertexCode = stageByteCount == 3 ? [0x00, 0x81, 0xFE] : Enumerable.Range(0, stageByteCount).Select(value => (byte)value).ToArray();
        byte[] pixelCode = [0xA0, 0xB1, 0xC2];
        Ps3RsxProgram vertex = new(ShaderStage.Vertex, vertexCode, "vertex dump", "toolchain", [constant]);
        Ps3RsxProgram pixel = new(ShaderStage.Pixel, pixelCode, "pixel dump", pixelToolchain, [samplerParameter]);
        return new Ps3ShaderProgramPair(vertex, pixel, vertexTranslation, pixelTranslation, bindings);
    }

    /// <summary>Creates a translation snapshot with the supplied uniform buffers and source samplers.</summary>
    /// <param name="stage">Source shader stage.</param>
    /// <param name="buffers">Uniform buffers.</param>
    /// <param name="samplers">Reflected samplers.</param>
    /// <returns>An immutable translation.</returns>
    static Ps3ShaderTranslation CreateTranslation(ShaderStage stage, IEnumerable<Ps3ShaderUniformBuffer> buffers,
        IEnumerable<Ps3ShaderSamplerBinding> samplers) => new(stage, "generated source", Array.Empty<string>(), Array.Empty<string>(),
        Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderUniformMember>(),
        samplers, Array.Empty<Ps3ShaderSourceDependency>(), buffers);

    /// <summary>Computes expected SHA-256 bytes from length-prefixed UTF-8 identity parts independently of production code.</summary>
    /// <param name="components">Identity components in required order.</param>
    /// <returns>Expected SHA-256 bytes.</returns>
    static byte[] SHA256LengthPrefixed(params string[] components) {
        using MemoryStream stream = new();
        Span<byte> length = stackalloc byte[4];
        foreach (string component in components) {
            byte[] encoded = Encoding.UTF8.GetBytes(component);
            BinaryPrimitives.WriteUInt32BigEndian(length, (uint)encoded.Length);
            stream.Write(length);
            stream.Write(encoded);
        }
        return System.Security.Cryptography.SHA256.HashData(stream.ToArray());
    }

    /// <summary>Decodes the sampler record from a complete artifact and checks preserved dimension and SDK type.</summary>
    /// <param name="bytes">Complete artifact bytes.</param>
    /// <param name="expectedDimension">Expected exact reflected dimension spelling.</param>
    /// <param name="expectedType">Expected SDK sampler type code.</param>
    static void AssertSamplerRecord(byte[] bytes, string expectedDimension, Ps3RsxDataType expectedType) {
        Decoder decoder = new(bytes);
        decoder.ReadBytes(4);
        decoder.ReadU32();
        decoder.ReadU32();
        decoder.ReadText();
        decoder.ReadText();
        decoder.ReadText();
        decoder.ReadText();
        decoder.ReadText();
        decoder.ReadText();
        SkipStage(decoder);
        SkipStage(decoder);
        uint attributeCount = decoder.ReadU32();
        for (uint index = 0; index < attributeCount; index++) {
            decoder.ReadText();
            decoder.ReadText();
            decoder.ReadText();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
        }
        Assert.Equal(1u, decoder.ReadU32());
        decoder.ReadText();
        decoder.ReadU32();
        decoder.ReadText();
        decoder.ReadU32();
        decoder.ReadText();
        Assert.Equal(expectedDimension, decoder.ReadText());
        Assert.Equal(3u, decoder.ReadU32());
        Assert.Equal((uint)expectedType, decoder.ReadU32());
        Assert.Equal(bytes.Length, decoder.Position);
    }

    /// <summary>Consumes one complete stage record while preserving bounded decoding behavior.</summary>
    /// <param name="decoder">Independent artifact decoder.</param>
    static void SkipStage(Decoder decoder) {
        decoder.ReadU32();
        decoder.ReadText();
        decoder.ReadBlob();
        uint bufferCount = decoder.ReadU32();
        for (uint index = 0; index < bufferCount; index++) {
            decoder.ReadText();
            decoder.ReadU32();
            decoder.ReadU32();
        }
        uint uniformCount = decoder.ReadU32();
        for (uint index = 0; index < uniformCount; index++) {
            decoder.ReadText();
            decoder.ReadU32();
            decoder.ReadText();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadText();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadU32();
            decoder.ReadText();
            decoder.ReadU32();
            decoder.ReadU32();
        }
    }

    /// <summary>Decodes the artifact using only bounded reads and big-endian primitives.</summary>
    sealed class Decoder {
        /// <summary>Stores the immutable artifact bytes being inspected.</summary>
        readonly byte[] bytes;

        /// <summary>Tracks the next unread byte offset.</summary>
        int offset;

        /// <summary>Creates a decoder over one artifact byte array.</summary>
        /// <param name="value">Artifact bytes.</param>
        public Decoder(byte[] value) => bytes = value;

        /// <summary>Gets the current byte offset for exact-EOF assertions.</summary>
        public int Position => offset;

        /// <summary>Reads one bounded unsigned big-endian integer.</summary>
        /// <returns>Decoded value.</returns>
        public uint ReadU32() {
            Ensure(4);
            uint value = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, 4));
            offset += 4;
            return value;
        }

        /// <summary>Reads a strict UTF-8 string whose declared byte count is within the format maximum.</summary>
        /// <returns>Decoded string.</returns>
        public string ReadText() {
            uint length = ReadU32();
            Assert.InRange(length, 1u, 65536u);
            return new UTF8Encoding(false, true).GetString(ReadBytes(checked((int)length)));
        }

        /// <summary>Reads a bounded SDK blob from its declared byte count.</summary>
        /// <returns>Copied blob bytes.</returns>
        public byte[] ReadBlob() {
            uint length = ReadU32();
            Assert.InRange(length, 1u, 1024u * 1024u);
            return ReadBytes(checked((int)length));
        }

        /// <summary>Reads exactly the requested number of bytes without crossing the artifact boundary.</summary>
        /// <param name="count">Number of bytes to copy.</param>
        /// <returns>Copied bytes.</returns>
        public byte[] ReadBytes(int count) {
            Ensure(count);
            byte[] value = bytes.AsSpan(offset, count).ToArray();
            offset += count;
            return value;
        }

        /// <summary>Ensures a read stays inside the supplied artifact.</summary>
        /// <param name="count">Requested byte count.</param>
        void Ensure(int count) {
            Assert.InRange(count, 0, bytes.Length);
            Assert.True(offset <= bytes.Length - count, "Artifact decoder read passed the end of the artifact.");
        }
    }
}
