#pragma once

#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Owns one validated SDK attribute-table input or texture record.</summary>
struct Ps3RsxAttributeEntry {
    /// <summary>Gets the exact strict UTF-8 parameter name referenced by the SDK record.</summary>
    std::string Name;

    /// <summary>Gets the physical vertex slot or pixel texture unit assigned by the SDK.</summary>
    std::uint32_t Index;

    /// <summary>Gets the exact one-byte SDK data-type code from the raw record.</summary>
    std::uint8_t DataType;
};

} // namespace helengine::ps3
