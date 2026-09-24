#include "Ps3StandardUniformProvider.hpp"

#include <cmath>
#include <cstring>

namespace helengine::ps3 {
namespace {

/// <summary>Formats a contextual failure without modifying any float destination.</summary>
/// <param name="uniform">Uniform metadata whose source identity failed validation.</param>
/// <param name="reason">Specific metadata, value, or destination error.</param>
/// <param name="error">Caller-owned diagnostic destination.</param>
/// <returns>False so it can be returned directly from the resolver.</returns>
bool Fail(const Ps3CookedShaderUniform& uniform, const char* reason, std::string& error) {
    error = "buffer '" + uniform.BufferName + "' member '" + uniform.MemberPath + "': " + reason;
    return false;
}

/// <summary>Checks every required source float for a finite IEEE-754 value.</summary>
/// <param name="uniform">Uniform mapping used to contextualize a rejected value.</param>
/// <param name="source">Caller-owned value sequence.</param>
/// <param name="count">Number of source values required by the mapping.</param>
/// <param name="error">Caller-owned diagnostic destination.</param>
/// <returns>True when all required values are finite.</returns>
bool ValidateFiniteSource(const Ps3CookedShaderUniform& uniform, const float* source,
    std::size_t count, std::string& error) {
    for (std::size_t index = 0u; index < count; ++index) {
        if (!std::isfinite(source[index])) {
            return Fail(uniform, "required draw value is not finite", error);
        }
    }
    return true;
}

} // namespace

/// <summary>Maps the finite ForwardStandard member set to local SDK-ready values, then publishes atomically.</summary>
/// <param name="uniform">Validated source-to-SDK uniform mapping from the cooked shader artifact.</param>
/// <param name="draw">Explicit per-draw matrix, material, camera, and lighting inputs.</param>
/// <param name="values">Destination for the SDK-packed vector or matrix floats.</param>
/// <param name="capacity">Number of float elements available at the destination.</param>
/// <param name="written">Receives the number of floats on success and is reset to zero on entry.</param>
/// <param name="error">Receives buffer/member context when the mapping cannot be resolved.</param>
/// <returns>True only when the exact member, metadata shape, and required draw values are valid.</returns>
bool Ps3StandardUniformProvider::Resolve(const Ps3CookedShaderUniform& uniform,
    const Ps3StandardDrawData& draw, float* values,
    std::size_t capacity, std::size_t& written, std::string& error) {
    written = 0u;
    error.clear();

    std::uint32_t expectedBinding = 0u;
    if (uniform.BufferName == "TransformBuffer") expectedBinding = 0u;
    else if (uniform.BufferName == "ForwardLightBuffer") expectedBinding = 1u;
    else if (uniform.BufferName == "ShadowBuffer") expectedBinding = 2u;
    else if (uniform.BufferName == "BaseColorBuffer") expectedBinding = 3u;
    else if (uniform.BufferName == "RoughnessBuffer") expectedBinding = 4u;
    else if (uniform.BufferName == "MetallicBuffer") expectedBinding = 5u;
    else if (uniform.BufferName == "SpecularBuffer") expectedBinding = 6u;
    else if (uniform.BufferName == "EmissiveColorBuffer") expectedBinding = 7u;
    else return Fail(uniform, "unknown standard uniform buffer", error);

    float resolved[16] = {};
    std::size_t valueCount = 4u;
    const float* source = nullptr;
    std::size_t sourceCount = 0u;
    bool directionalRgb = false;
    bool identityMatrix = false;
    bool whiteAmbient = false;
    bool oneDirectionalLight = false;
    float firstValue = 0.0f;
    bool hasFirstValue = false;
    bool knownMember = false;

    if (uniform.BufferName == "TransformBuffer") {
        if (uniform.MemberPath == "world") {
            valueCount = 16u;
            source = draw.World.Values;
            sourceCount = 16u;
            knownMember = true;
        } else if (uniform.MemberPath == "worldViewProj") {
            valueCount = 16u;
            source = draw.WorldViewProjection.Values;
            sourceCount = 16u;
            knownMember = true;
        } else if (uniform.MemberPath == "normalMatrix") {
            valueCount = 16u;
            source = draw.NormalMatrix.Values;
            sourceCount = 16u;
            knownMember = true;
        } else if (uniform.MemberPath == "cameraPosition") {
            source = draw.CameraPosition;
            sourceCount = 4u;
            knownMember = true;
        } else if (uniform.MemberPath == "materialFlags") {
            knownMember = true;
        }
    } else if (uniform.BufferName == "ForwardLightBuffer") {
        if (uniform.MemberPath == "ambientLightColor") {
            if (draw.Lit) {
                source = draw.AmbientLight;
                sourceCount = 4u;
            } else {
                whiteAmbient = true;
            }
            knownMember = true;
        } else if (uniform.MemberPath == "lightMetadata") {
            oneDirectionalLight = draw.Lit && draw.HasDirectionalLight;
            knownMember = true;
        } else if (uniform.MemberPath == "light0ColorAndType") {
            if (draw.Lit && draw.HasDirectionalLight) {
                source = draw.DirectionalColor;
                sourceCount = 3u;
                directionalRgb = true;
            }
            knownMember = true;
        } else if (uniform.MemberPath == "light0DirectionAndShadow") {
            if (draw.Lit && draw.HasDirectionalLight) {
                source = draw.DirectionalDirection;
                sourceCount = 3u;
                directionalRgb = true;
            }
            knownMember = true;
        } else {
            const char* const zeroMembers[] = {
                "light0PositionAndRange", "light0SpotAngles",
                "light1ColorAndType", "light1DirectionAndShadow", "light1PositionAndRange", "light1SpotAngles",
                "light2ColorAndType", "light2DirectionAndShadow", "light2PositionAndRange", "light2SpotAngles",
                "light3ColorAndType", "light3DirectionAndShadow", "light3PositionAndRange", "light3SpotAngles"
            };
            for (const char* member : zeroMembers) {
                if (uniform.MemberPath == member) {
                    knownMember = true;
                    break;
                }
            }
        }
    } else if (uniform.BufferName == "ShadowBuffer") {
        if (uniform.MemberPath == "shadowMetadata") {
            knownMember = true;
        } else {
            const char* const zeroMembers[] = {
                "shadowLight0AtlasRect", "shadowLight0Metadata",
                "shadowLight1AtlasRect", "shadowLight1Metadata",
                "shadowLight2AtlasRect", "shadowLight2Metadata",
                "shadowLight3AtlasRect", "shadowLight3Metadata"
            };
            for (const char* member : zeroMembers) {
                if (uniform.MemberPath == member) {
                    knownMember = true;
                    break;
                }
            }

            const char* const identityMembers[] = {
                "shadowLight0WorldToShadowClip", "shadowLight1WorldToShadowClip",
                "shadowLight2WorldToShadowClip", "shadowLight3WorldToShadowClip"
            };
            for (const char* member : identityMembers) {
                if (uniform.MemberPath == member) {
                    valueCount = 16u;
                    identityMatrix = true;
                    knownMember = true;
                    break;
                }
            }
        }
    } else if (uniform.BufferName == "BaseColorBuffer" && uniform.MemberPath == "baseColor") {
        source = draw.BaseColor;
        sourceCount = 4u;
        knownMember = true;
    } else if (uniform.BufferName == "RoughnessBuffer" && uniform.MemberPath == "roughnessValue") {
        firstValue = 0.4f;
        hasFirstValue = true;
        knownMember = true;
    } else if (uniform.BufferName == "MetallicBuffer" && uniform.MemberPath == "metallicValue") {
        knownMember = true;
    } else if (uniform.BufferName == "SpecularBuffer" && uniform.MemberPath == "specularValue") {
        firstValue = 0.5f;
        hasFirstValue = true;
        knownMember = true;
    } else if (uniform.BufferName == "EmissiveColorBuffer" && uniform.MemberPath == "emissiveColorValue") {
        knownMember = true;
    }

    if (!knownMember) return Fail(uniform, "unknown standard uniform member", error);
    if (uniform.AuthoredBinding != expectedBinding) {
        return Fail(uniform, "authored buffer binding does not match the standard shader contract", error);
    }
    if (uniform.ScalarType != "FP3232") {
        return Fail(uniform, "scalar type is not FP3232", error);
    }
    if (uniform.ArrayLength != 1u) {
        return Fail(uniform, "array length must be one", error);
    }

    const bool matrix = valueCount == 16u;
    const std::uint32_t expectedSdkType = matrix ? 11u : 9u;
    const std::uint32_t expectedSdkElements = matrix ? 4u : 1u;
    const std::uint32_t minimumByteSize = matrix ? 64u : 16u;
    if (uniform.VectorWidth != 4u || uniform.Columns != (matrix ? 4u : 1u)) {
        return Fail(uniform, matrix ? "matrix shape must be float4x4" : "vector shape must be float4", error);
    }
    if (uniform.SdkDataType != expectedSdkType) {
        return Fail(uniform, "SDK data type does not match the standard float shape", error);
    }
    if (uniform.SdkElementCount != expectedSdkElements) {
        return Fail(uniform, "SDK element count does not match the standard float shape", error);
    }
    if (uniform.ByteSize < minimumByteSize) {
        return Fail(uniform, "declared byte size is too small for the standard float value", error);
    }
    if (source != nullptr && !ValidateFiniteSource(uniform, source, sourceCount, error)) return false;
    if (values == nullptr) return Fail(uniform, "destination pointer is null", error);
    if (capacity < valueCount) return Fail(uniform, "destination capacity is too small", error);

    if (source != nullptr) {
        if (directionalRgb) {
            resolved[0] = source[0];
            resolved[1] = source[1];
            resolved[2] = source[2];
        } else {
            std::memcpy(resolved, source, sourceCount * sizeof(float));
        }
    }
    if (identityMatrix) {
        resolved[0] = 1.0f;
        resolved[5] = 1.0f;
        resolved[10] = 1.0f;
        resolved[15] = 1.0f;
    }
    if (whiteAmbient) {
        resolved[0] = 1.0f;
        resolved[1] = 1.0f;
        resolved[2] = 1.0f;
        resolved[3] = 1.0f;
    }
    if (oneDirectionalLight) resolved[0] = 1.0f;
    if (hasFirstValue) resolved[0] = firstValue;

    std::memcpy(values, resolved, valueCount * sizeof(float));
    written = valueCount;
    return true;
}

} // namespace helengine::ps3
