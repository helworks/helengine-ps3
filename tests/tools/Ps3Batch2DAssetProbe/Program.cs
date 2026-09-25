using System.Security.Cryptography;
using System.Text.Json;
using helengine;
using FileAssetSerializer = helengine.files.AssetSerializer;
using FilesFontAssetBinarySerializer = helengine.files.FontAssetBinarySerializer;

/// <summary>
/// Reads cooked texture and font inputs through Helengine's production serializers for focused native asset diagnostics.
/// </summary>
internal static class Program {
    /// <summary>
    /// Starts the probe, writes one JSON result for asset commands, or bounded status lines for self-test.
    /// </summary>
    /// <param name="args">Command and absolute source paths supplied by the caller.</param>
    /// <returns>Zero on success and one when validation or decoding fails.</returns>
    private static int Main(string[] args) {
        try {
            if (args.Length == 1 && args[0] == "self-test") {
                return RunSelfTests();
            }

            if (args.Length == 2 && args[0] == "texture") {
                WriteJson(ProbeTexture(RequireAbsoluteFile(args[1])));
                return 0;
            }

            if (args.Length == 3 && args[0] == "font") {
                string fontPath = RequireAbsoluteFile(args[1]);
                string contentRoot = RequireAbsoluteDirectory(args[2]);
                WriteJson(ProbeFont(fontPath, contentRoot));
                return 0;
            }

            throw new ArgumentException("Usage: self-test | texture <absolute-file> | font <absolute-file> <absolute-content-root>");
        } catch (Exception exception) {
            Console.Error.WriteLine($"FAIL {exception.GetType().Name}: {exception.Message}");
            return 1;
        }
    }

    /// <summary>
    /// Decodes a serialized texture asset and validates its dimensions, supported representation, and complete payload.
    /// </summary>
    /// <param name="stream">Readable asset stream positioned at its beginning.</param>
    /// <returns>The owned texture asset decoded by the production reader.</returns>
    private static TextureAsset ReadTexture(Stream stream) {
        Asset decoded = FileAssetSerializer.Deserialize(stream);
        if (decoded is not TextureAsset texture) {
            (decoded as IDisposable)?.Dispose();
            throw new InvalidDataException($"Expected TextureAsset, found {decoded.GetType().FullName}.");
        }

        try {
            EnsureEndOfStream(stream);
            if (texture.Width <= 0 || texture.Height <= 0) {
                throw new InvalidDataException("Texture dimensions must both be positive.");
            }
            if (texture.ColorFormat != TextureAssetColorFormat.Rgba32) {
                throw new InvalidDataException($"Unsupported texture color format '{texture.ColorFormat}'.");
            }

            int expectedByteCount = checked((int)texture.Width * texture.Height * 4);
            if (texture.Colors == null || texture.Colors.Length != expectedByteCount) {
                throw new InvalidDataException($"RGBA32 texture requires exactly {expectedByteCount} color bytes.");
            }

            return texture;
        } catch {
            texture.Dispose();
            throw;
        }
    }

    /// <summary>
    /// Builds the JSON-ready texture record from an absolute cooked or imported texture file.
    /// </summary>
    /// <param name="path">Absolute path to the serialized texture.</param>
    /// <returns>Input identity, texture metadata, and exact RGBA bytes.</returns>
    private static object ProbeTexture(string path) {
        byte[] sourceBytes = File.ReadAllBytes(path);
        using var stream = new MemoryStream(sourceBytes, writable: false);
        using TextureAsset texture = ReadTexture(stream);
        return DescribeTexture(path, sourceBytes, texture);
    }

