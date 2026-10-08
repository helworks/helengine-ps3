#include "platform/ps3/Ps3Batch2DRenderer.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3Batch2DPrograms.hpp"
#include "platform/ps3/Ps3Batch2DResources.hpp"
#include "platform/ps3/Ps3Batch2DSink.hpp"
#include "platform/ps3/Ps3ContentStreamSource.hpp"
#include "platform/ps3/Ps3RuntimeTexture.hpp"
#include "platform/ps3/Ps3Video.hpp"

#include "Batch2DGeometry.hpp"
#include "Batch2DWriter.hpp"
#include "Batch2DBuffer.hpp"
#include "Batch2DRun.hpp"
#include "Batch2DVariant.hpp"
#include "Batch2DVertex.hpp"
#include "TextureAsset.hpp"
#include "float4.hpp"
#include "float4x4.hpp"
#include "runtime/array.hpp"
#include "runtime/native_exceptions.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <malloc.h>
#include <stdexcept>
#include <string>

#include <rsx/rsx.h>

namespace {

/// <summary>Counts fixture assertions reported to the runner.</summary>
std::size_t Passed = 0u;

/// <summary>Counts failed fixture assertions reported to the runner.</summary>
std::size_t Failed = 0u;

/// <summary>Records one named fixture expectation without hiding later cleanup.</summary>
void Expect(bool condition, const char* name) {
    if (condition) {
        ++Passed;
        std::printf("PASS %s\n", name);
    } else {
        ++Failed;
        std::printf("FAIL %s\n", name);
    }
}

static_assert(sizeof(::Batch2DVertex) == 96u, "GPU fixture shared vertex stride changed");
static_assert(offsetof(::Batch2DVertex, Position) == 0u, "GPU fixture position offset changed");
static_assert(offsetof(::Batch2DVertex, TexLocal) == 16u, "GPU fixture texture offset changed");
static_assert(offsetof(::Batch2DVertex, Color) == 32u, "GPU fixture color offset changed");
static_assert(offsetof(::Batch2DVertex, Shape) == 48u, "GPU fixture shape offset changed");
static_assert(offsetof(::Batch2DVertex, Corners) == 64u, "GPU fixture corners offset changed");
static_assert(offsetof(::Batch2DVertex, BorderColor) == 80u, "GPU fixture border offset changed");

/// <summary>Fails the fixture immediately when a required native setup operation rejects its inputs.</summary>
void Require(bool condition, const std::string& reason) {
    if (!condition) throw std::runtime_error(reason);
}

/// <summary>Creates one generated texture asset from exact row-major RGBA fixture bytes.</summary>
::TextureAsset* CreateAsset(std::uint16_t width, std::uint16_t height, const std::uint8_t* rgba, std::size_t byteCount) {
    ::TextureAsset* asset = new ::TextureAsset();
    asset->Width = width;
    asset->Height = height;
    asset->ColorFormat = ::TextureAssetColorFormat::Rgba32;
    asset->AlphaPrecision = ::TextureAssetAlphaPrecision::A8;
    asset->Colors = new ::Array<std::uint8_t>(static_cast<std::int32_t>(byteCount));
    for (std::size_t index = 0u; index < byteCount; ++index) (*asset->Colors)[static_cast<std::int32_t>(index)] = rgba[index];
    return asset;
}

/// <summary>Copies a test texture to RSX storage and releases the temporary generated asset.</summary>
::helengine::ps3::Ps3RuntimeTexture* BuildTexture(helengine::ps3::Ps3Batch2DResources& resources,
    std::uint16_t width, std::uint16_t height, const std::uint8_t* rgba, std::size_t byteCount) {
    ::TextureAsset* asset = CreateAsset(width, height, rgba, byteCount);
    try {
        ::helengine::ps3::Ps3RuntimeTexture* texture = resources.BuildRaw(*asset);
        asset->Dispose();
        delete asset;
        return texture;
    } catch (...) {
        asset->Dispose();
        delete asset;
        throw;
    }
}

/// <summary>Builds one initialized shared quad corner with explicit position, UV, and straight-alpha tint.</summary>
::Batch2DVertex MakeVertex(float x, float y, float u, float v, const ::float4& color) {
    ::Batch2DVertex vertex;
    vertex.Position = ::float4(x, y, 0.0f, 1.0f);
    vertex.TexLocal = ::float4(u, v, 0.0f, 0.0f);
    vertex.Color = color;
    vertex.Shape = ::float4(0.0f);
    vertex.Corners = ::float4(0.0f);
    vertex.BorderColor = ::float4(0.0f);
    return vertex;
}

/// <summary>Appends a full viewport quad in top-left, top-right, bottom-right, bottom-left order.</summary>
void AppendFullQuad(::Batch2DWriter& writer, const ::Batch2DRun& run, const ::float4& color) {
    writer.AppendQuad(run,
        MakeVertex(-1.0f, 1.0f, 0.0f, 0.0f, color),
        MakeVertex(1.0f, 1.0f, 1.0f, 0.0f, color),
        MakeVertex(1.0f, -1.0f, 1.0f, 1.0f, color),
        MakeVertex(-1.0f, -1.0f, 0.0f, 1.0f, color));
}

/// <summary>Maps pixel-space batch positions to clip coordinates for a viewport-sized orthographic camera.</summary>
::float4x4 PixelProjection(float width, float height) {
    return ::float4x4(2.0f / width, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f / height, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f, 1.0f);
}

/// <summary>Creates Core geometry for one rounded-shape run and sends it through the actual shared writer.</summary>
void AppendRoundedQuad(::Batch2DWriter& writer, const ::Batch2DRun& run,
    double x, double y, double width, double height, double radius, double borderWidth,
    const ::float4& corners, const ::float4& fill, const ::float4& border) {
    ::Batch2DVertex topLeft;
    ::Batch2DVertex topRight;
    ::Batch2DVertex bottomRight;
    ::Batch2DVertex bottomLeft;
    ::Batch2DGeometry::CreateRoundedQuad__out11_out12_out13_out14(x, y, width, height,
        0.0, 0.0, radius, borderWidth, corners, fill, border,
        topLeft, topRight, bottomRight, bottomLeft);
    writer.AppendQuad(run, topLeft, topRight, bottomRight, bottomLeft);
}

/// <summary>Queues present and opens the following frame only after the flip completion.</summary>
void PresentAndWait(helengine::ps3::Ps3Video& video) {
    Require(video.Present(), "fixture present failed");
    Require(video.BeginFrame(true), "fixture could not wait for the previous present");
}

/// <summary>Checks one predetermined displayed pixel against RGB byte expectations and logs both values.</summary>
void CheckPixel(helengine::ps3::Ps3Video& video, const char* label, unsigned x, unsigned y,
    unsigned red, unsigned green, unsigned blue, unsigned tolerance = 3u) {
    unsigned packed = 0u;
    const bool readable = video.ReadDisplayedPixel(x, y, &packed);
    const unsigned actualRed = (packed >> 16u) & 0xFFu;
    const unsigned actualGreen = (packed >> 8u) & 0xFFu;
    const unsigned actualBlue = packed & 0xFFu;
    const bool matched = readable
        && std::abs(static_cast<int>(actualRed) - static_cast<int>(red)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualGreen) - static_cast<int>(green)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualBlue) - static_cast<int>(blue)) <= static_cast<int>(tolerance);
    std::printf("PS3_BATCH2D_PIXEL name=%s sample=(%u,%u) expected=(%u,%u,%u) actual=%s(%u,%u,%u)\n",
        label, x, y, red, green, blue, readable ? "" : "unavailable/", actualRed, actualGreen, actualBlue);
    Expect(matched, label);
}

