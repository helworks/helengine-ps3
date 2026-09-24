#pragma once

#include <cstdint>
#include <vector>

namespace helengine::ps3 {

/// <summary>Owns one SDK constant element's stage-specific register or relocation references.</summary>
struct Ps3RsxConstantElement {
    /// <summary>Gets the vertex register index or absolute fragment relocation-table offset.</summary>
    std::uint32_t Index;

    /// <summary>Gets whether the SDK marked this element as an internal constant.</summary>
    std::uint8_t IsInternal;

    /// <summary>Gets validated microcode-relative fragment word offsets in their original order.</summary>
    std::vector<std::uint32_t> RelocationOffsets;
};

} // namespace helengine::ps3