    /// <summary>
    /// Decodes font metrics and obtains its actual atlas from embedded source data or its canonical cooked-relative path.
    /// </summary>
    /// <param name="path">Absolute path to the serialized font.</param>
    /// <param name="contentRoot">Absolute root against which an external cooked atlas path is resolved.</param>
    /// <returns>Font metadata, sorted glyph records, and decoded atlas data.</returns>
    private static object ProbeFont(string path, string contentRoot) {
        byte[] sourceBytes = File.ReadAllBytes(path);
        using var stream = new MemoryStream(sourceBytes, writable: false);
        FontAsset font = FilesFontAssetBinarySerializer.Deserialize(stream);
        try {
            EnsureEndOfStream(stream);
            ValidateFont(font);

            string? atlasPath = null;
            string atlasSourceKind;
            byte[] atlasSourceBytes;
            TextureAsset atlas;
            if (font.SourceTextureAsset != null) {
                atlas = font.SourceTextureAsset;
                ValidateTexture(atlas);
                atlasSourceKind = "embedded";
                atlasSourceBytes = sourceBytes;
            } else {
                if (string.IsNullOrWhiteSpace(font.CookedAtlasTextureRelativePath)) {
                    throw new InvalidDataException("Font contains neither source atlas data nor an external cooked atlas path.");
                }
                atlasPath = ResolveContentPath(contentRoot, font.CookedAtlasTextureRelativePath);
                atlasSourceKind = "external-file";
                atlasSourceBytes = File.ReadAllBytes(atlasPath);
                using var atlasStream = new MemoryStream(atlasSourceBytes, writable: false);
                atlas = ReadTexture(atlasStream);
            }

            try {
                object atlasDescription = DescribeTexture(
                    atlasPath ?? path, atlasSourceBytes, atlas, atlasSourceKind);
                return new {
                    inputPath = path,
                    inputSha256 = Sha256(sourceBytes),
                    type = font.GetType().FullName,
                    fontInfo = new { name = font.FontInfo.Name, lineSpacing = font.FontInfo.LineSpacing, spaceWidth = font.FontInfo.SpaceWidth },
                    lineHeight = font.LineHeight,
                    atlasWidth = font.AtlasWidth,
                    atlasHeight = font.AtlasHeight,
                    cookedAtlasTextureRelativePath = font.CookedAtlasTextureRelativePath,
                    atlasPath,
                    atlas = atlasDescription,
                    glyphs = font.Characters
                        .OrderBy(entry => entry.Key)
                        .Select(entry => new {
                            character = (int)entry.Key,
                            sourceRect = new[] { entry.Value.SourceRect.X, entry.Value.SourceRect.Y, entry.Value.SourceRect.Z, entry.Value.SourceRect.W },
                            offsetY = entry.Value.OffsetY,
                            advanceWidth = entry.Value.AdvanceWidth,
                            bearingX = entry.Value.BearingX,
                            bearingY = entry.Value.BearingY
                        }).ToArray()
                };
            } finally {
                if (atlasPath != null) {
                    atlas.Dispose();
                }
            }
        } finally {
            font.Dispose();
        }
    }

    /// <summary>
    /// Validates a texture object supplied directly by the font serializer without converting its pixel representation.
    /// </summary>
    /// <param name="texture">Embedded source atlas.</param>
    private static void ValidateTexture(TextureAsset texture) {
        if (texture.Width <= 0 || texture.Height <= 0) {
            throw new InvalidDataException("Texture dimensions must both be positive.");
        }
        if (texture.ColorFormat != TextureAssetColorFormat.Rgba32) {
            throw new InvalidDataException($"Unsupported texture color format '{texture.ColorFormat}'.");
        }
        int expectedByteCount = checked((int)texture.Width * texture.Height * 4);
        if (texture.Colors == null || texture.Colors.Length != expectedByteCount) {
            throw new InvalidDataException($"RGBA32 texture requires exactly {expectedByteCount} color bytes.");
        }
    }

