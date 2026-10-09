using helengine;
using helengine.editor;
using helengine.baseplatform.Definitions;

namespace helengine.ps3.builder;

/// <summary>Publishes RSX layouts and legal alpha combinations as builder-owned texture capabilities.</summary>
public static class Ps3TextureFormatCapabilities {
    /// <summary>Builds native setting identifiers for linear and swizzled storage, with optional complete mip chains.</summary>
    public static PlatformTextureFormatCapabilityDefinition Create() {
        List<string> ids = ["Rgba32", "Ps3.GtfSource"];
        List<PlatformTextureFormatCombinationDefinition> combinations = [new("Rgba32", TextureAssetAlphaPrecision.Opaque),
            new("Rgba32", TextureAssetAlphaPrecision.A8), new("Ps3.GtfSource", TextureAssetAlphaPrecision.Opaque), new("Ps3.GtfSource", TextureAssetAlphaPrecision.A8)];
        foreach (PlayStation3TextureFormat format in PlayStation3TextureFormatCatalog.Formats) {
            foreach (string layout in format.IsCompressed ? new[] { "Linear" } : new[] { "Linear", "Swizzled" }) {
                foreach (string suffix in new[] { string.Empty, ".Mipmapped" }) {
                    string id = $"Ps3.{format.Name}.{layout}{suffix}"; ids.Add(id);
                    foreach (TextureAssetAlphaPrecision alpha in GetAlphaPrecisions(format)) combinations.Add(new(id, alpha));
                }
            }
        }
        return new(ids.ToArray(), [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary, TextureAssetAlphaPrecision.A4, TextureAssetAlphaPrecision.A8], combinations.ToArray());
    }
    /// <summary>Enumerates alpha precision requests representable by an ordinary conversion to this encoding.</summary>
    public static IReadOnlyList<TextureAssetAlphaPrecision> GetAlphaPrecisions(PlayStation3TextureFormat format) {
        return format.AlphaPrecision switch {
            TextureAssetAlphaPrecision.Opaque => [TextureAssetAlphaPrecision.Opaque],
            TextureAssetAlphaPrecision.Binary => [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary],
            TextureAssetAlphaPrecision.A4 => [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary, TextureAssetAlphaPrecision.A4],
            _ => [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary, TextureAssetAlphaPrecision.A4, TextureAssetAlphaPrecision.A8]
        };
    }
}
