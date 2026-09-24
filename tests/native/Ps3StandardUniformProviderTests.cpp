#include "platform/ps3/Ps3ShaderArtifactReader.hpp"
#include "platform/ps3/Ps3StandardUniformProvider.hpp"

#include <sys/process.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

SYS_PROCESS_PARAM(1001, 0x100000);

namespace {

constexpr const char* FixturePath = "/app_home/USRDIR/content/standard.ps3sp";
constexpr std::size_t MaximumArtifactBytes = 16u * 1024u * 1024u;

std::vector<std::uint8_t> ArtifactBytes;
helengine::ps3::Ps3CookedShaderProgram Program;
std::size_t PassedCases = 0u;
std::size_t FailedCases = 0u;

/// <summary>Names the complete finite set of active ForwardStandard buffer/member identities.</summary>
const char* const ExpectedUniformIdentities[] = {
    "BaseColorBuffer/baseColor",
    "EmissiveColorBuffer/emissiveColorValue",
    "ForwardLightBuffer/ambientLightColor",
    "ForwardLightBuffer/light0ColorAndType",
    "ForwardLightBuffer/light0DirectionAndShadow",
    "ForwardLightBuffer/light0PositionAndRange",
    "ForwardLightBuffer/light0SpotAngles",
    "ForwardLightBuffer/light1ColorAndType",
    "ForwardLightBuffer/light1DirectionAndShadow",
    "ForwardLightBuffer/light1PositionAndRange",
    "ForwardLightBuffer/light1SpotAngles",
    "ForwardLightBuffer/light2ColorAndType",
    "ForwardLightBuffer/light2DirectionAndShadow",
    "ForwardLightBuffer/light2PositionAndRange",
    "ForwardLightBuffer/light2SpotAngles",
    "ForwardLightBuffer/light3ColorAndType",
    "ForwardLightBuffer/light3DirectionAndShadow",
    "ForwardLightBuffer/light3PositionAndRange",
    "ForwardLightBuffer/light3SpotAngles",
    "ForwardLightBuffer/lightMetadata",
    "MetallicBuffer/metallicValue",
    "RoughnessBuffer/roughnessValue",
    "ShadowBuffer/shadowLight0AtlasRect",
    "ShadowBuffer/shadowLight0Metadata",
    "ShadowBuffer/shadowLight0WorldToShadowClip",
    "ShadowBuffer/shadowLight1AtlasRect",
    "ShadowBuffer/shadowLight1Metadata",
    "ShadowBuffer/shadowLight1WorldToShadowClip",
    "ShadowBuffer/shadowLight2AtlasRect",
    "ShadowBuffer/shadowLight2Metadata",
    "ShadowBuffer/shadowLight2WorldToShadowClip",
    "ShadowBuffer/shadowLight3AtlasRect",
    "ShadowBuffer/shadowLight3Metadata",
    "ShadowBuffer/shadowLight3WorldToShadowClip",
    "ShadowBuffer/shadowMetadata",
    "SpecularBuffer/specularValue",
    "TransformBuffer/cameraPosition",
    "TransformBuffer/materialFlags",
    "TransformBuffer/normalMatrix",
    "TransformBuffer/world",
    "TransformBuffer/worldViewProj"
};
constexpr std::size_t ExpectedUniformIdentityCount =
    sizeof(ExpectedUniformIdentities) / sizeof(ExpectedUniformIdentities[0]);

/// <summary>Stops the current test case when a provider or fixture contract is violated.</summary>
/// <param name="condition">Condition that must hold for the current assertion.</param>
/// <param name="message">Specific failure detail printed in the PPU TTY log.</param>
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

/// <summary>Loads the staged immutable P3SP fixture into bounded CPU-owned storage.</summary>
/// <param name="error">Receives a useful reason if the staged file cannot be read.</param>
/// <returns>True only when a nonempty artifact no larger than the reader's format bound is loaded.</returns>
bool LoadArtifact(std::string& error) {
    std::ifstream input(FixturePath, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "cannot open staged standard P3SP fixture";
        return false;
    }

    const std::streamoff length = input.tellg();
    if (length < 12 || static_cast<std::uint64_t>(length) > MaximumArtifactBytes) {
        error = "staged standard P3SP fixture has an invalid bounded length";
        return false;
    }

    ArtifactBytes.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(ArtifactBytes.data()), length);
    if (!input || input.gcount() != length) {
        error = "short read while loading staged standard P3SP fixture";
        ArtifactBytes.clear();
        return false;
    }
    return true;
}

