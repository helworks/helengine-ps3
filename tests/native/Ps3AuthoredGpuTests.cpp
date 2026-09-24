#include "platform/ps3/Ps3AuthoredProgram.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3CookedShaderProgram.hpp"
#include "platform/ps3/Ps3RsxProgramValidator.hpp"
#include "platform/ps3/Ps3ShaderArtifactReader.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include <malloc.h>
#include <sys/process.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <rsx/rsx.h>

SYS_PROCESS_PARAM(1001, 0x100000);

namespace {

/// <summary>Bounds the complete P3SP input accepted by the native fixture test.</summary>
constexpr std::size_t MaximumArtifactBytes = 16u * 1024u * 1024u;

/// <summary>Bounds each exact SDK stage blob supplied with the native fixture.</summary>
constexpr std::size_t MaximumStageBytes = 1024u * 1024u;

/// <summary>Alignment used for immutable test geometry stored in RSX local memory.</summary>
constexpr std::size_t GeometryAlignment = 128u;

/// <summary>Stride of the fixture's position3-plus-UV2 vertex record.</summary>
constexpr std::uint32_t VertexStride = 20u;

/// <summary>Number of passing assertions written to the emulator TTY.</summary>
std::size_t PassedCases = 0u;

/// <summary>Number of failed assertions written to the emulator TTY.</summary>
std::size_t FailedCases = 0u;

/// <summary>One position and normalized texture coordinate used by the fullscreen test quad.</summary>
struct TestVertex {
    /// <summary>Gets the vertex position's X component.</summary>
    float X;

    /// <summary>Gets the vertex position's Y component.</summary>
    float Y;

    /// <summary>Gets the vertex position's Z component.</summary>
    float Z;

    /// <summary>Gets the normalized horizontal texture coordinate.</summary>
    float U;

    /// <summary>Gets the normalized vertical texture coordinate.</summary>
    float V;
};

/// <summary>Stores an independently specified RGB color for pixel comparison.</summary>
struct ExpectedRgb {
    /// <summary>Gets the expected red channel.</summary>
    unsigned Red;

    /// <summary>Gets the expected green channel.</summary>
    unsigned Green;

    /// <summary>Gets the expected blue channel.</summary>
    unsigned Blue;
};

/// <summary>Records one named observable result for the runner's fresh TTY summary.</summary>
void Expect(bool passed, const char* name, const std::string& detail = std::string()) {
    if (passed) {
        ++PassedCases;
        std::printf("PASS %s%s%s\n", name, detail.empty() ? "" : " ", detail.c_str());
        return;
    }
    ++FailedCases;
    std::printf("FAIL %s%s%s\n", name, detail.empty() ? "" : " ", detail.c_str());
}

/// <summary>Stops dependent GPU assertions with an explicit fixture or lifecycle error.</summary>
void Require(bool condition, const std::string& error) {
    if (!condition) {
        throw std::runtime_error(error);
    }
}

/// <summary>Reads an exact bounded fixture file without accepting short or trailing input.</summary>
bool LoadBoundedFile(const char* path, std::size_t minimum, std::size_t maximum,
    std::vector<std::uint8_t>& destination, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = std::string("cannot open fixture ") + path;
        return false;
    }
    const std::streamoff length = stream.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) < minimum
        || static_cast<std::uint64_t>(length) > maximum) {
        error = std::string("fixture length is outside its bound: ") + path;
        return false;
    }
    stream.seekg(0, std::ios::beg);
    destination.resize(static_cast<std::size_t>(length));
    stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    if (!stream) {
        error = std::string("cannot read complete fixture ") + path;
        destination.clear();
        return false;
    }
    return true;
}

/// <summary>Finds one reflected active uniform by authored buffer and member identity.</summary>
std::size_t FindUniformIndex(const helengine::ps3::Ps3CookedShaderStage& stage,
    const char* bufferName, const char* memberPath) {
    for (std::size_t index = 0u; index < stage.Uniforms.size(); ++index) {
        if (stage.Uniforms[index].BufferName == bufferName && stage.Uniforms[index].MemberPath == memberPath) {
            return index;
        }
    }
    throw std::runtime_error(std::string("missing active fixture uniform ") + bufferName + "." + memberPath);
}