    /// <summary>
    /// Rejects absent font data and non-finite line, spacing, or glyph metrics before reporting them.
    /// </summary>
    /// <param name="font">Decoded font asset whose serialized values are being inspected.</param>
    private static void ValidateFont(FontAsset font) {
        if (font.FontInfo == null) {
            throw new InvalidDataException("Font asset has no font information.");
        }
        if (font.AtlasWidth <= 0 || font.AtlasHeight <= 0) {
            throw new InvalidDataException("Font atlas dimensions must both be positive.");
        }
        if (font.Characters == null) {
            throw new InvalidDataException("Font asset has no glyph map.");
        }
        RequireFinite(font.FontInfo.SpaceWidth, "font space width");
        RequireFinite(font.LineHeight, "font line height");
        foreach (KeyValuePair<char, FontChar> entry in font.Characters) {
            FontChar glyph = entry.Value;
            RequireFinite(glyph.SourceRect.X, "glyph source rectangle X");
            RequireFinite(glyph.SourceRect.Y, "glyph source rectangle Y");
            RequireFinite(glyph.SourceRect.Z, "glyph source rectangle width");
            RequireFinite(glyph.SourceRect.W, "glyph source rectangle height");
            RequireFinite(glyph.OffsetY, "glyph offset Y");
            RequireFinite(glyph.AdvanceWidth, "glyph advance width");
            RequireFinite(glyph.BearingX, "glyph bearing X");
            RequireFinite(glyph.BearingY, "glyph bearing Y");
        }
    }

    /// <summary>
    /// Rejects a serialized floating-point metric that cannot be represented as a finite JSON number.
    /// </summary>
    /// <param name="value">Metric value to validate.</param>
    /// <param name="name">Metric label used in the explicit error.</param>
    private static void RequireFinite(float value, string name) {
        if (!float.IsFinite(value)) {
            throw new InvalidDataException($"{name} must be finite.");
        }
    }

    /// <summary>
    /// Creates the common metadata representation for a validated RGBA32 texture.
    /// </summary>
    /// <param name="path">Actual source path associated with the texture.</param>
    /// <param name="sourceBytes">Serialized texture bytes used to identify the input.</param>
    /// <param name="texture">Decoded production texture asset.</param>
    /// <returns>JSON-ready texture details, including the exact raw RGBA payload.</returns>
    private static object DescribeTexture(string path, byte[] sourceBytes, TextureAsset texture, string sourceKind = "file") => new {
        inputPath = path,
        inputSha256 = Sha256(sourceBytes),
        sourceKind,
        type = texture.GetType().FullName,
        width = texture.Width,
        height = texture.Height,
        colorFormat = texture.ColorFormat.ToString(),
        alphaPrecision = texture.AlphaPrecision.ToString(),
        rgbaSha256 = Sha256(texture.Colors),
        rgbaBase64 = Convert.ToBase64String(texture.Colors)
    };

