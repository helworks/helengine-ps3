#include "platform/ps3/Ps3AuthoredProgram.hpp"

#include <malloc.h>
#include <rsx/rsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

#include "platform/ps3/Ps3RsxProgramValidator.hpp"
#include "platform/ps3/Ps3Video.hpp"

namespace helengine::ps3 {
namespace {

/// <summary>Resolves the number of SDK floats per constant element and its one-element upload type.</summary>
bool GetUploadShape(std::uint32_t sdkType, std::uint8_t& oneElementType, std::size_t& laneCount) {
    switch (sdkType) {
    case PARAM_FLOAT1:
        oneElementType = PARAM_FLOAT1;
        laneCount = 1u;
        return true;
    case PARAM_FLOAT2:
        oneElementType = PARAM_FLOAT2;
        laneCount = 2u;
        return true;
    case PARAM_FLOAT3:
        oneElementType = PARAM_FLOAT3;
        laneCount = 3u;
        return true;
    case PARAM_FLOAT4:
        oneElementType = PARAM_FLOAT4;
        laneCount = 4u;
        return true;
    case PARAM_FLOAT3x3:
    case PARAM_FLOAT4x3:
        oneElementType = PARAM_FLOAT3;
        laneCount = 3u;
        return true;
    case PARAM_FLOAT3x4:
    case PARAM_FLOAT4x4:
        oneElementType = PARAM_FLOAT4;
        laneCount = 4u;
        return true;
    default:
        oneElementType = 0u;
        laneCount = 0u;
        return false;
    }
}

/// <summary>Writes one concrete authored-program validation or lifecycle failure to the caller.</summary>
void SetFailure(std::string& error, const char* stage, const std::string& reason) {
    error = std::string("PS3 authored program ") + stage + ": " + reason;
}

} // namespace

Ps3AuthoredProgram::Ps3AuthoredProgram()
    : ProgramValue()
    , VideoValue(nullptr)
    , VertexProgramBytes(nullptr)
    , PixelProgramBytes(nullptr)
    , VertexProgramValue(nullptr)
    , PixelProgramValue(nullptr)
    , VertexUcodeValue(nullptr)
    , FragmentUcodeBuffer(nullptr)
    , FragmentUcodeOffset(0u)
    , FragmentUcodeBytes(0u)
    , OwnedGpuBytesValue(0u)
    , VertexUniformUploaded()
    , PixelUniformUploaded()
    , Initialized(false)
    , DrawActive(false)
    , ShutdownComplete(false) {
}

Ps3AuthoredProgram::~Ps3AuthoredProgram() {
    if (!Initialized) {
        return;
    }
    std::printf("PS3_AUTHORED_PROGRAM_ERROR stage=destructor reason=missing-explicit-ShutdownAfterDrain retainedGpuBytes=%lu\n",
        static_cast<unsigned long>(OwnedGpuBytesValue));
    if (PixelProgramBytes != nullptr) {
        std::free(PixelProgramBytes);
    }
    if (VertexProgramBytes != nullptr) {
        std::free(VertexProgramBytes);
    }
}

bool Ps3AuthoredProgram::Initialize(Ps3Video& video, const Ps3CookedShaderProgram& program, std::string& error) {
    error.clear();
    if (Initialized || ShutdownComplete) {
        SetFailure(error, "initialize", "owner is already initialized or retired");
        return false;
    }
    if (video.Context() == nullptr) {
        SetFailure(error, "initialize", "video context is not initialized");
        return false;
    }
    if (video.HasDrained()) {
        SetFailure(error, "initialize", "video has already completed its terminal drain");
        return false;
    }

    std::string validationError;
    if (!Ps3RsxProgramValidator::Validate(program, validationError)) {
        SetFailure(error, "validation", validationError);
        return false;
    }

    Ps3CookedShaderProgram candidateProgram;
    std::vector<std::uint8_t> candidateVertexUniforms;
    std::vector<std::uint8_t> candidatePixelUniforms;
    try {
        candidateProgram = program;
        candidateVertexUniforms.assign(candidateProgram.Vertex.Uniforms.size(), 0u);
        candidatePixelUniforms.assign(candidateProgram.Pixel.Uniforms.size(), 0u);
    } catch (const std::bad_alloc&) {
        SetFailure(error, "initialize", "CPU metadata or active-uniform tracking allocation failed");
        return false;
    }

    void* candidateVertexBytes = memalign(CpuProgramAlignment, candidateProgram.Vertex.Bytecode.size());
    if (candidateVertexBytes == nullptr) {
        SetFailure(error, "vertex", "aligned CPU program allocation failed");
        return false;
    }
    std::memcpy(candidateVertexBytes, candidateProgram.Vertex.Bytecode.data(), candidateProgram.Vertex.Bytecode.size());

    void* candidatePixelBytes = memalign(CpuProgramAlignment, candidateProgram.Pixel.Bytecode.size());
    if (candidatePixelBytes == nullptr) {
        std::free(candidateVertexBytes);
        SetFailure(error, "pixel", "aligned CPU program allocation failed");
        return false;
    }
    std::memcpy(candidatePixelBytes, candidateProgram.Pixel.Bytecode.data(), candidateProgram.Pixel.Bytecode.size());

    const rsxVertexProgram* candidateVertexProgram = static_cast<const rsxVertexProgram*>(candidateVertexBytes);
    const rsxFragmentProgram* candidatePixelProgram = static_cast<const rsxFragmentProgram*>(candidatePixelBytes);
    void* candidateVertexUcode = nullptr;
    u32 candidateVertexUcodeBytes = 0u;
    rsxVertexProgramGetUCode(candidateVertexProgram, &candidateVertexUcode, &candidateVertexUcodeBytes);
    if (candidateVertexUcode == nullptr || candidateVertexUcodeBytes == 0u) {
        std::free(candidatePixelBytes);
        std::free(candidateVertexBytes);
        SetFailure(error, "vertex", "validated SDK program produced no microcode");
        return false;
    }

    void* candidateFragmentUcodeSource = nullptr;
    u32 candidateFragmentUcodeBytes = 0u;
    rsxFragmentProgramGetUCode(candidatePixelProgram, &candidateFragmentUcodeSource, &candidateFragmentUcodeBytes);
    if (candidateFragmentUcodeSource == nullptr || candidateFragmentUcodeBytes == 0u
        || (candidateFragmentUcodeBytes & 3u) != 0u) {
        std::free(candidatePixelBytes);
        std::free(candidateVertexBytes);
        SetFailure(error, "pixel", "validated SDK program produced empty or non-word-aligned microcode");
        return false;
    }

    void* candidateFragmentUcode = rsxMemalign(FragmentUcodeAlignment, candidateFragmentUcodeBytes);
    if (candidateFragmentUcode == nullptr) {
        std::free(candidatePixelBytes);
        std::free(candidateVertexBytes);
        SetFailure(error, "pixel", "RSX fragment microcode allocation failed");
        return false;
    }
    std::memcpy(candidateFragmentUcode, candidateFragmentUcodeSource, candidateFragmentUcodeBytes);

    u32 candidateFragmentOffset = 0u;
    const s32 offsetResult = rsxAddressToOffset(candidateFragmentUcode, &candidateFragmentOffset);
    if (offsetResult != 0) {
        rsxFree(candidateFragmentUcode);
        std::free(candidatePixelBytes);
        std::free(candidateVertexBytes);
        SetFailure(error, "pixel", std::string("RSX microcode offset resolution failed with code ")
            + std::to_string(static_cast<int>(offsetResult)));
        return false;
    }

    ProgramValue = std::move(candidateProgram);
    VideoValue = &video;
    VertexProgramBytes = candidateVertexBytes;
    PixelProgramBytes = candidatePixelBytes;
    VertexProgramValue = candidateVertexProgram;
    PixelProgramValue = candidatePixelProgram;
    VertexUcodeValue = candidateVertexUcode;
    FragmentUcodeBuffer = candidateFragmentUcode;
    FragmentUcodeOffset = candidateFragmentOffset;
    FragmentUcodeBytes = candidateFragmentUcodeBytes;
    OwnedGpuBytesValue = candidateFragmentUcodeBytes;
    VertexUniformUploaded = std::move(candidateVertexUniforms);
    PixelUniformUploaded = std::move(candidatePixelUniforms);
    Initialized = true;
    DrawActive = false;
    ShutdownComplete = false;

    std::printf("PS3_AUTHORED_PROGRAM_READY asset=%s vertex=%s pixel=%s vertexBlobBytes=%lu pixelBlobBytes=%lu fragmentUcodeBytes=%u ownedGpuBytes=%lu\n",
        ProgramValue.ShaderAssetId.c_str(), ProgramValue.VertexProgramName.c_str(), ProgramValue.PixelProgramName.c_str(),
        static_cast<unsigned long>(ProgramValue.Vertex.Bytecode.size()),
        static_cast<unsigned long>(ProgramValue.Pixel.Bytecode.size()), FragmentUcodeBytes,
        static_cast<unsigned long>(OwnedGpuBytesValue));
    return true;
}

bool Ps3AuthoredProgram::BeginDraw(std::string& error) {
    error.clear();
    if (!Initialized || ShutdownComplete) {
        SetFailure(error, "begin-draw", "owner is not initialized");
        return false;
    }
    if (VideoValue == nullptr || VideoValue->Context() == nullptr) {
        SetFailure(error, "begin-draw", "video context is unavailable");
        return false;
    }
    if (VideoValue->HasDrained()) {
        SetFailure(error, "begin-draw", "video has already completed its terminal drain");
        return false;
    }
    if (DrawActive) {
        SetFailure(error, "begin-draw", "the previous draw has not completed its active uniforms");
        return false;
    }

    std::fill(VertexUniformUploaded.begin(), VertexUniformUploaded.end(), 0u);
    std::fill(PixelUniformUploaded.begin(), PixelUniformUploaded.end(), 0u);
    gcmContextData* context = VideoValue->Context();
    rsxLoadVertexProgram(context, VertexProgramValue, VertexUcodeValue);
    rsxSetFragmentProgramControl(context, PixelProgramValue, GCM_TRUE, 0u, 0u);
    rsxLoadFragmentProgramLocation(context, PixelProgramValue, FragmentUcodeOffset, GCM_LOCATION_RSX);
    DrawActive = true;
    return true;
}

bool Ps3AuthoredProgram::UploadUniform(std::uint32_t stage, std::size_t uniformIndex,
    const float* values, std::size_t valueCount, std::string& error) {
    error.clear();
    if (!Initialized || ShutdownComplete || !DrawActive) {
        SetFailure(error, "uniform", "a successful BeginDraw is required before uploads");
        return false;
    }
    if (VideoValue == nullptr || VideoValue->Context() == nullptr || VideoValue->HasDrained()) {
        SetFailure(error, "uniform", "video context is unavailable or already drained");
        return false;
    }
    if (stage > 1u) {
        SetFailure(error, "uniform", "stage must be zero for vertex or one for pixel");
        return false;
    }

    const Ps3CookedShaderStage& cookedStage = stage == 0u ? ProgramValue.Vertex : ProgramValue.Pixel;
    std::vector<std::uint8_t>& uploaded = stage == 0u ? VertexUniformUploaded : PixelUniformUploaded;
    if (uniformIndex >= cookedStage.Uniforms.size()) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform", "uniform index is outside the active mapping list");
        return false;
    }
    if (values == nullptr) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform", "value pointer is null");
        return false;
    }

    const Ps3CookedShaderUniform& uniform = cookedStage.Uniforms[uniformIndex];
    std::uint8_t oneElementType = 0u;
    std::size_t laneCount = 0u;
    if (!GetUploadShape(uniform.SdkDataType, oneElementType, laneCount)) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("unsupported SDK float type for ") + uniform.GeneratedName);
        return false;
    }
    if (uniform.SdkElementCount == 0u
        || static_cast<std::size_t>(uniform.SdkElementCount) > std::numeric_limits<std::size_t>::max() / laneCount) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("SDK element count overflows the float count for ") + uniform.GeneratedName);
        return false;
    }
    const std::size_t requiredValueCount = static_cast<std::size_t>(uniform.SdkElementCount) * laneCount;
    if (valueCount != requiredValueCount) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("value count must be ") + std::to_string(requiredValueCount) + " for " + uniform.GeneratedName);
        return false;
    }
    for (std::size_t valueIndex = 0u; valueIndex < valueCount; ++valueIndex) {
        if (!std::isfinite(values[valueIndex])) {
            SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
                std::string("non-finite float in ") + uniform.GeneratedName);
            return false;
        }
    }

    const rsxProgramConst* parameter = stage == 0u
        ? rsxVertexProgramGetConst(VertexProgramValue, uniform.GeneratedName.c_str())
        : rsxFragmentProgramGetConst(PixelProgramValue, uniform.GeneratedName.c_str());
    if (parameter == nullptr) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("generated SDK constant is missing: ") + uniform.GeneratedName);
        return false;
    }
    if (parameter->type != uniform.SdkDataType || parameter->count != uniform.SdkElementCount) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("SDK type or element count changed after validation for ") + uniform.GeneratedName);
        return false;
    }

    const rsxProgramConst* constantTable = stage == 0u
        ? rsxVertexProgramGetConsts(VertexProgramValue)
        : rsxFragmentProgramGetConsts(PixelProgramValue);
    const std::uint32_t constantCount = stage == 0u
        ? rsxVertexProgramGetNumConst(VertexProgramValue)
        : rsxFragmentProgramGetNumConst(PixelProgramValue);
    if (constantTable == nullptr || parameter < constantTable) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("SDK constant table is unavailable for ") + uniform.GeneratedName);
        return false;
    }
    const std::ptrdiff_t tableIndex = parameter - constantTable;
    if (tableIndex < 0 || static_cast<std::size_t>(tableIndex) > constantCount
        || uniform.SdkElementCount > constantCount - static_cast<std::uint32_t>(tableIndex)) {
        SetFailure(error, stage == 0u ? "vertex uniform" : "pixel uniform",
            std::string("SDK constant element records exceed the validated table for ") + uniform.GeneratedName);
        return false;
    }

    gcmContextData* context = VideoValue->Context();
    for (std::uint32_t elementIndex = 0u; elementIndex < uniform.SdkElementCount; ++elementIndex) {
        const rsxProgramConst* elementParameter = parameter + elementIndex;
        rsxProgramConst oneElement = *elementParameter;
        oneElement.type = oneElementType;
        oneElement.count = 1u;
        const float* elementValues = values + static_cast<std::size_t>(elementIndex) * laneCount;
        if (stage == 0u) {
            rsxSetVertexProgramParameter(context, VertexProgramValue, &oneElement, elementValues);
        } else {
            rsxSetFragmentProgramParameter(context, PixelProgramValue, &oneElement, elementValues,
                FragmentUcodeOffset, GCM_LOCATION_RSX);
        }
    }
    uploaded[uniformIndex] = 1u;
    return true;
}

