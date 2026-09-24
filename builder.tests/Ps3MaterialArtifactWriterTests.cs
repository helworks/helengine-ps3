using System.Buffers.Binary;
using System.Text;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Checks P3MT output using a test-owned decoder independent of the production writer.</summary>
public sealed class Ps3MaterialArtifactWriterTests {
    /// <summary>Maximum allowed number of UTF-8 bytes in one envelope string.</summary>
    const int MaximumStringBytes = 65_536;

    /// <summary>Verifies the exact P3MT header, field order, identity, channels and final byte offset.</summary>
    [Fact]
    public void Write_ProducesSpecifiedEnvelopeAndFixedShaderIdentity() {
        Ps3ShaderArtifactIdentity identity = Ps3MaterialArtifactWriter.CreateShaderIdentity();

        Assert.Equal("ForwardStandardShader", identity.ShaderAssetId);
        Assert.Equal("ForwardStandardShader.vs", identity.VertexProgramName);
        Assert.Equal("ForwardStandardShader.ps", identity.PixelProgramName);
        Assert.Equal("ForwardStandard", identity.VariantName);

        byte[] bytes = Ps3MaterialArtifactWriter.Write(
            "Materials/Cube", 192, 32, 16, 64, true, true, true, "cooked/textures/fixture.hasset");

        Assert.Equal(new byte[] { 80, 51, 77, 84 }, bytes[..4]);
        Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(4, 4)));
        Assert.Equal((uint)bytes.Length, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(8, 4)));
        AssertDecodedEnvelope(
            bytes,
            "Materials/Cube",
            "ps3-standard",
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard",
            "cooked/" + identity.RelativePath,
            192,
            32,
            16,
            64,
            7,
            "cooked/textures/fixture.hasset");
    }

    /// <summary>Verifies repeated writes produce byte-identical envelopes for identical input.</summary>
    [Fact]
    public void Write_IsDeterministicForRepeatedInput() {
        byte[] first = Ps3MaterialArtifactWriter.Write("Materials/Cube", 1, 2, 3, 4, true, false, true, "textures/albedo.hasset");
        byte[] second = Ps3MaterialArtifactWriter.Write("Materials/Cube", 1, 2, 3, 4, true, false, true, "textures/albedo.hasset");

        Assert.Equal(first, second);
    }

    /// <summary>Verifies required material identity text round-trips as strict UTF-8.</summary>
    [Fact]
    public void Write_RoundTripsUnicodeMaterialAssetId() {
        string materialAssetId = "Materials/雪/椅子";
        byte[] bytes = Ps3MaterialArtifactWriter.Write(materialAssetId, 4, 3, 2, 1, false, false, false, string.Empty);

        AssertDecodedEnvelope(
            bytes,
            materialAssetId,
            "ps3-standard",
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard",
            Ps3MaterialArtifactWriter.CreateShaderIdentity().RuntimeRelativePath,
            4,
            3,
            2,
            1,
            0,
            string.Empty);
    }

    /// <summary>Verifies each surface flag independently and in every combined state.</summary>
    /// <param name="lit">Whether lighting is enabled.</param>
    /// <param name="doubleSided">Whether both face orientations are visible.</param>
    /// <param name="useVertexColor">Whether vertex colors modulate the base color.</param>
    /// <param name="expectedFlags">Expected bit mask encoded as a big-endian U32.</param>
    [Theory]
    [InlineData(false, false, false, 0u)]
    [InlineData(true, false, false, 1u)]
    [InlineData(false, true, false, 2u)]
    [InlineData(false, false, true, 4u)]
    [InlineData(true, true, false, 3u)]
    [InlineData(true, false, true, 5u)]
    [InlineData(false, true, true, 6u)]
    [InlineData(true, true, true, 7u)]
    public void Write_EncodesSurfaceFlagBits(bool lit, bool doubleSided, bool useVertexColor, uint expectedFlags) {
        byte[] bytes = Ps3MaterialArtifactWriter.Write(
            "Materials/Flags", 1, 2, 3, 4, lit, doubleSided, useVertexColor, string.Empty);

        Assert.Equal(expectedFlags, ReadSurfaceFlags(bytes));
    }

    /// <summary>Verifies alpha remains an authored raw byte, including full transparency.</summary>
    [Fact]
    public void Write_PreservesTransparentRgbaChannels() {
        byte[] bytes = Ps3MaterialArtifactWriter.Write("Materials/Transparent", 192, 32, 16, 0, false, true, false, string.Empty);

        AssertDecodedEnvelope(
            bytes,
            "Materials/Transparent",
            "ps3-standard",
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard",
            Ps3MaterialArtifactWriter.CreateShaderIdentity().RuntimeRelativePath,
            192,
            32,
            16,
            0,
            2,
            string.Empty);
    }

    /// <summary>Verifies the optional texture may be empty and valid nested paths retain their spelling.</summary>
    [Fact]
    public void Write_AllowsEmptyAndNestedTexturePaths() {
        byte[] emptyTexture = Ps3MaterialArtifactWriter.Write("Materials/Default", 1, 2, 3, 4, false, false, false, string.Empty);
        byte[] nestedTexture = Ps3MaterialArtifactWriter.Write(
            "Materials/Fixture", 5, 6, 7, 8, false, false, false, "cooked/textures/fixture.hasset");

        AssertDecodedEnvelope(
            emptyTexture,
            "Materials/Default",
            "ps3-standard",
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard",
            Ps3MaterialArtifactWriter.CreateShaderIdentity().RuntimeRelativePath,
            1,
            2,
            3,
            4,
            0,
            string.Empty);
        AssertDecodedEnvelope(
            nestedTexture,
            "Materials/Fixture",
            "ps3-standard",
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard",
            Ps3MaterialArtifactWriter.CreateShaderIdentity().RuntimeRelativePath,
            5,
            6,
            7,
            8,
            0,
            "cooked/textures/fixture.hasset");
    }

    /// <summary>Verifies unsafe, malformed and non-relative texture paths are rejected without normalization.</summary>
    /// <param name="texturePath">Path that violates one P3MT relative-path rule.</param>
    [Theory]
    [InlineData("/textures/albedo.hasset")]
    [InlineData("textures/albedo.hasset/")]
    [InlineData("C:textures/albedo.hasset")]
    [InlineData("C:/textures/albedo.hasset")]
    [InlineData("textures\\albedo.hasset")]
    [InlineData("textures//albedo.hasset")]
    [InlineData("textures/./albedo.hasset")]
    [InlineData("textures/../albedo.hasset")]
    [InlineData("textures/\u0000albedo.hasset")]
    [InlineData("textures/\nalbedo.hasset")]
    [InlineData("   ")]
    public void Write_RejectsInvalidTextureRelativePath(string texturePath) {
        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write(
            "Materials/Invalid", 1, 2, 3, 4, false, false, false, texturePath));
    }

    /// <summary>Verifies blank required material identities are rejected.</summary>
    [Fact]
    public void Write_RejectsBlankMaterialAssetId() {
        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write(" \t", 1, 2, 3, 4, false, false, false, string.Empty));
    }

    /// <summary>Verifies embedded NUL is rejected in required material identity text.</summary>
    [Fact]
    public void Write_RejectsNulInMaterialAssetId() {
        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write("Materials\0/Cube", 1, 2, 3, 4, false, false, false, string.Empty));
    }

    /// <summary>Verifies an unpaired UTF-16 surrogate is rejected instead of being replaced during UTF-8 encoding.</summary>
    [Fact]
    public void Write_RejectsUnpairedSurrogatesInMaterialAndTextureText() {
        string unpairedSurrogate = "Materials/" + '\ud800';

        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write(
            unpairedSurrogate, 1, 2, 3, 4, false, false, false, string.Empty));
        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write(
            "Materials/Valid", 1, 2, 3, 4, false, false, false, "textures/" + '\ud800'));
    }

    /// <summary>Verifies material identities longer than the UTF-8 byte bound are rejected.</summary>
    [Fact]
    public void Write_RejectsOverLimitMaterialAssetId() {
        string materialAssetId = new('m', MaximumStringBytes + 1);

        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write(
            materialAssetId, 1, 2, 3, 4, false, false, false, string.Empty));
    }

    /// <summary>Verifies texture paths longer than the UTF-8 byte bound are rejected.</summary>
    [Fact]
    public void Write_RejectsOverLimitTexturePath() {
        string texturePath = new('t', MaximumStringBytes + 1);

        Assert.Throws<ArgumentException>(() => Ps3MaterialArtifactWriter.Write(
            "Materials/Valid", 1, 2, 3, 4, false, false, false, texturePath));
    }

    /// <summary>Verifies null required identity and optional texture inputs fail explicitly.</summary>
    [Fact]
    public void Write_RejectsNullMaterialIdAndTexturePath() {
        Assert.Throws<ArgumentNullException>(() => Ps3MaterialArtifactWriter.Write(null, 1, 2, 3, 4, false, false, false, string.Empty));
        Assert.Throws<ArgumentNullException>(() => Ps3MaterialArtifactWriter.Write("Materials/Valid", 1, 2, 3, 4, false, false, false, null));
    }

    /// <summary>Verifies the maximum allowed UTF-8 byte count is accepted for variable strings.</summary>
    [Fact]
    public void Write_AcceptsMaximumStringByteLength() {
        string materialAssetId = new('m', MaximumStringBytes);
        string texturePath = new('t', MaximumStringBytes);

        byte[] bytes = Ps3MaterialArtifactWriter.Write(materialAssetId, 1, 2, 3, 4, false, false, false, texturePath);

        AssertDecodedEnvelope(
            bytes,
            materialAssetId,
            "ps3-standard",
            "ForwardStandardShader",
            "ForwardStandardShader.vs",
            "ForwardStandardShader.ps",
            "ForwardStandard",
            Ps3MaterialArtifactWriter.CreateShaderIdentity().RuntimeRelativePath,
            1,
            2,
            3,
            4,
            0,
            texturePath);
    }

    /// <summary>Reads and validates every P3MT field with an independent test-owned cursor.</summary>
    /// <param name="bytes">Envelope under test.</param>
    /// <param name="materialAssetId">Expected material asset identifier.</param>
    /// <param name="rendererFamilyId">Expected renderer family.</param>
    /// <param name="shaderAssetId">Expected shader asset identifier.</param>
    /// <param name="vertexProgramName">Expected vertex program name.</param>
    /// <param name="pixelProgramName">Expected pixel program name.</param>
    /// <param name="variantName">Expected standard variant name.</param>
    /// <param name="shaderRelativePath">Expected hash-derived P3SP path.</param>
    /// <param name="red">Expected red byte.</param>
    /// <param name="green">Expected green byte.</param>
    /// <param name="blue">Expected blue byte.</param>
    /// <param name="alpha">Expected alpha byte.</param>
    /// <param name="surfaceFlags">Expected U32 surface flag mask.</param>
    /// <param name="textureRelativePath">Expected optional texture path.</param>
    static void AssertDecodedEnvelope(
        byte[] bytes,
        string materialAssetId,
        string rendererFamilyId,
        string shaderAssetId,
        string vertexProgramName,
        string pixelProgramName,
        string variantName,
        string shaderRelativePath,
        byte red,
        byte green,
        byte blue,
        byte alpha,
        uint surfaceFlags,
        string textureRelativePath) {
        Assert.True(bytes.Length >= 12);
        Assert.Equal(new byte[] { 80, 51, 77, 84 }, bytes[..4]);
        Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(4, 4)));
        Assert.Equal((uint)bytes.Length, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(8, 4)));

        int offset = 12;
        Assert.Equal(materialAssetId, ReadString(bytes, ref offset));
        Assert.Equal(rendererFamilyId, ReadString(bytes, ref offset));
        Assert.Equal(shaderAssetId, ReadString(bytes, ref offset));
        Assert.Equal(vertexProgramName, ReadString(bytes, ref offset));
        Assert.Equal(pixelProgramName, ReadString(bytes, ref offset));
        Assert.Equal(variantName, ReadString(bytes, ref offset));
        Assert.Equal(shaderRelativePath, ReadString(bytes, ref offset));

        Assert.Equal(red, bytes[offset++]);
        Assert.Equal(green, bytes[offset++]);
        Assert.Equal(blue, bytes[offset++]);
        Assert.Equal(alpha, bytes[offset++]);
        Assert.Equal(surfaceFlags, ReadUInt32(bytes, ref offset));
        Assert.Equal(textureRelativePath, ReadString(bytes, ref offset));
        Assert.Equal(bytes.Length, offset);
    }

    /// <summary>Decodes one length-prefixed string using strict UTF-8 and the test cursor.</summary>
    /// <param name="bytes">Envelope bytes.</param>
    /// <param name="offset">Current byte position, advanced past the decoded string.</param>
    /// <returns>Decoded string value.</returns>
    static string ReadString(byte[] bytes, ref int offset) {
        uint byteLength = ReadUInt32(bytes, ref offset);
        int length = checked((int)byteLength);
        Assert.True(offset <= bytes.Length - length);
        string value = new UTF8Encoding(false, true).GetString(bytes, offset, length);
        offset = checked(offset + length);
        return value;
    }

    /// <summary>Reads one big-endian U32 and advances the test cursor.</summary>
    /// <param name="bytes">Envelope bytes.</param>
    /// <param name="offset">Current byte position, advanced by four bytes.</param>
    /// <returns>Decoded unsigned integer.</returns>
    static uint ReadUInt32(byte[] bytes, ref int offset) {
        Assert.True(offset <= bytes.Length - sizeof(uint));
        uint value = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, sizeof(uint)));
        offset = checked(offset + sizeof(uint));
        return value;
    }

    /// <summary>Reads the P3MT surface flag word after the fixed identity strings and color channels.</summary>
    /// <param name="bytes">Envelope bytes whose flag field is returned.</param>
    /// <returns>Big-endian surface flag word.</returns>
    static uint ReadSurfaceFlags(byte[] bytes) {
        int offset = 12;
        for (int index = 0; index < 7; index++) {
            _ = ReadString(bytes, ref offset);
        }
        offset = checked(offset + 4);
        return ReadUInt32(bytes, ref offset);
    }
}