/// <summary>Assigns distinctive finite draw values so packing, ordering, and stale-state errors are visible.</summary>
/// <param name="draw">Draw input whose every provider-consumed field is initialized explicitly.</param>
void PopulateDistinctDraw(helengine::ps3::Ps3StandardDrawData& draw) {
    for (std::size_t index = 0u; index < 16u; ++index) {
        draw.World.Values[index] = 10.25f + static_cast<float>(index) * 1.125f;
        draw.WorldViewProjection.Values[index] = -20.5f - static_cast<float>(index) * 1.25f;
        draw.NormalMatrix.Values[index] = 30.75f + static_cast<float>(index) * 1.375f;
    }

    draw.CameraPosition[0] = 2.25f;
    draw.CameraPosition[1] = -3.5f;
    draw.CameraPosition[2] = 4.75f;
    draw.CameraPosition[3] = 1.0f;
    draw.BaseColor[0] = 0.125f;
    draw.BaseColor[1] = 0.375f;
    draw.BaseColor[2] = 0.625f;
    draw.BaseColor[3] = 0.875f;
    draw.AmbientLight[0] = 0.2f;
    draw.AmbientLight[1] = 0.35f;
    draw.AmbientLight[2] = 0.55f;
    draw.AmbientLight[3] = 1.0f;
    draw.DirectionalColor[0] = 0.72f;
    draw.DirectionalColor[1] = 0.53f;
    draw.DirectionalColor[2] = 0.31f;
    draw.DirectionalColor[3] = 0.91f;
    draw.DirectionalDirection[0] = -0.15f;
    draw.DirectionalDirection[1] = 0.45f;
    draw.DirectionalDirection[2] = -0.8f;
    draw.DirectionalDirection[3] = 0.66f;
    draw.Lit = true;
    draw.HasDirectionalLight = true;
    draw.DoubleSided = false;
}

/// <summary>Copies one expected vector or matrix into test-local storage without changing its values.</summary>
/// <param name="source">Exact expected floats from the independent draw-data policy.</param>
/// <param name="count">Number of expected floats to copy.</param>
/// <param name="destination">Sixteen-float test buffer receiving the expectation.</param>
/// <param name="written">Receives the expected element count.</param>
void SetExpected(const float* source, std::size_t count, float destination[16], std::size_t& written) {
    std::copy(source, source + count, destination);
    written = count;
}

