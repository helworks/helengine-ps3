#pragma once

#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Owns every decoded field from one validated PS3 P3MT standard material artifact.</summary>
struct Ps3CookedMaterial {
    /// <summary>Stable material asset identifier copied byte-for-byte from the P3MT payload.</summary>
    std::string MaterialAssetId;

    /// <summary>Renderer family selected by the material schema; version one requires ps3-standard.</summary>
    std::string RendererFamilyId;

    /// <summary>Shader asset identifier for the fixed ForwardStandard pair.</summary>
    std::string ShaderAssetId;

    /// <summary>Logical vertex program name required by the fixed standard pair.</summary>
    std::string VertexProgramName;

    /// <summary>Logical pixel program name required by the fixed standard pair.</summary>
    std::string PixelProgramName;

    /// <summary>Selected shader variant name; version one requires ForwardStandard.</summary>
    std::string VariantName;

    /// <summary>Canonical content-relative P3SP lookup path derived from the fixed pair identity.</summary>
    std::string ShaderRelativePath;

    /// <summary>Authored base-color red channel in its original 8-bit form.</summary>
    std::uint8_t BaseColorR;

    /// <summary>Authored base-color green channel in its original 8-bit form.</summary>
    std::uint8_t BaseColorG;

    /// <summary>Authored base-color blue channel in its original 8-bit form.</summary>
    std::uint8_t BaseColorB;

    /// <summary>Authored base-color alpha channel in its original 8-bit form.</summary>
    std::uint8_t BaseColorA;

    /// <summary>Preserved version-one surface choices: lit, double-sided and vertex-color bits.</summary>
    std::uint32_t SurfaceFlags;

    /// <summary>Optional validated content-relative diffuse texture path; empty selects white.</summary>
    std::string DiffuseTextureRelativePath;
};

} // namespace helengine::ps3
