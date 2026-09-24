#pragma once

#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Owns the authored identity and declared byte range of one shader uniform buffer.</summary>
struct Ps3CookedShaderBuffer {
    /// <summary>Gets the exact authored buffer name stored in the artifact.</summary>
    std::string BufferName;

    /// <summary>Gets the source register binding assigned to this buffer.</summary>
    std::uint32_t AuthoredBinding;

    /// <summary>Gets the compiler-declared byte size of the buffer.</summary>
    std::uint32_t DeclaredByteSize;
};

} // namespace helengine::ps3
