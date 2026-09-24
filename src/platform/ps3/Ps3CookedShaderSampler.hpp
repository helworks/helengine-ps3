#pragma once

#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Owns an authored texture/sampler pair and its validated pixel-program texture unit.</summary>
struct Ps3CookedShaderSampler {
    /// <summary>Gets the authored texture resource name.</summary>
    std::string TextureName;

    /// <summary>Gets the authored texture register binding.</summary>
    std::uint32_t TextureBinding;

    /// <summary>Gets the authored sampler-state resource name.</summary>
    std::string SamplerName;

    /// <summary>Gets the authored sampler register binding.</summary>
    std::uint32_t SamplerBinding;

    /// <summary>Gets the generated SDK sampler name validated against the pixel program.</summary>
    std::string GeneratedName;

    /// <summary>Gets the exact source texture dimension label.</summary>
    std::string Dimension;

    /// <summary>Gets the actual RSX pixel texture unit assigned by the SDK compiler.</summary>
    std::uint32_t TextureUnit;

    /// <summary>Gets the exact SDK sampler data-type code stored in the artifact.</summary>
    std::uint32_t SdkDataType;
};

} // namespace helengine::ps3
