using helengine;
using helengine.editor;
using helengine.baseplatform.Manifest;
using System.Security.Cryptography;

namespace helengine.ps3.builder;

/// <summary>Executes requested PS3 texture cooks and returns declared receipts for content staging.</summary>
public sealed class Ps3PlatformCookWorkItemExecutor {
    /// <summary>Validates ownership and paths, cooks the resolved texture setting and declares its exact resulting bytes.</summary>
    public PlatformBuildArtifact Execute(PlatformCookWorkItem item, string packageSourceRoot) {
        ArgumentNullException.ThrowIfNull(item);
        if (item.TargetPlatformId != "ps3" || item.TargetArtifactKind != "runtime-texture"
            || (item.SourceAssetKind != "texture" && item.SourceAssetKind != "font-atlas-texture"))
            throw new InvalidDataException($"Unsupported PS3 cook work item '{item.WorkItemId}'.");
        string relativePath = Ps3ContentStager.NormalizeRelativePath(item.OutputRelativePath);
        string root = Path.GetFullPath(packageSourceRoot);
        string destination = Path.GetFullPath(Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!destination.StartsWith(root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("PS3 texture destination escapes package source.");
        Ps3ContentStager.ValidateNoReparsePoints(destination);
        TextureAssetProcessorSettings settings = Ps3TextureCookSettingsSerializer.Deserialize(item.SerializedPlatformSettings);
        byte[] bytes;
        if (settings.ColorFormatId == "Ps3.GtfSource") {
            FileInfo source = new(item.SourceAssetPath);
            if (source.Length > 65 * 1024 * 1024) throw new InvalidDataException("GTF source exceeds its input limit.");
            bytes = File.ReadAllBytes(source.FullName);
            PlayStation3GtfTexture texture = new(bytes);
            if (PlayStation3GtfTexture.Read32(bytes, 8) != 1) throw new InvalidDataException("Source-preserving runtime GTF must contain exactly one texture.");
            if (settings.MaxResolution > 0 && Math.Max(texture.Width, texture.Height) > settings.MaxResolution)
                throw new InvalidDataException("Source-preserving GTF exceeds maximum resolution; select a conversion format to resize.");
        } else {
            using TextureAsset decoded = new Ps3SourceTextureDecoder().Decode(item.SourceAssetPath);
            decoded.Id = item.OutputLogicalArtifactId;
            TextureAsset processed = null;
            try {
                processed = new TextureAssetProcessor().Apply(decoded, new TextureAssetProcessorSettings {
                    MaxResolution = settings.MaxResolution, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = settings.AlphaPrecision });
                bytes = new Ps3TextureCooker().Cook(processed, settings);
            } finally { if (!ReferenceEquals(processed, decoded)) processed?.Dispose(); }
        }
        Directory.CreateDirectory(Path.GetDirectoryName(destination));
        File.WriteAllBytes(destination, bytes);
        return new PlatformBuildArtifact(relativePath, item.OutputLogicalArtifactId, "sha256:" + Convert.ToHexString(SHA256.HashData(bytes)), "runtime-texture", item.SettingsHash);
    }
}
