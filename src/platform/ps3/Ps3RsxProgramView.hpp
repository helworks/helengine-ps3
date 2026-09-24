#pragma once

#include "Ps3RsxAttributeEntry.hpp"
#include "Ps3RsxConstantGroup.hpp"

#include <cstdint>
#include <vector>

namespace helengine::ps3 {

/// <summary>Owns the bounded structural metadata extracted from one raw SDK RSX stage program.</summary>
struct Ps3RsxProgramView {
    /// <summary>Gets whether the source program is the vertex stage rather than the pixel stage.</summary>
    bool IsVertex;

    /// <summary>Gets the absolute byte offset where this program's microcode begins.</summary>
    std::uint32_t MicrocodeOffset;

    /// <summary>Gets the exact byte length of the instruction-aligned microcode region.</summary>
    std::uint64_t MicrocodeBytes;

    /// <summary>Gets validated SDK attributes and samplers in their original table order.</summary>
    std::vector<Ps3RsxAttributeEntry> Attributes;

    /// <summary>Gets validated SDK constant groups in their original head-record order.</summary>
    std::vector<Ps3RsxConstantGroup> Constants;
};

} // namespace helengine::ps3