/// <summary>Builds the exact expected value for each member in the finite standard-shader contract.</summary>
/// <param name="uniform">Active source member from the real P3SP artifact.</param>
/// <param name="draw">Explicit draw values used to derive caller-provided members.</param>
/// <param name="expected">Sixteen-float destination for the expected SDK-packed value.</param>
/// <param name="expectedCount">Receives four for float4 values or sixteen for matrices.</param>
/// <returns>True when the buffer/member pair is one of the contract's supported mappings.</returns>
bool BuildExpectedValue(const helengine::ps3::Ps3CookedShaderUniform& uniform,
    const helengine::ps3::Ps3StandardDrawData& draw, float expected[16], std::size_t& expectedCount) {
    std::fill(expected, expected + 16u, 0.0f);
    expectedCount = 4u;

    if (uniform.BufferName == "TransformBuffer") {
        if (uniform.MemberPath == "world") {
            SetExpected(draw.World.Values, 16u, expected, expectedCount);
            return true;
        }
        if (uniform.MemberPath == "worldViewProj") {
            SetExpected(draw.WorldViewProjection.Values, 16u, expected, expectedCount);
            return true;
        }
        if (uniform.MemberPath == "normalMatrix") {
            SetExpected(draw.NormalMatrix.Values, 16u, expected, expectedCount);
            return true;
        }
        if (uniform.MemberPath == "cameraPosition") {
            SetExpected(draw.CameraPosition, 4u, expected, expectedCount);
            return true;
        }
        if (uniform.MemberPath == "materialFlags") return true;
        return false;
    }

    if (uniform.BufferName == "ForwardLightBuffer") {
        if (uniform.MemberPath == "ambientLightColor") {
            if (draw.Lit) SetExpected(draw.AmbientLight, 4u, expected, expectedCount);
            else expected[0] = expected[1] = expected[2] = expected[3] = 1.0f;
            return true;
        }
        if (uniform.MemberPath == "lightMetadata") {
            if (draw.Lit && draw.HasDirectionalLight) expected[0] = 1.0f;
            return true;
        }
        if (uniform.MemberPath == "light0ColorAndType") {
            if (draw.Lit && draw.HasDirectionalLight) {
                expected[0] = draw.DirectionalColor[0];
                expected[1] = draw.DirectionalColor[1];
                expected[2] = draw.DirectionalColor[2];
            }
            return true;
        }
        if (uniform.MemberPath == "light0DirectionAndShadow") {
            if (draw.Lit && draw.HasDirectionalLight) {
                expected[0] = draw.DirectionalDirection[0];
                expected[1] = draw.DirectionalDirection[1];
                expected[2] = draw.DirectionalDirection[2];
            }
            return true;
        }

        const char* const zeroMembers[] = {
            "light0PositionAndRange", "light0SpotAngles",
            "light1ColorAndType", "light1DirectionAndShadow", "light1PositionAndRange", "light1SpotAngles",
            "light2ColorAndType", "light2DirectionAndShadow", "light2PositionAndRange", "light2SpotAngles",
            "light3ColorAndType", "light3DirectionAndShadow", "light3PositionAndRange", "light3SpotAngles"
        };
        for (const char* member : zeroMembers) {
            if (uniform.MemberPath == member) return true;
        }
        return false;
    }

    if (uniform.BufferName == "ShadowBuffer") {
        if (uniform.MemberPath == "shadowMetadata") return true;
        const char* const zeroMembers[] = {
            "shadowLight0AtlasRect", "shadowLight0Metadata",
            "shadowLight1AtlasRect", "shadowLight1Metadata",
            "shadowLight2AtlasRect", "shadowLight2Metadata",
            "shadowLight3AtlasRect", "shadowLight3Metadata"
        };
        for (const char* member : zeroMembers) {
            if (uniform.MemberPath == member) return true;
        }

        const char* const identityMembers[] = {
            "shadowLight0WorldToShadowClip", "shadowLight1WorldToShadowClip",
            "shadowLight2WorldToShadowClip", "shadowLight3WorldToShadowClip"
        };
        for (const char* member : identityMembers) {
            if (uniform.MemberPath == member) {
                std::fill(expected, expected + 16u, 0.0f);
                expected[0] = expected[5] = expected[10] = expected[15] = 1.0f;
                expectedCount = 16u;
                return true;
            }
        }
        return false;
    }

    if (uniform.BufferName == "BaseColorBuffer" && uniform.MemberPath == "baseColor") {
        SetExpected(draw.BaseColor, 4u, expected, expectedCount);
        return true;
    }
    if (uniform.BufferName == "RoughnessBuffer" && uniform.MemberPath == "roughnessValue") {
        expected[0] = 0.4f;
        return true;
    }
    if (uniform.BufferName == "MetallicBuffer" && uniform.MemberPath == "metallicValue") return true;
    if (uniform.BufferName == "SpecularBuffer" && uniform.MemberPath == "specularValue") {
        expected[0] = 0.5f;
        return true;
    }
    if (uniform.BufferName == "EmissiveColorBuffer" && uniform.MemberPath == "emissiveColorValue") return true;
    return false;
}

/// <summary>Finds an active member by exact buffer and member identity in either shader stage.</summary>
/// <param name="bufferName">Exact authored uniform-buffer name.</param>
/// <param name="memberPath">Exact authored member path.</param>
/// <returns>A reference to the matching active member or fails with its requested identity.</returns>
const helengine::ps3::Ps3CookedShaderUniform& FindUniform(const char* bufferName, const char* memberPath) {
    const std::vector<helengine::ps3::Ps3CookedShaderUniform>* stages[] = {
        &Program.Vertex.Uniforms, &Program.Pixel.Uniforms
    };
    for (const auto* uniforms : stages) {
        for (const helengine::ps3::Ps3CookedShaderUniform& uniform : *uniforms) {
            if (uniform.BufferName == bufferName && uniform.MemberPath == memberPath) return uniform;
        }
    }
    throw std::runtime_error(std::string("fixture has no active uniform ") + bufferName + "/" + memberPath);
}

