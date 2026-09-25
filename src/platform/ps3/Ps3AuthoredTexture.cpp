#include "platform/ps3/Ps3AuthoredTexture.hpp"

#include <rsx/rsx.h>

#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "platform/ps3/Ps3Video.hpp"

namespace helengine::ps3 {
namespace {

/// <summary>Combines one explicit RGBA byte tuple into the SDK's A8R8G8B8 channel-word convention.</summary>
std::uint32_t PackA8R8G8B8(const std::uint8_t* rgba) {
    return (static_cast<std::uint32_t>(rgba[3]) << 24u)
        | (static_cast<std::uint32_t>(rgba[0]) << 16u)
        | (static_cast<std::uint32_t>(rgba[1]) << 8u)
        | static_cast<std::uint32_t>(rgba[2]);
}

/// <summary>Returns the SDK identity channel remap for normalized texture sampling.</summary>
std::uint32_t IdentityColorRemap() {
    return GCM_TEXTURE_REMAP_MODE(GCM_TEXTURE_REMAP_ORDER_XYXY,
        GCM_TEXTURE_REMAP_COLOR_A, GCM_TEXTURE_REMAP_COLOR_R,
        GCM_TEXTURE_REMAP_COLOR_G, GCM_TEXTURE_REMAP_COLOR_B,
        GCM_TEXTURE_REMAP_TYPE_REMAP, GCM_TEXTURE_REMAP_TYPE_REMAP,
        GCM_TEXTURE_REMAP_TYPE_REMAP, GCM_TEXTURE_REMAP_TYPE_REMAP);
}

/// <summary>Stores one caller-visible authored-texture validation or lifecycle failure.</summary>
void SetFailure(std::string& error, const char* operation, const std::string& reason) {
    error = std::string("PS3 authored texture ") + operation + ": " + reason;
}

/// <summary>Validates video state before any texture allocation can be attempted.</summary>
bool ValidateVideo(Ps3Video& video, const char* operation, std::string& error) {
    if (video.Context() == nullptr) {
        SetFailure(error, operation, "video context is not initialized");
        return false;
    }
    if (video.HasDrained()) {
        SetFailure(error, operation, "video has already completed its terminal drain");
        return false;
    }
    return true;
}

} // namespace

Ps3AuthoredTexture::Ps3AuthoredTexture()
    : VideoValue(nullptr)
    , TextureValue{}
    , TextureBuffer(nullptr)
    , OwnedGpuBytesValue(0u)
    , Initialized(false)
    , ShutdownComplete(false) {
}

Ps3AuthoredTexture::~Ps3AuthoredTexture() {
    if (!Initialized) {
        return;
    }
    std::printf("PS3_AUTHORED_TEXTURE_ERROR stage=destructor reason=missing-explicit-ShutdownAfterDrain retainedGpuBytes=%lu\n",
        static_cast<unsigned long>(OwnedGpuBytesValue));
}

bool Ps3AuthoredTexture::InitializeRgba2D(Ps3Video& video, std::uint32_t width, std::uint32_t height,
    const std::uint8_t* rgba, std::size_t byteCount, std::string& error) {
    error.clear();
    if (Initialized || ShutdownComplete) {
        SetFailure(error, "initialize-2d", "owner is already initialized or retired");
        return false;
    }
    if (!ValidateVideo(video, "initialize-2d", error)) {
        return false;
    }
    if (rgba == nullptr) {
        SetFailure(error, "initialize-2d", "RGBA source pointer is null");
        return false;
    }
    if (width == 0u || height == 0u || width > 4096u || height > 4096u) {
        SetFailure(error, "initialize-2d", "dimensions must each be between one and 4096 texels");
        return false;
    }
    const std::size_t widthValue = static_cast<std::size_t>(width);
    const std::size_t heightValue = static_cast<std::size_t>(height);
    if (widthValue > std::numeric_limits<std::size_t>::max() / heightValue
        || widthValue * heightValue > std::numeric_limits<std::size_t>::max() / 4u) {
        SetFailure(error, "initialize-2d", "checked RGBA byte size overflows size_t");
        return false;
    }
    const std::size_t expectedBytes = widthValue * heightValue * 4u;
    if (byteCount != expectedBytes) {
        SetFailure(error, "initialize-2d",
            std::string("byte count must be exactly ") + std::to_string(expectedBytes));
        return false;
    }
    if (width > std::numeric_limits<std::uint16_t>::max()
        || height > std::numeric_limits<std::uint16_t>::max()
        || width > std::numeric_limits<std::uint32_t>::max() / 4u) {
        SetFailure(error, "initialize-2d", "dimensions cannot be represented by the SDK descriptor pitch fields");
        return false;
    }

    void* candidateBuffer = rsxMemalign(TextureAlignment, expectedBytes);
    if (candidateBuffer == nullptr) {
        SetFailure(error, "initialize-2d", "RSX texture allocation failed");
        return false;
    }
    std::uint32_t candidateOffset = 0u;
    const s32 offsetResult = rsxAddressToOffset(candidateBuffer, &candidateOffset);
    if (offsetResult != 0) {
        rsxFree(candidateBuffer);
        SetFailure(error, "initialize-2d",
            std::string("RSX texture offset resolution failed with code ")
                + std::to_string(static_cast<int>(offsetResult)));
        return false;
    }

    const std::size_t texelCount = widthValue * heightValue;
    std::uint32_t* destination = static_cast<std::uint32_t*>(candidateBuffer);
    for (std::size_t texelIndex = 0u; texelIndex < texelCount; ++texelIndex) {
        destination[texelIndex] = PackA8R8G8B8(rgba + texelIndex * 4u);
    }

    gcmTexture candidateTexture{};
    candidateTexture.format = static_cast<u8>(GCM_TEXTURE_FORMAT_LIN
        | GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_NRM);
    candidateTexture.mipmap = 1u;
    candidateTexture.dimension = GCM_TEXTURE_DIMS_2D;
    candidateTexture.cubemap = GCM_FALSE;
    candidateTexture.remap = IdentityColorRemap();
    candidateTexture.width = static_cast<u16>(width);
    candidateTexture.height = static_cast<u16>(height);
    candidateTexture.depth = 1u;
    candidateTexture.location = GCM_LOCATION_RSX;
    candidateTexture._pad = 0u;
    candidateTexture.pitch = width * 4u;
    candidateTexture.offset = candidateOffset;

    VideoValue = &video;
    TextureValue = candidateTexture;
    TextureBuffer = candidateBuffer;
    OwnedGpuBytesValue = expectedBytes;
    Initialized = true;
    ShutdownComplete = false;
    std::printf("PS3_AUTHORED_TEXTURE_READY kind=rgba2d width=%u height=%u pitch=%u ownedGpuBytes=%lu\n",
        width, height, TextureValue.pitch, static_cast<unsigned long>(OwnedGpuBytesValue));
    return true;
}

bool Ps3AuthoredTexture::UpdateRgbaRegion(std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba,
    std::size_t byteCount, std::size_t rowPitch, std::string& error) {
    error.clear();
    if (!Initialized || ShutdownComplete) {
        SetFailure(error, "update-rgba-region", "owner is not initialized or has already been retired");
        return false;
    }
    if (VideoValue == nullptr || VideoValue->Context() == nullptr) {
        SetFailure(error, "update-rgba-region", "video context is unavailable or synchronization has failed");
        return false;
    }
    if (TextureBuffer == nullptr || OwnedGpuBytesValue == 0u) {
        SetFailure(error, "update-rgba-region", "owned texture allocation is unavailable");
        return false;
    }
    if (TextureValue.dimension != GCM_TEXTURE_DIMS_2D || TextureValue.cubemap != GCM_FALSE
        || (TextureValue.format & GCM_TEXTURE_FORMAT_LIN) != GCM_TEXTURE_FORMAT_LIN
        || TextureValue.pitch == 0u) {
        SetFailure(error, "update-rgba-region", "updates require an owned linear non-cube 2D texture");
        return false;
    }
    if (rgba == nullptr) {
        SetFailure(error, "update-rgba-region", "RGBA source pointer is null");
        return false;
    }
    if (width == 0u || height == 0u) {
        SetFailure(error, "update-rgba-region", "region dimensions must be non-zero");
        return false;
    }
    if (x > TextureValue.width || y > TextureValue.height
        || width > static_cast<std::uint32_t>(TextureValue.width) - x
        || height > static_cast<std::uint32_t>(TextureValue.height) - y) {
        SetFailure(error, "update-rgba-region", "region lies outside the texture bounds");
        return false;
    }

    const std::size_t widthValue = static_cast<std::size_t>(width);
    const std::size_t heightValue = static_cast<std::size_t>(height);
    const std::size_t sizeMaximum = std::numeric_limits<std::size_t>::max();
    if (widthValue > sizeMaximum / 4u) {
        SetFailure(error, "update-rgba-region", "row byte size overflows size_t");
        return false;
    }
    const std::size_t rowBytes = widthValue * 4u;
    if (rowPitch < rowBytes) {
        SetFailure(error, "update-rgba-region", "source row pitch is smaller than the RGBA row size");
        return false;
    }
    if (heightValue - 1u > (sizeMaximum - rowBytes) / rowPitch) {
        SetFailure(error, "update-rgba-region", "source byte extent overflows size_t");
        return false;
    }
    const std::size_t requiredBytes = (heightValue - 1u) * rowPitch + rowBytes;
    if (byteCount < requiredBytes) {
        SetFailure(error, "update-rgba-region", "source byte count is smaller than the checked row extent");
        return false;
    }
    const std::size_t destinationPitch = static_cast<std::size_t>(TextureValue.pitch);
    if (static_cast<std::size_t>(TextureValue.width) > sizeMaximum / 4u
        || destinationPitch < static_cast<std::size_t>(TextureValue.width) * 4u) {
        SetFailure(error, "update-rgba-region", "destination pitch cannot contain the texture rows");
        return false;
    }

    if (!VideoValue->WaitForSubmittedWork()) {
        SetFailure(error, "update-rgba-region", "submitted RSX work did not become idle; texture remains unchanged");
        return false;
    }

    const std::uint8_t* sourceBytes = rgba;
    std::uint8_t* destinationBytes = static_cast<std::uint8_t*>(TextureBuffer);
    for (std::size_t row = 0u; row < heightValue; ++row) {
        const std::uint8_t* sourceRow = sourceBytes + row * rowPitch;
        std::uint32_t* destinationRow = reinterpret_cast<std::uint32_t*>(
            destinationBytes + (static_cast<std::size_t>(y) + row) * destinationPitch);
        for (std::size_t column = 0u; column < widthValue; ++column) {
            destinationRow[static_cast<std::size_t>(x) + column] = PackA8R8G8B8(sourceRow + column * 4u);
        }
    }
    rsxInvalidateTextureCache(VideoValue->Context(), GCM_INVALIDATE_TEXTURE);
    std::printf("PS3_AUTHORED_TEXTURE_UPDATED x=%u y=%u width=%u height=%u rowPitch=%lu requiredBytes=%lu ownedGpuBytes=%lu\n",
        x, y, width, height, static_cast<unsigned long>(rowPitch), static_cast<unsigned long>(requiredBytes),
        static_cast<unsigned long>(OwnedGpuBytesValue));
    return true;
}

bool Ps3AuthoredTexture::InitializeSolidCube(Ps3Video& video, const std::uint8_t rgba[4], std::string& error) {
    error.clear();
    if (Initialized || ShutdownComplete) {
        SetFailure(error, "initialize-cube", "owner is already initialized or retired");
        return false;
    }
    if (!ValidateVideo(video, "initialize-cube", error)) {
        return false;
    }
    if (rgba == nullptr) {
        SetFailure(error, "initialize-cube", "RGBA face color pointer is null");
        return false;
    }

    constexpr std::size_t faceAlignment = 128u;
    constexpr std::size_t faceCount = 6u;
    constexpr std::size_t faceBytes = faceAlignment;
    constexpr std::size_t cubeBytes = faceCount * faceBytes;
    void* candidateBuffer = rsxMemalign(TextureAlignment, cubeBytes);
    if (candidateBuffer == nullptr) {
        SetFailure(error, "initialize-cube", "RSX cube texture allocation failed");
        return false;
    }
    std::uint32_t candidateOffset = 0u;
    const s32 offsetResult = rsxAddressToOffset(candidateBuffer, &candidateOffset);
    if (offsetResult != 0) {
        rsxFree(candidateBuffer);
        SetFailure(error, "initialize-cube",
            std::string("RSX cube offset resolution failed with code ")
                + std::to_string(static_cast<int>(offsetResult)));
        return false;
    }

    std::memset(candidateBuffer, 0, cubeBytes);
    const std::uint32_t packedColor = PackA8R8G8B8(rgba);
    for (std::size_t face = 0u; face < faceCount; ++face) {
        std::uint8_t* faceAddress = static_cast<std::uint8_t*>(candidateBuffer) + face * faceBytes;
        std::memcpy(faceAddress, &packedColor, sizeof(packedColor));
    }

    gcmTexture candidateTexture{};
    candidateTexture.format = static_cast<u8>(GCM_TEXTURE_FORMAT_SWZ
        | GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_NRM);
    candidateTexture.mipmap = 1u;
    candidateTexture.dimension = GCM_TEXTURE_DIMS_2D;
    candidateTexture.cubemap = GCM_TRUE;
    candidateTexture.remap = IdentityColorRemap();
    candidateTexture.width = 1u;
    candidateTexture.height = 1u;
    candidateTexture.depth = 1u;
    candidateTexture.location = GCM_LOCATION_RSX;
    candidateTexture._pad = 0u;
    candidateTexture.pitch = 0u;
    candidateTexture.offset = candidateOffset;

    VideoValue = &video;
    TextureValue = candidateTexture;
    TextureBuffer = candidateBuffer;
    OwnedGpuBytesValue = cubeBytes;
    Initialized = true;
    ShutdownComplete = false;
    std::printf("PS3_AUTHORED_TEXTURE_READY kind=solid-cube faces=%lu faceStride=%lu ownedGpuBytes=%lu\n",
        static_cast<unsigned long>(faceCount), static_cast<unsigned long>(faceBytes),
        static_cast<unsigned long>(OwnedGpuBytesValue));
    return true;
}

bool Ps3AuthoredTexture::Bind(std::uint32_t textureUnit, std::string& error) {
    return BindWithFilter(textureUnit, GCM_TEXTURE_NEAREST, "bind", false, error);
}

bool Ps3AuthoredTexture::BindLinearClamp(std::uint32_t textureUnit, std::string& error) {
    return BindWithFilter(textureUnit, GCM_TEXTURE_LINEAR, "bind-linear-clamp", true, error);
}

bool Ps3AuthoredTexture::BindWithFilter(std::uint32_t textureUnit, std::uint32_t filter,
    const char* operation, bool requireLinear2D, std::string& error) {
    error.clear();
    if (!Initialized || ShutdownComplete) {
        SetFailure(error, operation, "owner is not initialized");
        return false;
    }
    if (VideoValue == nullptr || VideoValue->Context() == nullptr) {
        SetFailure(error, operation, "video context is unavailable, drained, or synchronization has failed");
        return false;
    }
    if (textureUnit > 15u) {
        SetFailure(error, operation, "physical texture unit must be between zero and fifteen");
        return false;
    }
    if (requireLinear2D && (TextureValue.dimension != GCM_TEXTURE_DIMS_2D || TextureValue.cubemap != GCM_FALSE)) {
        SetFailure(error, operation, "linear clamp requires a non-cube 2D texture");
        return false;
    }

    gcmContextData* context = VideoValue->Context();
    const u8 unit = static_cast<u8>(textureUnit);
    rsxInvalidateTextureCache(context, GCM_INVALIDATE_TEXTURE);
    rsxLoadTexture(context, unit, &TextureValue);
    rsxTextureControl(context, unit, GCM_TRUE, 0u, 0u, GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureFilter(context, unit, 0u, filter, filter,
        GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureWrapMode(context, unit, GCM_TEXTURE_CLAMP_TO_EDGE,
        GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE,
        GCM_TEXTURE_UNSIGNED_REMAP_NORMAL, GCM_TEXTURE_ZFUNC_ALWAYS, 0u);
    return true;
}

bool Ps3AuthoredTexture::RetireAfterIdle(std::string& error) {
    error.clear();
    if (ShutdownComplete) {
        return true;
    }
    if (!Initialized) {
        SetFailure(error, "retire", "owner is not initialized");
        return false;
    }
    if (VideoValue == nullptr || VideoValue->Context() == nullptr) {
        SetFailure(error, "retire", "video context is unavailable; owned texture storage is retained");
        return false;
    }
    if (!VideoValue->WaitForSubmittedWork()) {
        SetFailure(error, "retire", "submitted RSX work did not become idle; owned texture storage is retained");
        return false;
    }
    ReleaseOwnedResources();
    ShutdownComplete = true;
    std::printf("PS3_AUTHORED_TEXTURE_RETIRED\n");
    return true;
}

bool Ps3AuthoredTexture::ShutdownAfterDrain(std::string& error) {
    error.clear();
    if (ShutdownComplete) {
        return true;
    }
    if (!Initialized) {
        SetFailure(error, "shutdown", "owner is not initialized");
        return false;
    }
    if (VideoValue == nullptr || !VideoValue->HasDrained()) {
        SetFailure(error, "shutdown", "Ps3Video::Drain has not completed successfully");
        return false;
    }
    ReleaseOwnedResources();
    ShutdownComplete = true;
    if (OwnedGpuBytesValue != 0u) {
        SetFailure(error, "shutdown", "owned RSX bytes remain after release");
        return false;
    }
    return true;
}

std::size_t Ps3AuthoredTexture::OwnedGpuBytes() const {
    return OwnedGpuBytesValue;
}

void Ps3AuthoredTexture::ReleaseOwnedResources() {
    if (TextureBuffer != nullptr) {
        rsxFree(TextureBuffer);
        TextureBuffer = nullptr;
    }
    std::memset(&TextureValue, 0, sizeof(TextureValue));
    OwnedGpuBytesValue = 0u;
    VideoValue = nullptr;
    Initialized = false;
}

} // namespace helengine::ps3
