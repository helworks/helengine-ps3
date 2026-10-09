#include "platform/ps3/Ps3GtfTexture.hpp"

#include <algorithm>

namespace helengine::ps3 {
namespace {
/// <summary>Reads a big-endian field after the containing header has passed its length check.</summary>
std::uint32_t Read32(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) | (static_cast<std::uint32_t>(bytes[1]) << 16u)
        | (static_cast<std::uint32_t>(bytes[2]) << 8u) | bytes[3];
}
/// <summary>Reads a two-byte extent from a validated GTF attribute record.</summary>
std::uint16_t Read16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8u) | bytes[1]);
}
/// <summary>Rejects input with a specific parser error and no descriptor publication.</summary>
bool Fail(std::string& error, const char* reason) { error = reason; return false; }
} // namespace

bool Ps3GtfTexture::TryParse(const std::uint8_t* bytes, std::size_t size, Ps3GtfTexture& texture, std::string& error) {
    error.clear();
    if (bytes == nullptr || size < 48u || size > 65u * 1024u * 1024u) return Fail(error, "GTF header is truncated or exceeds 65 MiB");
    const std::uint32_t version = Read32(bytes);
    if (version != 0x02000101u && version != 0x02000000u && version != 0x02000001u && version != 0x02000100u)
        return Fail(error, "unsupported GTF version");
    if (Read32(bytes + 8u) != 1u) return Fail(error, "runtime GTF requires exactly one texture attribute");
    if ((bytes[24] & 0x80u) == 0u) return Fail(error, "runtime GTF requires borderless texture storage");
    if (bytes[26] != 2u || bytes[27] != 0u || Read16(bytes + 36u) != 1u)
        return Fail(error, "runtime texture assets require a non-cube 2D GTF");
    if ((bytes[24] & 0x40u) != 0u) return Fail(error, "engine texture shaders require normalized coordinates");
    const std::uint8_t code = bytes[24] & 0x1fu;
    std::uint32_t blockBytes = 0u, blockWidth = 1u, blockHeight = 1u;
    bool compressed = false;
    switch (code) {
        case 0x01: blockBytes = 1u; break;
        case 0x02: case 0x03: case 0x04: case 0x0b: case 0x0f: case 0x12: case 0x13:
        case 0x14: case 0x17: case 0x1d: blockBytes = 2u; break;
        case 0x05: case 0x10: case 0x11: case 0x15: case 0x1c: case 0x1e: case 0x1f: blockBytes = 4u; break;
        case 0x1a: blockBytes = 8u; break;
        case 0x1b: blockBytes = 16u; break;
        case 0x06: case 0x07: case 0x08: blockBytes = code == 0x06 ? 8u : 16u; blockWidth = blockHeight = 4u; compressed = true; break;
        case 0x0d: case 0x0e: blockBytes = 4u; blockWidth = 2u; compressed = true; break;
        case 0x18: case 0x19: blockBytes = 2u; break;
        default: return Fail(error, "reserved or unsupported RSX texture encoding");
    }
    Ps3GtfTexture candidate;
    candidate.Format = bytes[24] & 0x7fu; candidate.MipCount = bytes[25];
    candidate.Width = Read16(bytes + 32u); candidate.Height = Read16(bytes + 34u);
    candidate.Remap = Read32(bytes + 28u); candidate.Pitch = Read32(bytes + 40u);
    if (candidate.Width == 0u || candidate.Height == 0u || candidate.Width > 4096u || candidate.Height > 4096u)
        return Fail(error, "GTF dimensions must be 1..4096");
    if ((candidate.Remap & ~0x1ffffu) != 0u) return Fail(error, "GTF remap contains reserved bits");
    for (unsigned channel = 0u; channel < 4u; ++channel) {
        if (((candidate.Remap >> (8u + channel * 2u)) & 3u) == 3u) return Fail(error, "GTF remap contains a reserved operation");
    }
    const std::uint32_t selectors = candidate.Remap & 0xffu;
    if ((code == 0x1au || code == 0x1bu || code == 0x1cu || code == 0x1fu) && selectors != 0xe4u)
        return Fail(error, "floating RSX channels require identity remap selectors");
    if ((code == 0x14u || code == 0x15u || code == 0x18u || code == 0x19u)
        && selectors != 0xe4u && selectors != 0x4eu && selectors != 0xeeu && selectors != 0x44u)
        return Fail(error, "RSX 16-bit channel-pair remap contains invalid selectors");
    const bool linear = (candidate.Format & 0x20u) != 0u;
    if (!linear && !compressed && ((candidate.Width & (candidate.Width - 1u)) != 0u || (candidate.Height & (candidate.Height - 1u)) != 0u))
        return Fail(error, "swizzled GTF requires power-of-two dimensions");
    if (candidate.Pitch > 0xfffffu || (linear &&
        (candidate.Pitch < ((candidate.Width + blockWidth - 1u) / blockWidth) * blockBytes || candidate.Pitch % blockBytes != 0u)))
        return Fail(error, "GTF row pitch is invalid");
    std::uint32_t width = candidate.Width, height = candidate.Height, maximumMips = 1u;
    while (width > 1u || height > 1u) { width = std::max(1u, width / 2u); height = std::max(1u, height / 2u); ++maximumMips; }
    if (candidate.MipCount == 0u || candidate.MipCount > maximumMips) return Fail(error, "GTF mip count exceeds its dimensions");
    width = candidate.Width; height = candidate.Height;
    std::uint64_t required = 0u;
    for (unsigned mip = 0u; mip < candidate.MipCount; ++mip) {
        const std::uint32_t rowBytes = linear ? candidate.Pitch : ((width + blockWidth - 1u) / blockWidth) * blockBytes;
        required += static_cast<std::uint64_t>(rowBytes) * ((height + blockHeight - 1u) / blockHeight);
        width = std::max(1u, width / 2u); height = std::max(1u, height / 2u);
    }
    const std::uint32_t offset = Read32(bytes + 16u), length = Read32(bytes + 20u);
    if (offset < 48u || length < required || length > 64u * 1024u * 1024u || offset > size || length > size - offset)
        return Fail(error, "GTF native payload or complete mip chain lies outside the file");
    candidate.Pixels = bytes + offset; candidate.PixelBytes = length;
    texture = candidate;
    return true;
}

} // namespace helengine::ps3
