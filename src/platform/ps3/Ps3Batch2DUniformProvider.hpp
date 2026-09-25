#pragma once

#include "Ps3CookedShaderUniform.hpp"

#include <cstddef>
#include <string>

namespace helengine::ps3 {

/// <summary>Resolves the exact active SpriteBatchShader projection mapping into SDK packed floats.</summary>
class Ps3Batch2DUniformProvider {
public:
    /// <summary>Validates the projection mapping and copies its already SDK-column-packed matrix unchanged.</summary>
    /// <param name="uniform">One active vertex uniform mapping from a validated P3SP artifact.</param>
    /// <param name="sdkColumns">Exactly sixteen finite values packed in SDK column order by the eventual adapter.</param>
    /// <param name="sourceCount">Must be exactly sixteen.</param>
    /// <param name="values">Destination receiving all sixteen values only after successful validation.</param>
    /// <param name="capacity">Destination float capacity, which must be at least sixteen.</param>
    /// <param name="written">Set to sixteen on success and zero on every failure.</param>
    /// <param name="error">Receives member-specific context for any rejected mapping or value.</param>
    /// <returns>True only for the exact measured BatchCameraBuffer.projection mapping and finite input.</returns>
    static bool Resolve(const Ps3CookedShaderUniform& uniform, const float* sdkColumns,
        std::size_t sourceCount, float* values, std::size_t capacity,
        std::size_t& written, std::string& error);
};

} // namespace helengine::ps3