    /// <summary>
    /// Resolves a font atlas reference and rejects rooted paths or any canonical path outside the content root.
    /// </summary>
    /// <param name="contentRoot">Canonical absolute content directory.</param>
    /// <param name="relativePath">Serialized runtime-relative atlas path.</param>
    /// <returns>Canonical absolute atlas path inside the content root.</returns>
    private static string ResolveContentPath(string contentRoot, string relativePath) {
        if (Path.IsPathRooted(relativePath)) {
            throw new InvalidDataException("Cooked atlas path must be relative to the supplied content root.");
        }

        string root = Path.GetFullPath(contentRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string candidate = Path.GetFullPath(Path.Combine(root, relativePath));
        string rootPrefix = root + Path.DirectorySeparatorChar;
        StringComparison comparison = OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal;
        if (!candidate.StartsWith(rootPrefix, comparison)) {
            throw new InvalidDataException("Cooked atlas path resolves outside the supplied content root.");
        }
        if (!File.Exists(candidate)) {
            throw new FileNotFoundException("Cooked atlas texture referenced by the font was not found.", candidate);
        }
        return candidate;
    }

    /// <summary>
    /// Executes production-serializer round trips and validates corrupt data, wrong asset types, and traversal rejection.
    /// </summary>
    /// <returns>Zero when each focused check passes, otherwise one.</returns>
    private static int RunSelfTests() {
        int failures = 0;
        failures += Check("texture serializer preserves 2x2 RGBA", TestTextureRoundTrip);
        failures += Check("font serializer preserves glyph metrics", TestFontRoundTrip);
        failures += Check("truncated texture input is rejected", TestTruncatedInput);
        failures += Check("font data is rejected by texture reader", TestIncorrectType);
        failures += Check("atlas path traversal is rejected", TestPathTraversal);
        Console.WriteLine(failures == 0 ? "PASS all 5 self-tests" : $"FAIL {failures} of 5 self-tests");
        return failures == 0 ? 0 : 1;
    }

    /// <summary>
    /// Runs one self-test and emits only its bounded pass or failure summary.
    /// </summary>
    /// <param name="name">Short human-readable case label.</param>
    /// <param name="test">Assertion action to execute.</param>
    /// <returns>Zero on success and one on failure.</returns>
    private static int Check(string name, Action test) {
        try {
            test();
            Console.WriteLine($"PASS {name}");
            return 0;
        } catch (Exception exception) {
            Console.WriteLine($"FAIL {name}: {exception.GetType().Name}");
            return 1;
        }
    }

    /// <summary>
    /// Verifies the production texture serializer preserves known dimensions, format, and all 16 RGBA bytes.
    /// </summary>
    private static void TestTextureRoundTrip() {
        byte[] expected = Enumerable.Range(0, 16).Select(value => (byte)value).ToArray();
        using var source = new TextureAsset { Width = 2, Height = 2, ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8, Colors = expected };
        byte[] encoded = FileAssetSerializer.SerializeToBytes(source);
        using var stream = new MemoryStream(encoded, writable: false);
        using TextureAsset decoded = ReadTexture(stream);
        Require(decoded.Width == 2 && decoded.Height == 2 && decoded.ColorFormat == TextureAssetColorFormat.Rgba32, "Texture metadata did not round-trip.");
        Require(decoded.Colors.SequenceEqual(expected), "Texture RGBA bytes did not round-trip.");
    }

    /// <summary>
    /// Verifies production font serialization preserves one known glyph and its float metrics exactly.
    /// </summary>
    private static void TestFontRoundTrip() {
        var chars = new Dictionary<char, FontChar> { ['A'] = new FontChar(new float4(0.1f, 0.2f, 0.3f, 0.4f), 5.5f, 12.25f, -1.5f, 7.75f) };
        using var source = new FontAsset(new FontInfo("Probe", 14, 4.5f), null, chars, 16.5f, 2, 2) {
            SourceTextureAsset = new TextureAsset { Width = 2, Height = 2, ColorFormat = TextureAssetColorFormat.Rgba32,
                AlphaPrecision = TextureAssetAlphaPrecision.A8, Colors = new byte[16] }
        };
        using var encoded = new MemoryStream();
        FilesFontAssetBinarySerializer.Serialize(encoded, source);
        encoded.Position = 0;
        using FontAsset decoded = FilesFontAssetBinarySerializer.Deserialize(encoded);
        EnsureEndOfStream(encoded);
        ValidateFont(decoded);
        Require(decoded.Characters.TryGetValue('A', out FontChar glyph), "Known glyph was missing after round-trip.");
        Require(glyph.SourceRect.X == 0.1f && glyph.SourceRect.Y == 0.2f && glyph.SourceRect.Z == 0.3f && glyph.SourceRect.W == 0.4f,
            "Glyph source rectangle did not round-trip.");
        Require(glyph.OffsetY == 5.5f && glyph.AdvanceWidth == 12.25f && glyph.BearingX == -1.5f && glyph.BearingY == 7.75f,
            "Glyph metrics did not round-trip.");
        using var nonfiniteFont = new FontAsset(new FontInfo("Probe", 14, float.NaN), null, new Dictionary<char, FontChar>(), 16f, 2, 2);
        ExpectFailure(() => ValidateFont(nonfiniteFont));
        using var nullGlyphMap = new FontAsset(new FontInfo("Probe", 14, 4f), null, null!, 16f, 2, 2);
        ExpectFailure(() => ValidateFont(nullGlyphMap));
    }

    /// <summary>
    /// Verifies a truncated production texture payload fails decoding instead of returning partial data.
    /// </summary>
    private static void TestTruncatedInput() {
        using var source = new TextureAsset { Width = 2, Height = 2, ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8, Colors = new byte[16] };
        byte[] encoded = FileAssetSerializer.SerializeToBytes(source);
        using var truncated = new MemoryStream(encoded[..^1], writable: false);
        ExpectFailure(() => ReadTexture(truncated));
    }

    /// <summary>
    /// Verifies the font serializer's record kind is rejected when the texture reader expects a texture.
    /// </summary>
    private static void TestIncorrectType() {
        var source = new FontAsset(new FontInfo("Probe", 14, 4f), null, new Dictionary<char, FontChar>(), 16f, 2, 2) {
            SourceTextureAsset = new TextureAsset { Width = 2, Height = 2, ColorFormat = TextureAssetColorFormat.Rgba32,
                AlphaPrecision = TextureAssetAlphaPrecision.A8, Colors = new byte[16] }
        };
        using (source)
        using (var encoded = new MemoryStream()) {
            FilesFontAssetBinarySerializer.Serialize(encoded, source);
            encoded.Position = 0;
            ExpectFailure(() => ReadTexture(encoded));
        }
    }

    /// <summary>
    /// Verifies canonical path resolution rejects an atlas reference that escapes the selected content root.
    /// </summary>
    private static void TestPathTraversal() {
        try {
            ResolveContentPath(Path.GetTempPath(), ".." + Path.DirectorySeparatorChar + "outside.hetex");
        } catch (InvalidDataException exception) when (exception.Message.Contains("outside the supplied content root", StringComparison.Ordinal)) {
            return;
        }
        throw new InvalidOperationException("Expected an outside-root InvalidDataException for atlas path traversal.");
    }

    /// <summary>
    /// Requires a boolean assertion to hold and reports its specific diagnostic when it does not.
    /// </summary>
    /// <param name="condition">Condition that must be true.</param>
    /// <param name="message">Failure explanation.</param>
    private static void Require(bool condition, string message) {
        if (!condition) {
            throw new InvalidOperationException(message);
        }
    }

    /// <summary>
    /// Requires an action to throw, ensuring malformed inputs cannot pass silently.
    /// </summary>
    /// <param name="action">Operation expected to reject its input.</param>
    private static void ExpectFailure(Action action) {
        try {
            action();
        } catch (Exception) {
            return;
        }
        throw new InvalidOperationException("Expected the operation to reject its input.");
    }

    /// <summary>
    /// Requires stream position to equal its length after asset decoding.
    /// </summary>
    /// <param name="stream">Decoded asset stream.</param>
    private static void EnsureEndOfStream(Stream stream) {
        if (!stream.CanSeek || stream.Position != stream.Length) {
            throw new InvalidDataException("Asset contains trailing or unread data.");
        }
    }

    /// <summary>
    /// Validates and canonicalizes an absolute existing file argument.
    /// </summary>
    /// <param name="value">Command-line path.</param>
    /// <returns>Canonical file path.</returns>
    private static string RequireAbsoluteFile(string value) {
        if (!Path.IsPathFullyQualified(value)) {
            throw new ArgumentException("Input file paths must be absolute.");
        }
        string path = Path.GetFullPath(value);
        if (!File.Exists(path)) {
            throw new FileNotFoundException("Input asset file was not found.", path);
        }
        return path;
    }

    /// <summary>
    /// Validates and canonicalizes an absolute existing content directory argument.
    /// </summary>
    /// <param name="value">Command-line directory path.</param>
    /// <returns>Canonical directory path.</returns>
    private static string RequireAbsoluteDirectory(string value) {
        if (!Path.IsPathFullyQualified(value)) {
            throw new ArgumentException("Content root must be an absolute directory path.");
        }
        string path = Path.GetFullPath(value);
        if (!Directory.Exists(path)) {
            throw new DirectoryNotFoundException($"Content root was not found: {path}");
        }
        return path;
    }

    /// <summary>
    /// Returns the lowercase SHA256 digest for an exact byte sequence.
    /// </summary>
    /// <param name="bytes">Bytes to hash.</param>
    /// <returns>64-character lowercase hexadecimal digest.</returns>
    private static string Sha256(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    /// <summary>
    /// Writes a compact, invariant JSON document to stdout for caller-controlled redirection.
    /// </summary>
    /// <param name="value">JSON-ready probe output.</param>
    private static void WriteJson(object value) => Console.WriteLine(JsonSerializer.Serialize(value));
}
