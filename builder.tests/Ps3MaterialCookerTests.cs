using System.Buffers.Binary;
using System.Text;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies standard material translation emits the P3MT contract and matching shader dependency.</summary>
public sealed class Ps3MaterialCookerTests {
    /// <summary>Verifies an authored color reaches the envelope channel for channel.</summary>
    [Fact]
    public void CookMaterial_EmitsAuthoredBaseColor() {
        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#C02010FF"))));

        Assert.Equal("Materials/Test", cooked.MaterialAssetId);
        Assert.Equal(Ps3MaterialSchemaIds.StandardGraphicsProfileId, cooked.RendererFamilyId);
        Assert.Equal((byte)0xC0, cooked.Red);
        Assert.Equal((byte)0x20, cooked.Green);
        Assert.Equal((byte)0x10, cooked.Blue);
        Assert.Equal(byte.MaxValue, cooked.Alpha);
    }

    /// <summary>Verifies the short color form is accepted and receives opaque alpha.</summary>
    [Fact]
    public void CookMaterial_TreatsShortColorFormAsOpaque() {
        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#18D040"))));

        Assert.Equal((byte)0x18, cooked.Red);
        Assert.Equal((byte)0xD0, cooked.Green);
        Assert.Equal((byte)0x40, cooked.Blue);
        Assert.Equal(byte.MaxValue, cooked.Alpha);
    }

    /// <summary>Verifies translucent base color alpha survives material cooking.</summary>
    [Fact]
    public void CookMaterial_PreservesTranslucentAlpha() {
        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#40FF4040"))));

        Assert.Equal((byte)0x40, cooked.Alpha);
    }