/// <summary>Finds the required authored vertex semantic without substituting source or ordinal locations.</summary>
const helengine::ps3::Ps3CookedShaderAttribute& FindAttribute(
    const helengine::ps3::Ps3CookedShaderProgram& program, const char* semantic) {
    for (const helengine::ps3::Ps3CookedShaderAttribute& attribute : program.Attributes) {
        if (attribute.FullAuthoredSemantic == semantic) {
            return attribute;
        }
    }
    throw std::runtime_error(std::string("fixture has no authored vertex semantic ") + semantic);
}

/// <summary>Verifies an API rejects invalid input before changing RSX command state.</summary>
void ExpectRejectedWithoutCommands(bool result, const std::string& error,
    helengine::ps3::Ps3Video& video, unsigned commandOffset, const char* name) {
    const bool passed = !result && !error.empty() && video.CommandWriteOffset() == commandOffset;
    Expect(passed, name, passed ? std::string() : "rejection must report an error and emit no RSX commands");
}

/// <summary>Uploads one complete, explicitly packed SDK uniform vector set for the current draw.</summary>
void UploadUniformOrThrow(helengine::ps3::Ps3AuthoredProgram& program, std::uint32_t stage,
    std::size_t index, const float* values, std::size_t count, const char* name) {
    std::string error;
    Require(program.UploadUniform(stage, index, values, count, error),
        std::string("valid uniform upload failed for ") + name + ": " + error);
}

