using helengine;
using helengine.editor;
using helengine.ps3.builder;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Requests;
using FilesAssetSerializer = helengine.files.AssetSerializer;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies target numeric policies survive PS3 texture cooking and real builder staging independently of authoring precision.</summary>
public sealed class Ps3TextureNumericPolicyTests : IDisposable {
    /// <summary>Fixed physical temporary root avoids macOS's /tmp alias while preserving production's filesystem-link guard.</summary>
    readonly string Root = Path.Combine(OperatingSystem.IsWindows() ? "C:\\temp" : OperatingSystem.IsMacOS() ? "/private/tmp" : "/tmp", "helengine", "ps3", "texture-policy-tests");

    /// <summary>Independent source texels whose exact RGBA bytes must survive authoring decode and cooking.</summary>
    readonly byte[] Pixels = [11, 22, 33, 255, 44, 55, 66, 255];

    /// <summary>Creates source data explicitly serialized with the editor's 64-bit numeric contract.</summary>
    public Ps3TextureNumericPolicyTests() {
        if (Directory.Exists(Root)) Directory.Delete(Root, true);
        Directory.CreateDirectory(Root);
        using TextureAsset source = CreateTexture();
        File.WriteAllBytes(SourcePath, FilesAssetSerializer.SerializeToBytes(source, new NumericPrecisionPolicy(64, 64)));
    }

    /// <summary>Gets the source atlas/texture written by the authoring serializer, independently of the consuming target.</summary>
    string SourcePath => Path.Combine(Root, "authoring64.hetex");

    /// <summary>Writes and reads a real HELE texture under each explicit target numeric contract.</summary>
    [Theory]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    public void Cooker_Rgba32PreservesTargetEnvelopeAndPixels(int bits) {
        using TextureAsset source = CreateTexture();
        NumericPrecisionPolicy policy = new(bits, bits);

        byte[] payload = new Ps3TextureCooker().Cook(source, Settings("Rgba32"), policy);

        AssertPayload(payload, policy, "source");
    }

    /// <summary>Preserves the legacy overload's host representation for existing callers while production supplies its own policy.</summary>
    [Fact]
    public void Cooker_LegacyOverloadPreservesHostEnvelope() {
        using TextureAsset source = CreateTexture();
        AssertPayload(new Ps3TextureCooker().Cook(source, Settings("Rgba32")), RealRepresentation.Policy, "source");
    }

    /// <summary>Keeps the fixed RSX GTF format independent of the configurable HELE numeric envelope.</summary>
    [Theory]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    public void Cooker_NativeGtfRetainsItsFixedHeaderAndTexels(int bits) {
        using TextureAsset source = CreateTexture();
        byte[] payload = new Ps3TextureCooker().Cook(source, Settings("Ps3.A8R8G8B8.Linear"), new NumericPrecisionPolicy(bits, bits));

        Assert.Equal(new byte[] { 2, 0, 1, 1 }, payload[..4]);
        Assert.Equal(Pixels, PlayStation3GtfPixelCodec.Decode(new PlayStation3GtfTexture(payload)));
    }

    /// <summary>Decodes 64-bit editor textures and font atlases before emitting the independently selected target policy.</summary>
    [Theory]
    [InlineData(16, "texture")]
    [InlineData(32, "texture")]
    [InlineData(64, "texture")]
    [InlineData(16, "font-atlas-texture")]
    [InlineData(32, "font-atlas-texture")]
    [InlineData(64, "font-atlas-texture")]
    public void Executor_ConvertsAuthoring64ToRequestedTarget(int bits, string sourceKind) {
        NumericPrecisionPolicy policy = new(bits, bits);
        PlatformCookWorkItem item = CreateItem(sourceKind, "textures/cooked.bin", "cooked-texture");
        string package = Path.Combine(Root, "package");
        byte[] originalSource = File.ReadAllBytes(SourcePath);

        PlatformBuildArtifact receipt = new Ps3PlatformCookWorkItemExecutor().Execute(item, package, policy);

        AssertPayload(File.ReadAllBytes(Path.Combine(package, receipt.RelativePath)), policy, "cooked-texture");
        Assert.Equal(originalSource, File.ReadAllBytes(SourcePath));
        Assert.Equal("textures/cooked.bin", receipt.RelativePath);
    }