bool Ps3AuthoredProgram::FinishUniformUpdates(std::string& error) {
    error.clear();
    if (!Initialized || ShutdownComplete || !DrawActive) {
        SetFailure(error, "finish-uniforms", "a successful BeginDraw is required before completion");
        return false;
    }
    if (VideoValue == nullptr || VideoValue->Context() == nullptr || VideoValue->HasDrained()) {
        SetFailure(error, "finish-uniforms", "video context is unavailable or already drained");
        return false;
    }
    for (std::size_t index = 0u; index < VertexUniformUploaded.size(); ++index) {
        if (VertexUniformUploaded[index] == 0u) {
            SetFailure(error, "finish-uniforms",
                std::string("active vertex uniform was not uploaded: ") + ProgramValue.Vertex.Uniforms[index].GeneratedName);
            return false;
        }
    }
    for (std::size_t index = 0u; index < PixelUniformUploaded.size(); ++index) {
        if (PixelUniformUploaded[index] == 0u) {
            SetFailure(error, "finish-uniforms",
                std::string("active pixel uniform was not uploaded: ") + ProgramValue.Pixel.Uniforms[index].GeneratedName);
            return false;
        }
    }

    rsxUpdateFragmentProgramLocation(VideoValue->Context(), FragmentUcodeOffset, GCM_LOCATION_RSX);
    DrawActive = false;
    return true;
}

