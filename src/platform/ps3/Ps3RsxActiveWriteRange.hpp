#pragma once

#include "Ps3RsxConstantGroup.hpp"

#include <cstddef>
#include <cstdint>

namespace helengine::ps3 {

/// <summary>Identifies a validated active microcode write range and the SDK element that owns it.</summary>
struct Ps3RsxActiveWriteRange {
    /// <summary>Gets the inclusive byte offset of the write within fragment microcode.</summary>
    std::uint64_t Begin;

    /// <summary>Gets the exclusive byte offset after the complete typed write.</summary>
    std::uint64_t End;

    /// <summary>Gets the SDK constant group whose mapped element owns this write.</summary>
    const Ps3RsxConstantGroup* Group;

    /// <summary>Gets the element ordinal within the owning constant group.</summary>
    std::size_t ElementOrdinal;
};

} // namespace helengine::ps3
