#pragma once

#include "Ps3CookedShaderUniform.hpp"
#include "Ps3StandardDrawData.hpp"

#include <cstddef>
#include <string>

namespace helengine::ps3 {

/// <summary>Resolves exact ForwardStandard uniform metadata to validated per-draw SDK float values.</summary>
class Ps3StandardUniformProvider {
public:
    /// <summary>Resolves one supported active uniform without changing output storage on failure.</summary>
    /// <param name="uniform">Validated source-to-SDK uniform mapping from the cooked shader artifact.</param>
    /// <param name="draw">Explicit per-draw matrix, material, camera, and lighting inputs.</param>
    /// <param name="values">Destination for the SDK-packed vector or matrix floats.</param>
    /// <param name="capacity">Number of float elements available at the destination.</param>
    /// <param name="written">Receives the number of floats on success and is reset to zero on entry.</param>
    /// <param name="error">Receives buffer/member context when the mapping cannot be resolved.</param>
    /// <returns>True only when the exact member, metadata shape, and required draw values are valid.</returns>
    static bool Resolve(const Ps3CookedShaderUniform& uniform,
        const Ps3StandardDrawData& draw, float* values,
        std::size_t capacity, std::size_t& written, std::string& error);
};

} // namespace helengine::ps3
