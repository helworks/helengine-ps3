using helengine.baseplatform.Definitions;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies authored material translation and the state the platform refuses.</summary>
public sealed class Ps3MaterialCookerTests {
    /// <summary>Confirms an authored color reaches the cooked payload channel for channel.</summary>
    [Fact]
    public void CookMaterial_EmitsAuthoredBaseColor() {
        PlatformMaterialCookResult result = new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#C02010FF")));

        helengine.PlatformMaterialAsset cooked = Deserialize(result);
        Assert.Equal(Ps3MaterialSchemaIds.StandardGraphicsProfileId, cooked.RendererFamilyId);
        Assert.Equal((byte)0xC0, cooked.BaseColorR);
        Assert.Equal((byte)0x20, cooked.BaseColorG);
        Assert.Equal((byte)0x10, cooked.BaseColorB);
        Assert.Equal(byte.MaxValue, cooked.BaseColorA);
    }

    /// <summary>Confirms the short color form is accepted and given an opaque alpha.</summary>
    [Fact]
    public void CookMaterial_TreatsShortColorFormAsOpaque() {
        PlatformMaterialCookResult result = new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#18D040")));

        helengine.PlatformMaterialAsset cooked = Deserialize(result);
        Assert.Equal((byte)0x18, cooked.BaseColorR);
        Assert.Equal((byte)0xD0, cooked.BaseColorG);
        Assert.Equal((byte)0x40, cooked.BaseColorB);
        Assert.Equal(byte.MaxValue, cooked.BaseColorA);
    }

    /// <summary>
    /// Confirms a translucent base color survives cooking. Demodisc authors these, and the
    /// cooked payload carries alpha, so refusing them would reject real content.
    /// </summary>
    [Fact]
    public void CookMaterial_PreservesTranslucentAlpha() {
        PlatformMaterialCookResult result = new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#40FF40FF".Remove(7) + "40")));

        Assert.Equal((byte)0x40, Deserialize(result).BaseColorA);
    }

    /// <summary>Confirms double-sided and vertex-color choices are recorded rather than dropped.</summary>
    [Fact]
    public void CookMaterial_RecordsSurfaceState() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.DoubleSidedFieldId] = "true";
        fields[Ps3MaterialSchemaIds.VertexColorModeFieldId] = Ps3MaterialSchemaIds.VertexColorModulateValue;
        fields[Ps3MaterialSchemaIds.LightingModeFieldId] = Ps3MaterialSchemaIds.LightingLitValue;

        helengine.PlatformMaterialAsset cooked = Deserialize(new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.True(cooked.DoubleSided);
        Assert.True(cooked.UseVertexColor);
        Assert.True(cooked.Lit);
    }

    /// <summary>
    /// Confirms a material carrying none of the schema's own fields still cooks. No authored
    /// material selects the PlayStation 3 schema yet, so every field arrives as its default.
    /// </summary>
    [Fact]
    public void CookMaterial_AppliesSchemaDefaultsWhenNoFieldsAreAuthored() {
        helengine.PlatformMaterialAsset cooked = Deserialize(
            new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(new Dictionary<string, string>())));

        Assert.Equal(byte.MaxValue, cooked.BaseColorR);
        Assert.Equal(byte.MaxValue, cooked.BaseColorA);
        Assert.False(cooked.DoubleSided);
        Assert.False(cooked.UseVertexColor);
        Assert.False(cooked.Lit);
        Assert.Equal(string.Empty, cooked.TextureRelativePath);
    }

    /// <summary>Confirms cooking reports no shader dependency, which is what PlayStation 3 authors.</summary>
    [Fact]
    public void CookMaterial_ReportsNoShaderDependency() {
        PlatformMaterialCookResult result = new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(CreateFields("#FFFFFFFF")));

        Assert.Empty(result.ReferencedShaderDependencies);
    }

    /// <summary>Confirms a texture binding is refused, because the build stages no textures.</summary>
    [Fact]
    public void CookMaterial_RefusesTextureBinding() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.TextureRelativePathFieldId] = "textures/cube.png";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains("texture bindings", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Confirms an unknown choice value is named rather than silently defaulted.</summary>
    [Fact]
    public void CookMaterial_RefusesUnknownLightingMode() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[Ps3MaterialSchemaIds.LightingModeFieldId] = "baked";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => new Ps3PlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains(Ps3MaterialSchemaIds.LightingModeFieldId, exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Confirms another platform's schema is refused by name.</summary>
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

    /// <summary>
    /// Confirms the platform definition publishes the schema. Without it the editor discards
    /// this builder for material work, which is what broke the end-to-end build before.
    /// </summary>
    [Fact]
    public void Definition_PublishesTheStandardMaterialSchema() {
        PlatformDefinition definition = Ps3PlatformDefinitionFactory.Create();

        PlatformMaterialSchemaDefinition schema = Assert.Single(definition.MaterialSchemas);
        Assert.Equal(Ps3MaterialSchemaIds.StandardSchemaId, schema.SchemaId);
        Assert.Contains(Ps3MaterialSchemaIds.StandardGraphicsProfileId, schema.GraphicsProfileIds);
    }

    /// <summary>Builds one cook request around the supplied authored fields.</summary>
    /// <param name="fields">Authored field map.</param>
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

    /// <summary>Builds the authored field map for one base color.</summary>
    /// <param name="baseColor">Authored base color value.</param>
    /// <returns>Mutable field map carrying the schema defaults.</returns>
    static Dictionary<string, string> CreateFields(string baseColor) {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            [Ps3MaterialSchemaIds.BaseColorFieldId] = baseColor,
            [Ps3MaterialSchemaIds.TextureRelativePathFieldId] = string.Empty,
            [Ps3MaterialSchemaIds.LightingModeFieldId] = Ps3MaterialSchemaIds.LightingUnlitValue,
            [Ps3MaterialSchemaIds.DoubleSidedFieldId] = "false",
            [Ps3MaterialSchemaIds.VertexColorModeFieldId] = Ps3MaterialSchemaIds.VertexColorIgnoreValue
        };
    }

    /// <summary>Reads the cooked bytes back into a platform material asset.</summary>
    /// <param name="result">Cook result to read.</param>
    /// <returns>Deserialized cooked material.</returns>
    static helengine.PlatformMaterialAsset Deserialize(PlatformMaterialCookResult result) {
        return Assert.IsType<helengine.PlatformMaterialAsset>(
            helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
    }
}