/// <summary>Uploads all three active fixture uniforms for one visible tint phase.</summary>
void UploadFixtureUniforms(helengine::ps3::Ps3AuthoredProgram& program,
    std::size_t transformIndex, std::size_t tintIndex, std::size_t pixelTintIndex,
    const float* tint, const float* pixelTint) {
    const float transform[16] = {
        0.5f, 0.0f, 0.0f, 0.25f,
        0.0f, 0.75f, 0.0f, -0.125f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string error;
    Require(program.BeginDraw(error), std::string("BeginDraw failed: ") + error);
    UploadUniformOrThrow(program, 0u, transformIndex, transform, 16u, "Frame.Transform");
    UploadUniformOrThrow(program, 0u, tintIndex, tint == nullptr ? white : tint, 4u, "Frame.Tint");
    UploadUniformOrThrow(program, 1u, pixelTintIndex, pixelTint == nullptr ? white : pixelTint, 4u, "Surface.PixelTint");
    Require(program.FinishUniformUpdates(error), std::string("uniform completion failed: ") + error);
}

/// <summary>Allocates the fixed quad and 16-bit index list in aligned RSX local memory.</summary>
void AllocateGeometry(void*& vertexBuffer, std::uint32_t& vertexOffset,
    void*& indexBuffer, std::uint32_t& indexOffset) {
    static_assert(sizeof(TestVertex) == VertexStride, "authored GPU test vertex stride must be 20 bytes");
    const TestVertex vertices[4] = {
        { -1.0f,  1.0f, 0.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 0.0f, 1.0f, 0.0f },
        { -1.0f, -1.0f, 0.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 0.0f, 1.0f, 1.0f }
    };
    const std::uint16_t indices[6] = { 0u, 1u, 2u, 1u, 3u, 2u };
    const std::size_t vertexBytes = sizeof(vertices);
    const std::size_t indexBytes = sizeof(indices);
    vertexBuffer = rsxMemalign(GeometryAlignment, vertexBytes);
    Require(vertexBuffer != nullptr, "RSX quad vertex allocation failed");
    if (rsxAddressToOffset(vertexBuffer, &vertexOffset) != 0) {
        rsxFree(vertexBuffer);
        vertexBuffer = nullptr;
        throw std::runtime_error("RSX quad vertex offset resolution failed");
    }
    indexBuffer = rsxMemalign(GeometryAlignment, indexBytes);
    if (indexBuffer == nullptr) {
        rsxFree(vertexBuffer);
        vertexBuffer = nullptr;
        throw std::runtime_error("RSX quad index allocation failed");
    }
    if (rsxAddressToOffset(indexBuffer, &indexOffset) != 0) {
        rsxFree(indexBuffer);
        rsxFree(vertexBuffer);
        indexBuffer = nullptr;
        vertexBuffer = nullptr;
        throw std::runtime_error("RSX quad index offset resolution failed");
    }
    std::memcpy(vertexBuffer, vertices, vertexBytes);
    std::memcpy(indexBuffer, indices, indexBytes);
}

/// <summary>Releases test geometry only after the video owner confirms all queued RSX work drained.</summary>
void ReleaseGeometry(helengine::ps3::Ps3Video& video,
    void*& vertexBuffer, void*& indexBuffer) {
    Require(video.HasDrained(), "test geometry cannot be released before successful Ps3Video::Drain");
    if (indexBuffer != nullptr) {
        rsxFree(indexBuffer);
        indexBuffer = nullptr;
    }
    if (vertexBuffer != nullptr) {
        rsxFree(vertexBuffer);
        vertexBuffer = nullptr;
    }
}

/// <summary>Configures two reflected input slots, binds the quad and submits its indexed triangles.</summary>
void DrawQuad(helengine::ps3::Ps3Video& video, helengine::ps3::Ps3AuthoredProgram& program,
    std::uint32_t vertexOffset, std::uint32_t indexOffset) {
    const helengine::ps3::Ps3CookedShaderProgram& artifact = program.Program();
    const helengine::ps3::Ps3CookedShaderAttribute& position = FindAttribute(artifact, "POSITION");
    const helengine::ps3::Ps3CookedShaderAttribute& uv = FindAttribute(artifact, "TEXCOORD0");
    Require(position.VectorWidth == 3u && uv.VectorWidth == 2u,
        "fixture input shapes do not match position3-plus-UV2 test geometry");
    Require(position.HardwareSlot <= 255u && uv.HardwareSlot <= 255u,
        "fixture hardware attribute slot exceeds the RSX command width");

    gcmContextData* context = video.Context();
    Require(context != nullptr, "video context disappeared before the fixture draw");
    rsxBindVertexArrayAttrib(context, static_cast<u8>(position.HardwareSlot), 0u,
        vertexOffset, static_cast<u8>(VertexStride), 3u, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(context, static_cast<u8>(uv.HardwareSlot), 0u,
        vertexOffset + 12u, static_cast<u8>(VertexStride), 2u, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxSetCullFaceEnable(context, GCM_FALSE);
    rsxSetDepthTestEnable(context, GCM_FALSE);
    rsxSetDepthWriteEnable(context, GCM_FALSE);
    rsxSetBlendEnable(context, GCM_FALSE);
    rsxSetShadeModel(context, GCM_SHADE_MODEL_SMOOTH);
    rsxDrawIndexArray(context, GCM_TYPE_TRIANGLES, indexOffset, 6u,
        GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
}

/// <summary>Reads one safely interior pixel and compares RGB channels with a two-code-value tolerance.</summary>
void CheckPixel(helengine::ps3::Ps3Video& video, const char* phase,
    float fractionX, float fractionY, ExpectedRgb expected, const char* label) {
    const unsigned x = static_cast<unsigned>(std::floor(fractionX * static_cast<float>(video.Width())));
    const unsigned y = static_cast<unsigned>(std::floor(fractionY * static_cast<float>(video.Height())));
    unsigned packed = 0u;
    if (!video.ReadDisplayedPixel(x, y, &packed)) {
        Expect(false, label, "displayed pixel readback unavailable after BeginFrame");
        std::printf("PS3_AUTHORED_GPU_PIXEL phase=%s sample=(%.3f,%.3f) pixel=(%u,%u) expected=(%u,%u,%u) actual=unavailable\n",
            phase, fractionX, fractionY, x, y, expected.Red, expected.Green, expected.Blue);
        return;
    }
    const unsigned actualRed = (packed >> 16u) & 0xFFu;
    const unsigned actualGreen = (packed >> 8u) & 0xFFu;
    const unsigned actualBlue = packed & 0xFFu;
    const bool matched = std::abs(static_cast<int>(actualRed) - static_cast<int>(expected.Red)) <= 2
        && std::abs(static_cast<int>(actualGreen) - static_cast<int>(expected.Green)) <= 2
        && std::abs(static_cast<int>(actualBlue) - static_cast<int>(expected.Blue)) <= 2;
    std::printf("PS3_AUTHORED_GPU_PIXEL phase=%s sample=(%.3f,%.3f) pixel=(%u,%u) expected=(%u,%u,%u) actual=(%u,%u,%u)\n",
        phase, fractionX, fractionY, x, y, expected.Red, expected.Green, expected.Blue,
        actualRed, actualGreen, actualBlue);
    Expect(matched, label, matched ? std::string() : "RGB differs by more than two code values");
}

/// <summary>Checks every fixture sample after the matching present has been waited by BeginFrame.</summary>
void CheckPhasePixels(helengine::ps3::Ps3Video& video, const char* phase,
    const ExpectedRgb colors[4]) {
    CheckPixel(video, phase, 0.5f, 0.375f, colors[0], phase[0] == 'A' ? "phaseA-red" : "phaseB-red");
    CheckPixel(video, phase, 0.75f, 0.375f, colors[1], phase[0] == 'A' ? "phaseA-green" : "phaseB-green");
    CheckPixel(video, phase, 0.5f, 0.75f, colors[2], phase[0] == 'A' ? "phaseA-blue" : "phaseB-blue");
    CheckPixel(video, phase, 0.75f, 0.75f, colors[3], phase[0] == 'A' ? "phaseA-yellow" : "phaseB-yellow");
    CheckPixel(video, phase, 0.1f, 0.1f, ExpectedRgb{ 100u, 149u, 237u },
        phase[0] == 'A' ? "phaseA-background" : "phaseB-background");
}

/// <summary>Proves uninitialized owners reject operations and report lifecycle errors.</summary>
void TestUninitializedOperations() {
    helengine::ps3::Ps3AuthoredProgram program;
    helengine::ps3::Ps3AuthoredTexture texture;
    std::string error;
    const float value[1] = { 1.0f };
    error.clear();
    Expect(!program.BeginDraw(error) && !error.empty(), "program-begindraw-before-initialize");
    error.clear();
    Expect(!program.UploadUniform(0u, 0u, value, 1u, error) && !error.empty(), "program-uniform-before-initialize");
    error.clear();
    Expect(!program.FinishUniformUpdates(error) && !error.empty(), "program-finish-before-initialize");
    error.clear();
    Expect(!program.ShutdownAfterDrain(error) && !error.empty(), "program-shutdown-before-initialize");
    bool programAccessThrew = false;
    try {
        (void)program.Program();
    } catch (const std::logic_error&) {
        programAccessThrew = true;
    }
    Expect(programAccessThrew, "program-metadata-before-initialize");
    error.clear();
    Expect(!texture.Bind(0u, error) && !error.empty(), "texture-bind-before-initialize");
    error.clear();
    Expect(!texture.ShutdownAfterDrain(error) && !error.empty(), "texture-shutdown-before-initialize");
}

/// <summary>Uploads a valid set of uniforms through the production API and fails on its first error.</summary>
void UploadUniformChecked(helengine::ps3::Ps3AuthoredProgram& program,
    std::uint32_t stage, std::size_t index, const float* values, std::size_t count,
    const char* name) {
    UploadUniformOrThrow(program, stage, index, values, count, name);
}

} // namespace

int main() {
    static const std::uint8_t texels[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 0u, 255u
    };
    static const std::uint8_t cubeColor[4] = { 255u, 255u, 255u, 255u };
    static const float phaseATransform[16] = {
        0.5f, 0.0f, 0.0f, 0.25f,
        0.0f, 0.75f, 0.0f, -0.125f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    static const float phaseAWhite[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    static const float phaseBVertexTint[4] = { 1.0f, 0.5f, 1.0f, 1.0f };
    static const float phaseBPixelTint[4] = { 0.5f, 1.0f, 1.0f, 1.0f };

    helengine::ps3::Ps3Video video;
    helengine::ps3::Ps3AuthoredProgram program;
    helengine::ps3::Ps3AuthoredTexture texture;
    helengine::ps3::Ps3AuthoredTexture cubeTexture;
    helengine::ps3::Ps3CookedShaderProgram artifactProgram;
    std::vector<std::uint8_t> artifactBytes;
    std::vector<std::uint8_t> rawVertexBytes;
    std::vector<std::uint8_t> rawPixelBytes;
    void* vertexBuffer = nullptr;
    void* indexBuffer = nullptr;
    std::uint32_t vertexOffset = 0u;
    std::uint32_t indexOffset = 0u;
    bool videoInitialized = false;

    try {
        std::string error;
        Require(LoadBoundedFile("/app_home/USRDIR/content/fixture.ps3sp", 12u, MaximumArtifactBytes, artifactBytes, error)
            && LoadBoundedFile("/app_home/USRDIR/content/vertex.bin", 1u, MaximumStageBytes, rawVertexBytes, error)
            && LoadBoundedFile("/app_home/USRDIR/content/pixel.bin", 1u, MaximumStageBytes, rawPixelBytes, error), error);
        Require(helengine::ps3::Ps3ShaderArtifactReader::Read(
            artifactBytes.data(), artifactBytes.size(), artifactProgram, error),
            std::string("reader rejected the real authored-GPU fixture: ") + error);
        Require(artifactProgram.Vertex.Bytecode == rawVertexBytes && artifactProgram.Pixel.Bytecode == rawPixelBytes,
            "P3SP stage blobs differ from the producer's exact raw fixture files");
        Require(helengine::ps3::Ps3RsxProgramValidator::Validate(artifactProgram, error),
            std::string("GPU test fixture failed direct program validation: ") + error);
        Expect(artifactProgram.ShaderAssetId == "authored-gpu-fixture"
            && artifactProgram.VertexProgramName == "gpu.vs"
            && artifactProgram.PixelProgramName == "gpu.ps"
            && artifactProgram.VariantName == "default", "fixture-identity");

        std::printf("PS3_AUTHORED_GPU_ARTIFACT asset=%s vertex=%s pixel=%s variant=%s\n",
            artifactProgram.ShaderAssetId.c_str(), artifactProgram.VertexProgramName.c_str(),
            artifactProgram.PixelProgramName.c_str(), artifactProgram.VariantName.c_str());
        TestUninitializedOperations();

        Require(video.Initialize(), "Ps3Video initialization failed");
        videoInitialized = true;
        Require(video.BeginFrame(true), "initial Ps3Video::BeginFrame failed");
        Require(video.ClearSurface(true, true, false, 0xFF6495EDu, 0xFFFFFF00u),
            "cornflower-blue fixture clear submission failed");

        helengine::ps3::Ps3AuthoredTexture invalidTexture;
        error.clear();
        Expect(!invalidTexture.InitializeRgba2D(video, 0u, 2u, texels, sizeof(texels), error)
            && !error.empty() && invalidTexture.OwnedGpuBytes() == 0u, "texture-zero-width-rejected");
        error.clear();
        Expect(!invalidTexture.InitializeRgba2D(video, 2u, 2u, texels, sizeof(texels) - 1u, error)
            && !error.empty() && invalidTexture.OwnedGpuBytes() == 0u, "texture-wrong-byte-count-rejected");

        Require(program.Initialize(video, artifactProgram, error),
            std::string("authored program initialization failed: ") + error);
        Require(texture.InitializeRgba2D(video, 2u, 2u, texels, sizeof(texels), error),
            std::string("RGBA texture initialization failed: ") + error);
        Require(cubeTexture.InitializeSolidCube(video, cubeColor, error),
            std::string("solid cube initialization failed: ") + error);
        Expect(program.OwnedGpuBytes() > 0u && texture.OwnedGpuBytes() == 16u
            && cubeTexture.OwnedGpuBytes() == 768u, "initialized-gpu-byte-ownership");

        const std::size_t programBytesBeforeDuplicate = program.OwnedGpuBytes();
        error.clear();
        const bool duplicateProgram = program.Initialize(video, artifactProgram, error);
        Expect(!duplicateProgram && !error.empty() && program.OwnedGpuBytes() == programBytesBeforeDuplicate,
            "duplicate-program-initialize-rejected");
        const std::size_t textureBytesBeforeDuplicate = texture.OwnedGpuBytes();
        error.clear();
        const bool duplicateTexture = texture.InitializeRgba2D(video, 2u, 2u, texels, sizeof(texels), error);
        Expect(!duplicateTexture && !error.empty() && texture.OwnedGpuBytes() == textureBytesBeforeDuplicate,
            "duplicate-texture-initialize-rejected");

        const std::size_t transformIndex = FindUniformIndex(artifactProgram.Vertex, "Frame", "Transform");
        const std::size_t tintIndex = FindUniformIndex(artifactProgram.Vertex, "Frame", "Tint");
        const std::size_t pixelTintIndex = FindUniformIndex(artifactProgram.Pixel, "Surface", "PixelTint");
        const helengine::ps3::Ps3CookedShaderSampler& albedo = [&]() -> const helengine::ps3::Ps3CookedShaderSampler& {
            for (const helengine::ps3::Ps3CookedShaderSampler& sampler : artifactProgram.PixelSamplers) {
                if (sampler.TextureName == "Albedo") return sampler;
            }
            throw std::runtime_error("fixture has no Albedo texture mapping");
        }();
        Require(albedo.Dimension == "Dim2D" && albedo.TextureUnit <= 15u,
            "fixture Albedo sampler has an unsupported dimension or texture unit");
        const helengine::ps3::Ps3CookedShaderAttribute& position = FindAttribute(artifactProgram, "POSITION");
        const helengine::ps3::Ps3CookedShaderAttribute& uv = FindAttribute(artifactProgram, "TEXCOORD0");
        std::printf("PS3_AUTHORED_GPU_MAPPING positionSlot=%u uvSlot=%u textureUnit=%u\n",
            position.HardwareSlot, uv.HardwareSlot, albedo.TextureUnit);

        Require(program.BeginDraw(error), std::string("uniform preflight BeginDraw failed: ") + error);
        const unsigned preflightOffset = video.CommandWriteOffset();
        const std::size_t programBytesBeforeRejects = program.OwnedGpuBytes();
        const float finiteValue[16] = { 1.0f };
        float nonfiniteValue[16] = { 0.0f };
        nonfiniteValue[0] = std::numeric_limits<float>::quiet_NaN();
        error.clear();
        const bool badStage = program.UploadUniform(2u, transformIndex, finiteValue, 16u, error);
        ExpectRejectedWithoutCommands(badStage, error, video, preflightOffset, "uniform-invalid-stage-rejected");
        error.clear();
        const bool badIndex = program.UploadUniform(0u, 4096u, finiteValue, 16u, error);
        ExpectRejectedWithoutCommands(badIndex, error, video, preflightOffset, "uniform-invalid-index-rejected");
        error.clear();
        const bool badCount = program.UploadUniform(0u, transformIndex, finiteValue, 1u, error);
        ExpectRejectedWithoutCommands(badCount, error, video, preflightOffset, "uniform-wrong-value-count-rejected");
        error.clear();
        const bool nullValues = program.UploadUniform(0u, transformIndex, nullptr, 16u, error);
        ExpectRejectedWithoutCommands(nullValues, error, video, preflightOffset, "uniform-null-values-rejected");
        error.clear();
        const bool nonfiniteValues = program.UploadUniform(0u, transformIndex, nonfiniteValue, 16u, error);
        ExpectRejectedWithoutCommands(nonfiniteValues, error, video, preflightOffset, "uniform-nonfinite-values-rejected");
        Expect(program.OwnedGpuBytes() == programBytesBeforeRejects,
            "uniform-rejections-preserve-owned-gpu-bytes");
        error.clear();
        const bool missingUniforms = program.FinishUniformUpdates(error);
        ExpectRejectedWithoutCommands(missingUniforms, error, video, preflightOffset,
            "finish-rejects-missing-active-uniforms");

        error.clear();
        const bool badTextureUnit = texture.Bind(16u, error);
        ExpectRejectedWithoutCommands(badTextureUnit, error, video, preflightOffset,
            "texture-invalid-unit-rejected");
        error.clear();
        const bool earlyProgramShutdown = program.ShutdownAfterDrain(error);
        Expect(!earlyProgramShutdown && !error.empty()
            && program.OwnedGpuBytes() == programBytesBeforeRejects,
            "program-shutdown-before-video-drain-preserves-memory");
        error.clear();
        const bool earlyTextureShutdown = texture.ShutdownAfterDrain(error);
        Expect(!earlyTextureShutdown && !error.empty() && texture.OwnedGpuBytes() == 16u,
            "texture-shutdown-before-video-drain-preserves-memory");
        error.clear();
        const bool earlyCubeShutdown = cubeTexture.ShutdownAfterDrain(error);
        Expect(!earlyCubeShutdown && !error.empty() && cubeTexture.OwnedGpuBytes() == 768u,
            "cube-shutdown-before-video-drain-preserves-memory");

        UploadUniformChecked(program, 0u, transformIndex, phaseATransform, 16u, "Frame.Transform");
        UploadUniformChecked(program, 0u, tintIndex, phaseAWhite, 4u, "Frame.Tint");
        UploadUniformChecked(program, 1u, pixelTintIndex, phaseAWhite, 4u, "Surface.PixelTint");
        Require(program.FinishUniformUpdates(error), std::string("phase A uniforms failed: ") + error);
        Require(texture.Bind(albedo.TextureUnit, error), std::string("phase A texture bind failed: ") + error);
        AllocateGeometry(vertexBuffer, vertexOffset, indexBuffer, indexOffset);
        DrawQuad(video, program, vertexOffset, indexOffset);
        Require(video.Present(), "phase A present failed");
        Require(video.BeginFrame(true), "phase A readback frame did not wait for its flip");
        const ExpectedRgb phaseAColors[4] = {
            { 255u, 0u, 0u }, { 0u, 255u, 0u }, { 0u, 0u, 255u }, { 255u, 255u, 0u }
        };
        CheckPhasePixels(video, "A", phaseAColors);

        Require(video.ClearSurface(true, true, false, 0xFF6495EDu, 0xFFFFFF00u),
            "phase B cornflower-blue clear submission failed");
        UploadFixtureUniforms(program, transformIndex, tintIndex, pixelTintIndex,
            phaseBVertexTint, phaseBPixelTint);
        Require(texture.Bind(albedo.TextureUnit, error), std::string("phase B texture bind failed: ") + error);
        DrawQuad(video, program, vertexOffset, indexOffset);
        Require(video.Present(), "phase B present failed");
        Require(video.BeginFrame(true), "phase B readback frame did not wait for its flip");
        const ExpectedRgb phaseBColors[4] = {
            { 128u, 0u, 0u }, { 0u, 128u, 0u }, { 0u, 0u, 255u }, { 128u, 128u, 0u }
        };
        CheckPhasePixels(video, "B", phaseBColors);

        Require(video.Drain(), "final video drain failed; authored GPU allocations remain retained");
        ReleaseGeometry(video, vertexBuffer, indexBuffer);
        Require(program.ShutdownAfterDrain(error), std::string("authored program drain shutdown failed: ") + error);
        Require(texture.ShutdownAfterDrain(error), std::string("RGBA texture drain shutdown failed: ") + error);
        Require(cubeTexture.ShutdownAfterDrain(error), std::string("cube texture drain shutdown failed: ") + error);
        Expect(program.OwnedGpuBytes() == 0u && texture.OwnedGpuBytes() == 0u
            && cubeTexture.OwnedGpuBytes() == 0u, "drained-gpu-owners-return-to-zero-bytes");
        Expect(artifactProgram.Vertex.Bytecode == rawVertexBytes
            && artifactProgram.Pixel.Bytecode == rawPixelBytes
            && artifactBytes.size() <= MaximumArtifactBytes,
            "producer-artifact-remains-unchanged-after-draws");
    } catch (const std::exception& exception) {
        Expect(false, "authored-gpu-fatal", exception.what());
    }

    if (videoInitialized && !video.HasDrained()) {
        const bool drainedForCleanup = video.Drain();
        if (!drainedForCleanup) {
            Expect(false, "cleanup-drain", "failed; GPU allocations are intentionally retained");
        } else {
            if (indexBuffer != nullptr || vertexBuffer != nullptr) {
                ReleaseGeometry(video, vertexBuffer, indexBuffer);
            }
            std::string cleanupError;
            if (program.OwnedGpuBytes() != 0u) (void)program.ShutdownAfterDrain(cleanupError);
            cleanupError.clear();
            if (texture.OwnedGpuBytes() != 0u) (void)texture.ShutdownAfterDrain(cleanupError);
            cleanupError.clear();
            if (cubeTexture.OwnedGpuBytes() != 0u) (void)cubeTexture.ShutdownAfterDrain(cleanupError);
        }
    }
    if (videoInitialized) {
        video.Shutdown();
    }
    std::printf("PS3_AUTHORED_GPU_TESTS passed=%lu failed=%lu\n",
        static_cast<unsigned long>(PassedCases), static_cast<unsigned long>(FailedCases));
    return FailedCases == 0u ? 0 : 1;
}
