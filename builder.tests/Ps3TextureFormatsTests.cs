using helengine;
using helengine.editor;
using helengine.ps3.builder;
using helengine.baseplatform.Manifest;

namespace helengine.ps3.builder.tests;

/// <summary>Exercises RSX encoding, GTF interoperability and cooking receipts independently of GPU allocation.</summary>
public sealed class Ps3TextureFormatsTests {
    /// <summary>Enumerates every supported native encoding/layout pair with and without complete mipmaps.</summary>
    public static IEnumerable<object[]> NativeFormats() {
        foreach (PlayStation3TextureFormat format in PlayStation3TextureFormatCatalog.Formats)
            foreach (string layout in format.IsCompressed ? new[] { "Linear" } : new[] { "Linear", "Swizzled" })
                foreach (string suffix in new[] { string.Empty, ".Mipmapped" }) yield return new object[] { $"Ps3.{format.Name}.{layout}{suffix}" };
    }
    /// <summary>Checks that every published encoding produces bounded, importable native storage and matching VRAM estimates.</summary>
    [Theory]
    [MemberData(nameof(NativeFormats))]
    public void Cook_AllNativeFormatsProduceValidGtf(string id) {
        using TextureAsset source = CreateTexture(8, 4);
        TextureAssetProcessorSettings settings = Settings(id);
        byte[] bytes = new Ps3TextureCooker().Cook(source, settings);
        PlayStation3GtfTexture gtf = new(bytes);
        Assert.Equal(8, gtf.Width); Assert.Equal(4, gtf.Height);
        Assert.Equal(id.EndsWith(".Mipmapped", StringComparison.Ordinal) ? 4 : 1, gtf.MipCount);
        using TextureAsset preview = new PlayStation3GtfTextureImporter().ImportTexture(new MemoryStream(bytes));
        Assert.Equal(8 * 4 * 4, preview.Colors.Length);
        Assert.True(PlayStation3TextureFormatCatalog.TryCalculateMemory(id, 8, 4, out long vram));
        Assert.Equal(bytes.Length - 128, vram);
        string fixtures = Path.Combine(WorkspaceBuildRoot(), "fixtures"); Directory.CreateDirectory(fixtures);
        File.WriteAllBytes(Path.Combine(fixtures, id + ".gtf"), bytes);
    }
    /// <summary>Checks literal bytes required by the external GTF and big-endian A8R8G8B8 contracts.</summary>
    [Fact]
    public void Cook_Argb8UsesStandardHeaderAndNativeChannelOrder() {
        using TextureAsset source = CreateTexture(1, 1);
        source.Colors = new byte[] { 0x12, 0x34, 0x56, 0x78 };
        byte[] bytes = new Ps3TextureCooker().Cook(source, Settings("Ps3.A8R8G8B8.Linear"));
        Assert.Equal(new byte[] { 2, 0, 1, 1, 0, 0, 0, 4, 0, 0, 0, 1 }, bytes[..12]);
        Assert.Equal(128u, PlayStation3GtfTexture.Read32(bytes, 16));
        Assert.Equal(0xa5, bytes[24]); Assert.Equal(0xaae4u, PlayStation3GtfTexture.Read32(bytes, 28));
        Assert.Equal(new byte[] { 0x78, 0x12, 0x34, 0x56 }, bytes[128..]);
        Assert.Equal(source.Colors, PlayStation3GtfPixelCodec.Decode(new(bytes)));
    }
    /// <summary>Confirms that float-depth encodings use RSX E4M12 and truncated float24 rather than IEEE half storage.</summary>
    [Theory]
    [InlineData("Ps3.DEPTH16_FLOAT.Linear", 0x70, 0x00)]
    [InlineData("Ps3.DEPTH24_D8_FLOAT.Linear", 0x7f, 0x00)]
    public void Cook_FloatDepthOneHasHardwareRepresentation(string id, byte first, byte second) {
        using TextureAsset source = CreateTexture(1, 1); source.Colors = new byte[] { 255, 255, 255, 255 };
        byte[] bytes = new Ps3TextureCooker().Cook(source, Settings(id));
        Assert.Equal(first, bytes[128]); Assert.Equal(second, bytes[129]);
        Assert.Equal(new byte[] { 255, 255, 255, 255 }, PlayStation3GtfPixelCodec.Decode(new(bytes)));
    }
    /// <summary>Confirms constant linear row pitch, including the smallest mip, instead of a tightly packed CPU mip chain.</summary>
    [Fact]
    public void Cook_LinearMipsRetainBasePitch() {
        using TextureAsset source = CreateTexture(8, 4);
        PlayStation3GtfTexture texture = new(new Ps3TextureCooker().Cook(source, Settings("Ps3.A8R8G8B8.Linear.Mipmapped")));
        Assert.Equal(32, texture.Pitch); Assert.Equal(32 * (4 + 2 + 1 + 1), texture.Pixels.Length);
        Assert.Equal(new byte[16], texture.Pixels.AsSpan(128 + 16, 16).ToArray());
    }