/// <summary>Clears the active backbuffer to a fixed opaque black sample destination.</summary>
void ClearBlack(helengine::ps3::Ps3Video& video) {
    Require(video.ClearSurface(true, true, false, 0xFF000000u, 0xFFFFFF00u), "fixture black clear failed");
}

/// <summary>Checks RGB and destination alpha from one preselected A8R8G8B8 readback word.</summary>
void CheckAlphaPixel(const char* label, std::uint32_t packed, unsigned red, unsigned green, unsigned blue,
    unsigned alpha, unsigned tolerance = 3u) {
    const unsigned actualRed = (packed >> 16u) & 0xFFu;
    const unsigned actualGreen = (packed >> 8u) & 0xFFu;
    const unsigned actualBlue = packed & 0xFFu;
    const unsigned actualAlpha = (packed >> 24u) & 0xFFu;
    const bool matched = std::abs(static_cast<int>(actualRed) - static_cast<int>(red)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualGreen) - static_cast<int>(green)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualBlue) - static_cast<int>(blue)) <= static_cast<int>(tolerance)
        && std::abs(static_cast<int>(actualAlpha) - static_cast<int>(alpha)) <= static_cast<int>(tolerance);
    std::printf("PS3_BATCH2D_RGBA_PIXEL name=%s expected=(%u,%u,%u,%u) actual=(%u,%u,%u,%u)\n",
        label, red, green, blue, alpha, actualRed, actualGreen, actualBlue, actualAlpha);
    Expect(matched, label);
}

