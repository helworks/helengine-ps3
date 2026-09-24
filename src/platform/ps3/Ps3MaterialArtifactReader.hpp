#pragma once

#include "Ps3CookedMaterial.hpp"
#include "Ps3CookedShaderProgram.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Decodes the exact version-one PS3 standard material envelope and checks its cooked shader identity.</summary>
class Ps3MaterialArtifactReader {
public:
    /// <summary>Reads and validates one complete P3MT envelope without partially changing caller output.</summary>
    /// <param name="data">Complete artifact bytes supplied by the caller.</param>
    /// <param name="size">Exact artifact length in bytes.</param>
    /// <param name="result">Caller-owned destination left unchanged when validation fails.</param>
    /// <param name="error">Receives a nonempty field-specific diagnostic on failure.</param>
    /// <returns>True only when every envelope field and the fixed PS3 standard contract validate.</returns>
    static bool Read(
        const std::uint8_t* data,
        std::size_t size,
        Ps3CookedMaterial& result,
        std::string& error);

    /// <summary>Requires the material's fixed shader identity to match a separately validated P3SP program.</summary>
    /// <param name="material">Decoded or caller-owned material to check against the fixed schema.</param>
    /// <param name="program">CPU shader program already decoded by Ps3ShaderArtifactReader.</param>
    /// <param name="error">Receives a nonempty identity diagnostic when the pair differs.</param>
    /// <returns>True only when the fixed material contract and all four program identity strings match exactly.</returns>
    static bool MatchesProgram(
        const Ps3CookedMaterial& material,
        const Ps3CookedShaderProgram& program,
        std::string& error);
};

} // namespace helengine::ps3