/// <summary>Requires every expected active identity exactly once across both shader stages.</summary>
/// <param name="program">Cooked program whose reflected active uniforms are being checked.</param>
/// <param name="error">Receives a duplicate, missing, or unexpected identity description.</param>
/// <returns>True only when the program's active identities exactly match the 41-member contract.</returns>
bool HasExactExpectedUniformIdentities(const helengine::ps3::Ps3CookedShaderProgram& program,
    std::string& error) {
    bool seen[ExpectedUniformIdentityCount] = {};
    std::string duplicateIdentity;
    std::string unexpectedIdentity;
    const std::vector<helengine::ps3::Ps3CookedShaderUniform>* stages[] = {
        &program.Vertex.Uniforms, &program.Pixel.Uniforms
    };

    for (const auto* uniforms : stages) {
        for (const helengine::ps3::Ps3CookedShaderUniform& uniform : *uniforms) {
            const std::string identity = uniform.BufferName + "/" + uniform.MemberPath;
            bool expected = false;
            for (std::size_t index = 0u; index < ExpectedUniformIdentityCount; ++index) {
                if (identity == ExpectedUniformIdentities[index]) {
                    expected = true;
                    if (seen[index] && duplicateIdentity.empty()) duplicateIdentity = identity;
                    seen[index] = true;
                    break;
                }
            }
            if (!expected && unexpectedIdentity.empty()) unexpectedIdentity = identity;
        }
    }

    if (!duplicateIdentity.empty()) {
        error = "duplicate active uniform " + duplicateIdentity;
        return false;
    }
    for (std::size_t index = 0u; index < ExpectedUniformIdentityCount; ++index) {
        if (!seen[index]) {
            error = std::string("missing expected active uniform ") + ExpectedUniformIdentities[index];
            return false;
        }
    }
    if (!unexpectedIdentity.empty()) {
        error = "unexpected active uniform " + unexpectedIdentity;
        return false;
    }

    error.clear();
    return true;
}

/// <summary>Checks an expected packed float sequence by exact element comparison.</summary>
/// <param name="actual">Resolver output values.</param>
/// <param name="expected">Independent expected values.</param>
/// <param name="count">Number of elements to compare.</param>
/// <param name="identity">Uniform identity included in assertion diagnostics.</param>
void RequireValuesEqual(const float* actual, const float* expected, std::size_t count,
    const std::string& identity) {
    for (std::size_t index = 0u; index < count; ++index) {
        if (actual[index] != expected[index]) {
            throw std::runtime_error(identity + " has an unexpected float at element " + std::to_string(index));
        }
    }
}

/// <summary>Resolves one active member and compares every emitted float with its independent expectation.</summary>
/// <param name="uniform">Active metadata from the immutable standard shader fixture.</param>
/// <param name="draw">Draw-specific values supplied to the pure provider.</param>
void RequireResolvedValue(const helengine::ps3::Ps3CookedShaderUniform& uniform,
    const helengine::ps3::Ps3StandardDrawData& draw) {
    float expected[16];
    std::size_t expectedCount = 0u;
    Require(BuildExpectedValue(uniform, draw, expected, expectedCount),
        "test expectation table does not cover " + uniform.BufferName + "/" + uniform.MemberPath);

    float actual[16];
    std::fill(actual, actual + 16u, -71.0f);
    std::size_t written = 99u;
    std::string error;
    const bool resolved = helengine::ps3::Ps3StandardUniformProvider::Resolve(
        uniform, draw, actual, 16u, written, error);
    Require(resolved, uniform.BufferName + "/" + uniform.MemberPath + " failed: " + error);
    Require(error.empty(), uniform.BufferName + "/" + uniform.MemberPath + " returned an error on success");
    Require(written == expectedCount, uniform.BufferName + "/" + uniform.MemberPath + " wrote an unexpected float count");
    RequireValuesEqual(actual, expected, expectedCount, uniform.BufferName + "/" + uniform.MemberPath);
}

/// <summary>Checks a rejected request leaves all sixteen destination floats untouched.</summary>
/// <param name="uniform">Malformed or unsupported active metadata to resolve.</param>
/// <param name="draw">Draw values for the request.</param>
/// <param name="capacity">Destination capacity supplied to the provider.</param>
/// <param name="destination">Optional destination pointer, null for the null-destination case.</param>
void RequireRejectedWithoutWrites(const helengine::ps3::Ps3CookedShaderUniform& uniform,
    const helengine::ps3::Ps3StandardDrawData& draw, std::size_t capacity = 16u, float* destination = nullptr) {
    float localOutput[16];
    if (destination == nullptr) {
        destination = localOutput;
        std::fill(destination, destination + 16u, -91.0f);
    }
    float originalOutput[16];
    if (destination == localOutput) std::memcpy(originalOutput, destination, sizeof(originalOutput));
    std::size_t written = 77u;
    std::string error;
    const bool resolved = helengine::ps3::Ps3StandardUniformProvider::Resolve(
        uniform, draw, destination, capacity, written, error);
    Require(!resolved, uniform.BufferName + "/" + uniform.MemberPath + " was unexpectedly accepted");
    Require(written == 0u, uniform.BufferName + "/" + uniform.MemberPath + " did not reset written on failure");
    Require(!error.empty(), uniform.BufferName + "/" + uniform.MemberPath + " returned no diagnostic");
    Require(error.find(uniform.BufferName) != std::string::npos,
        uniform.BufferName + "/" + uniform.MemberPath + " diagnostic omitted the buffer name");
    Require(error.find(uniform.MemberPath) != std::string::npos,
        uniform.BufferName + "/" + uniform.MemberPath + " diagnostic omitted the member name");
    if (destination == localOutput) {
        Require(std::memcmp(destination, originalOutput, sizeof(originalOutput)) == 0,
            uniform.BufferName + "/" + uniform.MemberPath + " changed output on failure");
    }
}

