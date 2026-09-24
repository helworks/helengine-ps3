#pragma once

#include <cstdint>
#include <string>

namespace helengine::ps3 {

/// <summary>Owns one validated source-member to SDK-constant mapping for a shader stage.</summary>
struct Ps3CookedShaderUniform {
    /// <summary>Gets the authored uniform-buffer name containing the member.</summary>
    std::string BufferName;

    /// <summary>Gets the authored binding of the containing uniform buffer.</summary>
    std::uint32_t AuthoredBinding;

    /// <summary>Gets the complete authored member path within its uniform buffer.</summary>
    std::string MemberPath;

    /// <summary>Gets the member byte offset within the source buffer.</summary>
    std::uint32_t ByteOffset;

    /// <summary>Gets the declared member byte size.</summary>
    std::uint32_t ByteSize;

    /// <summary>Gets the source array stride in bytes, including zero for non-array members.</summary>
    std::uint32_t ArrayStride;

    /// <summary>Gets the physical matrix stride in bytes, including zero for non-matrix members.</summary>
    std::uint32_t MatrixStride;

    /// <summary>Gets the encoded row-major flag as its original unsigned wire value.</summary>
    std::uint32_t RowMajor;

    /// <summary>Gets the source scalar type and width label.</summary>
    std::string ScalarType;

    /// <summary>Gets the number of scalar components in each vector.</summary>
    std::uint32_t VectorWidth;

    /// <summary>Gets the number of matrix columns, or one for a non-matrix value.</summary>
    std::uint32_t Columns;

    /// <summary>Gets the positive product of source array dimensions.</summary>
    std::uint32_t ArrayLength;

    /// <summary>Gets the generated SDK parameter name validated against the stage program.</summary>
    std::string GeneratedName;

    /// <summary>Gets the exact SDK constant data-type code stored in the artifact.</summary>
    std::uint32_t SdkDataType;

    /// <summary>Gets the SDK constant's declared element count.</summary>
    std::uint32_t SdkElementCount;
};

} // namespace helengine::ps3
