using System.Text.Json;
using helengine.editor;

namespace helengine.ps3.builder;

/// <summary>Validates and serializes the platform-specific texture settings persisted by the editor build graph.</summary>
public static class Ps3TextureCookSettingsSerializer {
    /// <summary>Serializes the existing texture setting schema while retaining native RSX string identifiers.</summary>
    public static string Serialize(TextureAssetProcessorSettings settings) {
        ArgumentNullException.ThrowIfNull(settings);
        return JsonSerializer.Serialize(new Dictionary<string, object> { ["maxResolution"] = settings.MaxResolution,
            ["colorFormat"] = settings.ColorFormatId, ["alphaPrecision"] = settings.AlphaPrecision.ToString() });
    }
    /// <summary>Requires explicit extent, color and alpha fields and rejects incompatible native alpha requests.</summary>
    public static TextureAssetProcessorSettings Deserialize(string serialized) {
        using JsonDocument document = JsonDocument.Parse(serialized);
        JsonElement root = document.RootElement;
        if (!root.TryGetProperty("maxResolution", out JsonElement maximum) || !root.TryGetProperty("colorFormat", out JsonElement color)
            || !root.TryGetProperty("alphaPrecision", out JsonElement alpha)) throw new InvalidDataException("PS3 texture settings require maxResolution, colorFormat and alphaPrecision.");
        int limit = maximum.GetInt32(); string id = color.GetString();
        if (limit < 0 || limit > 4096 || !Enum.TryParse(alpha.GetString(), out helengine.TextureAssetAlphaPrecision precision) || !Enum.IsDefined(precision))
            throw new InvalidDataException("PS3 texture maximum resolution or alpha precision is invalid.");
        if (id != "Rgba32" && id != "Ps3.GtfSource") {
            PlayStation3TextureFormat format = PlayStation3TextureFormatCatalog.Resolve(id, out _, out _);
            if (!Ps3TextureFormatCapabilities.GetAlphaPrecisions(format).Contains(precision)) throw new InvalidDataException($"{id} cannot store {precision} alpha.");
        } else if (precision != helengine.TextureAssetAlphaPrecision.A8 && precision != helengine.TextureAssetAlphaPrecision.Opaque) {
            throw new InvalidDataException("PS3 RGBA and source-preserving GTF settings accept Opaque or A8.");
        }
        return new TextureAssetProcessorSettings { MaxResolution = limit, ColorFormatId = id, AlphaPrecision = precision };
    }
}
