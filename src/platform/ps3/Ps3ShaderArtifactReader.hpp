#pragma once

#include "Ps3CookedShaderProgram.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Decodes and validates bounded P3SP artifacts into owned CPU snapshots.</summary>
class Ps3ShaderArtifactReader {
public:
    /// <summary>Reads one exact version-one artifact and publishes output only after every check succeeds.</summary>
    /// <param name="data">Complete artifact bytes supplied by the caller.</param>
    /// <param name="size">Exact artifact length in bytes.</param>
    /// <param name="result">Caller-owned destination left unchanged when reading fails.</param>
    /// <param name="error">Receives stage or resource context when reading fails.</param>
    /// <returns>True when the complete artifact and both SDK programs pass validation.</returns>
    static bool Read(const std::uint8_t* data, std::size_t size,
        Ps3CookedShaderProgram& result, std::string& error);
};

} // namespace helengine::ps3