/// <summary>Reads one RSX-local A8R8G8B8 pixel through the SDK DMA path into mapped PPU main memory.</summary>
void CheckAlphaTargetTransferReadback(helengine::ps3::Ps3Video& video, std::uint32_t sourceOffset,
    std::size_t sampleX, std::size_t sampleY, std::size_t sourcePitch) {
    constexpr std::size_t TransferAllocationBytes = 1024u * 1024u;
    std::printf("PS3_BATCH2D_DMA stage=before-allocation\n");
    std::fflush(stdout);
    void* destination = memalign(TransferAllocationBytes, TransferAllocationBytes);
    std::printf("PS3_BATCH2D_DMA stage=after-allocation address=%p\n", destination);
    std::fflush(stdout);
    Require(destination != nullptr, "alpha transfer destination allocation failed");
    std::memset(destination, 0xA5, TransferAllocationBytes);

    std::uint32_t destinationOffset = 0u;
    std::printf("PS3_BATCH2D_DMA stage=before-map address=%p bytes=%u\n", destination,
        static_cast<unsigned>(TransferAllocationBytes));
    std::fflush(stdout);
    const s32 mapResult = gcmMapMainMemory(destination, static_cast<u32>(TransferAllocationBytes), &destinationOffset);
    std::printf("PS3_BATCH2D_DMA stage=after-map result=%d address=%p offset=%u\n", mapResult,
        destination, destinationOffset);
    std::fflush(stdout);
    if (mapResult != 0) {
        std::free(destination);
        throw std::runtime_error("alpha transfer destination could not be mapped to RSX main memory");
    }

    const std::size_t sourceByteOffset = sampleY * sourcePitch + sampleX * sizeof(std::uint32_t);
    const std::uint64_t absoluteSourceOffset = static_cast<std::uint64_t>(sourceOffset) + sourceByteOffset;
    if (absoluteSourceOffset > std::numeric_limits<std::uint32_t>::max()) {
        std::printf("PS3_BATCH2D_DMA stage=before-unmap reason=source-offset-overflow\n");
        std::fflush(stdout);
        const s32 unmapResult = gcmUnmapEaIoAddress(destination);
        std::printf("PS3_BATCH2D_DMA stage=after-unmap reason=source-offset-overflow result=%d\n", unmapResult);
        std::fflush(stdout);
        if (unmapResult != 0) {
            std::printf("PS3_BATCH2D_ALPHA_TRANSFER cleanup=retained reason=unmap-failed\n");
            std::fflush(stdout);
            throw std::runtime_error("alpha transfer destination remained mapped after source validation failed");
        }
        std::free(destination);
        throw std::runtime_error("alpha transfer source offset exceeds the SDK address width");
    }

    // Push the initialized sentinel out before DMA; discard the CPU line after GPU completion.
    std::printf("PS3_BATCH2D_DMA stage=before-dcbf-sync address=%p\n", destination);
    std::fflush(stdout);
    __asm__ volatile("dcbf 0,%0" : : "r"(destination) : "memory");
    __asm__ volatile("sync" : : : "memory");
    std::printf("PS3_BATCH2D_DMA stage=after-dcbf-sync\n");
    std::fflush(stdout);
    std::printf("PS3_BATCH2D_DMA stage=before-transfer sourceOffset=%u destinationOffset=%u\n",
        static_cast<u32>(absoluteSourceOffset), destinationOffset);
    std::fflush(stdout);
    rsxSetTransferDataMode(video.Context(), GCM_TRANSFER_LOCAL_TO_MAIN);
    rsxSetTransferDataFormat(video.Context(), 4u, 4u, 4u, 1u, 1u, 1u);
    rsxSetTransferDataOffset(video.Context(), destinationOffset, static_cast<u32>(absoluteSourceOffset));
    std::printf("PS3_BATCH2D_DMA stage=after-transfer\n");
    std::fflush(stdout);
    std::printf("PS3_BATCH2D_DMA stage=before-wait\n");
    std::fflush(stdout);
    const bool transferCompleted = video.WaitForSubmittedWork();
    std::printf("PS3_BATCH2D_DMA stage=after-wait completed=%d\n", transferCompleted ? 1 : 0);
    std::fflush(stdout);
    if (!transferCompleted) {
        std::printf("PS3_BATCH2D_ALPHA_TRANSFER cleanup=retained reason=completion-unproven\n");
        std::fflush(stdout);
        throw std::runtime_error("RSX-to-main-memory alpha transfer did not complete");
    }
    std::printf("PS3_BATCH2D_DMA stage=before-dcbi-sync address=%p\n", destination);
    std::fflush(stdout);
    __asm__ volatile("sync" : : : "memory");
    __asm__ volatile("dcbi 0,%0" : : "r"(destination) : "memory");
    __asm__ volatile("sync" : : : "memory");
    std::printf("PS3_BATCH2D_DMA stage=after-dcbi-sync\n");
    std::fflush(stdout);

    const std::uint32_t transferred = *static_cast<const volatile std::uint32_t*>(destination);
    CheckAlphaPixel("a8r8g8b8-source-over-alpha-transfer-readback", transferred, 64u, 0u, 128u, 192u, 3u);
    std::printf("PS3_BATCH2D_DMA stage=before-unmap reason=readback\n");
    std::fflush(stdout);
    const s32 unmapResult = gcmUnmapEaIoAddress(destination);
    std::printf("PS3_BATCH2D_DMA stage=after-unmap reason=readback result=%d\n", unmapResult);
    std::fflush(stdout);
    if (unmapResult != 0) {
        std::printf("PS3_BATCH2D_ALPHA_TRANSFER cleanup=retained reason=unmap-failed\n");
        std::fflush(stdout);
        throw std::runtime_error("alpha transfer destination remained mapped after readback");
    }
    std::free(destination);
}