    /// <summary>Checks that linear DXT block mips retain the base block-row pitch instead of using packed swizzled-level sizes.</summary>
    [Fact]
    public void Cook_LinearDxtMipsRetainBlockPitch() {
        using TextureAsset source = CreateTexture(8, 4);
        PlayStation3GtfTexture texture = new(new Ps3TextureCooker().Cook(source, Settings("Ps3.DXT1.Linear.Mipmapped")));
        Assert.Equal(16, texture.Pitch); Assert.Equal(64, texture.Pixels.Length);
        Assert.Equal(new byte[8], texture.Pixels.AsSpan(24, 8).ToArray());
        Assert.Equal(40, PlayStation3TextureFormatCatalog.GetPayloadLength(texture.Format, 8, 4, 4, false, 0));
    }
    /// <summary>Checks independently selected Morton addresses for wide float texels, where channels cross interleaved lanes.</summary>
    [Fact]
    public void WideFloatSwizzleUses32BitLanes() {
        PlayStation3TextureFormat format = PlayStation3TextureFormatCatalog.ResolveCode(0x1b);
        Assert.Equal(0, PlayStation3GtfPixelCodec.GetByteOffset(format, false, 0, 2, 2, 0, 0, 0));
        Assert.Equal(4, PlayStation3GtfPixelCodec.GetByteOffset(format, false, 0, 2, 2, 0, 0, 4));
        Assert.Equal(16, PlayStation3GtfPixelCodec.GetByteOffset(format, false, 0, 2, 2, 0, 0, 8));
        Assert.Equal(32, PlayStation3GtfPixelCodec.GetByteOffset(format, false, 0, 2, 2, 1, 0, 0));
        Assert.Equal(8, PlayStation3GtfPixelCodec.GetByteOffset(format, false, 0, 2, 2, 0, 1, 0));
    }

    /// <summary>Checks a literal external DXT1 block without relying on the engine encoder to generate its expected color.</summary>
    [Fact]
    public void Import_ExternalDxt1BlockIsLittleEndian() {
        using TextureAsset source = CreateTexture(4, 4);
        byte[] bytes = new Ps3TextureCooker().Cook(source, Settings("Ps3.DXT1.Linear"));
        new byte[] { 0x00, 0xf8, 0x00, 0x00, 0, 0, 0, 0 }.CopyTo(bytes, 128);
        byte[] rgba = PlayStation3GtfPixelCodec.Decode(new(bytes));
        for (int p = 0; p < 16; p++) Assert.Equal(new byte[] { 255, 0, 0, 255 }, rgba.AsSpan(p * 4, 4).ToArray());
    }

    /// <summary>Checks RSX channel-pair remapping and its XXXY override using independent literal channel words.</summary>
    [Theory]
    [InlineData(0xaae4u, 17, 238, 17, 238)]
    [InlineData(0x1aae4u, 17, 17, 17, 238)]
    [InlineData(0xaa4eu, 238, 17, 238, 17)]
    public void Import_ChannelPairRemapMatchesRsx(uint remap, byte r, byte g, byte b, byte a) {
        using TextureAsset source = CreateTexture(1, 1);
        byte[] bytes = new Ps3TextureCooker().Cook(source, Settings("Ps3.Y16_X16.Linear"));
        PlayStation3GtfPixelCodec.Write32(bytes, 28, remap);
        new byte[] { 0x11, 0x11, 0xee, 0xee }.CopyTo(bytes, 128);
        Assert.Equal(new byte[] { r, g, b, a }, PlayStation3GtfPixelCodec.Decode(new(bytes)));
    }