/// <summary>Verifies fixture identity corresponds to the exact cooked ForwardStandard variant.</summary>
void TestRealArtifactIdentity() {
    Require(Program.ShaderAssetId == "ForwardStandardShader", "fixture shader asset identity differs");
    Require(Program.VertexProgramName == "ForwardStandardShader.vs", "fixture vertex program identity differs");
    Require(Program.PixelProgramName == "ForwardStandardShader.ps", "fixture pixel program identity differs");
    Require(Program.VariantName == "ForwardStandard", "fixture standard variant identity differs");
}

/// <summary>Prints and verifies exact values for every active uniform in both real shader stages.</summary>
void TestEveryActiveMappingHasExplicitValues() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    std::string coverageError;
    Require(HasExactExpectedUniformIdentities(Program, coverageError),
        "real fixture active identities do not match the exact standard set: " + coverageError);
    std::size_t total = 0u;
    const char* const stageNames[] = { "Vertex", "Pixel" };
    const std::vector<helengine::ps3::Ps3CookedShaderUniform>* stages[] = {
        &Program.Vertex.Uniforms, &Program.Pixel.Uniforms
    };
    for (std::size_t stageIndex = 0u; stageIndex < 2u; ++stageIndex) {
        for (const helengine::ps3::Ps3CookedShaderUniform& uniform : *stages[stageIndex]) {
            std::printf("UNIFORM stage=%s buffer=%s member=%s binding=%lu scalar=%s width=%lu columns=%lu array=%lu byteSize=%lu matrixStride=%lu rowMajor=%lu sdkType=%lu sdkCount=%lu\n",
                stageNames[stageIndex], uniform.BufferName.c_str(), uniform.MemberPath.c_str(),
                static_cast<unsigned long>(uniform.AuthoredBinding), uniform.ScalarType.c_str(),
                static_cast<unsigned long>(uniform.VectorWidth), static_cast<unsigned long>(uniform.Columns),
                static_cast<unsigned long>(uniform.ArrayLength), static_cast<unsigned long>(uniform.ByteSize),
                static_cast<unsigned long>(uniform.MatrixStride), static_cast<unsigned long>(uniform.RowMajor),
                static_cast<unsigned long>(uniform.SdkDataType), static_cast<unsigned long>(uniform.SdkElementCount));
            RequireResolvedValue(uniform, draw);
            ++total;
        }
    }
    Require(total == ExpectedUniformIdentityCount,
        "actual ForwardStandard active-uniform count differs from the exact expected identity set");
}

/// <summary>Rejects one expected member replaced by a duplicate while retaining all 41 rows.</summary>
void TestCoverageRejectsDuplicateReplacingMember() {
    const std::vector<std::uint8_t> fixtureSnapshot = ArtifactBytes;
    helengine::ps3::Ps3CookedShaderProgram alteredProgram = Program;
    std::vector<helengine::ps3::Ps3CookedShaderUniform>* stages[] = {
        &alteredProgram.Vertex.Uniforms, &alteredProgram.Pixel.Uniforms
    };
    bool replaced = false;
    for (std::size_t stageIndex = 0u; stageIndex < 2u && !replaced; ++stageIndex) {
        for (helengine::ps3::Ps3CookedShaderUniform& uniform : *stages[stageIndex]) {
            if (uniform.BufferName == "TransformBuffer" && uniform.MemberPath == "worldViewProj") {
                uniform.MemberPath = "world";
                replaced = true;
                break;
            }
        }
    }
    Require(replaced, "test could not create its bounded duplicate-replaces-member mutation");
    const std::size_t activeCount = alteredProgram.Vertex.Uniforms.size() + alteredProgram.Pixel.Uniforms.size();
    Require(activeCount == ExpectedUniformIdentityCount,
        "duplicate-replaces-member mutation changed the active-row count");

    std::string error;
    Require(!HasExactExpectedUniformIdentities(alteredProgram, error),
        "coverage accepted a duplicate identity replacing an expected member");
    Require(error.find("duplicate active uniform TransformBuffer/world") != std::string::npos,
        "coverage did not identify the duplicate replacing member");
    Require(ArtifactBytes == fixtureSnapshot, "duplicate coverage mutation changed immutable fixture bytes");
}