/// <summary>Checks rejection occurs before GPU commands and without incrementing the draw counter.</summary>
void ExpectRejectedWithoutCommands(helengine::ps3::Ps3Batch2DRenderer& renderer,
    helengine::ps3::Ps3Batch2DPrograms& programs, helengine::ps3::Ps3Video& video,
    ::Batch2DBuffer& buffer, const ::Batch2DRun& run, const char* name) {
    const std::uint64_t priorDraws = renderer.DrawCount();
    const unsigned priorCommandOffset = video.CommandWriteOffset();
    bool rejected = false;
    try {
        renderer.Submit(programs, buffer, run);
    } catch (const std::exception&) {
        rejected = true;
    } catch (...) {
        rejected = true;
    }
    Expect(rejected && renderer.DrawCount() == priorDraws
        && video.CommandWriteOffset() == priorCommandOffset, name);
}

/// <summary>Fills one direct generated buffer with valid quad data before an individual invalid-field mutation.</summary>
void FillDirectBuffer(::Batch2DBuffer& buffer) {
    const ::Batch2DVertex vertices[4] = {
        MakeVertex(-1.0f, 1.0f, 0.0f, 0.0f, ::float4(1.0f)),
        MakeVertex(1.0f, 1.0f, 1.0f, 0.0f, ::float4(1.0f)),
        MakeVertex(1.0f, -1.0f, 1.0f, 1.0f, ::float4(1.0f)),
        MakeVertex(-1.0f, -1.0f, 0.0f, 1.0f, ::float4(1.0f))
    };
    const std::uint16_t indices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    for (std::int32_t index = 0; index < 4; ++index) (*buffer.Vertices)[index] = vertices[index];
    for (std::int32_t index = 0; index < 6; ++index) (*buffer.Indices)[index] = indices[index];
    buffer.VertexCount = 4;
    buffer.IndexCount = 6;
}

