#pragma once

#include "Ps3RsxConstantElement.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace helengine::ps3 {

/// <summary>Owns one SDK constant group, including its head-record type and ordered elements.</summary>
struct Ps3RsxConstantGroup {
    /// <summary>Gets the SDK parameter name, or empty for an unnamed internal constant group.</summary>
    std::string Name;

    /// <summary>Gets the exact type code declared by the group's head record.</summary>
    std::uint8_t DataType;

    /// <summary>Gets the owned constant elements grouped by the head record's element count.</summary>
    std::vector<Ps3RsxConstantElement> Elements;
};

} // namespace helengine::ps3
