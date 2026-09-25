#include "platform/ps3/Ps3AuthoredProgram.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3CookedShaderProgram.hpp"
#include "platform/ps3/Ps3RsxProgramValidator.hpp"
#include "platform/ps3/Ps3ShaderArtifactReader.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include <malloc.h>
#include <sys/process.h>

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

#include <rsx/rsx.h>

SYS_PROCESS_PARAM(1001, 0x100000);

namespace {

/// <summary>Bounds the complete P3SP fixture accepted by the native resource test.</summary>
constexpr std::size_t MaximumArtifactBytes = 16u * 1024u * 1024u;

/// <summary>Bounds each raw SDK shader-stage file used to verify the P3SP fixture.</summary>
constexpr std::size_t MaximumStageBytes = 1024u * 1024u;

/// <summary>Alignment required for test geometry allocated in RSX local memory.</summary>
constexpr std::size_t GeometryAlignment = 128u;

/// <summary>Stride of each position3-plus-UV2 test vertex.</summary>
constexpr std::uint32_t VertexStride = 20u;

/// <summary>Counts the fixture assertions written to the emulator TTY.</summary>
std::size_t PassedCases = 0u;

/// <summary>Counts the failed fixture assertions written to the emulator TTY.</summary>
std::size_t FailedCases = 0u;

/// <summary>Stores one vertex position and normalized texture coordinate for the test quad.</summary>
struct TestVertex {
    /// <summary>Gets the vertex position's X component.</summary>
    float X;

    /// <summary>Gets the vertex position's Y component.</summary>
    float Y;

    /// <summary>Gets the vertex position's Z component.</summary>
    float Z;

    /// <summary>Gets the horizontal texture coordinate.</summary>
    float U;

    /// <summary>Gets the vertical texture coordinate.</summary>
    float V;
};

/// <summary>Stores an independently derived RGB expectation for one framebuffer sample.</summary>
struct ExpectedRgb {
    /// <summary>Gets the expected red channel.</summary>
    unsigned Red;

    /// <summary>Gets the expected green channel.</summary>
    unsigned Green;