    /// <summary>Runs actual builder cooking and staging with a native handoff probe, proving request policy reaches both texture kinds.</summary>
    [Theory]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    public async Task Builder_PropagatesRequestPolicyThroughCookAndStaging(int bits) {
        string working = Path.Combine(Root, "working");
        string package = Path.Combine(working, "package-source");
        Directory.CreateDirectory(package);
        List<PlatformBuildArtifact> artifacts = [new("scenes/main.bin", "main", "hash", "scene", "default")];
        foreach (string variant in new[] { "Textured", "RoundedShape" }) {
            Ps3ShaderArtifactIdentity identity = new("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant);
            artifacts.Add(new(identity.RuntimeRelativePath, identity.RelativePath, "hash", "shader", variant));
        }
        foreach (PlatformBuildArtifact artifact in artifacts) {
            string destination = Path.Combine(package, artifact.RelativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            File.WriteAllBytes(destination, [1, 2, 3]);
        }
        PlatformCookWorkItem[] items = [CreateItem("texture", "textures/cooked.bin", "texture-id"), CreateItem("font-atlas-texture", "fonts/atlas.bin", "atlas-id")];
        PlatformBuildManifest manifest = new(1, "project", "1.0", "0.4.0", "ps3", "0.2.0", "main",
            [new("main", "Main", "scenes/main.bin", [], [])], [], artifacts.ToArray(), [], [], new PlatformContainerWritePlan("ps3", []), items, PlatformBuildRuntimeFeatureManifest.Empty);
        PlatformCookProfile profile = new("ps3", "PS3", new("rsx", "gtf", "pcm", "hele", PlatformSerializationEndianness.BigEndian));
        PlatformBuildRequest request = new(manifest, [new("ps3", "ps3", "ps3", "ps3")], [profile], Path.Combine(Root, "output"), working,
            "debug", "ps3-standard", "default", new Dictionary<string, string>(), new Dictionary<string, string>(),
            new Dictionary<string, string> { [PlatformCodegenSettingIds.RealStorageBits] = bits.ToString(), [PlatformCodegenSettingIds.RealArithmeticBits] = bits.ToString() },
            Path.Combine(Root, "generated"));
        Ps3TextureBuildProbe probe = new();

        var report = await new Ps3PlatformAssetBuilder(probe).BuildAsync(request, probe, probe, CancellationToken.None);

        Assert.True(report.Succeeded, string.Join("; ", probe.Diagnostics.Select(diagnostic => diagnostic.Message)));
        Assert.NotEmpty(probe.ContentRoot);
        foreach (PlatformCookWorkItem item in items) {
            AssertPayload(File.ReadAllBytes(Path.Combine(probe.ContentRoot, item.OutputRelativePath)), new NumericPrecisionPolicy(bits, bits), item.OutputLogicalArtifactId);
        }
    }

    /// <summary>Removes only the fixed fixture tree owned by these tests.</summary>
    public void Dispose() {
        if (Directory.Exists(Root)) Directory.Delete(Root, true);
    }

    /// <summary>Creates one small processed RGBA source with independently specified pixel bytes.</summary>
    TextureAsset CreateTexture() => new() {
        Id = "source", Width = 2, Height = 1, Colors = (byte[])Pixels.Clone(), PaletteColors = [],
        ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8
    };

    /// <summary>Creates a legal full-alpha texture setting for either the legacy envelope or fixed native GTF format.</summary>
    static TextureAssetProcessorSettings Settings(string format) => new() { MaxResolution = 4096, ColorFormatId = format, AlphaPrecision = TextureAssetAlphaPrecision.A8 };

    /// <summary>Creates a real builder work item referencing the independent authoring source.</summary>
    PlatformCookWorkItem CreateItem(string sourceKind, string destination, string assetId) => new(
        sourceKind, SourcePath, sourceKind, "ps3", "runtime-texture", destination, assetId, "source-hash", "settings-hash",
        Ps3TextureCookSettingsSerializer.Serialize(Settings("Rgba32")), []);

    /// <summary>Reads the policy header and strict runtime payload, then verifies asset identity, dimensions and literal pixels.</summary>
    void AssertPayload(byte[] payload, NumericPrecisionPolicy policy, string assetId) {
        using MemoryStream stream = new(payload, false);
        EngineBinaryHeader header = EngineBinaryHeaderSerializer.Read(stream);
        using (EngineBinaryReader reader = EngineBinaryReader.Create(stream, header.Endianness)) {
            Assert.Equal(policy.Identity, RealBinaryCodec.ReadPolicy(reader).Identity);
        }
        stream.Position = 0;
        using TextureAsset actual = Assert.IsType<TextureAsset>(PackagedAssetBinarySerializer.Deserialize(stream, policy));
        Assert.Equal(assetId, actual.Id);
        Assert.Equal(2, actual.Width);
        Assert.Equal(1, actual.Height);
        Assert.Equal(Pixels, actual.Colors);
    }
}