    /// <summary>Checks ordinary RGBA conversion assigns channels according to hardware readout rather than the encoding's naming order.</summary>
    [Theory]
    [InlineData("Ps3.G8B8.Linear")]
    [InlineData("Ps3.Y16_X16.Linear")]
    [InlineData("Ps3.HILO8.Linear")]
    [InlineData("Ps3.HILO_S8.Linear")]
    [InlineData("Ps3.Y16_X16_FLOAT.Linear")]
    public void Cook_TwoChannelFormatsRetainSourceRedAndGreen(string id) {
        using TextureAsset source = CreateTexture(1, 1); source.Colors = new byte[] { 51, 204, 0, 255 };
        byte[] rgba = PlayStation3GtfPixelCodec.Decode(new(new Ps3TextureCooker().Cook(source, Settings(id))));
        Assert.InRange(rgba[0], 50, 52); Assert.InRange(rgba[1], 203, 205); Assert.Equal(rgba[0], rgba[2]); Assert.Equal(255, rgba[3]);
    }

    /// <summary>Checks X16 grayscale conversion selects the real 16-bit channel for all RGB outputs.</summary>
    [Fact]
    public void Cook_X16SamplesAsOpaqueGrayscale() {
        using TextureAsset source = CreateTexture(1, 1); source.Colors = new byte[] { 51, 204, 0, 255 };
        Assert.Equal(new byte[] { 51, 51, 51, 255 }, PlayStation3GtfPixelCodec.Decode(new(new Ps3TextureCooker().Cook(source, Settings("Ps3.X16.Linear")))));
    }

    /// <summary>Checks that only valid alpha/layout combinations reach editor controls and both texture asset kinds use builder cooking.</summary>
    [Fact]
    public void Capabilities_ExposeEveryEncodingAndRetainLegacyDefault() {
        Assert.Equal(27, PlayStation3TextureFormatCatalog.Formats.Count);
        helengine.baseplatform.Definitions.PlatformTextureFormatCapabilityDefinition formats = Ps3TextureFormatCapabilities.Create();
        Assert.Equal(96, formats.SupportedColorFormatIds.Length);
        foreach (object[] format in NativeFormats()) Assert.Contains((string)format[0], formats.SupportedColorFormatIds);
        foreach (helengine.baseplatform.Definitions.PlatformAssetCookCapabilityDefinition capability in Ps3PlatformDefinitionFactory.Create().AssetCookCapabilities) {
            Assert.Equal(helengine.baseplatform.Definitions.PlatformAssetCookOwnershipKind.BuilderOwned, capability.OwnershipKind);
            Assert.Equal("Rgba32", Ps3TextureCookSettingsSerializer.Deserialize(capability.DefaultSerializedPlatformSettings).ColorFormatId);
        }
        Assert.Throws<InvalidDataException>(() => Ps3TextureCookSettingsSerializer.Deserialize("{\"maxResolution\":4096,\"colorFormat\":\"Ps3.R5G6B5.Linear\",\"alphaPrecision\":\"A8\"}"));
    }