    /// <summary>Gets the expected blue channel.</summary>
    unsigned Blue;
};

/// <summary>Writes one named assertion to TTY and retains its result for the terminal summary.</summary>
void Expect(bool passed, const char* name, const std::string& detail = std::string()) {
    if (passed) {
        ++PassedCases;
        std::printf("PASS %s%s%s\n", name, detail.empty() ? "" : " ", detail.c_str());
        return;
    }
    ++FailedCases;
    std::printf("FAIL %s%s%s\n", name, detail.empty() ? "" : " ", detail.c_str());
}

/// <summary>Stops dependent fixture work when setup cannot satisfy its explicit preconditions.</summary>
void Require(bool condition, const std::string& error) {
    if (!condition) {
        throw std::runtime_error(error);
    }
}

/// <summary>Reads one exact bounded fixture file and rejects short or trailing input.</summary>
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

/// <summary>Finds one reflected uniform by its authored buffer and member identity.</summary>
std::size_t FindUniformIndex(const helengine::ps3::Ps3CookedShaderStage& stage,
    const char* bufferName, const char* memberPath) {
    for (std::size_t index = 0u; index < stage.Uniforms.size(); ++index) {
        if (stage.Uniforms[index].BufferName == bufferName
            && stage.Uniforms[index].MemberPath == memberPath) {
            return index;
        }
    }
    throw std::runtime_error(std::string("missing active fixture uniform ") + bufferName + "." + memberPath);
}

/// <summary>Finds one SDK-mapped vertex attribute by its authored semantic name.</summary>
const helengine::ps3::Ps3CookedShaderAttribute& FindAttribute(
    const helengine::ps3::Ps3CookedShaderProgram& program, const char* semantic) {
    for (const helengine::ps3::Ps3CookedShaderAttribute& attribute : program.Attributes) {
        if (attribute.FullAuthoredSemantic == semantic) {
            return attribute;
        }
    }
    throw std::runtime_error(std::string("fixture has no authored vertex semantic ") + semantic);
}

/// <summary>Finds the actual Albedo sampler unit from validated shader metadata.</summary>
std::uint32_t FindAlbedoUnit(const helengine::ps3::Ps3CookedShaderProgram& program) {
    for (const helengine::ps3::Ps3CookedShaderSampler& sampler : program.PixelSamplers) {
        if (sampler.TextureName == "Albedo" && sampler.Dimension == "Dim2D" && sampler.TextureUnit <= 15u) {
            return sampler.TextureUnit;
        }
    }
    throw std::runtime_error("fixture has no supported 2D Albedo sampler mapping");
}

/// <summary>Uploads one exact reflected uniform payload through the production program owner.</summary>
void UploadUniform(helengine::ps3::Ps3AuthoredProgram& program, std::uint32_t stage,
    std::size_t index, const float* values, std::size_t count, const char* name) {
    std::string error;
    Require(program.UploadUniform(stage, index, values, count, error),
        std::string("uniform upload failed for ") + name + ": " + error);
}

/// <summary>Uploads the fixture's identity-space transform and white vertex and pixel tints.</summary>
void UploadWhiteFixtureUniforms(helengine::ps3::Ps3AuthoredProgram& program,
    std::size_t transformIndex, std::size_t tintIndex, std::size_t pixelTintIndex) {
    const float transform[16] = {
        0.5f, 0.0f, 0.0f, 0.25f,
        0.0f, 0.75f, 0.0f, -0.125f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    std::string error;
    Require(program.BeginDraw(error), std::string("BeginDraw failed: ") + error);
    UploadUniform(program, 0u, transformIndex, transform, 16u, "Frame.Transform");
    UploadUniform(program, 0u, tintIndex, white, 4u, "Frame.Tint");
    UploadUniform(program, 1u, pixelTintIndex, white, 4u, "Surface.PixelTint");
    Require(program.FinishUniformUpdates(error), std::string("uniform completion failed: ") + error);
}

/// <summary>Allocates one aligned quad and six 16-bit indices in RSX local memory.</summary>
void AllocateGeometry(void*& vertexBuffer, std::uint32_t& vertexOffset,
    void*& indexBuffer, std::uint32_t& indexOffset) {
    static_assert(sizeof(TestVertex) == VertexStride, "resource test vertex stride must be 20 bytes");
    const std::uint16_t indices[6] = { 0u, 1u, 2u, 1u, 3u, 2u };
    vertexBuffer = rsxMemalign(GeometryAlignment, sizeof(TestVertex) * 4u);
    Require(vertexBuffer != nullptr, "RSX test vertex allocation failed");
    if (rsxAddressToOffset(vertexBuffer, &vertexOffset) != 0) {
        rsxFree(vertexBuffer);
        vertexBuffer = nullptr;
        throw std::runtime_error("RSX test vertex offset resolution failed");
    }
    indexBuffer = rsxMemalign(GeometryAlignment, sizeof(indices));
    if (indexBuffer == nullptr) {
        rsxFree(vertexBuffer);
        vertexBuffer = nullptr;
        throw std::runtime_error("RSX test index allocation failed");
    }
    if (rsxAddressToOffset(indexBuffer, &indexOffset) != 0) {
        rsxFree(indexBuffer);
        rsxFree(vertexBuffer);
        indexBuffer = nullptr;
        vertexBuffer = nullptr;
        throw std::runtime_error("RSX test index offset resolution failed");
    }
    std::memcpy(indexBuffer, indices, sizeof(indices));
}

#ifndef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
/// <summary>Releases test geometry only after the video owner confirms its successful final drain.</summary>
void ReleaseGeometry(helengine::ps3::Ps3Video& video, void*& vertexBuffer, void*& indexBuffer) {
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
#endif

/// <summary>Writes the UV rectangle, binds SDK-mapped attributes, and submits the real fixture quad.</summary>
void DrawQuad(helengine::ps3::Ps3Video& video, helengine::ps3::Ps3AuthoredProgram& program,
    void* vertexBuffer, std::uint32_t vertexOffset, std::uint32_t indexOffset,
    float uMinimum, float uMaximum, float vMinimum, float vMaximum) {
    const TestVertex vertices[4] = {
        { -1.0f,  1.0f, 0.0f, uMinimum, vMinimum },
        {  1.0f,  1.0f, 0.0f, uMaximum, vMinimum },
        { -1.0f, -1.0f, 0.0f, uMinimum, vMaximum },
        {  1.0f, -1.0f, 0.0f, uMaximum, vMaximum }
    };
    static_assert(sizeof(vertices) == VertexStride * 4u, "resource test quad must contain four packed vertices");
    std::memcpy(vertexBuffer, vertices, sizeof(vertices));

    const helengine::ps3::Ps3CookedShaderProgram& artifact = program.Program();
    const helengine::ps3::Ps3CookedShaderAttribute& position = FindAttribute(artifact, "POSITION");
    const helengine::ps3::Ps3CookedShaderAttribute& uv = FindAttribute(artifact, "TEXCOORD0");
    Require(position.VectorWidth == 3u && uv.VectorWidth == 2u,
        "fixture input shapes differ from the position3-plus-UV2 test geometry");
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

/// <summary>Begins a deferred-clear frame and sets an independently selected opaque background.</summary>
void BeginClearFrame(helengine::ps3::Ps3Video& video) {
    Require(video.BeginFrame(true), "Ps3Video::BeginFrame failed");
    Require(video.ClearSurface(true, true, false, 0xFF6495EDu, 0xFFFFFF00u),
        "fixture clear submission failed");
}

#ifndef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
/// <summary>Queues a real shader draw with either the compatibility nearest path or the explicit linear path.</summary>
void QueueTexturedQuad(helengine::ps3::Ps3Video& video,
    helengine::ps3::Ps3AuthoredProgram& program, helengine::ps3::Ps3AuthoredTexture& texture,
    std::uint32_t textureUnit, std::size_t transformIndex, std::size_t tintIndex,
    std::size_t pixelTintIndex, void* vertexBuffer, std::uint32_t vertexOffset,
    std::uint32_t indexOffset, float uMinimum, float uMaximum, float vMinimum, float vMaximum,
    bool linearClamp) {
    std::string error;
    UploadWhiteFixtureUniforms(program, transformIndex, tintIndex, pixelTintIndex);
    const bool bound = linearClamp
        ? texture.BindLinearClamp(textureUnit, error)
        : texture.Bind(textureUnit, error);
    Require(bound, std::string("fixture texture binding failed: ") + error);
    DrawQuad(video, program, vertexBuffer, vertexOffset, indexOffset,
        uMinimum, uMaximum, vMinimum, vMaximum);
}
#endif

/// <summary>Waits for the queued flip by beginning the next frame without clearing it.</summary>
void PresentAndWaitForPixels(helengine::ps3::Ps3Video& video) {
    Require(video.Present(), "fixture present failed");
    Require(video.BeginFrame(true), "fixture readback frame did not wait for its flip");
}

/// <summary>Reads one preselected interior pixel and reports expected and actual RGB values.</summary>
void CheckPixel(helengine::ps3::Ps3Video& video, const char* phase, const char* name,
    float fractionX, float fractionY, ExpectedRgb expected, unsigned tolerance = 3u) {
    const unsigned x = static_cast<unsigned>(std::floor(fractionX * static_cast<float>(video.Width())));
    const unsigned y = static_cast<unsigned>(std::floor(fractionY * static_cast<float>(video.Height())));
    unsigned packed = 0u;
    if (!video.ReadDisplayedPixel(x, y, &packed)) {
        Expect(false, name, "displayed pixel unavailable after the fixture waited for its flip");
        std::printf("PS3_BATCH2D_PIXEL phase=%s name=%s sample=(%.4f,%.4f) expected=(%u,%u,%u) actual=unavailable\n",
            phase, name, fractionX, fractionY, expected.Red, expected.Green, expected.Blue);
        return;
    }
    const unsigned actualRed = (packed >> 16u) & 0xFFu;
    const unsigned actualGreen = (packed >> 8u) & 0xFFu;
    const unsigned actualBlue = packed & 0xFFu;
    const bool matched = std::abs(static_cast<int>(actualRed) - static_cast<int>(expected.Red)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualGreen) - static_cast<int>(expected.Green)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualBlue) - static_cast<int>(expected.Blue)) <= static_cast<int>(tolerance);
    std::printf("PS3_BATCH2D_PIXEL phase=%s name=%s sample=(%.4f,%.4f) expected=(%u,%u,%u) actual=(%u,%u,%u)\n",
        phase, name, fractionX, fractionY, expected.Red, expected.Green, expected.Blue,
        actualRed, actualGreen, actualBlue);
    Expect(matched, name, matched ? std::string() : "RGB exceeds the preselected tolerance");
}

/// <summary>Checks all four independently specified texel centers after the previous present completed.</summary>
void CheckFourTexels(helengine::ps3::Ps3Video& video, const char* phase,
    ExpectedRgb topLeft, ExpectedRgb topRight, ExpectedRgb bottomLeft, ExpectedRgb bottomRight) {
    CheckPixel(video, phase, "top-left", 0.5f, 0.375f, topLeft);
    CheckPixel(video, phase, "top-right", 0.75f, 0.375f, topRight);
    CheckPixel(video, phase, "bottom-left", 0.5f, 0.75f, bottomLeft);
    CheckPixel(video, phase, "bottom-right", 0.75f, 0.75f, bottomRight);
}

#ifndef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
/// <summary>Confirms invalid texture updates fail before waiting or emitting an RSX command.</summary>
void ExpectRejectedUpdate(helengine::ps3::Ps3AuthoredTexture& texture,
    std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
    const std::uint8_t* rgba, std::size_t byteCount, std::size_t rowPitch,
    helengine::ps3::Ps3Video& video, const char* name) {
    const unsigned commandOffset = video.CommandWriteOffset();
    std::string error;
    const bool updated = texture.UpdateRgbaRegion(x, y, width, height, rgba, byteCount, rowPitch, error);
    const bool rejectedCleanly = !updated && !error.empty() && video.CommandWriteOffset() == commandOffset;
    Expect(rejectedCleanly, name,
        rejectedCleanly ? std::string() : "invalid region must report an error without queuing a wait or write");
}
#endif

/// <summary>Reads and validates the real compiled authored-GPU program and raw stage files.</summary>
void LoadProgramFixture(helengine::ps3::Ps3CookedShaderProgram& artifact,
    std::vector<std::uint8_t>& artifactBytes, std::vector<std::uint8_t>& rawVertexBytes,
    std::vector<std::uint8_t>& rawPixelBytes) {
    std::string error;
    Require(LoadBoundedFile("/app_home/USRDIR/content/fixture.ps3sp", 12u, MaximumArtifactBytes, artifactBytes, error)
        && LoadBoundedFile("/app_home/USRDIR/content/vertex.bin", 1u, MaximumStageBytes, rawVertexBytes, error)
        && LoadBoundedFile("/app_home/USRDIR/content/pixel.bin", 1u, MaximumStageBytes, rawPixelBytes, error), error);
    Require(helengine::ps3::Ps3ShaderArtifactReader::Read(
        artifactBytes.data(), artifactBytes.size(), artifact, error),
        std::string("shader artifact reader rejected the producer fixture: ") + error);
    Require(artifact.Vertex.Bytecode == rawVertexBytes && artifact.Pixel.Bytecode == rawPixelBytes,
        "P3SP bytecode differs from the producer's exact raw stage files");
    Require(helengine::ps3::Ps3RsxProgramValidator::Validate(artifact, error),
        std::string("shader fixture failed direct RSX validation: ") + error);
    Require(artifact.ShaderAssetId == "authored-gpu-fixture"
        && artifact.VertexProgramName == "gpu.vs"
        && artifact.PixelProgramName == "gpu.ps"
        && artifact.VariantName == "default", "producer fixture identity changed");
    std::printf("PS3_BATCH2D_ARTIFACT asset=%s vertex=%s pixel=%s variant=%s\n",
        artifact.ShaderAssetId.c_str(), artifact.VertexProgramName.c_str(),
        artifact.PixelProgramName.c_str(), artifact.VariantName.c_str());
}

#ifndef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
/// <summary>Checks safe runtime mutation, sampling, retirement, and clean final drain with real framebuffer pixels.</summary>
void RunNormalFixture() {
    static const std::uint8_t initialPixels[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 0u, 255u
    };
    static const std::uint8_t paddedRightColumn[15] = {
        255u, 0u, 255u, 255u, 11u, 22u, 33u, 44u,
        0u, 255u, 255u, 255u, 55u, 66u, 77u
    };
    static const std::uint8_t cubeColor[4] = { 255u, 255u, 255u, 255u };

    helengine::ps3::Ps3Video video;
    helengine::ps3::Ps3AuthoredProgram program;
    helengine::ps3::Ps3AuthoredTexture texture;
    helengine::ps3::Ps3AuthoredTexture cubeTexture;
    helengine::ps3::Ps3CookedShaderProgram artifact;
    std::vector<std::uint8_t> artifactBytes;
    std::vector<std::uint8_t> rawVertexBytes;
    std::vector<std::uint8_t> rawPixelBytes;
    void* vertexBuffer = nullptr;
    void* indexBuffer = nullptr;
    std::uint32_t vertexOffset = 0u;
    std::uint32_t indexOffset = 0u;
    std::string error;

    Expect(!video.WaitForSubmittedWork(), "runtime-wait-before-initialize-rejected");
    LoadProgramFixture(artifact, artifactBytes, rawVertexBytes, rawPixelBytes);
    Require(video.Initialize(), "Ps3Video initialization failed");
    BeginClearFrame(video);
    Require(program.Initialize(video, artifact, error), std::string("program initialization failed: ") + error);
    Require(texture.InitializeRgba2D(video, 2u, 2u, initialPixels, sizeof(initialPixels), error),
        std::string("2x2 RGBA texture initialization failed: ") + error);
    Require(cubeTexture.InitializeSolidCube(video, cubeColor, error),
        std::string("cube texture initialization failed: ") + error);
    Expect(texture.OwnedGpuBytes() == 16u && cubeTexture.OwnedGpuBytes() == 768u,
        "initial-texture-ownership-is-exact");

    const std::size_t transformIndex = FindUniformIndex(artifact.Vertex, "Frame", "Transform");
    const std::size_t tintIndex = FindUniformIndex(artifact.Vertex, "Frame", "Tint");
    const std::size_t pixelTintIndex = FindUniformIndex(artifact.Pixel, "Surface", "PixelTint");
    const std::uint32_t textureUnit = FindAlbedoUnit(artifact);
    const helengine::ps3::Ps3CookedShaderAttribute& position = FindAttribute(artifact, "POSITION");
    const helengine::ps3::Ps3CookedShaderAttribute& uv = FindAttribute(artifact, "TEXCOORD0");
    std::printf("PS3_BATCH2D_MAPPING positionSlot=%u uvSlot=%u textureUnit=%u\n",
        position.HardwareSlot, uv.HardwareSlot, textureUnit);
    AllocateGeometry(vertexBuffer, vertexOffset, indexBuffer, indexOffset);

    UploadWhiteFixtureUniforms(program, transformIndex, tintIndex, pixelTintIndex);
    Require(texture.Bind(textureUnit, error), std::string("nearest compatibility bind failed: ") + error);
    DrawQuad(video, program, vertexBuffer, vertexOffset, indexOffset, 0.0f, 1.0f, 0.0f, 1.0f);
    PresentAndWaitForPixels(video);
    CheckFourTexels(video, "initial-nearest",
        ExpectedRgb{ 255u, 0u, 0u }, ExpectedRgb{ 0u, 255u, 0u },
        ExpectedRgb{ 0u, 0u, 255u }, ExpectedRgb{ 255u, 255u, 0u });
    CheckPixel(video, "initial-nearest", "nearest-midpoint-selects-left-texel",
        0.5625f, 0.375f, ExpectedRgb{ 255u, 0u, 0u });

    const bool runtimeWaited = video.WaitForSubmittedWork();
    Expect(runtimeWaited && !video.HasDrained(), "runtime-wait-does-not-authorize-final-drain");

    const std::size_t allocatedBytes = texture.OwnedGpuBytes();
    const bool firstUpdate = texture.UpdateRgbaRegion(1u, 0u, 1u, 2u,
        paddedRightColumn, sizeof(paddedRightColumn), 8u, error);
    Expect(firstUpdate && texture.OwnedGpuBytes() == allocatedBytes,
        "padded-update-preserves-owned-byte-count");
    error.clear();
    const bool repeatedUpdate = texture.UpdateRgbaRegion(1u, 0u, 1u, 2u,
        paddedRightColumn, sizeof(paddedRightColumn), 8u, error);
    Expect(repeatedUpdate && texture.OwnedGpuBytes() == allocatedBytes,
        "repeated-padded-update-does-not-grow-allocation");

    ExpectRejectedUpdate(texture, 0u, 0u, 1u, 1u, nullptr, 4u, 4u,
        video, "update-null-rgba-rejected");
    ExpectRejectedUpdate(texture, 0u, 0u, 0u, 1u, initialPixels, 4u, 4u,
        video, "update-zero-width-rejected");
    ExpectRejectedUpdate(texture, 2u, 0u, 1u, 1u, initialPixels, 4u, 4u,
        video, "update-out-of-bounds-rejected");
    ExpectRejectedUpdate(texture, 0u, 0u, 1u, 2u, paddedRightColumn, sizeof(paddedRightColumn), 3u,
        video, "update-short-row-pitch-rejected");
    ExpectRejectedUpdate(texture, 1u, 0u, 1u, 2u, paddedRightColumn, 11u, 8u,
        video, "update-short-padded-byte-count-rejected");
    const std::size_t maximumBytes = std::numeric_limits<std::size_t>::max();
    ExpectRejectedUpdate(texture, 0u, 0u, 1u, 2u, initialPixels, maximumBytes, maximumBytes,
        video, "update-checked-extent-overflow-rejected");
    ExpectRejectedUpdate(cubeTexture, 0u, 0u, 1u, 1u, cubeColor, sizeof(cubeColor), 4u,
        video, "update-cube-texture-rejected");
    Expect(texture.OwnedGpuBytes() == allocatedBytes,
        "invalid-updates-preserve-existing-texture-allocation");

    BeginClearFrame(video);
    QueueTexturedQuad(video, program, texture, textureUnit, transformIndex, tintIndex,
        pixelTintIndex, vertexBuffer, vertexOffset, indexOffset, 0.0f, 1.0f, 0.0f, 1.0f, false);
    PresentAndWaitForPixels(video);
    CheckFourTexels(video, "padded-update",
        ExpectedRgb{ 255u, 0u, 0u }, ExpectedRgb{ 255u, 0u, 255u },
        ExpectedRgb{ 0u, 0u, 255u }, ExpectedRgb{ 0u, 255u, 255u });
    CheckPixel(video, "padded-update", "nearest-path-remains-nearest",
        0.5625f, 0.375f, ExpectedRgb{ 255u, 0u, 0u });

    BeginClearFrame(video);
    QueueTexturedQuad(video, program, texture, textureUnit, transformIndex, tintIndex,
        pixelTintIndex, vertexBuffer, vertexOffset, indexOffset, -0.25f, 1.25f, 0.0f, 1.0f, true);
    PresentAndWaitForPixels(video);
    CheckPixel(video, "linear-clamp", "linear-filter-mixes-neighbor-texels",
        0.5833f, 0.375f, ExpectedRgb{ 255u, 0u, 64u }, 5u);
    CheckPixel(video, "linear-clamp", "linear-clamp-keeps-outside-u-on-edge-texel",
        0.425f, 0.375f, ExpectedRgb{ 255u, 0u, 0u }, 5u);

    const bool retired = texture.RetireAfterIdle(error);
    Expect(retired && texture.OwnedGpuBytes() == 0u,
        "retire-after-idle-releases-texture-storage");
    error.clear();
    Expect(texture.RetireAfterIdle(error) && texture.OwnedGpuBytes() == 0u,
        "repeated-retirement-is-idempotent");
    error.clear();
    Expect(texture.ShutdownAfterDrain(error) && texture.OwnedGpuBytes() == 0u,
        "retired-owner-shutdown-is-safe-before-final-drain");
    ExpectRejectedUpdate(texture, 0u, 0u, 1u, 1u, initialPixels, 4u, 4u,
        video, "retired-texture-update-rejected");
    const unsigned bindOffset = video.CommandWriteOffset();
    error.clear();
    const bool retiredBind = texture.Bind(textureUnit, error);
    Expect(!retiredBind && !error.empty() && video.CommandWriteOffset() == bindOffset,
        "retired-texture-bind-rejected-without-commands");

    Require(video.Drain(), "final video drain failed");
    ReleaseGeometry(video, vertexBuffer, indexBuffer);
    error.clear();
    Require(program.ShutdownAfterDrain(error), std::string("program final-drain shutdown failed: ") + error);
    error.clear();
    Require(cubeTexture.ShutdownAfterDrain(error), std::string("cube final-drain shutdown failed: ") + error);
    error.clear();
    Expect(texture.ShutdownAfterDrain(error) && texture.OwnedGpuBytes() == 0u,
        "retired-owner-final-shutdown-remains-idempotent");
    Expect(program.OwnedGpuBytes() == 0u && cubeTexture.OwnedGpuBytes() == 0u,
        "normal-final-drain-returns-live-owners-to-zero-bytes");
    Expect(!video.WaitForSubmittedWork(), "runtime-wait-after-final-drain-rejected");
    video.Shutdown();
    Expect(!video.WaitForSubmittedWork(), "runtime-wait-after-video-shutdown-rejected");
    std::printf("PS3_BATCH2D_RESOURCE_OWNERS mode=normal textureBytes=0 programBytes=0 cubeBytes=0 videoDrained=%d\n",
        video.HasDrained() ? 1 : 0);
}
#endif

#ifdef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
/// <summary>Exercises the latched timeout path and proves the last completed texture pixels and allocations remain retained.</summary>
void RunFaultFixture() {
    static const std::uint8_t initialPixels[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 0u, 255u
    };
    static const std::uint8_t replacementPixel[4] = { 255u, 0u, 255u, 255u };

    helengine::ps3::Ps3Video video;
    helengine::ps3::Ps3AuthoredProgram program;
    helengine::ps3::Ps3AuthoredTexture texture;
    helengine::ps3::Ps3CookedShaderProgram artifact;
    std::vector<std::uint8_t> artifactBytes;
    std::vector<std::uint8_t> rawVertexBytes;
    std::vector<std::uint8_t> rawPixelBytes;
    void* vertexBuffer = nullptr;
    void* indexBuffer = nullptr;
    std::uint32_t vertexOffset = 0u;
    std::uint32_t indexOffset = 0u;
    std::string error;

    Expect(!video.WaitForSubmittedWork(), "fault-runtime-wait-before-initialize-rejected");
    LoadProgramFixture(artifact, artifactBytes, rawVertexBytes, rawPixelBytes);
    Require(video.Initialize(), "fault-fixture Ps3Video initialization failed");
    BeginClearFrame(video);
    Require(program.Initialize(video, artifact, error), std::string("fault-fixture program initialization failed: ") + error);
    Require(texture.InitializeRgba2D(video, 2u, 2u, initialPixels, sizeof(initialPixels), error),
        std::string("fault-fixture texture initialization failed: ") + error);
    const std::size_t textureBytesBeforeTimeout = texture.OwnedGpuBytes();
    const std::size_t programBytesBeforeTimeout = program.OwnedGpuBytes();
    const std::size_t transformIndex = FindUniformIndex(artifact.Vertex, "Frame", "Transform");
    const std::size_t tintIndex = FindUniformIndex(artifact.Vertex, "Frame", "Tint");
    const std::size_t pixelTintIndex = FindUniformIndex(artifact.Pixel, "Surface", "PixelTint");
    const std::uint32_t textureUnit = FindAlbedoUnit(artifact);
    const helengine::ps3::Ps3CookedShaderAttribute& position = FindAttribute(artifact, "POSITION");
    const helengine::ps3::Ps3CookedShaderAttribute& uv = FindAttribute(artifact, "TEXCOORD0");
    std::printf("PS3_BATCH2D_MAPPING positionSlot=%u uvSlot=%u textureUnit=%u\n",
        position.HardwareSlot, uv.HardwareSlot, textureUnit);
    AllocateGeometry(vertexBuffer, vertexOffset, indexBuffer, indexOffset);

    UploadWhiteFixtureUniforms(program, transformIndex, tintIndex, pixelTintIndex);
    Require(texture.Bind(textureUnit, error), std::string("fault-fixture texture bind failed: ") + error);
    DrawQuad(video, program, vertexBuffer, vertexOffset, indexOffset, 0.0f, 1.0f, 0.0f, 1.0f);
    PresentAndWaitForPixels(video);
    CheckFourTexels(video, "fault-before-timeout",
        ExpectedRgb{ 255u, 0u, 0u }, ExpectedRgb{ 0u, 255u, 0u },
        ExpectedRgb{ 0u, 0u, 255u }, ExpectedRgb{ 255u, 255u, 0u });

    const bool runtimeWaited = video.WaitForSubmittedWork();
    Expect(!runtimeWaited && !video.HasDrained(), "withheld-runtime-label-times-out-and-latches");
    CheckFourTexels(video, "fault-after-timeout",
        ExpectedRgb{ 255u, 0u, 0u }, ExpectedRgb{ 0u, 255u, 0u },
        ExpectedRgb{ 0u, 0u, 255u }, ExpectedRgb{ 255u, 255u, 0u });

    const unsigned afterTimeoutOffset = video.CommandWriteOffset();
    Expect(!video.WaitForSubmittedWork() && video.CommandWriteOffset() == afterTimeoutOffset,
        "later-runtime-wait-rejects-latched-failure");
    error.clear();
    const bool updatedAfterFailure = texture.UpdateRgbaRegion(0u, 0u, 1u, 1u,
        replacementPixel, sizeof(replacementPixel), sizeof(replacementPixel), error);
    Expect(!updatedAfterFailure && !error.empty() && texture.OwnedGpuBytes() == textureBytesBeforeTimeout
        && video.CommandWriteOffset() == afterTimeoutOffset,
        "timeout-update-rejected-and-texture-bytes-retained");
    error.clear();
    const bool retiredAfterFailure = texture.RetireAfterIdle(error);
    Expect(!retiredAfterFailure && !error.empty() && texture.OwnedGpuBytes() == textureBytesBeforeTimeout,
        "timeout-retirement-fails-without-freeing-texture");
    Expect(!video.Drain() && !video.HasDrained(), "latched-failure-blocks-terminal-drain-authorization");
    Expect(!video.BeginFrame(true), "latched-failure-blocks-frame-begin");
    Expect(!video.ClearSurface(true, true, false, 0xFF6495EDu, 0xFFFFFF00u),
        "latched-failure-blocks-frame-clear");
    Expect(!video.Present(), "latched-failure-blocks-frame-present");
    Expect(video.CommandWriteOffset() == afterTimeoutOffset,
        "latched-frame-rejections-submit-no-commands");
    error.clear();
    Expect(!texture.ShutdownAfterDrain(error) && !error.empty()
        && texture.OwnedGpuBytes() == textureBytesBeforeTimeout,
        "failed-video-drain-cannot-authorize-texture-release");
    Expect(program.OwnedGpuBytes() == programBytesBeforeTimeout,
        "timeout-retains-shader-owner-bytes");
    std::printf("PS3_BATCH2D_RESOURCE_OWNERS mode=fault textureBytes=%lu programBytes=%lu videoDrained=%d\n",
        static_cast<unsigned long>(texture.OwnedGpuBytes()),
        static_cast<unsigned long>(program.OwnedGpuBytes()), video.HasDrained() ? 1 : 0);
    video.Shutdown();
    Expect(!video.WaitForSubmittedWork(), "fault-runtime-wait-after-shutdown-rejected");
}
#endif

} // namespace

/// <summary>Runs the normal lifecycle fixture or its separately compiled timeout-injection variant.</summary>
int main() {
    try {
#ifdef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
        std::printf("PS3_BATCH2D_RESOURCES_MODE mode=fault\n");
        RunFaultFixture();
#else
        std::printf("PS3_BATCH2D_RESOURCES_MODE mode=normal\n");
        RunNormalFixture();
#endif
    } catch (const std::exception& exception) {
        Expect(false, "fixture-fatal", exception.what());
    }
    std::printf("PS3_BATCH2D_RESOURCES_TESTS mode=%s passed=%lu failed=%lu\n",
#ifdef HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL
        "fault",
#else
        "normal",
#endif
        static_cast<unsigned long>(PassedCases), static_cast<unsigned long>(FailedCases));
    return FailedCases == 0u ? 0 : 1;
}
