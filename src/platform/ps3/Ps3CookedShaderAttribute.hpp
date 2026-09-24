#pragma once

#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Owns one authored vertex semantic and its validated physical RSX input slot.</summary>
struct Ps3CookedShaderAttribute {
    /// <summary>Gets the full authored semantic, including any numeric semantic suffix.</summary>
    std::string FullAuthoredSemantic;

    /// <summary>Gets the generated SDK attribute name used to validate the RSX program.</summary>
    std::string GeneratedName;

    /// <summary>Gets the source scalar type and width label.</summary>
    std::string ScalarType;

    /// <summary>Gets the number of scalar components in each source vector.</summary>
    std::uint32_t VectorWidth;

    /// <summary>Gets the number of matrix columns represented by the source attribute.</summary>
    std::uint32_t Columns;

    /// <summary>Gets the positive product of source array dimensions.</summary>
    std::uint32_t ArrayLength;

    /// <summary>Gets the actual RSX vertex input slot assigned by the SDK compiler.</summary>
    std::uint32_t HardwareSlot;

    /// <summary>Gets the exact SDK attribute data-type code stored in the artifact.</summary>
    std::uint32_t SdkDataType;
};

} // namespace helengine::ps3
