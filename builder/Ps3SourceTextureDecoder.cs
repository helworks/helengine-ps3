using ImageMagick;
using helengine;
using helengine.editor;

namespace helengine.ps3.builder;

/// <summary>Decodes authored images and serialized engine font atlases into owned RGBA textures for PS3 cooking.</summary>
public sealed class Ps3SourceTextureDecoder {
    /// <summary>Sniffs engine assets before selecting a native container or the portable raster image decoder.</summary>
    public TextureAsset Decode(string path) {
        using FileStream stream = File.OpenRead(path);
        if (stream.Length > 65 * 1024 * 1024) throw new InvalidDataException("PS3 source texture exceeds the input size limit.");
        byte[] magic = new byte[4]; stream.ReadExactly(magic); stream.Position = 0;
        if (magic.AsSpan().SequenceEqual("HELE"u8)) {
            Asset asset = helengine.files.AssetSerializer.Deserialize(stream);
            if (asset is not TextureAsset texture) {
                if (asset is IDisposable disposable) disposable.Dispose();
                throw new InvalidDataException("PS3 texture source does not contain a TextureAsset.");
            }
            using (texture) {
                return new TextureAsset { Id = texture.Id, Width = texture.Width, Height = texture.Height,
                    Colors = TextureAssetPixelCodec.DecodeToRgba32(texture), PaletteColors = Array.Empty<byte>(),
                    ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8 };
            }
        }
        string extension = Path.GetExtension(path);
        if (extension.Equals(".gtf", StringComparison.OrdinalIgnoreCase)) return new PlayStation3GtfTextureImporter().ImportTexture(stream);
        if (extension.Equals(".tim", StringComparison.OrdinalIgnoreCase)) return new PlayStationTimTextureImporter().ImportTexture(stream);
        if (extension.Equals(".pvr", StringComparison.OrdinalIgnoreCase)) return new DreamcastPvrTextureImporter().ImportTexture(stream);
        MagickReadSettings readSettings = new();
        if (extension.Equals(".tga", StringComparison.OrdinalIgnoreCase) || extension.Equals(".targa", StringComparison.OrdinalIgnoreCase)) readSettings.Format = MagickFormat.Tga;
        MagickImageInfo info = new(stream, readSettings);
        if (info.Width < 1 || info.Height < 1 || info.Width > 4096 || info.Height > 4096) throw new InvalidDataException("PS3 source textures require dimensions 1..4096.");
        stream.Position = 0;
        using MagickImage bitmap = new(stream, readSettings);
        using IPixelCollection<byte> pixels = bitmap.GetPixels();
        byte[] rgba = pixels.ToByteArray("RGBA");
        return new TextureAsset { Width = (ushort)bitmap.Width, Height = (ushort)bitmap.Height, Colors = rgba,
            PaletteColors = Array.Empty<byte>(), ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8 };
    }
}