    /// <summary>Checks raster decoding remains available after PS3 takes ownership of cooking, including compressed authoring sources.</summary>
    [Theory]
    [InlineData(ImageMagick.MagickFormat.Png)]
    [InlineData(ImageMagick.MagickFormat.Tga)]
    [InlineData(ImageMagick.MagickFormat.Dds)]
    [InlineData(ImageMagick.MagickFormat.WebP)]
    public void Execute_CommonRasterSourcesRemainSupported(ImageMagick.MagickFormat format) {
        string root = Path.Combine(WorkspaceBuildRoot(), "image-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
        string path = Path.Combine(root, "source." + format.ToString().ToLowerInvariant());
        using (ImageMagick.MagickImage image = new(ImageMagick.MagickColors.Red, 4, 4)) image.Write(path, format);
        PlatformCookWorkItem item = new("image", path, "texture", "ps3", "runtime-texture", "textures/image.bin", "image-id", "hash", "settings",
            Ps3TextureCookSettingsSerializer.Serialize(Settings("Ps3.A8R8G8B8.Linear")), []);
        new Ps3PlatformCookWorkItemExecutor().Execute(item, Path.Combine(root, "package"));
        byte[] rgba = PlayStation3GtfPixelCodec.Decode(new(File.ReadAllBytes(Path.Combine(root, "package", "textures", "image.bin"))));
        Assert.InRange(rgba[0], 240, 255); Assert.InRange(rgba[1], 0, 15); Assert.InRange(rgba[2], 0, 15); Assert.Equal(255, rgba[3]);
    }
    /// <summary>Rejects hostile dimensions, reserved formats, invalid pitches and truncated mip chains before upload.</summary>
    [Theory]
    [InlineData("version")]
    [InlineData("count")]
    [InlineData("offset")]
    [InlineData("length")]
    [InlineData("mips")]
    [InlineData("pitch")]
    [InlineData("width")]
    [InlineData("cube")]
    [InlineData("volume")]
    [InlineData("unnormalized")]
    [InlineData("format")]
    [InlineData("border")]
    public void Import_RejectsInvalidDescriptors(string corruption) {
        using TextureAsset source = CreateTexture(8, 4);
        byte[] bytes = new Ps3TextureCooker().Cook(source, Settings("Ps3.A8R8G8B8.Linear.Mipmapped"));
        switch (corruption) {
            case "version": bytes[0] = 3; break;
            case "count": PlayStation3GtfPixelCodec.Write32(bytes, 8, uint.MaxValue); break;
            case "offset": PlayStation3GtfPixelCodec.Write32(bytes, 16, uint.MaxValue); break;
            case "length": PlayStation3GtfPixelCodec.Write32(bytes, 20, 1); break;
            case "mips": bytes[25] = 255; break;
            case "pitch": PlayStation3GtfPixelCodec.Write32(bytes, 40, 1); break;
            case "width": PlayStation3GtfPixelCodec.Write16(bytes, 32, 0); break;
            case "cube": bytes[27] = 1; break;
            case "volume": bytes[26] = 3; break;
            case "unnormalized": bytes[24] |= 0x40; break;
            case "format": bytes[24] = 0xa9; break;
            case "border": bytes[24] &= 0x7f; break;
        }
        Assert.Throws<InvalidDataException>(() => new PlayStation3GtfTexture(bytes));
    }
    /// <summary>Preserves signed/HDR source bytes, descriptor remaps and mipmaps through the builder and staging receipt.</summary>
    [Fact]
    public void Execute_SourcePreservingGtfStagesExactBytes() {
        string root = Path.Combine(WorkspaceBuildRoot(), "cook-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
        using TextureAsset texture = CreateTexture(4, 4);
        byte[] bytes = new Ps3TextureCooker().Cook(texture, Settings("Ps3.W32_Z32_Y32_X32_FLOAT.Swizzled.Mipmapped"));
        PlayStation3GtfPixelCodec.Write32(bytes, 128, BitConverter.SingleToUInt32Bits(-3.5f));
        string source = Path.Combine(root, "source.gtf"); File.WriteAllBytes(source, bytes);
        PlatformCookWorkItem item = new("texture", source, "texture", "ps3", "runtime-texture", "textures/native.bin", "texture-id", "source-hash", "settings-hash",
            Ps3TextureCookSettingsSerializer.Serialize(Settings("Ps3.GtfSource")), []);
        string package = Path.Combine(root, "package");
        PlatformBuildArtifact artifact = new Ps3PlatformCookWorkItemExecutor().Execute(item, package);
        string staging = Path.Combine(root, "staging");
        new Ps3ContentStager().Stage([artifact], package, staging, CancellationToken.None);
        Assert.Equal(bytes, File.ReadAllBytes(Path.Combine(staging, "textures", "native.bin")));
    }
    /// <summary>Proves that engine font-atlas sources are decoded and cooked by the same PS3 native path.</summary>
    [Fact]
    public void Execute_EngineFontAtlasProducesNativeGtf() {
        string root = Path.Combine(WorkspaceBuildRoot(), "atlas-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
        using TextureAsset texture = CreateTexture(4, 4);
        string source = Path.Combine(root, "atlas.bin"); File.WriteAllBytes(source, helengine.files.AssetSerializer.SerializeToBytes(texture));
        PlatformCookWorkItem item = new("atlas", source, "font-atlas-texture", "ps3", "runtime-texture", "fonts/atlas.bin", "atlas-id", "hash", "settings",
            Ps3TextureCookSettingsSerializer.Serialize(Settings("Ps3.A4R4G4B4.Linear")), []);
        new Ps3PlatformCookWorkItemExecutor().Execute(item, Path.Combine(root, "package"));
        Assert.Equal(0x03, new PlayStation3GtfTexture(File.ReadAllBytes(Path.Combine(root, "package", "fonts", "atlas.bin"))).Format.Code);
    }

    /// <summary>Checks builder-owned outputs reach native staging even when the editor has no cooked artifact receipt for them.</summary>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task BuildAsync_CooksBeforeNativeHandoffAndRejectsOutputCollisions(bool collision) {
        string root = Path.Combine(WorkspaceBuildRoot(), "build-" + Guid.NewGuid().ToString("N"));
        string working = Path.Combine(root, "working"), package = Path.Combine(working, "package-source"); Directory.CreateDirectory(package);
        using TextureAsset texture = CreateTexture(4, 4);
        string source = Path.Combine(root, "source.bin"); File.WriteAllBytes(source, helengine.files.AssetSerializer.SerializeToBytes(texture));
        PlatformCookWorkItem item = new("texture", source, "texture", "ps3", "runtime-texture", "textures/native.bin", "texture-id", "hash", "settings",
            Ps3TextureCookSettingsSerializer.Serialize(Settings("Ps3.A8R8G8B8.Linear")), []);
        List<PlatformBuildArtifact> artifacts = [new("scenes/main.bin", "main", "hash", "scene", "default")];
        foreach (string variant in new[] { "Textured", "RoundedShape" }) {
            Ps3ShaderArtifactIdentity identity = new("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant);
            artifacts.Add(new(identity.RuntimeRelativePath, identity.RelativePath, "hash", "shader", variant));
        }
        if (collision) artifacts.Add(new(item.OutputRelativePath, "hash", "runtime-texture", "default"));
        foreach (PlatformBuildArtifact artifact in artifacts) {
            string path = Path.Combine(package, artifact.RelativePath.Replace('/', Path.DirectorySeparatorChar));
            Directory.CreateDirectory(Path.GetDirectoryName(path)); File.WriteAllBytes(path, new byte[] { 1, 2, 3 });
        }
        PlatformBuildManifest manifest = new(1, "project", "1.0", "0.4.0", "ps3", "0.2.0", "main",
            [new("main", "Main", "scenes/main.bin", [], [])], [], artifacts.ToArray(), [], [], new PlatformContainerWritePlan("ps3", []), [item], PlatformBuildRuntimeFeatureManifest.Empty);
        helengine.baseplatform.Profiles.PlatformCookProfile profile = new("ps3", "PS3", new("rsx", "gtf", "pcm", "hele", helengine.baseplatform.Profiles.PlatformSerializationEndianness.BigEndian));
        helengine.baseplatform.Requests.PlatformBuildRequest request = new(manifest, [new("ps3", "ps3", "ps3", "ps3")], [profile], Path.Combine(root, "output"), working,
            "release", "ps3-standard", "default", new Dictionary<string, string>(), new Dictionary<string, string>(), new Dictionary<string, string>(), Path.Combine(root, "generated"));
        Ps3TextureBuildProbe probe = new();
        await new Ps3PlatformAssetBuilder(probe).BuildAsync(request, probe, probe, CancellationToken.None);
        if (collision) {
            Assert.Empty(probe.ContentRoot); Assert.Single(probe.Diagnostics);
            Assert.Equal(new byte[] { 1, 2, 3 }, File.ReadAllBytes(Path.Combine(package, "textures", "native.bin")));
        } else {
            Assert.Empty(probe.Diagnostics); Assert.NotEmpty(probe.ContentRoot);
            PlayStation3GtfTexture native = new(File.ReadAllBytes(Path.Combine(probe.ContentRoot, "textures", "native.bin")));
            Assert.Equal(4, native.Width); Assert.Equal(5, native.Format.Code);
        }
    }
    /// <summary>Creates a deterministic multi-channel source whose texel positions expose layout mistakes.</summary>
    static TextureAsset CreateTexture(int width, int height) {
        byte[] rgba = new byte[width * height * 4];
        for (int p = 0; p < width * height; p++) { rgba[p * 4] = (byte)(p * 23); rgba[p * 4 + 1] = (byte)(p * 31); rgba[p * 4 + 2] = (byte)(p * 17); rgba[p * 4 + 3] = 255; }
        return new TextureAsset { Width = (ushort)width, Height = (ushort)height, Colors = rgba, PaletteColors = Array.Empty<byte>(), ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8 };
    }
    /// <summary>Creates a legal alpha configuration for each native encoding or source-preservation setting.</summary>
    static TextureAssetProcessorSettings Settings(string id) {
        TextureAssetAlphaPrecision alpha = id == "Ps3.GtfSource" ? TextureAssetAlphaPrecision.A8
            : PlayStation3TextureFormatCatalog.Resolve(id, out _, out _).AlphaPrecision;
        return new TextureAssetProcessorSettings { MaxResolution = 4096, ColorFormatId = id, AlphaPrecision = alpha };
    }
    /// <summary>Locates the visible task artifact root without directing fixture files to the user temporary directory.</summary>
    static string WorkspaceBuildRoot() => @"C:\dev\helworks\builds\helengine-ps3\texture-formats-20261008";
}
