#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Validated standard GTF descriptor borrowing a bounded native texture payload until upload completes.</summary>
struct Ps3GtfTexture {
    /// <summary>Native pixel encoding and linear storage flag, without Sony's descriptor border bit.</summary>
    std::uint8_t Format = 0u;
    /// <summary>Stored mip level count including the largest image.</summary>
    std::uint8_t MipCount = 0u;
    /// <summary>Base image width in texels.</summary>
    std::uint16_t Width = 0u;
    /// <summary>Base image height in texels.</summary>
    std::uint16_t Height = 0u;
    /// <summary>ARGB component remapping retained from the source descriptor.</summary>
    std::uint32_t Remap = 0u;
    /// <summary>Linear byte stride shared by all mip levels, or ignored for packed layouts.</summary>
    std::uint32_t Pitch = 0u;
    /// <summary>Borrowed immutable bytes beginning at the first mip level.</summary>
    const std::uint8_t* Pixels = nullptr;
    /// <summary>Exact declared texture storage copied into the owned RSX allocation.</summary>
    std::size_t PixelBytes = 0u;

    /// <summary>Validates a single normalized 2D texture, including every mip range, before exposing any payload.</summary>
    static bool TryParse(const std::uint8_t* bytes, std::size_t size, Ps3GtfTexture& texture, std::string& error);
};

} // namespace helengine::ps3