/// <summary>Rejects one missing expected member replaced by an unknown identity at the same row count.</summary>
void TestCoverageRejectsMissingExpectedIdentity() {
    const std::vector<std::uint8_t> fixtureSnapshot = ArtifactBytes;
    helengine::ps3::Ps3CookedShaderProgram alteredProgram = Program;
    std::vector<helengine::ps3::Ps3CookedShaderUniform>* stages[] = {
        &alteredProgram.Vertex.Uniforms, &alteredProgram.Pixel.Uniforms
    };
    bool replaced = false;
    for (std::size_t stageIndex = 0u; stageIndex < 2u && !replaced; ++stageIndex) {
        for (helengine::ps3::Ps3CookedShaderUniform& uniform : *stages[stageIndex]) {
            if (uniform.BufferName == "TransformBuffer" && uniform.MemberPath == "worldViewProj") {
                uniform.MemberPath = "testOnlyMissingIdentity";
                replaced = true;
                break;
            }
        }
    }
    Require(replaced, "test could not create its bounded missing-identity mutation");
    const std::size_t activeCount = alteredProgram.Vertex.Uniforms.size() + alteredProgram.Pixel.Uniforms.size();
    Require(activeCount == ExpectedUniformIdentityCount,
        "missing-identity mutation changed the active-row count");

    std::string error;
    Require(!HasExactExpectedUniformIdentities(alteredProgram, error),
        "coverage accepted a missing expected identity at the original row count");
    Require(error.find("missing expected active uniform TransformBuffer/worldViewProj") != std::string::npos,
        "coverage did not identify the missing expected member");
    Require(ArtifactBytes == fixtureSnapshot, "missing-identity mutation changed immutable fixture bytes");
}

/// <summary>Verifies the lit ambient member receives the caller's nonwhite scene radiance.</summary>
void TestLitAmbientUsesCallerRadiance() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "ambientLightColor"), draw);
}

/// <summary>Verifies one directional light publishes count one, type zero, and unnegated source direction.</summary>
void TestDirectionalCountTypeAndDirection() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "lightMetadata"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "light0ColorAndType"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "light0DirectionAndShadow"), draw);
}

/// <summary>Verifies a lit draw without a directional light keeps ambient radiance and clears every light slot.</summary>
void TestLitWithoutDirectionalLightUsesNoSlots() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    draw.HasDirectionalLight = false;
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "ambientLightColor"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "lightMetadata"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "light0ColorAndType"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "light0DirectionAndShadow"), draw);
}

/// <summary>Verifies an unlit draw uses white ambient and disables directional slots explicitly.</summary>
void TestUnlitUsesWhiteAmbientAndNoDirectionalLights() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    draw.Lit = false;
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "ambientLightColor"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "lightMetadata"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "light0ColorAndType"), draw);
    RequireResolvedValue(FindUniform("ForwardLightBuffer", "light0DirectionAndShadow"), draw);
}

/// <summary>Verifies consecutive resolves use each draw's base color instead of retaining prior data.</summary>
void TestConsecutiveDrawsDoNotRetainValues() {
    const helengine::ps3::Ps3CookedShaderUniform& uniform = FindUniform("BaseColorBuffer", "baseColor");
    helengine::ps3::Ps3StandardDrawData first{};
    PopulateDistinctDraw(first);
    RequireResolvedValue(uniform, first);

    helengine::ps3::Ps3StandardDrawData second{};
    PopulateDistinctDraw(second);
    second.BaseColor[0] = 0.91f;
    second.BaseColor[1] = 0.82f;
    second.BaseColor[2] = 0.73f;
    second.BaseColor[3] = 0.64f;
    RequireResolvedValue(uniform, second);
}

/// <summary>Verifies double-sided raster state does not alter a supported shader uniform value.</summary>
void TestDoubleSidedRemainsRasterState() {
    const helengine::ps3::Ps3CookedShaderUniform& uniform = FindUniform("BaseColorBuffer", "baseColor");
    helengine::ps3::Ps3StandardDrawData first{};
    PopulateDistinctDraw(first);
    first.DoubleSided = false;
    float singleSided[16];
    std::size_t singleCount = 0u;
    std::string error;
    Require(helengine::ps3::Ps3StandardUniformProvider::Resolve(uniform, first,
        singleSided, 16u, singleCount, error), "single-sided base color failed: " + error);

    first.DoubleSided = true;
    float doubleSided[16];
    std::size_t doubleCount = 0u;
    error.clear();
    Require(helengine::ps3::Ps3StandardUniformProvider::Resolve(uniform, first,
        doubleSided, 16u, doubleCount, error), "double-sided base color failed: " + error);
    Require(singleCount == doubleCount, "double-sided raster state changed the uniform element count");
    RequireValuesEqual(singleSided, doubleSided, singleCount, "double-sided baseColor");
}