/// <summary>Exercises source-over RGB and alpha equations on an independently allocated RGBA-capable target.</summary>
void TestAlphaEquation(helengine::ps3::Ps3Video& video, ::Batch2DWriter& writer,
    const ::Batch2DRun& redRun, const ::Batch2DRun& blueRun) {
    const std::size_t width = video.Width();
    const std::size_t height = video.Height();
    const std::size_t pitch = width * sizeof(std::uint32_t);
    const std::size_t surfaceBytes = pitch * height;
    void* colorSurface = rsxMemalign(64u, surfaceBytes);
    void* depthSurface = rsxMemalign(64u, surfaceBytes);
    if (colorSurface == nullptr || depthSurface == nullptr) {
        if (depthSurface != nullptr) rsxFree(depthSurface);
        if (colorSurface != nullptr) rsxFree(colorSurface);
        throw std::runtime_error("A8R8G8B8 alpha-test surfaces could not be allocated");
    }
    u32 colorOffset = 0u;
    u32 depthOffset = 0u;
    if (rsxAddressToOffset(colorSurface, &colorOffset) != 0 || rsxAddressToOffset(depthSurface, &depthOffset) != 0) {
        rsxFree(depthSurface);
        rsxFree(colorSurface);
        throw std::runtime_error("A8R8G8B8 alpha-test surface offset conversion failed");
    }

    gcmSurface surface = {};
    surface.type = GCM_SURFACE_TYPE_LINEAR;
    surface.antiAlias = GCM_SURFACE_CENTER_1;
    surface.colorFormat = GCM_SURFACE_A8R8G8B8;
    surface.colorTarget = GCM_SURFACE_TARGET_0;
    for (u32 slot = 0u; slot < GCM_MAX_MRT_COUNT; ++slot) {
        surface.colorLocation[slot] = GCM_LOCATION_RSX;
        surface.colorOffset[slot] = 0u;
        surface.colorPitch[slot] = 64u;
    }
    surface.colorOffset[0] = colorOffset;
    surface.colorPitch[0] = static_cast<u32>(pitch);
    surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    surface.depthLocation = GCM_LOCATION_RSX;
    surface.depthOffset = depthOffset;
    surface.depthPitch = static_cast<u32>(pitch);
    surface.width = static_cast<u16>(width);
    surface.height = static_cast<u16>(height);
    surface.x = 0u;
    surface.y = 0u;
    rsxSetSurface(video.Context(), &surface);

    const ::float4 viewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    writer.ConfigureCamera(::float4x4::get_Identity(), viewport);
    const ::Batch2DRun fullRedRun(::Batch2DVariant::Textured, redRun.Texture, 0, 0,
        static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
    const ::Batch2DRun fullBlueRun(::Batch2DVariant::Textured, blueRun.Texture, 0, 0,
        static_cast<std::int32_t>(width), static_cast<std::int32_t>(height));
    Require(video.ClearSurface(true, true, false, 0xFF123456u, 0xFFFFFF00u), "alpha target sentinel clear failed");
    Require(video.WaitForSubmittedWork(), "alpha target sentinel clear wait failed");
    const std::size_t sampleX = width / 2u;
    const std::size_t sampleY = height / 2u;
    const volatile std::uint32_t* sampleAddress = reinterpret_cast<const volatile std::uint32_t*>(
        static_cast<const std::uint8_t*>(colorSurface) + sampleY * pitch + sampleX * sizeof(std::uint32_t));
    const std::uint32_t cleared = *sampleAddress;
    std::printf("PS3_BATCH2D_ALPHA_TARGET_CLEAR expected=(18,52,86,255) actual=(%u,%u,%u,%u)\n",
        (cleared >> 16u) & 0xFFu, (cleared >> 8u) & 0xFFu, cleared & 0xFFu, (cleared >> 24u) & 0xFFu);
    Require(video.ClearSurface(true, true, false, 0x00000000u, 0xFFFFFF00u), "alpha surface transparent clear failed");
    AppendFullQuad(writer, fullRedRun, ::float4(1.0f));
    AppendFullQuad(writer, fullBlueRun, ::float4(1.0f));
    writer.Flush();
    Require(video.WaitForSubmittedWork(), "alpha-surface GPU readback wait failed");

    const std::uint32_t packed = *sampleAddress;
    CheckAlphaPixel("a8r8g8b8-source-over-alpha", packed, 64u, 0u, 128u, 192u, 3u);
    CheckAlphaTargetTransferReadback(video, colorOffset, sampleX, sampleY, pitch);
    std::printf("PS3_BATCH2D_ALPHA_TARGET format=A8R8G8B8 size=%lux%lu sample=(%lu,%lu)\n",
        static_cast<unsigned long>(width), static_cast<unsigned long>(height),
        static_cast<unsigned long>(sampleX), static_cast<unsigned long>(sampleY));

    Require(video.BeginFrame(true), "video surface was not restored after alpha-target readback");
    rsxFree(depthSurface);
    rsxFree(colorSurface);
}

/// <summary>Runs real shared writer submissions through the native GPU sink and records pixels and draw counts.</summary>
void RunGpuFixture() {
    using namespace helengine::ps3;
    constexpr unsigned viewportX = 120u;
    constexpr unsigned viewportY = 80u;
    constexpr unsigned viewportWidth = 360u;
    constexpr unsigned viewportHeight = 240u;
    constexpr unsigned sampleX = viewportX + viewportWidth / 2u;
    constexpr unsigned sampleY = viewportY + viewportHeight / 2u;
    static const std::uint8_t checker[16] = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 0u, 255u
    };
    static const std::uint8_t halfRed[4] = { 255u, 0u, 0u, 128u };
    static const std::uint8_t halfBlue[4] = { 0u, 0u, 255u, 128u };
    static const std::uint8_t opaqueWhite[4] = { 255u, 255u, 255u, 255u };

    Ps3Video video;
    Require(video.Initialize(), "Ps3Video initialization failed");
    Require(video.BeginFrame(true), "initial deferred-clear frame failed");
    ClearBlack(video);

    Batch2DWriter* writerReference = nullptr;
    Ps3Batch2DResources resources(video, [&writerReference]() {
        if (writerReference != nullptr) writerReference->Flush();
    });
    Ps3ContentStreamSource source;
    Ps3Batch2DPrograms programs(video, source);
    programs.Initialize(
        "cooked/shaders/842895fde6e1db0816b78c52d176284de1fbb2d40dc819196b6ba7db7d09558a.ps3sp",
        "cooked/shaders/9a0867e0cecd7e04a48ecb452b32db7a42deecaab88912cbcc71427cf4b3233d.ps3sp");
    Ps3Batch2DRenderer renderer(video, resources);
    renderer.Initialize();
    Ps3Batch2DSink sink(renderer, programs);
    Batch2DWriter writer(&sink, 1024);
    writerReference = &writer;

    Ps3RuntimeTexture* checkerTexture = BuildTexture(resources, 2u, 2u, checker, sizeof(checker));
    Ps3RuntimeTexture* redTexture = BuildTexture(resources, 1u, 1u, halfRed, sizeof(halfRed));
    Ps3RuntimeTexture* blueTexture = BuildTexture(resources, 1u, 1u, halfBlue, sizeof(halfBlue));
    Ps3RuntimeTexture* whiteTexture = BuildTexture(resources, 1u, 1u, opaqueWhite, sizeof(opaqueWhite));
    const ::float4 fullViewport(static_cast<float>(viewportX), static_cast<float>(viewportY),
        static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));
    const ::float4x4 identity = ::float4x4::get_Identity();
    writer.ConfigureCamera(identity, fullViewport);
    const ::Batch2DRun checkerRun(::Batch2DVariant::Textured, checkerTexture,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    AppendFullQuad(writer, checkerRun, ::float4(1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() == 1u, "one-checker-run-one-draw");
    PresentAndWait(video);
    CheckPixel(video, "checker-top-left", viewportX + viewportWidth / 4u, viewportY + viewportHeight / 4u, 255u, 0u, 0u);
    CheckPixel(video, "checker-top-right", viewportX + 3u * viewportWidth / 4u, viewportY + viewportHeight / 4u, 0u, 255u, 0u);
    CheckPixel(video, "checker-bottom-left", viewportX + viewportWidth / 4u, viewportY + 3u * viewportHeight / 4u, 0u, 0u, 255u);
    CheckPixel(video, "checker-bottom-right", viewportX + 3u * viewportWidth / 4u, viewportY + 3u * viewportHeight / 4u, 255u, 255u, 0u);

    ClearBlack(video);
    const std::uint64_t beforeTwoLayerOrder = renderer.DrawCount();
    const ::Batch2DRun redRun(::Batch2DVariant::Textured, redTexture,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    const ::Batch2DRun blueRun(::Batch2DVariant::Textured, blueTexture,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    AppendFullQuad(writer, redRun, ::float4(1.0f));
    AppendFullQuad(writer, blueRun, ::float4(1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() - beforeTwoLayerOrder == 2u, "red-blue-runs-preserve-two-draw-order");
    PresentAndWait(video);
    CheckPixel(video, "red-then-blue", sampleX, sampleY, 64u, 0u, 128u, 3u);

    ClearBlack(video);
    const std::uint64_t beforeReverseOrder = renderer.DrawCount();
    AppendFullQuad(writer, blueRun, ::float4(1.0f));
    AppendFullQuad(writer, redRun, ::float4(1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() - beforeReverseOrder == 2u, "blue-red-runs-preserve-reverse-order");
    PresentAndWait(video);
    CheckPixel(video, "blue-then-red", sampleX, sampleY, 128u, 0u, 64u, 3u);

    ClearBlack(video);
    const std::uint64_t beforeAba = renderer.DrawCount();
    AppendFullQuad(writer, redRun, ::float4(1.0f));
    AppendFullQuad(writer, blueRun, ::float4(1.0f));
    AppendFullQuad(writer, redRun, ::float4(1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() - beforeAba == 3u, "aba-runs-are-not-texture-sorted");
    PresentAndWait(video);
    CheckPixel(video, "red-blue-red", sampleX, sampleY, 159u, 0u, 64u, 4u);

    ClearBlack(video);
    const std::uint64_t beforeAdjacent = renderer.DrawCount();
    AppendFullQuad(writer, redRun, ::float4(1.0f));
    AppendFullQuad(writer, redRun, ::float4(1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() - beforeAdjacent == 1u, "adjacent-identical-runs-merge-to-one-draw");

    ClearBlack(video);
    const ::Batch2DRun whiteRun(::Batch2DVariant::Textured, whiteTexture,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    const std::uint64_t beforeCapacity = renderer.DrawCount();
    for (std::size_t quad = 0u; quad < 1025u; ++quad) AppendFullQuad(writer, whiteRun, ::float4(1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() - beforeCapacity == 2u, "1025-quads-submit-as-two-bounded-draws");
    PresentAndWait(video);
    CheckPixel(video, "bounded-1025-quad-run", sampleX, sampleY, 255u, 255u, 255u, 3u);

    ClearBlack(video);
    ::float4x4 translatedScale(0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.25f, -0.2f, 0.0f, 1.0f);
    writer.ConfigureCamera(translatedScale, fullViewport);
    const std::uint64_t beforeChangedCamera = renderer.DrawCount();
    AppendFullQuad(writer, whiteRun, ::float4(0.0f, 1.0f, 0.5f, 1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() - beforeChangedCamera == 1u, "changed-projection-tint-submits");
    PresentAndWait(video);
    CheckPixel(video, "translated-nonsquare-viewport", viewportX + 5u * viewportWidth / 8u,
        viewportY + 3u * viewportHeight / 5u, 0u, 255u, 128u, 3u);

    ClearBlack(video);
    writer.ConfigureCamera(PixelProjection(static_cast<float>(viewportWidth), static_cast<float>(viewportHeight)), fullViewport);
    const ::Batch2DRun shapeRun(::Batch2DVariant::RoundedShape, nullptr,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    AppendRoundedQuad(writer, shapeRun, 20.0, 20.0, 320.0, 200.0, 30.0, 12.0,
        ::float4(1.0f), ::float4(0.0f, 1.0f, 0.0f, 1.0f), ::float4(1.0f, 0.0f, 0.0f, 1.0f));
    writer.Flush();
    Expect(renderer.DrawCount() == beforeChangedCamera + 2u, "rounded-shape-variant-submits");
    PresentAndWait(video);
    CheckPixel(video, "rounded-fill-interior", viewportX + 180u, viewportY + 120u, 0u, 255u, 0u);
    CheckPixel(video, "rounded-border-interior", viewportX + 25u, viewportY + 120u, 255u, 0u, 0u);
    CheckPixel(video, "rounded-enabled-corner-alpha-outside", viewportX + 21u, viewportY + 21u, 0u, 0u, 0u);

    ClearBlack(video);
    AppendRoundedQuad(writer, shapeRun, 20.0, 20.0, 320.0, 200.0, 30.0, 0.0,
        ::float4(0.0f, 1.0f, 1.0f, 1.0f), ::float4(0.0f, 0.0f, 1.0f, 1.0f), ::float4(1.0f, 0.0f, 0.0f, 1.0f));
    writer.Flush();
    PresentAndWait(video);
    CheckPixel(video, "rounded-disabled-corner-square", viewportX + 22u, viewportY + 22u, 0u, 0u, 255u);
    CheckPixel(video, "rounded-zero-border-fill", viewportX + 180u, viewportY + 120u, 0u, 0u, 255u);

    ClearBlack(video);
    AppendRoundedQuad(writer, shapeRun, 20.0, 20.0, 320.0, 200.0, 30.0, 100.0,
        ::float4(1.0f), ::float4(0.0f, 1.0f, 0.0f, 1.0f), ::float4(1.0f, 1.0f, 0.0f, 1.0f));
    writer.Flush();
    PresentAndWait(video);
    CheckPixel(video, "rounded-full-border-center", viewportX + 180u, viewportY + 120u, 255u, 255u, 0u);

    ClearBlack(video);
    const ::Batch2DRun nestedScissorRun(::Batch2DVariant::RoundedShape, nullptr,
        static_cast<std::int32_t>(viewportX + 100u), static_cast<std::int32_t>(viewportY + 80u), 80, 60);
    AppendRoundedQuad(writer, nestedScissorRun, 0.0, 0.0, 360.0, 240.0, 0.0, 0.0,
        ::float4(0.0f), ::float4(1.0f, 0.0f, 1.0f, 1.0f), ::float4(0.0f));
    writer.Flush();
    PresentAndWait(video);
    CheckPixel(video, "nested-scissor-interior", viewportX + 140u, viewportY + 110u, 255u, 0u, 255u);
    CheckPixel(video, "nested-scissor-exterior", viewportX + 220u, viewportY + 110u, 0u, 0u, 0u);

    ClearBlack(video);
    const ::Batch2DRun emptyScissorRun(::Batch2DVariant::RoundedShape, nullptr,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY), 0, 120);
    const std::uint64_t beforeEmptyScissor = renderer.DrawCount();
    const unsigned emptyScissorOffset = video.CommandWriteOffset();
    AppendRoundedQuad(writer, emptyScissorRun, 0.0, 0.0, 360.0, 240.0, 0.0, 0.0,
        ::float4(0.0f), ::float4(1.0f), ::float4(0.0f));
    writer.Flush();
    Expect(renderer.DrawCount() == beforeEmptyScissor && video.CommandWriteOffset() == emptyScissorOffset,
        "empty-scissor-skips-draw-and-rsx-commands");

    ::Batch2DBuffer invalidBuffer(1);
    FillDirectBuffer(invalidBuffer);
    (*invalidBuffer.Indices)[5] = 4u;
    ExpectRejectedWithoutCommands(renderer, programs, video, invalidBuffer, whiteRun, "out-of-range-index-rejected-before-rsx");
    FillDirectBuffer(invalidBuffer);
    invalidBuffer.Vertices->Data[0].Position.X = std::numeric_limits<float>::quiet_NaN();
    ExpectRejectedWithoutCommands(renderer, programs, video, invalidBuffer, whiteRun, "nonfinite-vertex-rejected-before-rsx");
    FillDirectBuffer(invalidBuffer);
    invalidBuffer.VertexCount = 3;
    ExpectRejectedWithoutCommands(renderer, programs, video, invalidBuffer, whiteRun, "incomplete-quad-count-rejected-before-rsx");
    FillDirectBuffer(invalidBuffer);
    ::Batch2DRun unknownRun(::Batch2DVariant::Textured, whiteTexture,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    unknownRun.Variant = static_cast<::Batch2DVariant>(99);
    ExpectRejectedWithoutCommands(renderer, programs, video, invalidBuffer, unknownRun, "unknown-variant-rejected-before-rsx");

    Ps3Batch2DResources foreignResources(video, []() {});
    Ps3RuntimeTexture* foreignTexture = BuildTexture(foreignResources, 1u, 1u, opaqueWhite, sizeof(opaqueWhite));
    const ::Batch2DRun foreignRun(::Batch2DVariant::Textured, foreignTexture,
        static_cast<std::int32_t>(viewportX), static_cast<std::int32_t>(viewportY),
        static_cast<std::int32_t>(viewportWidth), static_cast<std::int32_t>(viewportHeight));
    FillDirectBuffer(invalidBuffer);
    ExpectRejectedWithoutCommands(renderer, programs, video, invalidBuffer, foreignRun, "foreign-texture-rejected-before-rsx");
    invalidBuffer.Dispose();

    const std::uint8_t replacementRed[4] = { 255u, 0u, 0u, 255u };
    ClearBlack(video);
    writer.ConfigureCamera(identity, fullViewport);
    AppendFullQuad(writer, whiteRun, ::float4(1.0f));
    resources.Update(*whiteTexture, 0u, 0u, 1u, 1u, replacementRed, sizeof(replacementRed), sizeof(replacementRed));
    PresentAndWait(video);
    CheckPixel(video, "queued-frame-completes-before-texture-update", sampleX, sampleY, 255u, 255u, 255u);
    ClearBlack(video);
    AppendFullQuad(writer, whiteRun, ::float4(1.0f));
    writer.Flush();
    PresentAndWait(video);
    CheckPixel(video, "updated-texture-next-frame", sampleX, sampleY, 255u, 0u, 0u);

    TestAlphaEquation(video, writer, redRun, blueRun);

    std::string error;
    checkerTexture->Dispose();
    redTexture->Dispose();
    blueTexture->Dispose();
    whiteTexture->Dispose();
    foreignTexture->Dispose();
    delete checkerTexture;
    delete redTexture;
    delete blueTexture;
    delete whiteTexture;
    delete foreignTexture;
    resources.DetachCpuSubmission();
    foreignResources.DetachCpuSubmission();
    writerReference = nullptr;
    writer.Dispose();
    Require(video.Drain(), "final video drain failed");
    Require(renderer.ShutdownAfterDrain(error), std::string("renderer final shutdown failed: ") + error);
    Require(programs.ShutdownAfterDrain(error), std::string("program final shutdown failed: ") + error);
    Require(resources.ShutdownAfterDrain(error), std::string("resource final shutdown failed: ") + error);
    Require(foreignResources.ShutdownAfterDrain(error), std::string("foreign resource shutdown failed: ") + error);
    Expect(renderer.OwnedGpuBytes() == 0u && programs.OwnedGpuBytes() == 0u && resources.OwnedGpuBytes() == 0u,
        "clean-final-drain-returns-batch-program-texture-bytes-to-zero");
    video.Shutdown();
}

} // namespace

/// <summary>Runs the shared writer through the native renderer against both real compiled SpriteBatchShader variants.</summary>
int main() {
    try {
        RunGpuFixture();
    } catch (::Exception* exception) {
        std::printf("FAIL fixture-fatal reason=%s\n", exception == nullptr ? "null-generated-exception" : exception->what());
        delete exception;
        ++Failed;
    } catch (const std::exception& exception) {
        std::printf("FAIL fixture-fatal reason=%s\n", exception.what());
        ++Failed;
    } catch (...) {
        std::printf("FAIL fixture-fatal reason=non-standard-exception\n");
        ++Failed;
    }
    std::printf("PS3_BATCH2D_GPU_TESTS passed=%lu failed=%lu\n",
        static_cast<unsigned long>(Passed), static_cast<unsigned long>(Failed));
    return Failed == 0u ? 0 : 1;
}
