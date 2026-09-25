#include "platform/ps3/Ps3Batch2DPrograms.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "platform/ps3/Ps3ArtifactReadUtilities.hpp"
#include "platform/ps3/Ps3CookedShaderAttribute.hpp"
#include "platform/ps3/Ps3CookedShaderBuffer.hpp"
#include "platform/ps3/Ps3CookedShaderSampler.hpp"
#include "platform/ps3/Ps3ShaderArtifactReader.hpp"
#include "platform/ps3/Ps3Batch2DUniformProvider.hpp"
#include "platform/ps3/Ps3Video.hpp"
#include "runtime/native_exceptions.hpp"
#include "system/io/stream.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace helengine::ps3 {
namespace {

/// <summary>Maximum total cooked program artifact size accepted by the existing P3SP reader.</summary>
constexpr std::size_t MaximumP3spBytes = 16u * 1024u * 1024u;

/// <summary>Formats a pair-contract failure with the shader variant and affected resource.</summary>
bool Reject(const char* variant, const std::string& reason, std::string& error) {
    error = std::string("PS3 batch 2D ") + variant + ": " + reason;
    return false;
}

/// <summary>Rejects unsafe or ambiguous content-relative artifact paths before opening a stream.</summary>
void ValidateContentPath(const std::string& path) {
    if (path.empty() || path.size() > 65536u || path.front() == '/' || path.back() == '/') {
        throw std::runtime_error("content path must be a nonempty bounded relative path");
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(path.data());
    std::size_t cursor = 0u;
    std::size_t segmentStart = 0u;
    bool visible = false;
    while (cursor < path.size()) {
        const std::size_t scalarStart = cursor;
        std::uint32_t scalar = 0u;
        if (!Ps3ArtifactReadUtilities::ReadUtf8CodePoint(bytes, path.size(), cursor, scalar)
            || scalar < 32u || (scalar >= 127u && scalar <= 159u)
            || scalar == '\\' || scalar == ':') {
            throw std::runtime_error("content path contains invalid UTF-8, a control, backslash, or colon");
        }
        if (scalar == '/') {
            const std::string segment = path.substr(segmentStart, scalarStart - segmentStart);
            if (!visible || segment == "." || segment == "..") {
                throw std::runtime_error("content path contains an empty, blank, or traversal segment");
            }
            visible = false;
            segmentStart = cursor;
        } else if (!Ps3ArtifactReadUtilities::IsWhitespace(scalar)) {
            visible = true;
        }
    }
    const std::string finalSegment = path.substr(segmentStart);
    if (!visible || finalSegment == "." || finalSegment == "..") {
        throw std::runtime_error("content path contains an empty, blank, or traversal segment");
    }
}

/// <summary>Requires a canonical manifest-provided path beneath cooked/shaders with a P3SP suffix.</summary>
void ValidateShaderPath(const std::string& path) {
    ValidateContentPath(path);
    static const std::string prefix = "cooked/shaders/";
    if (path.compare(0u, prefix.size(), prefix) != 0
        || path.size() <= prefix.size() + 6u
        || path.compare(path.size() - 6u, 6u, ".ps3sp") != 0) {
        throw std::runtime_error("batch program path must be a canonical cooked/shaders/*.ps3sp path: " + path);
    }
}

/// <summary>Disposes an owned stream once and destroys it even if its Dispose method throws.</summary>
void DisposeStream(std::unique_ptr<::Stream>& stream) {
    std::unique_ptr<::Stream> closing = std::move(stream);
    if (closing != nullptr) closing->Dispose();
}

/// <summary>Preserves a primary read or parse failure while attempting to dispose the failed stream.</summary>
void DisposeFailedStream(std::unique_ptr<::Stream>& stream) noexcept {
    try {
        DisposeStream(stream);
    } catch (::Exception* exception) {
        std::printf("PS3_BATCH2D_PROGRAM_ERROR stage=stream-dispose-during-failure reason=%s\n",
            exception == nullptr ? "null generated exception" : exception->what());
        delete exception;
    } catch (const std::exception& exception) {
        std::printf("PS3_BATCH2D_PROGRAM_ERROR stage=stream-dispose-during-failure reason=%s\n", exception.what());
    } catch (...) {
        std::printf("PS3_BATCH2D_PROGRAM_ERROR stage=stream-dispose-during-failure reason=unknown-secondary-exception\n");
    }
}

/// <summary>Reads one exact stream length with partial-read support and rejects growth, truncation, and zero progress.</summary>
std::vector<std::uint8_t> ReadBounded(::IContentStreamSource& source, const std::string& path) {
    ValidateShaderPath(path);
    std::unique_ptr<::Stream> stream(source.OpenRead(path));
    if (stream == nullptr) throw std::runtime_error("could not open required content: " + path);
    try {
        const std::size_t length = stream->Length();
        if (length == 0u || length > MaximumP3spBytes) {
            throw std::runtime_error("content length is empty or exceeds the 16 MiB P3SP limit: " + path);
        }
        std::vector<std::uint8_t> bytes(length);
        std::size_t offset = 0u;
        while (offset < length) {
            const std::size_t count = stream->Read(bytes.data(), offset, length - offset);
            if (count == 0u || count > length - offset) {
                throw std::runtime_error("short or invalid content read: " + path);
            }
            offset += count;
        }
        if (stream->ReadByte() != -1) throw std::runtime_error("content grew while being read: " + path);
        DisposeStream(stream);
        return bytes;
    } catch (...) {
        DisposeFailedStream(stream);
        throw;
    }
}

/// <summary>Checks one authored vertex semantic and its physical shape against the batch input contract.</summary>
bool IsExpectedAttribute(const Ps3CookedShaderAttribute& attribute, std::string& error) {
    if (attribute.ScalarType != "FP3232" || attribute.VectorWidth != 4u
        || attribute.Columns != 1u || attribute.ArrayLength != 1u
        || attribute.SdkDataType != 9u || attribute.HardwareSlot > 15u) {
        error = "semantic " + attribute.FullAuthoredSemantic + " must be the measured SDK type 9 FP3232 float4 input";
        return false;
    }
    return true;
}

/// <summary>Validates all known semantics, including permitted inactive rounded-shape inputs in a textured artifact.</summary>
bool ValidateAttributes(const Ps3CookedShaderProgram& program, const char* variant,
    bool requireAll, std::string& error) {
    static const char* const semantics[] = {
        "POSITION0", "TEXCOORD0", "COLOR0", "TEXCOORD1", "TEXCOORD2", "COLOR1"
    };
    static const char* const generatedNames[] = {
        "hel_ps3_input_location_0_POSITION0", "hel_ps3_input_location_1_TEXCOORD0",
        "hel_ps3_input_location_2_COLOR0", "hel_ps3_input_location_3_TEXCOORD1",
        "hel_ps3_input_location_4_TEXCOORD2", "hel_ps3_input_location_5_COLOR1"
    };
    std::unordered_set<std::string> seenSemantics;
    std::unordered_set<std::uint32_t> seenSlots;
    for (const Ps3CookedShaderAttribute& attribute : program.Attributes) {
        const auto found = std::find_if(std::begin(semantics), std::end(semantics),
            [&attribute](const char* semantic) { return attribute.FullAuthoredSemantic == semantic; });
        if (found == std::end(semantics)) {
            return Reject(variant, "unknown vertex semantic '" + attribute.FullAuthoredSemantic + "'", error);
        }
        if (!IsExpectedAttribute(attribute, error)) {
            const std::string reason = error;
            return Reject(variant, reason, error);
        }
        const std::size_t semanticIndex = static_cast<std::size_t>(found - std::begin(semantics));
        if (attribute.GeneratedName != generatedNames[semanticIndex]) {
            return Reject(variant, "semantic " + attribute.FullAuthoredSemantic + " has an unexpected generated SDK name", error);
        }
        if (!seenSemantics.insert(attribute.FullAuthoredSemantic).second) {
            return Reject(variant, "duplicate vertex semantic '" + attribute.FullAuthoredSemantic + "'", error);
        }
        if (!seenSlots.insert(attribute.HardwareSlot).second) {
            return Reject(variant, "two authored semantics share an SDK hardware input slot", error);
        }
    }
    const std::size_t requiredCount = requireAll ? 6u : 3u;
    for (std::size_t index = 0u; index < requiredCount; ++index) {
        if (seenSemantics.find(semantics[index]) == seenSemantics.end()) {
            return Reject(variant, "required vertex semantic '" + std::string(semantics[index]) + "' is missing", error);
        }
    }
    return true;
}

/// <summary>Allows only stage buffer declarations that exactly describe the shared batch projection buffer.</summary>
bool ValidateBuffers(const Ps3CookedShaderStage& stage, const char* variant,
    bool projectionBufferRequired, std::string& error) {
    std::unordered_set<std::string> names;
    std::unordered_set<std::uint32_t> bindings;
    for (const Ps3CookedShaderBuffer& buffer : stage.Buffers) {
        if (buffer.BufferName != "BatchCameraBuffer" || buffer.AuthoredBinding != 0u
            || buffer.DeclaredByteSize != 64u) {
            return Reject(variant, "unknown buffer declaration '" + buffer.BufferName + "' or incorrect b0/64-byte shape", error);
        }
        if (!names.insert(buffer.BufferName).second || !bindings.insert(buffer.AuthoredBinding).second) {
            return Reject(variant, "duplicate BatchCameraBuffer declaration", error);
        }
    }
    if (projectionBufferRequired && stage.Buffers.size() != 1u) {
        return Reject(variant, "vertex stage must declare BatchCameraBuffer at b0 with size 64", error);
    }
    return true;
}

/// <summary>Requires every active vertex uniform to be the measured projection matrix mapping.</summary>
bool ValidateUniforms(const Ps3CookedShaderStage& stage, const char* variant,
    std::string& error) {
    if (stage.Uniforms.size() != 1u) {
        return Reject(variant, "expected exactly one active vertex projection uniform", error);
    }
    const Ps3CookedShaderUniform& uniform = stage.Uniforms.front();
    const float values[16] = {1.0f};
    float resolved[16] = {};
    std::size_t written = 0u;
    if (!Ps3Batch2DUniformProvider::Resolve(uniform, values, 16u,
        resolved, 16u, written, error)) {
        const std::string reason = error;
        return Reject(variant, reason, error);
    }
    return true;
}

/// <summary>Checks the exact authored texture and sampler bindings and the SDK-resolved physical pixel unit.</summary>
bool ValidateSamplers(const Ps3CookedShaderProgram& program, const char* variant,
    bool textured, std::string& error) {
    if (!textured) {
        if (!program.PixelSamplers.empty()) return Reject(variant, "rounded variant must not declare pixel samplers", error);
        return true;
    }
    if (program.PixelSamplers.size() != 1u) {
        return Reject(variant, "textured variant requires exactly one BatchTexture/BatchSampler mapping", error);
    }
    const Ps3CookedShaderSampler& sampler = program.PixelSamplers.front();
    if (sampler.TextureName != "BatchTexture" || sampler.TextureBinding != 0u
        || sampler.SamplerName != "BatchSampler" || sampler.SamplerBinding != 0u
        || sampler.Dimension != "Dim2D" || sampler.TextureUnit > 15u
        || sampler.GeneratedName != "hel_ps3_sampler_t0_s0" || sampler.SdkDataType != 15u) {
        return Reject(variant, "pixel sampler mapping does not match BatchTexture t0/BatchSampler s0/Dim2D", error);
    }
    return true;
}

} // namespace

Ps3Batch2DPrograms::Ps3Batch2DPrograms(Ps3Video& video, ::IContentStreamSource& source)
    : VideoValue(&video)
    , SourceValue(&source)
    , TexturedValue()
    , RoundedShapeValue()
    , InitializationAttempted(false)
    , Ready(false)
    , ShutdownComplete(false) {}

Ps3Batch2DPrograms::~Ps3Batch2DPrograms() = default;

void Ps3Batch2DPrograms::Initialize(const std::string& texturedPath, const std::string& roundedPath) {
    if (InitializationAttempted) throw std::logic_error("PS3 batch 2D program pair initialization is one-shot");
    if (ShutdownComplete) throw std::logic_error("PS3 batch 2D program pair is retired");
    InitializationAttempted = true;

    try {
        const std::vector<std::uint8_t> texturedBytes = ReadBounded(*SourceValue, texturedPath);
        const std::vector<std::uint8_t> roundedBytes = ReadBounded(*SourceValue, roundedPath);
        Ps3CookedShaderProgram texturedProgram{};
        Ps3CookedShaderProgram roundedProgram{};
        std::string error;
        if (!Ps3ShaderArtifactReader::Read(texturedBytes.data(), texturedBytes.size(), texturedProgram, error)) {
            throw std::runtime_error("textured shader " + texturedPath + ": " + error);
        }
        if (!Ps3ShaderArtifactReader::Read(roundedBytes.data(), roundedBytes.size(), roundedProgram, error)) {
            throw std::runtime_error("rounded shader " + roundedPath + ": " + error);
        }
        if (!ValidatePair(texturedProgram, roundedProgram, error)) throw std::runtime_error(error);

        TexturedValue = std::make_unique<Ps3AuthoredProgram>();
        RoundedShapeValue = std::make_unique<Ps3AuthoredProgram>();
        if (!TexturedValue->Initialize(*VideoValue, texturedProgram, error)) {
            throw std::runtime_error("textured shader initialization failed: " + error);
        }
        if (!RoundedShapeValue->Initialize(*VideoValue, roundedProgram, error)) {
            throw std::runtime_error("rounded shader initialization failed: " + error);
        }
        Ready = true;
    } catch (...) {
        Ready = false;
        throw;
    }
}

bool Ps3Batch2DPrograms::MatchesSource(const ::IContentStreamSource& source) const {
    return SourceValue == &source;
}

Ps3AuthoredProgram& Ps3Batch2DPrograms::Textured() {
    if (!Ready || ShutdownComplete || TexturedValue == nullptr) {
        throw std::logic_error("PS3 batch 2D Textured program requires a ready, live pair");
    }
    return *TexturedValue;
}

Ps3AuthoredProgram& Ps3Batch2DPrograms::RoundedShape() {
    if (!Ready || ShutdownComplete || RoundedShapeValue == nullptr) {
        throw std::logic_error("PS3 batch 2D RoundedShape program requires a ready, live pair");
    }
    return *RoundedShapeValue;
}

bool Ps3Batch2DPrograms::ShutdownAfterDrain(std::string& error) {
    error.clear();
    if (ShutdownComplete) return true;
    if (VideoValue == nullptr || !VideoValue->HasDrained()) {
        error = "PS3 batch 2D program shutdown requires a successful Ps3Video::Drain";
        return false;
    }
    Ready = false;
    if (TexturedValue != nullptr && TexturedValue->OwnedGpuBytes() != 0u
        && !TexturedValue->ShutdownAfterDrain(error)) return false;
    if (RoundedShapeValue != nullptr && RoundedShapeValue->OwnedGpuBytes() != 0u
        && !RoundedShapeValue->ShutdownAfterDrain(error)) return false;
    TexturedValue.reset();
    RoundedShapeValue.reset();
    ShutdownComplete = true;
    return true;
}

std::size_t Ps3Batch2DPrograms::OwnedGpuBytes() const {
    const std::size_t texturedBytes = TexturedValue == nullptr ? 0u : TexturedValue->OwnedGpuBytes();
    const std::size_t roundedBytes = RoundedShapeValue == nullptr ? 0u : RoundedShapeValue->OwnedGpuBytes();
    if (roundedBytes > std::numeric_limits<std::size_t>::max() - texturedBytes) {
        throw std::overflow_error("PS3 batch 2D owned GPU byte count overflow");
    }
    return texturedBytes + roundedBytes;
}

bool Ps3Batch2DPrograms::ValidatePair(const Ps3CookedShaderProgram& textured,
    const Ps3CookedShaderProgram& rounded, std::string& error) {
    error.clear();
    if (textured.ShaderAssetId != "SpriteBatchShader" || rounded.ShaderAssetId != "SpriteBatchShader"
        || textured.VertexProgramName != "SpriteBatchShader.vs" || rounded.VertexProgramName != "SpriteBatchShader.vs"
        || textured.PixelProgramName != "SpriteBatchShader.ps" || rounded.PixelProgramName != "SpriteBatchShader.ps"
        || textured.VariantName != "Textured" || rounded.VariantName != "RoundedShape") {
        return Reject("pair", "artifact identity must be SpriteBatchShader.vs/ps with Textured and RoundedShape variants", error);
    }
    if (textured.SourceHash.empty() || textured.ToolchainIdentity.empty()
        || textured.SourceHash != rounded.SourceHash
        || textured.ToolchainIdentity != rounded.ToolchainIdentity) {
        return Reject("pair", "variants must share nonempty source hash and toolchain identity", error);
    }
    if (textured.Vertex.Stage != 0u || textured.Pixel.Stage != 1u
        || rounded.Vertex.Stage != 0u || rounded.Pixel.Stage != 1u
        || textured.Vertex.EntryPoint != "VS" || rounded.Vertex.EntryPoint != "VS"
        || textured.Pixel.EntryPoint != "PS" || rounded.Pixel.EntryPoint != "PS") {
        return Reject("pair", "stage identity or VS/PS entry point is incorrect", error);
    }
    const Ps3CookedShaderProgram* programs[] = {&textured, &rounded};
    const char* variants[] = {"Textured", "RoundedShape"};
    for (std::size_t index = 0u; index < 2u; ++index) {
        const Ps3CookedShaderProgram& program = *programs[index];
        if (!ValidateBuffers(program.Vertex, variants[index], true, error)
            || !ValidateBuffers(program.Pixel, variants[index], false, error)
            || !ValidateUniforms(program.Vertex, variants[index], error)
            || !ValidateAttributes(program, variants[index], index == 1u, error)
            || !ValidateSamplers(program, variants[index], index == 0u, error)) return false;
        if (!program.Pixel.Uniforms.empty()) {
            return Reject(variants[index], "pixel stage has unexpected active uniforms", error);
        }
    }
    return true;
}

} // namespace helengine::ps3

#endif
