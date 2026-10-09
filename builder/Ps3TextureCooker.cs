using helengine;
using helengine.editor;

namespace helengine.ps3.builder;

/// <summary>Writes standard GTF files from processed RGBA textures with deterministic RSX layouts and mipmaps.</summary>
public sealed class Ps3TextureCooker {
    /// <summary>Converts a processed source into native texels; the historical RGBA setting remains an engine asset.</summary>
    public byte[] Cook(TextureAsset texture, TextureAssetProcessorSettings settings) {
        ArgumentNullException.ThrowIfNull(texture); ArgumentNullException.ThrowIfNull(settings);
        _ = Ps3TextureCookSettingsSerializer.Deserialize(Ps3TextureCookSettingsSerializer.Serialize(settings));
        if (texture.ColorFormat != TextureAssetColorFormat.Rgba32 || texture.Colors == null || texture.Colors.Length != checked(texture.Width * texture.Height * 4))
            throw new InvalidDataException("PS3 cooking requires a processed RGBA32 texture.");
        if (settings.ColorFormatId == "Rgba32") return helengine.files.AssetSerializer.SerializeToBytes(texture);
        PlayStation3TextureFormat format = PlayStation3TextureFormatCatalog.Resolve(settings.ColorFormatId, out bool linear, out bool mipmapped);
        int width = texture.Width, height = texture.Height, mips = mipmapped ? PlayStation3TextureFormatCatalog.GetMipCount(width, height) : 1;
        int pitch = linear ? format.GetRowBytes(width) : 0;
        int length = PlayStation3TextureFormatCatalog.GetPayloadLength(format, width, height, mips, linear, pitch);
        byte[] result = new byte[checked(128 + length)];
        PlayStation3GtfPixelCodec.Write32(result, 0, 0x02000101);
        PlayStation3GtfPixelCodec.Write32(result, 4, (uint)length);
        PlayStation3GtfPixelCodec.Write32(result, 8, 1);
        PlayStation3GtfPixelCodec.Write32(result, 12, 0);
        PlayStation3GtfPixelCodec.Write32(result, 16, 128);
        PlayStation3GtfPixelCodec.Write32(result, 20, (uint)length);
        result[24] = (byte)(0x80 | format.Code | (linear ? 0x20 : 0)); result[25] = (byte)mips; result[26] = 2;
        uint remap = settings.AlphaPrecision == TextureAssetAlphaPrecision.Opaque ? 0xa9e4u : 0xaae4u;
        if (format.Code == 0x14) remap = (remap & 0xff00u) | 0xeeu;
        PlayStation3GtfPixelCodec.Write32(result, 28, remap);
        PlayStation3GtfPixelCodec.Write16(result, 32, texture.Width); PlayStation3GtfPixelCodec.Write16(result, 34, texture.Height);
        PlayStation3GtfPixelCodec.Write16(result, 36, 1); result[38] = 0;
        PlayStation3GtfPixelCodec.Write32(result, 40, (uint)pitch);
        byte[] rgba = texture.Colors; int offset = 128;
        for (int mip = 0; mip < mips; mip++) {
            PlayStation3GtfPixelCodec.EncodeLevel(rgba, width, height, format, linear, pitch, result, offset);
            offset += (linear ? pitch : format.GetRowBytes(width)) * ((height + format.BlockHeight - 1) / format.BlockHeight);
            if (mip + 1 < mips) rgba = Downsample(rgba, width, height);
            width = Math.Max(1, width / 2); height = Math.Max(1, height / 2);
        }
        _ = new PlayStation3GtfTexture(result);
        return result;
    }
    /// <summary>Averages the full source footprint for each mip texel, preserving odd-dimension edge coverage.</summary>
    static byte[] Downsample(byte[] source, int width, int height) {
        int nextWidth = Math.Max(1, width / 2), nextHeight = Math.Max(1, height / 2);
        byte[] result = new byte[checked(nextWidth * nextHeight * 4)];
        for (int y = 0; y < nextHeight; y++) for (int x = 0; x < nextWidth; x++) {
            int x0 = x * width / nextWidth, x1 = (x + 1) * width / nextWidth;
            int y0 = y * height / nextHeight, y1 = (y + 1) * height / nextHeight;
            for (int c = 0; c < 4; c++) {
                long sum = 0; int count = 0;
                for (int sy = y0; sy < y1; sy++) for (int sx = x0; sx < x1; sx++) { sum += source[(sy * width + sx) * 4 + c]; count++; }
                result[(y * nextWidth + x) * 4 + c] = (byte)((sum + count / 2) / count);
            }
        }
        return result;
    }
}