/// <summary>Verifies double-sided raster state is not accepted as an invented shader member.</summary>
void TestDoubleSidedDoesNotInventUniform() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.MemberPath = "DoubleSided";
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies switching lit state between consecutive draws does not retain ambient uniforms.</summary>
void TestLitUnlitTransitionDoesNotRetainAmbient() {
    const helengine::ps3::Ps3CookedShaderUniform& uniform = FindUniform("ForwardLightBuffer", "ambientLightColor");
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    RequireResolvedValue(uniform, draw);
    draw.Lit = false;
    RequireResolvedValue(uniform, draw);
    draw.Lit = true;
    RequireResolvedValue(uniform, draw);
}

/// <summary>Verifies asymmetric SDK-packed world-matrix floats are copied in original element order.</summary>
void TestPackedMatrixPreservesElementOrder() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    RequireResolvedValue(FindUniform("TransformBuffer", "world"), draw);
}

/// <summary>Verifies unknown buffers are rejected with unchanged output and a contextual diagnostic.</summary>
void TestUnknownBufferRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.BufferName = "UnprovidedBuffer";
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies unknown members are rejected with unchanged output and a contextual diagnostic.</summary>
void TestUnknownMemberRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.MemberPath = "unprovidedMember";
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies a known buffer with the wrong authored binding is rejected before output writes.</summary>
void TestWrongBindingRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    ++uniform.AuthoredBinding;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies a non-FP3232 scalar label is rejected before output writes.</summary>
void TestWrongScalarTypeRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.ScalarType = "FP1616";
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies a float4 member with incompatible vector width is rejected before output writes.</summary>
void TestWrongVectorShapeRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.VectorWidth = 3u;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies a matrix member with incompatible dimensions is rejected before output writes.</summary>
void TestWrongMatrixShapeRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("TransformBuffer", "world");
    uniform.Columns = 3u;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies array uniforms are rejected because this provider supports only one value.</summary>
void TestWrongArrayLengthRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.ArrayLength = 2u;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies a mismatched pinned SDK type code is rejected before output writes.</summary>
void TestWrongSdkTypeRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.SdkDataType = 255u;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies a mismatched SDK element count is rejected before output writes.</summary>
void TestWrongSdkElementCountRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("TransformBuffer", "world");
    uniform.SdkElementCount = 3u;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies metadata too small for a known float4 value is rejected before output writes.</summary>
void TestInsufficientByteSizeRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    auto uniform = FindUniform("BaseColorBuffer", "baseColor");
    uniform.ByteSize = 4u;
    RequireRejectedWithoutWrites(uniform, draw);
}

/// <summary>Verifies null output storage is rejected without publishing a written count.</summary>
void TestNullDestinationRejected() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    const auto& uniform = FindUniform("BaseColorBuffer", "baseColor");
    std::size_t written = 77u;
    std::string error;
    const bool resolved = helengine::ps3::Ps3StandardUniformProvider::Resolve(
        uniform, draw, nullptr, 16u, written, error);
    Require(!resolved, "null destination was accepted");
    Require(written == 0u && !error.empty(), "null destination failure did not clear count and report an error");
    Require(error.find(uniform.BufferName) != std::string::npos
        && error.find(uniform.MemberPath) != std::string::npos,
        "null destination diagnostic omitted its uniform identity");
}

/// <summary>Verifies insufficient output capacity is rejected without changing destination bytes.</summary>
void TestInsufficientCapacityRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    const auto& uniform = FindUniform("BaseColorBuffer", "baseColor");
    RequireRejectedWithoutWrites(uniform, draw, 3u);
}

/// <summary>Verifies a non-finite required caller color is rejected without changing destination bytes.</summary>
void TestNonfiniteRequiredValueRejectedWithoutWrites() {
    helengine::ps3::Ps3StandardDrawData draw{};
    PopulateDistinctDraw(draw);
    draw.BaseColor[2] = std::numeric_limits<float>::quiet_NaN();
    RequireRejectedWithoutWrites(FindUniform("BaseColorBuffer", "baseColor"), draw);
}