bool Ps3AuthoredProgram::ShutdownAfterDrain(std::string& error) {
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

const Ps3CookedShaderProgram& Ps3AuthoredProgram::Program() const {
    if (!Initialized || ShutdownComplete) {
        throw std::logic_error("Ps3AuthoredProgram::Program requires a successfully initialized live owner");
    }
    return ProgramValue;
}

std::size_t Ps3AuthoredProgram::OwnedGpuBytes() const {
    return OwnedGpuBytesValue;
}

void Ps3AuthoredProgram::ReleaseOwnedResources() {
    if (FragmentUcodeBuffer != nullptr) {
        rsxFree(FragmentUcodeBuffer);
        FragmentUcodeBuffer = nullptr;
    }
    if (PixelProgramBytes != nullptr) {
        std::free(PixelProgramBytes);
        PixelProgramBytes = nullptr;
    }
    if (VertexProgramBytes != nullptr) {
        std::free(VertexProgramBytes);
        VertexProgramBytes = nullptr;
    }
    FragmentUcodeOffset = 0u;
    FragmentUcodeBytes = 0u;
    OwnedGpuBytesValue = 0u;
    VertexProgramValue = nullptr;
    PixelProgramValue = nullptr;
    VertexUcodeValue = nullptr;
    VideoValue = nullptr;
    VertexUniformUploaded.clear();
    PixelUniformUploaded.clear();
    ProgramValue = Ps3CookedShaderProgram{};
    Initialized = false;
    DrawActive = false;
}

} // namespace helengine::ps3