    /// <summary>Verifies lighting, double-sided and vertex-color choices occupy their independent flag bits.</summary>
    [Fact]
    public void CookMaterial_RecordsSurfaceState() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.DoubleSidedFieldId] = "true";
        fields[Ps3MaterialSchemaIds.VertexColorModeFieldId] = Ps3MaterialSchemaIds.VertexColorModulateValue;
        fields[Ps3MaterialSchemaIds.LightingModeFieldId] = Ps3MaterialSchemaIds.LightingLitValue;

        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Equal(7u, cooked.SurfaceFlags);
    }

    /// <summary>Verifies schema defaults apply when no material-specific fields are authored.</summary>
    [Fact]
    public void CookMaterial_AppliesSchemaDefaultsWhenNoFieldsAreAuthored() {
        DecodedMaterial cooked = Decode(
            new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(new Dictionary<string, string>())));

        Assert.Equal(byte.MaxValue, cooked.Red);
        Assert.Equal(byte.MaxValue, cooked.Green);
        Assert.Equal(byte.MaxValue, cooked.Blue);
        Assert.Equal(byte.MaxValue, cooked.Alpha);
        Assert.Equal(0u, cooked.SurfaceFlags);
        Assert.Equal(string.Empty, cooked.TextureRelativePath);
        Assert.Equal("ForwardStandard", cooked.VariantName);
    }

    /// <summary>Verifies the sole reported dependency exactly matches all envelope identity fields and its derived path.</summary>
    [Fact]
    public void CookMaterial_ReportsDependencyMatchingEnvelopeIdentity() {
        PlatformMaterialCookResult result = new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#FFFFFFFF")));
        DecodedMaterial cooked = Decode(result);
        PlatformShaderDependency dependency = Assert.Single(result.ReferencedShaderDependencies);

        Assert.Equal(cooked.ShaderAssetId, dependency.ShaderAssetId);
        Assert.Equal(cooked.VertexProgramName, dependency.VertexProgramName);
        Assert.Equal(cooked.PixelProgramName, dependency.PixelProgramName);
        Assert.Equal(cooked.VariantName, dependency.VariantName);
        Ps3ShaderArtifactIdentity recomputedIdentity = new(
            dependency.ShaderAssetId,
            dependency.VertexProgramName,
            dependency.PixelProgramName,
            dependency.VariantName);
        Assert.Equal("cooked/" + recomputedIdentity.RelativePath, cooked.ShaderRelativePath);
        Assert.Equal("ForwardStandardShader", dependency.ShaderAssetId);
        Assert.Equal("ForwardStandardShader.vs", dependency.VertexProgramName);
        Assert.Equal("ForwardStandardShader.ps", dependency.PixelProgramName);
        Assert.Equal("ForwardStandard", dependency.VariantName);
    }

    /// <summary>Verifies valid staged texture paths are preserved in the material payload.</summary>
    [Fact]
    public void CookMaterial_PreservesValidTexturePath() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.TextureRelativePathFieldId] = "cooked/textures/fixture.hasset";

        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Equal("cooked/textures/fixture.hasset", cooked.TextureRelativePath);
    }

    /// <summary>Verifies an explicitly empty texture path retains the intentional default-white-texture selection.</summary>
    [Fact]
    public void CookMaterial_AcceptsExplicitEmptyTexturePath() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.TextureRelativePathFieldId] = string.Empty;

        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Equal(string.Empty, cooked.TextureRelativePath);
    }

    /// <summary>Verifies present whitespace-only texture paths reach validation instead of becoming the empty default.</summary>
    /// <param name="texturePath">Whitespace-only path value supplied by the material request.</param>
    [Theory]
    [InlineData(" ")]
    [InlineData(" \t ")]
    public void CookMaterial_RejectsPresentWhitespaceTexturePath(string texturePath) {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.TextureRelativePathFieldId] = texturePath;

        Assert.Throws<ArgumentException>(() => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));
    }

    /// <summary>Verifies a present null texture value reaches the writer and fails instead of selecting the empty default.</summary>
    [Fact]
    public void CookMaterial_RejectsPresentNullTexturePath() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.TextureRelativePathFieldId] = null;

        Assert.Throws<ArgumentNullException>(() => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));
    }

    /// <summary>Verifies traversal in a cooked texture path is rejected rather than normalized.</summary>
    [Fact]
    public void CookMaterial_RejectsInvalidTexturePath() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.TextureRelativePathFieldId] = "textures/../secret.hasset";

        Assert.Throws<ArgumentException>(() => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));
    }

    /// <summary>Verifies custom shader requests fail with an actionable error instead of falling back.</summary>
    [Fact]
    public void CookMaterial_RejectsExplicitCustomShaderRequest() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields["use-custom-shader"] = "true";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains("custom shader", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("ps3-standard", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Verifies an explicit false custom-shader selection retains the fixed standard identity.</summary>
    [Fact]
    public void CookMaterial_AcceptsExplicitFalseCustomShaderRequest() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields["use-custom-shader"] = "false";

        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Equal("ForwardStandardShader", cooked.ShaderAssetId);
        Assert.Equal("ForwardStandard", cooked.VariantName);
    }

    /// <summary>Verifies malformed nonblank custom-shader boolean values fail instead of defaulting.</summary>
    [Fact]
    public void CookMaterial_RejectsMalformedCustomShaderBoolean() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields["use-custom-shader"] = "sometimes";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains("use-custom-shader", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies the editor's unrelated Mesh variant handoff cannot change the PS3 standard shader selection.</summary>
    [Fact]
    public void CookMaterial_IgnoresUnrelatedMeshVariantField() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields["variant"] = "Mesh";

        DecodedMaterial cooked = Decode(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Equal("ForwardStandardShader", cooked.ShaderAssetId);
        Assert.Equal("ForwardStandard", cooked.VariantName);
    }

    /// <summary>Verifies an unknown lighting choice is named rather than silently defaulted.</summary>
    [Fact]
    public void CookMaterial_RefusesUnknownLightingMode() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.LightingModeFieldId] = "baked";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains(Ps3MaterialSchemaIds.LightingModeFieldId, exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies an unknown vertex-color choice is named rather than silently defaulted.</summary>
    [Fact]
    public void CookMaterial_RefusesUnknownVertexColorMode() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.VertexColorModeFieldId] = "add";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains(Ps3MaterialSchemaIds.VertexColorModeFieldId, exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies malformed authored double-sided booleans fail explicitly.</summary>
    [Fact]
    public void CookMaterial_RefusesMalformedDoubleSidedBoolean() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.DoubleSidedFieldId] = "sometimes";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains(Ps3MaterialSchemaIds.DoubleSidedFieldId, exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies malformed color channels fail as an explicit material error.</summary>
    [Fact]
    public void CookMaterial_RefusesInvalidColor() {
        Assert.Throws<InvalidOperationException>(() => new Ps3PlatformAssetBuilder().CookMaterial(
            CreateRequest(CreateFields("#GG0000"))));
    }

    /// <summary>Verifies materials for another target platform are refused.</summary>
    [Fact]
    public void CookMaterial_RefusesForeignTargetPlatform() {
        PlatformMaterialCookRequest request = new(
            "Materials/Test",
            "Materials/Test.hasset",
            "windows",
            "debug",
            Ps3MaterialSchemaIds.StandardGraphicsProfileId,
            Ps3MaterialSchemaIds.StandardSchemaId,
            CreateFields("#FFFFFFFF"));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(request));

        Assert.Contains("windows", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies another platform's schema is refused by name.</summary>
    [Fact]
    public void CookMaterial_RefusesForeignSchema() {
        PlatformMaterialCookRequest request = new(
            "Materials/Test",
            "Materials/Test.hasset",
            "ps3",
            "debug",
            Ps3MaterialSchemaIds.StandardGraphicsProfileId,
            "dc-standard-v1",
            CreateFields("#FFFFFFFF"));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(request));

        Assert.Contains("dc-standard-v1", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies a different selected graphics profile is refused by name.</summary>
    [Fact]
    public void CookMaterial_RefusesForeignGraphicsProfile() {
        PlatformMaterialCookRequest request = new(
            "Materials/Test",
            "Materials/Test.hasset",
            "ps3",
            "debug",
            "ps3-debug",
            Ps3MaterialSchemaIds.StandardSchemaId,
            CreateFields("#FFFFFFFF"));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(request));

        Assert.Contains("ps3-debug", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Verifies the platform definition publishes its standard material schema and graphics profile.</summary>
    [Fact]
    public void Definition_PublishesTheStandardMaterialSchema() {
        PlatformDefinition definition = Ps3PlatformDefinitionFactory.Create();

        PlatformMaterialSchemaDefinition schema = Assert.Single(definition.MaterialSchemas);
        Assert.Equal(Ps3MaterialSchemaIds.StandardSchemaId, schema.SchemaId);
        Assert.Contains(Ps3MaterialSchemaIds.StandardGraphicsProfileId, schema.GraphicsProfileIds);
    }

    /// <summary>Builds one cook request around authored fields and the standard PS3 schema.</summary>
    /// <param name="fields">Authored material field map.</param>
    /// <returns>Request targeting the PlayStation 3 standard schema.</returns>
    static PlatformMaterialCookRequest CreateRequest(IReadOnlyDictionary<string, string> fields) {
        return new PlatformMaterialCookRequest(
            "Materials/Test",
            "Materials/Test.hasset",
            "ps3",
            "debug",
            Ps3MaterialSchemaIds.StandardGraphicsProfileId,
            Ps3MaterialSchemaIds.StandardSchemaId,
            fields);
    }

    /// <summary>Builds a case-insensitive field map for one base-color value and schema defaults.</summary>
    /// <param name="baseColor">Authored color text.</param>
    /// <returns>Mutable material field map.</returns>
    static Dictionary<string, string> CreateFields(string baseColor) {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            [Ps3MaterialSchemaIds.BaseColorFieldId] = baseColor,
            [Ps3MaterialSchemaIds.TextureRelativePathFieldId] = string.Empty,
            [Ps3MaterialSchemaIds.LightingModeFieldId] = Ps3MaterialSchemaIds.LightingUnlitValue,
            [Ps3MaterialSchemaIds.DoubleSidedFieldId] = "false",
            [Ps3MaterialSchemaIds.VertexColorModeFieldId] = Ps3MaterialSchemaIds.VertexColorIgnoreValue
        };
    }

    /// <summary>Decodes a cooked envelope with test-owned big-endian and strict UTF-8 routines.</summary>
    /// <param name="result">Cook result whose payload is read.</param>
    /// <returns>Decoded wire fields used by the cooker assertions.</returns>
    static DecodedMaterial Decode(PlatformMaterialCookResult result) {
        byte[] bytes = result.CookedMaterialBytes;
        Assert.True(bytes.Length >= 12);
        Assert.Equal(new byte[] { 80, 51, 77, 84 }, bytes[..4]);
        Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(4, 4)));
        Assert.Equal((uint)bytes.Length, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(8, 4)));

        int offset = 12;
        string materialAssetId = ReadString(bytes, ref offset);
        string rendererFamilyId = ReadString(bytes, ref offset);
        string shaderAssetId = ReadString(bytes, ref offset);
        string vertexProgramName = ReadString(bytes, ref offset);
        string pixelProgramName = ReadString(bytes, ref offset);
        string variantName = ReadString(bytes, ref offset);
        string shaderRelativePath = ReadString(bytes, ref offset);
        byte red = bytes[offset++];
        byte green = bytes[offset++];
        byte blue = bytes[offset++];
        byte alpha = bytes[offset++];
        uint surfaceFlags = ReadUInt32(bytes, ref offset);
        string textureRelativePath = ReadString(bytes, ref offset);
        Assert.Equal(bytes.Length, offset);

        return new DecodedMaterial {
            MaterialAssetId = materialAssetId,
            RendererFamilyId = rendererFamilyId,
            ShaderAssetId = shaderAssetId,
            VertexProgramName = vertexProgramName,
            PixelProgramName = pixelProgramName,
            VariantName = variantName,
            ShaderRelativePath = shaderRelativePath,
            Red = red,
            Green = green,
            Blue = blue,
            Alpha = alpha,
            SurfaceFlags = surfaceFlags,
            TextureRelativePath = textureRelativePath
        };
    }

    /// <summary>Reads one strict length-prefixed UTF-8 string from the test cursor.</summary>
    /// <param name="bytes">Envelope bytes.</param>
    /// <param name="offset">Cursor position, advanced past the string.</param>
    /// <returns>Decoded string value.</returns>
    static string ReadString(byte[] bytes, ref int offset) {
        uint byteLength = ReadUInt32(bytes, ref offset);
        int length = checked((int)byteLength);
        Assert.True(offset <= bytes.Length - length);
        string value = new UTF8Encoding(false, true).GetString(bytes, offset, length);
        offset = checked(offset + length);
        return value;
    }

    /// <summary>Reads one big-endian U32 from the test cursor.</summary>
    /// <param name="bytes">Envelope bytes.</param>
    /// <param name="offset">Cursor position, advanced by four bytes.</param>
    /// <returns>Decoded unsigned integer.</returns>
    static uint ReadUInt32(byte[] bytes, ref int offset) {
        Assert.True(offset <= bytes.Length - sizeof(uint));
        uint value = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, sizeof(uint)));
        offset = checked(offset + sizeof(uint));
        return value;
    }

    /// <summary>Stores independently decoded P3MT fields for material cooker assertions.</summary>
    sealed class DecodedMaterial {
        /// <summary>Gets or initializes the exact material asset identifier.</summary>
        public string MaterialAssetId { get; init; }

        /// <summary>Gets or initializes the renderer family identifier.</summary>
        public string RendererFamilyId { get; init; }

        /// <summary>Gets or initializes the shader asset identifier.</summary>
        public string ShaderAssetId { get; init; }

        /// <summary>Gets or initializes the selected vertex program name.</summary>
        public string VertexProgramName { get; init; }

        /// <summary>Gets or initializes the selected pixel program name.</summary>
        public string PixelProgramName { get; init; }

        /// <summary>Gets or initializes the selected shader variant name.</summary>
        public string VariantName { get; init; }

        /// <summary>Gets or initializes the path derived from the four shader identity strings.</summary>
        public string ShaderRelativePath { get; init; }

        /// <summary>Gets or initializes the raw red channel byte.</summary>
        public byte Red { get; init; }

        /// <summary>Gets or initializes the raw green channel byte.</summary>
        public byte Green { get; init; }

        /// <summary>Gets or initializes the raw blue channel byte.</summary>
        public byte Blue { get; init; }

        /// <summary>Gets or initializes the raw alpha channel byte.</summary>
        public byte Alpha { get; init; }

        /// <summary>Gets or initializes the big-endian surface flag word.</summary>
        public uint SurfaceFlags { get; init; }

        /// <summary>Gets or initializes the optional content-relative diffuse texture path.</summary>
        public string TextureRelativePath { get; init; }
    }
}
