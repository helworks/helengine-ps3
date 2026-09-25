#include "Ps3Batch2DUniformProvider.hpp"

#include <cmath>
#include <cstring>

namespace helengine::ps3 {
namespace {

/// <summary>Formats a batch projection rejection with its authored buffer and member identity.</summary>
bool Reject(const Ps3CookedShaderUniform& uniform, const char* reason, std::string& error) {
    error = "buffer '" + uniform.BufferName + "' member '" + uniform.MemberPath + "': " + reason;
    return false;
}

} // namespace

bool Ps3Batch2DUniformProvider::Resolve(const Ps3CookedShaderUniform& uniform,
    const float* sdkColumns, std::size_t sourceCount, float* values,
    std::size_t capacity, std::size_t& written, std::string& error) {
    written = 0u;
    error.clear();

    if (uniform.BufferName != "BatchCameraBuffer" || uniform.AuthoredBinding != 0u
        || uniform.MemberPath != "projection") {
        return Reject(uniform, "expected BatchCameraBuffer b0 member projection", error);
    }
    if (uniform.ByteOffset != 0u || uniform.ByteSize != 64u
        || uniform.MatrixStride != 16u || uniform.ArrayStride != 0u) {
        return Reject(uniform, "projection source range must be offset 0, size 64, matrix stride 16, array stride 0", error);
    }
    if (uniform.ScalarType != "FP3232" || uniform.VectorWidth != 4u
        || uniform.Columns != 4u || uniform.ArrayLength != 1u || uniform.RowMajor != 1u) {
        return Reject(uniform, "projection must be the measured row-major FP3232 4x4 non-array matrix", error);
    }
    if (uniform.SdkDataType != 11u || uniform.SdkElementCount != 4u) {
        return Reject(uniform, "projection SDK mapping must use measured type 11 with four elements", error);
    }
    if (uniform.GeneratedName != "hel_ps3_uniform_buffer_binding_0.hel_ps3_uniform_type_BatchCameraBuffer_member_0") {
        return Reject(uniform, "projection generated SDK name differs from the measured P3SP spelling", error);
    }
    if (sdkColumns == nullptr) return Reject(uniform, "SDK-column source pointer is null", error);
    if (sourceCount != 16u) return Reject(uniform, "SDK-column source count must equal sixteen", error);
    for (std::size_t index = 0u; index < sourceCount; ++index) {
        if (!std::isfinite(sdkColumns[index])) {
            return Reject(uniform, "SDK-column source contains a non-finite value", error);
        }
    }
    if (values == nullptr) return Reject(uniform, "destination pointer is null", error);
    if (capacity < 16u) return Reject(uniform, "destination capacity is smaller than sixteen floats", error);

    std::memcpy(values, sdkColumns, 16u * sizeof(float));
    written = 16u;
    return true;
}

} // namespace helengine::ps3