/// <summary>Runs one named PPU assertion and records a unique PASS or FAIL line.</summary>
/// <param name="name">Stable machine-validated assertion name.</param>
/// <param name="test">Assertion function executed for this case.</param>
void RunCase(const char* name, void (*test)()) {
    try {
        test();
        ++PassedCases;
        std::printf("PASS %s\n", name);
    } catch (const std::exception& exception) {
        ++FailedCases;
        std::printf("FAIL %s: %s\n", name, exception.what());
    } catch (...) {
        ++FailedCases;
        std::printf("FAIL %s: unknown exception\n", name);
    }
}

} // namespace

/// <summary>Loads the immutable standard P3SP artifact and runs the named CPU-provider regression cases.</summary>
int main() {
    std::string error;
    if (!LoadArtifact(error)) {
        std::printf("FAIL fixture-load: %s\n", error.c_str());
        std::printf("PS3_STANDARD_UNIFORMS_TESTS passed=0 failed=1\n");
        return 1;
    }
    if (!helengine::ps3::Ps3ShaderArtifactReader::Read(
        ArtifactBytes.data(), ArtifactBytes.size(), Program, error)) {
        std::printf("FAIL fixture-read: %s\n", error.c_str());
        std::printf("PS3_STANDARD_UNIFORMS_TESTS passed=0 failed=1\n");
        return 1;
    }

    RunCase("real-artifact-identity", TestRealArtifactIdentity);
    RunCase("every-active-mapping-has-explicit-values", TestEveryActiveMappingHasExplicitValues);
    RunCase("coverage-rejects-duplicate-replacing-member", TestCoverageRejectsDuplicateReplacingMember);
    RunCase("coverage-rejects-missing-expected-identity", TestCoverageRejectsMissingExpectedIdentity);
    RunCase("lit-ambient-uses-caller-radiance", TestLitAmbientUsesCallerRadiance);
    RunCase("directional-count-type-and-direction", TestDirectionalCountTypeAndDirection);
    RunCase("lit-without-directional-light-clears-slots", TestLitWithoutDirectionalLightUsesNoSlots);
    RunCase("unlit-uses-white-ambient-and-no-lights", TestUnlitUsesWhiteAmbientAndNoDirectionalLights);
    RunCase("consecutive-draws-do-not-retain-values", TestConsecutiveDrawsDoNotRetainValues);
    RunCase("double-sided-remains-raster-state", TestDoubleSidedRemainsRasterState);
    RunCase("double-sided-does-not-invent-uniform", TestDoubleSidedDoesNotInventUniform);
    RunCase("lit-unlit-transition-does-not-retain-ambient", TestLitUnlitTransitionDoesNotRetainAmbient);
    RunCase("packed-matrix-preserves-element-order", TestPackedMatrixPreservesElementOrder);
    RunCase("unknown-buffer-rejected-without-writes", TestUnknownBufferRejectedWithoutWrites);
    RunCase("unknown-member-rejected-without-writes", TestUnknownMemberRejectedWithoutWrites);
    RunCase("wrong-binding-rejected-without-writes", TestWrongBindingRejectedWithoutWrites);
    RunCase("wrong-scalar-type-rejected-without-writes", TestWrongScalarTypeRejectedWithoutWrites);
    RunCase("wrong-vector-shape-rejected-without-writes", TestWrongVectorShapeRejectedWithoutWrites);
    RunCase("wrong-matrix-shape-rejected-without-writes", TestWrongMatrixShapeRejectedWithoutWrites);
    RunCase("wrong-array-length-rejected-without-writes", TestWrongArrayLengthRejectedWithoutWrites);
    RunCase("wrong-sdk-type-rejected-without-writes", TestWrongSdkTypeRejectedWithoutWrites);
    RunCase("wrong-sdk-element-count-rejected-without-writes", TestWrongSdkElementCountRejectedWithoutWrites);
    RunCase("insufficient-byte-size-rejected-without-writes", TestInsufficientByteSizeRejectedWithoutWrites);
    RunCase("null-destination-rejected", TestNullDestinationRejected);
    RunCase("insufficient-capacity-rejected-without-writes", TestInsufficientCapacityRejectedWithoutWrites);
    RunCase("nonfinite-required-value-rejected-without-writes", TestNonfiniteRequiredValueRejectedWithoutWrites);

    std::printf("PS3_STANDARD_UNIFORMS_TESTS passed=%lu failed=%lu\n",
        static_cast<unsigned long>(PassedCases), static_cast<unsigned long>(FailedCases));
    return FailedCases == 0u ? 0 : 1;
}
