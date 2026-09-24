#pragma once

#include "Ps3CookedShaderBuffer.hpp"
#include "Ps3CookedShaderUniform.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace helengine::ps3 {

/// <summary>Owns one validated stage's untouched SDK program bytes and source binding metadata.</summary>
struct Ps3CookedShaderStage {
    /// <summary>Gets the encoded stage identity: zero for vertex and one for pixel.</summary>
    std::uint32_t Stage;

    /// <summary>Gets the authored HLSL entry point used to compile this stage.</summary>
    std::string EntryPoint;

    /// <summary>Gets an owned byte-for-byte copy of the SDK program blob.</summary>
    std::vector<std::uint8_t> Bytecode;

    /// <summary>Gets the stage's declared uniform buffers in artifact order.</summary>
    std::vector<Ps3CookedShaderBuffer> Buffers;

    /// <summary>Gets the stage's active source-to-SDK uniform mappings in artifact order.</summary>
    std::vector<Ps3CookedShaderUniform> Uniforms;
};

} // namespace helengine::ps3
