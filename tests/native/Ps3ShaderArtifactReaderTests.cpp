#include "platform/ps3/Ps3ShaderArtifactReader.hpp"

#include <sys/process.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

SYS_PROCESS_PARAM(1001, 0x100000);

namespace {

constexpr std::size_t MaximumArtifactBytes = 16u * 1024u * 1024u;
constexpr std::size_t MaximumStageBytes = 1024u * 1024u;
constexpr std::uint32_t MaximumRecords = 4096u;
constexpr std::uint32_t SdkFloat4 = 9u;

struct StringField {
    std::size_t LengthOffset;
    std::size_t DataOffset;
    std::size_t Length;
    std::string Value;
};

struct BufferField {
    std::size_t Begin;
    std::size_t End;
    StringField Name;
    std::uint32_t AuthoredBinding;
    std::uint32_t DeclaredByteSize;
};

struct UniformField {
    std::size_t Begin;
    std::size_t End;
    StringField MemberPath;
    std::size_t ByteOffset;
    std::size_t ByteSize;
    std::size_t RowMajor;
    StringField ScalarType;
    std::size_t VectorWidth;
    std::size_t Columns;
    std::size_t ArrayLength;
    StringField GeneratedName;
    std::size_t SdkDataType;
    std::size_t SdkElementCount;
};

struct StageLayout {
    std::size_t Begin;
    std::size_t Stage;
    StringField EntryPoint;
    std::size_t BlobLength;
    std::size_t Blob;
    std::size_t BlobSize;
    std::size_t BlobEnd;
    std::size_t BufferCount;
    std::size_t BuffersBegin;
    std::size_t BuffersEnd;
    std::vector<BufferField> Buffers;
    std::size_t UniformCount;
    std::vector<UniformField> Uniforms;
    std::size_t End;
};

struct AttributeField {
    std::size_t Begin;
    std::size_t End;
    StringField Semantic;
    StringField GeneratedName;
    StringField ScalarType;
    std::size_t VectorWidth;
    std::size_t Columns;
    std::size_t ArrayLength;
    std::size_t HardwareSlot;
    std::size_t SdkDataType;
};

struct SamplerField {
    std::size_t Begin;
    std::size_t End;
    StringField TextureName;
    std::size_t TextureBinding;
    StringField SamplerName;
    std::size_t SamplerBinding;
    StringField GeneratedName;
    StringField Dimension;
    std::size_t TextureUnit;
    std::size_t SdkDataType;
};

struct WireLayout {
    StringField ShaderAssetId;
    StringField SourceHash;
    std::size_t IdentityEnd;
    StageLayout Stages[2];
    std::size_t AttributeCount;
    std::size_t AttributesBegin;
    std::size_t AttributesEnd;
    std::vector<AttributeField> Attributes;
    std::size_t SamplerCount;
    std::size_t SamplersBegin;
    std::size_t SamplersEnd;
    std::vector<SamplerField> Samplers;
};

std::vector<std::uint8_t> Artifact;
std::vector<std::uint8_t> RawVertex;
std::vector<std::uint8_t> RawPixel;
std::size_t PassedCases = 0;
std::size_t FailedCases = 0;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::uint32_t ReadU32At(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4u, "test walker found a truncated U32");
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u)
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u)
        | static_cast<std::uint32_t>(bytes[offset + 3u]);
}

std::uint16_t ReadU16At(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 2u, "test walker found a truncated U16");
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8u)
        | static_cast<std::uint16_t>(bytes[offset + 1u]));
}

void WriteU32At(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4u, "test mutation found a truncated U32");
    bytes[offset] = static_cast<std::uint8_t>(value >> 24u);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value);
}

void WriteU16At(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 2u, "test mutation found a truncated U16");
    bytes[offset] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value);
}

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 24u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 16u));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void AppendStringField(std::vector<std::uint8_t>& bytes, const std::string& value) {
    Require(value.size() <= std::numeric_limits<std::uint32_t>::max(), "test string exceeds the wire length field");
    AppendU32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::uint32_t ReadU32(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    std::uint32_t value = ReadU32At(bytes, cursor);
    cursor += 4u;
    return value;
}

StringField ReadStringField(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    StringField field{};
    field.LengthOffset = cursor;
    std::uint32_t encodedLength = ReadU32(bytes, cursor);
    field.DataOffset = cursor;
    field.Length = encodedLength;
    Require(field.Length <= bytes.size() - cursor, "test walker found a truncated string");
    field.Value.assign(reinterpret_cast<const char*>(bytes.data() + cursor), field.Length);
    cursor += field.Length;
    return field;
}

std::size_t ReadListCount(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    std::uint32_t count = ReadU32(bytes, cursor);
    Require(count <= MaximumRecords, "test walker found an excessive record count");
    return static_cast<std::size_t>(count);
}

StageLayout ReadStageLayout(const std::vector<std::uint8_t>& bytes, std::size_t& cursor) {
    StageLayout stage{};
    stage.Begin = cursor;
    stage.Stage = cursor;
    (void)ReadU32(bytes, cursor);
    stage.EntryPoint = ReadStringField(bytes, cursor);
    stage.BlobLength = cursor;
    stage.BlobSize = ReadU32(bytes, cursor);
    stage.Blob = cursor;
    Require(stage.BlobSize <= bytes.size() - cursor, "test walker found a truncated stage blob");
    cursor += stage.BlobSize;
    stage.BlobEnd = cursor;

    stage.BufferCount = cursor;
    std::size_t bufferCount = ReadListCount(bytes, cursor);
    stage.BuffersBegin = cursor;
    stage.Buffers.reserve(bufferCount);
    for (std::size_t index = 0; index < bufferCount; ++index) {
        BufferField buffer{};
        buffer.Begin = cursor;
        buffer.Name = ReadStringField(bytes, cursor);
        buffer.AuthoredBinding = ReadU32(bytes, cursor);
        buffer.DeclaredByteSize = ReadU32(bytes, cursor);
        buffer.End = cursor;
        stage.Buffers.push_back(std::move(buffer));
    }
    stage.BuffersEnd = cursor;

    stage.UniformCount = cursor;
    std::size_t uniformCount = ReadListCount(bytes, cursor);
    stage.Uniforms.reserve(uniformCount);
    for (std::size_t index = 0; index < uniformCount; ++index) {
        UniformField uniform{};
        uniform.Begin = cursor;
        (void)ReadStringField(bytes, cursor);
        (void)ReadU32(bytes, cursor);
        uniform.MemberPath = ReadStringField(bytes, cursor);
        uniform.ByteOffset = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.ByteSize = cursor;
        (void)ReadU32(bytes, cursor);
        (void)ReadU32(bytes, cursor);
        (void)ReadU32(bytes, cursor);
        uniform.RowMajor = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.ScalarType = ReadStringField(bytes, cursor);
        uniform.VectorWidth = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.Columns = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.ArrayLength = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.GeneratedName = ReadStringField(bytes, cursor);
        uniform.SdkDataType = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.SdkElementCount = cursor;
        (void)ReadU32(bytes, cursor);
        uniform.End = cursor;
        stage.Uniforms.push_back(std::move(uniform));
    }
    stage.End = cursor;
    return stage;
}

WireLayout ParseWireLayout(const std::vector<std::uint8_t>& bytes) {
    Require(bytes.size() >= 12u && bytes.size() <= MaximumArtifactBytes, "test walker input length is outside its bound");
    std::size_t cursor = 12u;
    WireLayout layout{};
    layout.ShaderAssetId = ReadStringField(bytes, cursor);
    (void)ReadStringField(bytes, cursor);
    (void)ReadStringField(bytes, cursor);
    (void)ReadStringField(bytes, cursor);
    layout.SourceHash = ReadStringField(bytes, cursor);
    (void)ReadStringField(bytes, cursor);
    layout.IdentityEnd = cursor;
    layout.Stages[0] = ReadStageLayout(bytes, cursor);
    layout.Stages[1] = ReadStageLayout(bytes, cursor);

    layout.AttributeCount = cursor;
    std::size_t attributeCount = ReadListCount(bytes, cursor);
    layout.AttributesBegin = cursor;
    layout.Attributes.reserve(attributeCount);
    for (std::size_t index = 0; index < attributeCount; ++index) {
        AttributeField attribute{};
        attribute.Begin = cursor;
        attribute.Semantic = ReadStringField(bytes, cursor);
        attribute.GeneratedName = ReadStringField(bytes, cursor);
        attribute.ScalarType = ReadStringField(bytes, cursor);
        attribute.VectorWidth = cursor;
        (void)ReadU32(bytes, cursor);
        attribute.Columns = cursor;
        (void)ReadU32(bytes, cursor);
        attribute.ArrayLength = cursor;
        (void)ReadU32(bytes, cursor);
        attribute.HardwareSlot = cursor;
        (void)ReadU32(bytes, cursor);
        attribute.SdkDataType = cursor;
        (void)ReadU32(bytes, cursor);
        attribute.End = cursor;
        layout.Attributes.push_back(std::move(attribute));
    }
    layout.AttributesEnd = cursor;

    layout.SamplerCount = cursor;
    std::size_t samplerCount = ReadListCount(bytes, cursor);
    layout.SamplersBegin = cursor;
    layout.Samplers.reserve(samplerCount);
    for (std::size_t index = 0; index < samplerCount; ++index) {
        SamplerField sampler{};
        sampler.Begin = cursor;
        sampler.TextureName = ReadStringField(bytes, cursor);
        sampler.TextureBinding = cursor;
        (void)ReadU32(bytes, cursor);
        sampler.SamplerName = ReadStringField(bytes, cursor);
        sampler.SamplerBinding = cursor;
        (void)ReadU32(bytes, cursor);
        sampler.GeneratedName = ReadStringField(bytes, cursor);
        sampler.Dimension = ReadStringField(bytes, cursor);
        sampler.TextureUnit = cursor;
        (void)ReadU32(bytes, cursor);
        sampler.SdkDataType = cursor;
        (void)ReadU32(bytes, cursor);
        sampler.End = cursor;
        layout.Samplers.push_back(std::move(sampler));
    }
    layout.SamplersEnd = cursor;
    Require(cursor == bytes.size(), "test walker found unexpected trailing bytes in producer fixture");
    return layout;
}

void UpdateArtifactTotal(std::vector<std::uint8_t>& bytes) {
    if (bytes.size() >= 12u && bytes.size() <= std::numeric_limits<std::uint32_t>::max()) {
        WriteU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size()));
    }
}

void ReplaceStageBlob(std::vector<std::uint8_t>& artifact, std::size_t stageIndex,
    const std::vector<std::uint8_t>& newBlob) {
    WireLayout layout = ParseWireLayout(artifact);
    const StageLayout& stage = layout.Stages[stageIndex];
    std::vector<std::uint8_t> replaced;
    replaced.reserve(artifact.size() - stage.BlobSize + newBlob.size());
    replaced.insert(replaced.end(), artifact.begin(), artifact.begin() + static_cast<std::ptrdiff_t>(stage.BlobLength));
    replaced.push_back(static_cast<std::uint8_t>(newBlob.size() >> 24u));
    replaced.push_back(static_cast<std::uint8_t>(newBlob.size() >> 16u));
    replaced.push_back(static_cast<std::uint8_t>(newBlob.size() >> 8u));
    replaced.push_back(static_cast<std::uint8_t>(newBlob.size()));
    replaced.insert(replaced.end(), newBlob.begin(), newBlob.end());
    replaced.insert(replaced.end(), artifact.begin() + static_cast<std::ptrdiff_t>(stage.BlobEnd), artifact.end());
    UpdateArtifactTotal(replaced);
    artifact = std::move(replaced);
}

std::vector<std::uint8_t> AddInternalFragmentConstants(
    const std::vector<std::uint8_t>& artifact,
    const std::vector<std::vector<std::uint32_t>>& elementRelocations) {
    WireLayout layout = ParseWireLayout(artifact);
    const StageLayout& pixel = layout.Stages[1];
    const std::vector<std::uint8_t> originalBlob(
        artifact.begin() + static_cast<std::ptrdiff_t>(pixel.Blob),
        artifact.begin() + static_cast<std::ptrdiff_t>(pixel.BlobEnd));
    Require(!elementRelocations.empty() && elementRelocations.size() <= 2u,
        "test fixture helper requires one or two constant elements");
    Require(ReadU16At(originalBlob, 8u) == 0u,
        "test fixture helper requires the real pixel program to have no constants");

    const std::uint32_t attributeOffset = ReadU32At(originalBlob, 12u);
    const std::uint16_t attributeCount = ReadU16At(originalBlob, 6u);
    const std::uint32_t oldConstantOffset = ReadU32At(originalBlob, 16u);
    const std::uint32_t oldMicrocodeOffset = ReadU32At(originalBlob, 20u);
    const std::uint16_t instructionCount = ReadU16At(originalBlob, 10u);
    const std::size_t microcodeBytes = static_cast<std::size_t>(instructionCount) * 16u;
    Require(static_cast<std::uint64_t>(attributeOffset) + static_cast<std::uint64_t>(attributeCount) * 12u
        <= oldConstantOffset, "test fixture attribute table overlaps the original constant-table offset");
    Require(oldConstantOffset <= oldMicrocodeOffset, "test fixture constant-table offset follows microcode");
    Require(oldMicrocodeOffset <= originalBlob.size() && microcodeBytes <= originalBlob.size() - oldMicrocodeOffset,
        "test fixture microcode bytes are truncated");
    Require(static_cast<std::size_t>(oldMicrocodeOffset) + microcodeBytes == originalBlob.size(),
        "test fixture has unexpected bytes after its original microcode");

    for (std::uint16_t ordinal = 0u; ordinal < attributeCount; ++ordinal) {
        const std::size_t record = static_cast<std::size_t>(attributeOffset)
            + static_cast<std::size_t>(ordinal) * 12u;
        const std::uint32_t nameOffset = ReadU32At(originalBlob, record);
        Require(nameOffset > 0u && nameOffset < oldMicrocodeOffset,
            "test fixture attribute name is outside the prefix preserved by relocation setup");
        bool terminatedBeforeMicrocode = false;
        for (std::size_t cursor = nameOffset; cursor < oldMicrocodeOffset; ++cursor) {
            if (originalBlob[cursor] == 0u) {
                terminatedBeforeMicrocode = true;
                break;
            }
        }
        Require(terminatedBeforeMicrocode,
            "test fixture attribute name does not terminate in the preserved prefix");
    }

    const std::vector<std::uint8_t> originalMicrocode(
        originalBlob.begin() + static_cast<std::ptrdiff_t>(oldMicrocodeOffset), originalBlob.end());
    const std::size_t constantOffset = oldMicrocodeOffset;
    const std::size_t constantEnd = constantOffset + elementRelocations.size() * 28u;
    const std::size_t newMicrocodeOffset = (constantEnd + 15u) & ~std::size_t(15u);
    std::vector<std::uint8_t> blob(
        originalBlob.begin(), originalBlob.begin() + static_cast<std::ptrdiff_t>(oldMicrocodeOffset));
    blob.resize(newMicrocodeOffset, 0u);
    WriteU16At(blob, 8u, static_cast<std::uint16_t>(elementRelocations.size()));
    WriteU32At(blob, 16u, static_cast<std::uint32_t>(constantOffset));
    WriteU32At(blob, 20u, static_cast<std::uint32_t>(newMicrocodeOffset));

    for (std::size_t index = 0u; index < elementRelocations.size(); ++index) {
        const std::size_t recordOffset = constantOffset + index * 28u;
        WriteU32At(blob, recordOffset, 0u);
        WriteU32At(blob, recordOffset + 4u, 0u);
        blob[recordOffset + 8u] = static_cast<std::uint8_t>(SdkFloat4);
        blob[recordOffset + 9u] = 1u;
        blob[recordOffset + 10u] = index == 0u
            ? static_cast<std::uint8_t>(elementRelocations.size())
            : 0u;
    }

    blob.insert(blob.end(), originalMicrocode.begin(), originalMicrocode.end());
    Require(blob.size() == newMicrocodeOffset + microcodeBytes,
        "test helper did not preserve the original microcode at its relocated offset");
    const std::size_t prefixEnd = oldMicrocodeOffset;
    Require(std::equal(originalBlob.begin(), originalBlob.begin() + 8, blob.begin())
        && std::equal(originalBlob.begin() + 10, originalBlob.begin() + 16, blob.begin() + 10)
        && std::equal(originalBlob.begin() + 24,
            originalBlob.begin() + static_cast<std::ptrdiff_t>(prefixEnd), blob.begin() + 24),
        "test helper changed the original prefix outside the intentionally updated constant-count and table offsets");

    for (std::size_t index = 0u; index < elementRelocations.size(); ++index) {
        const std::size_t recordOffset = constantOffset + index * 28u;
        Require(blob.size() <= std::numeric_limits<std::uint32_t>::max(),
            "test relocation table offset exceeds the SDK U32 field");
        WriteU32At(blob, recordOffset + 4u, static_cast<std::uint32_t>(blob.size()));
        AppendU32(blob, static_cast<std::uint32_t>(elementRelocations[index].size()));
        for (std::uint32_t relocation : elementRelocations[index]) AppendU32(blob, relocation);
    }
    std::vector<std::uint8_t> result = artifact;
    ReplaceStageBlob(result, 1u, blob);
    return result;
}

std::vector<std::uint8_t> AddMappedFragmentConstants(
    const std::vector<std::uint8_t>& artifact,
    const std::vector<std::vector<std::uint32_t>>& elementRelocations) {
    std::vector<std::uint8_t> result = AddInternalFragmentConstants(artifact, elementRelocations);
    WireLayout layout = ParseWireLayout(result);
    const StageLayout& pixel = layout.Stages[1];
    std::vector<std::uint8_t> sdk(
        result.begin() + static_cast<std::ptrdiff_t>(pixel.Blob),
        result.begin() + static_cast<std::ptrdiff_t>(pixel.BlobEnd));
    const std::uint32_t constantOffset = ReadU32At(sdk, 16u);
    const std::string bufferName = "ReaderRelocationOverlapBuffer";
    const std::string generatedName = bufferName + ".Values";
    Require(sdk.size() <= std::numeric_limits<std::uint32_t>::max(),
        "test SDK name offset exceeds its U32 field");
    WriteU32At(sdk, constantOffset, static_cast<std::uint32_t>(sdk.size()));
    for (std::size_t index = 0u; index < elementRelocations.size(); ++index) {
        sdk[constantOffset + index * 28u + 9u] = 0u;
    }
    sdk.insert(sdk.end(), generatedName.begin(), generatedName.end());
    sdk.push_back(0u);
    ReplaceStageBlob(result, 1u, sdk);

    layout = ParseWireLayout(result);
    const StageLayout& beforeBuffer = layout.Stages[1];
    for (const BufferField& buffer : beforeBuffer.Buffers) {
        Require(buffer.Name.Value != bufferName,
            "mapped relocation test buffer name conflicts with the producer fixture");
    }
    for (const UniformField& uniform : beforeBuffer.Uniforms) {
        Require(uniform.GeneratedName.Value != generatedName,
            "mapped relocation test uniform name conflicts with the producer fixture");
    }
    std::uint32_t bufferBinding = std::numeric_limits<std::uint32_t>::max();
    for (;;) {
        bool bindingInUse = false;
        for (const BufferField& buffer : beforeBuffer.Buffers) {
            if (buffer.AuthoredBinding == bufferBinding) {
                bindingInUse = true;
                break;
            }
        }
        if (!bindingInUse) break;
        Require(bufferBinding != 0u, "test helper exhausted the authored buffer-binding range");
        --bufferBinding;
    }
    std::vector<std::uint8_t> bufferRecord;
    AppendStringField(bufferRecord, bufferName);
    AppendU32(bufferRecord, bufferBinding);
    AppendU32(bufferRecord, 32u);
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(beforeBuffer.BuffersEnd),
        bufferRecord.begin(), bufferRecord.end());
    WriteU32At(result, beforeBuffer.BufferCount,
        static_cast<std::uint32_t>(beforeBuffer.Buffers.size() + 1u));
    UpdateArtifactTotal(result);

    layout = ParseWireLayout(result);
    const StageLayout& beforeUniform = layout.Stages[1];
    std::vector<std::uint8_t> uniformRecord;
    AppendStringField(uniformRecord, bufferName);
    AppendU32(uniformRecord, bufferBinding);
    AppendStringField(uniformRecord, "Values");
    AppendU32(uniformRecord, 0u);
    AppendU32(uniformRecord, 32u);
    AppendU32(uniformRecord, 16u);
    AppendU32(uniformRecord, 0u);
    AppendU32(uniformRecord, 0u);
    AppendStringField(uniformRecord, "FP3232");
    AppendU32(uniformRecord, 4u);
    AppendU32(uniformRecord, 1u);
    AppendU32(uniformRecord, 2u);
    AppendStringField(uniformRecord, generatedName);
    AppendU32(uniformRecord, SdkFloat4);
    AppendU32(uniformRecord, 2u);
    result.insert(result.begin() + static_cast<std::ptrdiff_t>(beforeUniform.End),
        uniformRecord.begin(), uniformRecord.end());
    WriteU32At(result, beforeUniform.UniformCount,
        static_cast<std::uint32_t>(beforeUniform.Uniforms.size() + 1u));
    UpdateArtifactTotal(result);
    return result;
}

bool LoadBoundedFile(const char* path, std::size_t minimum, std::size_t maximum,
    std::vector<std::uint8_t>& destination, std::string& error) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = std::string("cannot open fixture ") + path;
        return false;
    }
    std::streamoff length = stream.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) < minimum
        || static_cast<std::uint64_t>(length) > maximum) {
        error = std::string("fixture length is outside its bound: ") + path;
        return false;
    }
    stream.seekg(0, std::ios::beg);
    destination.resize(static_cast<std::size_t>(length));
    if (!destination.empty()) {
        stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    }
    if (!stream) {
        error = std::string("cannot read complete fixture ") + path;
        destination.clear();
        return false;
    }
    return true;
}

helengine::ps3::Ps3CookedShaderProgram MakeSentinelProgram() {
    helengine::ps3::Ps3CookedShaderProgram sentinel;
    sentinel.ShaderAssetId = "sentinel asset";
    sentinel.VertexProgramName = "sentinel.vs";
    sentinel.PixelProgramName = "sentinel.ps";
    sentinel.VariantName = "sentinel variant";
    sentinel.SourceHash = "sentinel hash";
    sentinel.ToolchainIdentity = "sentinel toolchain";
    sentinel.Vertex.Stage = 71u;
    sentinel.Vertex.EntryPoint = "sentinel vertex";
    sentinel.Vertex.Bytecode = {9u, 8u, 7u};
    helengine::ps3::Ps3CookedShaderBuffer buffer;
    buffer.BufferName = "sentinel buffer";
    buffer.AuthoredBinding = 72u;
    buffer.DeclaredByteSize = 73u;
    sentinel.Vertex.Buffers.push_back(buffer);
    helengine::ps3::Ps3CookedShaderUniform uniform;
    uniform.BufferName = "sentinel buffer";
    uniform.AuthoredBinding = 72u;
    uniform.MemberPath = "sentinel member";
    uniform.ByteOffset = 74u;
    uniform.ByteSize = 75u;
    uniform.ArrayStride = 76u;
    uniform.MatrixStride = 77u;
    uniform.RowMajor = 78u;
    uniform.ScalarType = "sentinel scalar";
    uniform.VectorWidth = 79u;
    uniform.Columns = 80u;
    uniform.ArrayLength = 81u;
    uniform.GeneratedName = "sentinel uniform";
    uniform.SdkDataType = 82u;
    uniform.SdkElementCount = 83u;
    sentinel.Vertex.Uniforms.push_back(uniform);
    sentinel.Pixel.Stage = 84u;
    sentinel.Pixel.EntryPoint = "sentinel pixel";
    sentinel.Pixel.Bytecode = {6u, 5u, 4u};
    helengine::ps3::Ps3CookedShaderAttribute attribute;
    attribute.FullAuthoredSemantic = "sentinel semantic";
    attribute.GeneratedName = "sentinel attribute";
    attribute.ScalarType = "sentinel scalar";
    attribute.VectorWidth = 85u;
    attribute.Columns = 86u;
    attribute.ArrayLength = 87u;
    attribute.HardwareSlot = 88u;
    attribute.SdkDataType = 89u;
    sentinel.Attributes.push_back(attribute);
    helengine::ps3::Ps3CookedShaderSampler sampler;
    sampler.TextureName = "sentinel texture";
    sampler.TextureBinding = 90u;
    sampler.SamplerName = "sentinel sampler";
    sampler.SamplerBinding = 91u;
    sampler.GeneratedName = "sentinel generated sampler";
    sampler.Dimension = "sentinel dimension";
    sampler.TextureUnit = 92u;
    sampler.SdkDataType = 93u;
    sentinel.PixelSamplers.push_back(sampler);
    return sentinel;
}

bool SameBuffer(const helengine::ps3::Ps3CookedShaderBuffer& left,
    const helengine::ps3::Ps3CookedShaderBuffer& right) {
    return left.BufferName == right.BufferName
        && left.AuthoredBinding == right.AuthoredBinding
        && left.DeclaredByteSize == right.DeclaredByteSize;
}

bool SameUniform(const helengine::ps3::Ps3CookedShaderUniform& left,
    const helengine::ps3::Ps3CookedShaderUniform& right) {
    return left.BufferName == right.BufferName
        && left.AuthoredBinding == right.AuthoredBinding
        && left.MemberPath == right.MemberPath
        && left.ByteOffset == right.ByteOffset
        && left.ByteSize == right.ByteSize
        && left.ArrayStride == right.ArrayStride
        && left.MatrixStride == right.MatrixStride
        && left.RowMajor == right.RowMajor
        && left.ScalarType == right.ScalarType
        && left.VectorWidth == right.VectorWidth
        && left.Columns == right.Columns
        && left.ArrayLength == right.ArrayLength
        && left.GeneratedName == right.GeneratedName
        && left.SdkDataType == right.SdkDataType
        && left.SdkElementCount == right.SdkElementCount;
}

bool SameAttribute(const helengine::ps3::Ps3CookedShaderAttribute& left,
    const helengine::ps3::Ps3CookedShaderAttribute& right) {
    return left.FullAuthoredSemantic == right.FullAuthoredSemantic
        && left.GeneratedName == right.GeneratedName
        && left.ScalarType == right.ScalarType
        && left.VectorWidth == right.VectorWidth
        && left.Columns == right.Columns
        && left.ArrayLength == right.ArrayLength
        && left.HardwareSlot == right.HardwareSlot
        && left.SdkDataType == right.SdkDataType;
}

bool SameSampler(const helengine::ps3::Ps3CookedShaderSampler& left,
    const helengine::ps3::Ps3CookedShaderSampler& right) {
    return left.TextureName == right.TextureName
        && left.TextureBinding == right.TextureBinding
        && left.SamplerName == right.SamplerName
        && left.SamplerBinding == right.SamplerBinding
        && left.GeneratedName == right.GeneratedName
        && left.Dimension == right.Dimension
        && left.TextureUnit == right.TextureUnit
        && left.SdkDataType == right.SdkDataType;
}

bool SameStage(const helengine::ps3::Ps3CookedShaderStage& left,
    const helengine::ps3::Ps3CookedShaderStage& right) {
    if (left.Stage != right.Stage || left.EntryPoint != right.EntryPoint || left.Bytecode != right.Bytecode
        || left.Buffers.size() != right.Buffers.size() || left.Uniforms.size() != right.Uniforms.size()) return false;
    for (std::size_t index = 0; index < left.Buffers.size(); ++index) {
        if (!SameBuffer(left.Buffers[index], right.Buffers[index])) return false;
    }
    for (std::size_t index = 0; index < left.Uniforms.size(); ++index) {
        if (!SameUniform(left.Uniforms[index], right.Uniforms[index])) return false;
    }
    return true;
}

bool SameProgram(const helengine::ps3::Ps3CookedShaderProgram& left,
    const helengine::ps3::Ps3CookedShaderProgram& right) {
    if (left.ShaderAssetId != right.ShaderAssetId
        || left.VertexProgramName != right.VertexProgramName
        || left.PixelProgramName != right.PixelProgramName
        || left.VariantName != right.VariantName
        || left.SourceHash != right.SourceHash
        || left.ToolchainIdentity != right.ToolchainIdentity
        || !SameStage(left.Vertex, right.Vertex)
        || !SameStage(left.Pixel, right.Pixel)
        || left.Attributes.size() != right.Attributes.size()
        || left.PixelSamplers.size() != right.PixelSamplers.size()) return false;
    for (std::size_t index = 0; index < left.Attributes.size(); ++index) {
        if (!SameAttribute(left.Attributes[index], right.Attributes[index])) return false;
    }
    for (std::size_t index = 0; index < left.PixelSamplers.size(); ++index) {
        if (!SameSampler(left.PixelSamplers[index], right.PixelSamplers[index])) return false;
    }
    return true;
}

void RequireRejected(const std::vector<std::uint8_t>& bytes) {
    helengine::ps3::Ps3CookedShaderProgram expected = MakeSentinelProgram();
    helengine::ps3::Ps3CookedShaderProgram actual = expected;
    std::string error;
    bool accepted = helengine::ps3::Ps3ShaderArtifactReader::Read(bytes.data(), bytes.size(), actual, error);
    Require(!accepted, "malformed artifact was accepted");
    Require(!error.empty(), "reader returned no diagnostic for malformed artifact");
    Require(SameProgram(expected, actual), "failed read changed the caller's output program");
}

void RequireRejectedWithError(const std::vector<std::uint8_t>& bytes, const std::string& expectedDiagnostic) {
    helengine::ps3::Ps3CookedShaderProgram expected = MakeSentinelProgram();
    helengine::ps3::Ps3CookedShaderProgram actual = expected;
    std::string error;
    bool accepted = helengine::ps3::Ps3ShaderArtifactReader::Read(bytes.data(), bytes.size(), actual, error);
    Require(!accepted, "malformed artifact was accepted");
    Require(error.find(expectedDiagnostic) != std::string::npos,
        "artifact was rejected for an unexpected reason: " + error);
    Require(SameProgram(expected, actual), "failed read changed the caller's output program");
}

void RequireAccepted(const std::vector<std::uint8_t>& bytes,
    helengine::ps3::Ps3CookedShaderProgram& result) {
    std::string error;
    Require(helengine::ps3::Ps3ShaderArtifactReader::Read(bytes.data(), bytes.size(), result, error), error);
    Require(error.empty(), "successful read returned a stale diagnostic");
}

void MutateStringBytes(std::vector<std::uint8_t>& bytes, const StringField& field,
    const std::vector<std::uint8_t>& replacement) {
    Require(replacement.size() <= field.Length, "test mutation does not fit its string field");
    std::copy(replacement.begin(), replacement.end(), bytes.begin() + static_cast<std::ptrdiff_t>(field.DataOffset));
}

void TruncateAndReject(std::size_t newSize) {
    std::vector<std::uint8_t> bytes = Artifact;
    Require(newSize <= bytes.size(), "test truncation extends the artifact");
    bytes.resize(newSize);
    UpdateArtifactTotal(bytes);
    RequireRejected(bytes);
}

void RunCase(const char* name, const std::function<void()>& test) {
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

void TestProducerFixture() {
    helengine::ps3::Ps3CookedShaderProgram program;
    RequireAccepted(Artifact, program);
    const std::string utf8AssetId = "native-fixture-\xC3\xA9";
    Require(program.ShaderAssetId == utf8AssetId, "UTF-8 shader asset identity did not survive decoding");
    Require(program.VertexProgramName == "native.vs", "vertex program identity differs from fixture");
    Require(program.PixelProgramName == "native.ps", "pixel program identity differs from fixture");
    Require(program.VariantName == "default", "variant identity differs from fixture");
    Require(program.SourceHash.size() == 64u, "source hash is not a 64-character SHA-256 value");
    Require(!program.ToolchainIdentity.empty(), "toolchain identity is empty");
    Require(program.Vertex.Stage == 0u && program.Pixel.Stage == 1u, "stage order differs from vertex then pixel");
    Require(program.Vertex.EntryPoint == "VS" && program.Pixel.EntryPoint == "PS", "stage entry point differs from fixture");
    Require(program.Vertex.Bytecode == RawVertex && program.Pixel.Bytecode == RawPixel,
        "decoded stage bytecode differs from the separately copied SDK programs");

    auto frame = std::find_if(program.Vertex.Buffers.begin(), program.Vertex.Buffers.end(),
        [](const helengine::ps3::Ps3CookedShaderBuffer& item) { return item.BufferName == "Frame"; });
    Require(frame != program.Vertex.Buffers.end(), "Frame buffer is missing");
    Require(frame->AuthoredBinding == 0u && frame->DeclaredByteSize == 80u, "Frame binding or declared size differs");
    auto transform = std::find_if(program.Vertex.Uniforms.begin(), program.Vertex.Uniforms.end(),
        [](const helengine::ps3::Ps3CookedShaderUniform& item) { return item.MemberPath == "Transform"; });
    auto tint = std::find_if(program.Vertex.Uniforms.begin(), program.Vertex.Uniforms.end(),
        [](const helengine::ps3::Ps3CookedShaderUniform& item) { return item.MemberPath == "Tint"; });
    Require(transform != program.Vertex.Uniforms.end() && tint != program.Vertex.Uniforms.end(), "Frame members are missing");
    Require(transform->ByteOffset == 0u && transform->ByteSize == 64u, "Transform source range differs");
    Require(transform->ArrayStride == 0u && transform->MatrixStride == 16u, "Transform stride metadata differs");
    Require(transform->RowMajor == 1u && transform->ScalarType == "FP3232", "Transform scalar or row-major flag differs");
    Require(transform->VectorWidth == 4u && transform->Columns == 4u && transform->ArrayLength == 1u,
        "Transform shape metadata differs");
    Require(transform->SdkDataType == 11u && transform->SdkElementCount == 4u,
        "Transform SDK type or element count differs");
    Require(tint->ByteOffset == 64u && tint->ByteSize == 16u, "Tint source range differs");
    Require(tint->ScalarType == "FP3232" && tint->VectorWidth == 4u && tint->Columns == 1u && tint->ArrayLength == 1u,
        "Tint shape metadata differs");
    Require(tint->SdkDataType == 9u && tint->SdkElementCount == 1u,
        "Tint SDK type or element count differs");

    auto position = std::find_if(program.Attributes.begin(), program.Attributes.end(),
        [](const helengine::ps3::Ps3CookedShaderAttribute& item) { return item.FullAuthoredSemantic == "POSITION"; });
    auto uv = std::find_if(program.Attributes.begin(), program.Attributes.end(),
        [](const helengine::ps3::Ps3CookedShaderAttribute& item) { return item.FullAuthoredSemantic == "TEXCOORD0"; });
    Require(position != program.Attributes.end() && uv != program.Attributes.end(), "POSITION/TEXCOORD0 semantics are missing");
    Require(position->ScalarType == "FP3232" && position->VectorWidth == 3u && position->Columns == 1u
        && position->ArrayLength == 1u, "POSITION source shape differs");
    Require(uv->ScalarType == "FP3232" && uv->VectorWidth == 2u && uv->Columns == 1u
        && uv->ArrayLength == 1u, "TEXCOORD0 source shape differs");

    auto sampler = std::find_if(program.PixelSamplers.begin(), program.PixelSamplers.end(),
        [](const helengine::ps3::Ps3CookedShaderSampler& item) { return item.TextureName == "Albedo"; });
    Require(sampler != program.PixelSamplers.end(), "Albedo sampler mapping is missing");
    Require(sampler->TextureBinding == 0u && sampler->SamplerName == "LinearSampler"
        && sampler->SamplerBinding == 0u && sampler->Dimension == "Dim2D"
        && sampler->TextureUnit == 0u && sampler->SdkDataType == 15u,
        "Albedo/LinearSampler SDK mapping differs");
}

void TestWrongMagic() {
    std::vector<std::uint8_t> bytes = Artifact;
    bytes[0] = 0u;
    RequireRejected(bytes);
}

void TestWrongVersion() {
    std::vector<std::uint8_t> bytes = Artifact;
    WriteU32At(bytes, 4u, 2u);
    RequireRejected(bytes);
}

void TestWrongTotalLength() {
    std::vector<std::uint8_t> bytes = Artifact;
    WriteU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size() - 1u));
    RequireRejected(bytes);
}

void TestTrailingByte() {
    std::vector<std::uint8_t> bytes = Artifact;
    bytes.push_back(0u);
    UpdateArtifactTotal(bytes);
    RequireRejected(bytes);
}

void TestDeclaredInputTooLarge() {
    helengine::ps3::Ps3CookedShaderProgram expected = MakeSentinelProgram();
    helengine::ps3::Ps3CookedShaderProgram actual = expected;
    std::string error;
    bool accepted = helengine::ps3::Ps3ShaderArtifactReader::Read(
        Artifact.data(), MaximumArtifactBytes + 1u, actual, error);
    Require(!accepted && !error.empty(), "reader accepted an input length beyond the artifact cap");
    Require(SameProgram(expected, actual), "oversized input changed the caller's output program");
}

void TestOversizedString() {
    std::vector<std::uint8_t> bytes = Artifact;
    WriteU32At(bytes, 12u, 65537u);
    RequireRejected(bytes);
}

void TestOversizedBlob() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    WriteU32At(bytes, layout.Stages[0].BlobLength, static_cast<std::uint32_t>(MaximumStageBytes + 1u));
    RequireRejected(bytes);
}

void TestOversizedList() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    WriteU32At(bytes, layout.Stages[0].BufferCount, MaximumRecords + 1u);
    RequireRejected(bytes);
}

void TestNulString() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    bytes[layout.ShaderAssetId.DataOffset] = 0u;
    RequireRejected(bytes);
}

void TestOverlongUtf8() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    bytes[layout.ShaderAssetId.DataOffset] = 0xC0u;
    bytes[layout.ShaderAssetId.DataOffset + 1u] = 0xAFu;
    RequireRejected(bytes);
}

void TestSurrogateUtf8() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    bytes[layout.ShaderAssetId.DataOffset] = 0xEDu;
    bytes[layout.ShaderAssetId.DataOffset + 1u] = 0xA0u;
    bytes[layout.ShaderAssetId.DataOffset + 2u] = 0x80u;
    RequireRejected(bytes);
}

void TestTruncatedUtf8() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    bytes[layout.ShaderAssetId.DataOffset + layout.ShaderAssetId.Length - 1u] = 0xC3u;
    RequireRejected(bytes);
}

void TestEmptyRequiredField() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    std::fill(bytes.begin() + static_cast<std::ptrdiff_t>(layout.ShaderAssetId.DataOffset),
        bytes.begin() + static_cast<std::ptrdiff_t>(layout.ShaderAssetId.DataOffset + layout.ShaderAssetId.Length),
        static_cast<std::uint8_t>(' '));
    RequireRejected(bytes);
}

void TestWrongVertexStage() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    WriteU32At(bytes, layout.Stages[0].Stage, 1u);
    RequireRejected(bytes);
}

void TestWrongPixelStage() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    WriteU32At(bytes, layout.Stages[1].Stage, 0u);
    RequireRejected(bytes);
}

void TestInvalidRowMajorFlag() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Stages[0].Uniforms.empty(), "fixture has no vertex uniform for flag mutation");
    WriteU32At(bytes, layout.Stages[0].Uniforms[0].RowMajor, 2u);
    RequireRejected(bytes);
}

void TestUnknownScalarType() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Stages[0].Uniforms.empty(), "fixture has no vertex uniform for scalar mutation");
    MutateStringBytes(bytes, layout.Stages[0].Uniforms[0].ScalarType,
        {static_cast<std::uint8_t>('X')});
    RequireRejected(bytes);
}

void TestUnknownSamplerDimension() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Samplers.empty(), "fixture has no sampler for dimension mutation");
    bytes[layout.Samplers[0].Dimension.DataOffset] = static_cast<std::uint8_t>('X');
    RequireRejected(bytes);
}

void TestDuplicateBuffer() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    const StageLayout& vertex = layout.Stages[0];
    Require(!vertex.Buffers.empty(), "fixture has no buffer to duplicate");
    const BufferField& source = vertex.Buffers[0];
    std::vector<std::uint8_t> duplicate(
        bytes.begin() + static_cast<std::ptrdiff_t>(source.Begin),
        bytes.begin() + static_cast<std::ptrdiff_t>(source.End));
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(vertex.BuffersEnd), duplicate.begin(), duplicate.end());
    WriteU32At(bytes, vertex.BufferCount, static_cast<std::uint32_t>(vertex.Buffers.size() + 1u));
    UpdateArtifactTotal(bytes);
    RequireRejected(bytes);
}

void TestDuplicateAttributeSlot() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(layout.Attributes.size() >= 2u, "fixture has fewer than two attributes");
    WriteU32At(bytes, layout.Attributes[1].HardwareSlot,
        ReadU32At(bytes, layout.Attributes[0].HardwareSlot));
    RequireRejected(bytes);
}

void TestDuplicateSamplerUnit() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Samplers.empty(), "fixture has no sampler to duplicate");
    const SamplerField& source = layout.Samplers[0];
    std::vector<std::uint8_t> duplicate(
        bytes.begin() + static_cast<std::ptrdiff_t>(source.Begin),
        bytes.begin() + static_cast<std::ptrdiff_t>(source.End));
    bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(layout.SamplersEnd), duplicate.begin(), duplicate.end());
    WriteU32At(bytes, layout.SamplerCount, static_cast<std::uint32_t>(layout.Samplers.size() + 1u));
    UpdateArtifactTotal(bytes);
    RequireRejected(bytes);
}

void TestUniformRangeOverflow() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Stages[0].Uniforms.empty(), "fixture has no vertex uniform for range mutation");
    WriteU32At(bytes, layout.Stages[0].Uniforms[0].ByteOffset, std::numeric_limits<std::uint32_t>::max());
    RequireRejected(bytes);
}

void MutateVertexSdk(std::vector<std::uint8_t>& artifact,
    const std::function<void(std::vector<std::uint8_t>&)>& mutation) {
    WireLayout layout = ParseWireLayout(artifact);
    const StageLayout& vertex = layout.Stages[0];
    std::vector<std::uint8_t> sdk(
        artifact.begin() + static_cast<std::ptrdiff_t>(vertex.Blob),
        artifact.begin() + static_cast<std::ptrdiff_t>(vertex.BlobEnd));
    mutation(sdk);
    ReplaceStageBlob(artifact, 0u, sdk);
}

void TestSdkBadMagic() {
    std::vector<std::uint8_t> bytes = Artifact;
    MutateVertexSdk(bytes, [](std::vector<std::uint8_t>& sdk) { WriteU16At(sdk, 0u, 0x4650u); });
    RequireRejected(bytes);
}

void TestSdkTableOverlap() {
    std::vector<std::uint8_t> bytes = Artifact;
    MutateVertexSdk(bytes, [](std::vector<std::uint8_t>& sdk) {
        WriteU32At(sdk, 16u, ReadU32At(sdk, 12u));
    });
    RequireRejected(bytes);
}

void TestSdkNameUnterminated() {
    std::vector<std::uint8_t> bytes = Artifact;
    MutateVertexSdk(bytes, [](std::vector<std::uint8_t>& sdk) {
        std::uint32_t attributeOffset = ReadU32At(sdk, 12u);
        std::size_t unterminated = sdk.size() - 1u;
        WriteU32At(sdk, attributeOffset, static_cast<std::uint32_t>(unterminated));
        sdk[unterminated] = static_cast<std::uint8_t>('X');
    });
    RequireRejected(bytes);
}

void TestSdkConstantContinuationOverrun() {
    std::vector<std::uint8_t> bytes = Artifact;
    MutateVertexSdk(bytes, [](std::vector<std::uint8_t>& sdk) {
        std::size_t constantOffset = ReadU32At(sdk, 16u);
        std::uint16_t count = ReadU16At(sdk, 8u);
        sdk[constantOffset + 10u] = static_cast<std::uint8_t>(count + 1u);
    });
    RequireRejected(bytes);
}

void TestMissingGeneratedName() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Stages[0].Uniforms.empty(), "fixture has no uniform generated name");
    bytes[layout.Stages[0].Uniforms[0].GeneratedName.DataOffset] = static_cast<std::uint8_t>('X');
    RequireRejected(bytes);
}

void TestMismatchedSdkType() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Stages[0].Uniforms.empty(), "fixture has no uniform SDK type");
    WriteU32At(bytes, layout.Stages[0].Uniforms[0].SdkDataType, 255u);
    RequireRejected(bytes);
}

void TestMismatchedSdkIndex() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Attributes.empty(), "fixture has no attribute SDK index");
    WriteU32At(bytes, layout.Attributes[0].HardwareSlot, 15u);
    RequireRejected(bytes);
}

void TestUnmappedNonInternalSdkConstant() {
    std::vector<std::uint8_t> bytes = Artifact;
    WireLayout layout = ParseWireLayout(bytes);
    const StageLayout& vertex = layout.Stages[0];
    auto mapping = std::find_if(vertex.Uniforms.begin(), vertex.Uniforms.end(),
        [](const UniformField& uniform) { return uniform.MemberPath.Value == "Transform"; });
    Require(mapping != vertex.Uniforms.end(), "fixture has no real Transform mapping to remove");
    const std::string generatedName = mapping->GeneratedName.Value;
    const std::vector<std::uint8_t> originalSdk(
        bytes.begin() + static_cast<std::ptrdiff_t>(vertex.Blob),
        bytes.begin() + static_cast<std::ptrdiff_t>(vertex.BlobEnd));

    bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(mapping->Begin),
        bytes.begin() + static_cast<std::ptrdiff_t>(mapping->End));
    WriteU32At(bytes, vertex.UniformCount, static_cast<std::uint32_t>(vertex.Uniforms.size() - 1u));
    UpdateArtifactTotal(bytes);

    WireLayout mutatedLayout = ParseWireLayout(bytes);
    const StageLayout& mutatedVertex = mutatedLayout.Stages[0];
    const std::vector<std::uint8_t> preservedSdk(
        bytes.begin() + static_cast<std::ptrdiff_t>(mutatedVertex.Blob),
        bytes.begin() + static_cast<std::ptrdiff_t>(mutatedVertex.BlobEnd));
    Require(preservedSdk == originalSdk, "removing the artifact mapping changed the SDK program bytes");
    RequireRejectedWithError(bytes,
        "vertex SDK constant group " + generatedName + " has an unmapped non-internal element");
}

/// Removes only one P3SP input mapping, retaining the complete raw SDK vertex program.
void TestUnmappedSdkVertexInput() {
    std::vector<std::uint8_t> bytes = Artifact;
    const WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Attributes.empty(), "fixture has no vertex input mapping to remove");
    const AttributeField& mapping = layout.Attributes.front();
    const std::string name = mapping.GeneratedName.Value;
    const std::vector<std::uint8_t> sdk(bytes.begin() + layout.Stages[0].Blob,
        bytes.begin() + layout.Stages[0].BlobEnd);
    bytes.erase(bytes.begin() + mapping.Begin, bytes.begin() + mapping.End);
    WriteU32At(bytes, layout.AttributeCount, static_cast<std::uint32_t>(layout.Attributes.size() - 1u));
    UpdateArtifactTotal(bytes);
    const WireLayout changed = ParseWireLayout(bytes);
    Require(std::vector<std::uint8_t>(bytes.begin() + changed.Stages[0].Blob,
        bytes.begin() + changed.Stages[0].BlobEnd) == sdk, "vertex mapping removal changed SDK bytes");
    RequireRejectedWithError(bytes, "vertex SDK input " + name + " has no artifact mapping");
}

/// Removes only one P3SP sampler mapping, retaining the complete raw SDK pixel program.
void TestUnmappedSdkPixelSampler() {
    std::vector<std::uint8_t> bytes = Artifact;
    const WireLayout layout = ParseWireLayout(bytes);
    Require(!layout.Samplers.empty(), "fixture has no pixel sampler mapping to remove");
    const SamplerField& mapping = layout.Samplers.front();
    const std::string name = mapping.GeneratedName.Value;
    const std::vector<std::uint8_t> sdk(bytes.begin() + layout.Stages[1].Blob,
        bytes.begin() + layout.Stages[1].BlobEnd);
    bytes.erase(bytes.begin() + mapping.Begin, bytes.begin() + mapping.End);
    WriteU32At(bytes, layout.SamplerCount, static_cast<std::uint32_t>(layout.Samplers.size() - 1u));
    UpdateArtifactTotal(bytes);
    const WireLayout changed = ParseWireLayout(bytes);
    Require(std::vector<std::uint8_t>(bytes.begin() + changed.Stages[1].Blob,
        bytes.begin() + changed.Stages[1].BlobEnd) == sdk, "sampler mapping removal changed SDK bytes");
    RequireRejectedWithError(bytes, "pixel SDK sampler " + name + " has no artifact mapping");
}

void TestFragmentRelocationOutOfBounds() {
    std::vector<std::uint8_t> bytes = AddInternalFragmentConstants(Artifact, {{32u}});
    RequireRejectedWithError(bytes, "fragment relocation does not reference a complete aligned constant write in microcode");
}

void TestFragmentFullWidthOverflow() {
    std::vector<std::uint8_t> bytes = AddInternalFragmentConstants(Artifact, {{28u}});
    RequireRejectedWithError(bytes, "fragment relocation does not reference a complete aligned constant write in microcode");
}

void TestRepeatedRelocationsWithinOneElement() {
    std::vector<std::uint8_t> bytes = AddInternalFragmentConstants(Artifact, {{0u, 0u}});
    helengine::ps3::Ps3CookedShaderProgram program;
    RequireAccepted(bytes, program);
    Require(!program.Pixel.Bytecode.empty(), "accepted pixel stage lost its SDK program");
}

void TestOverlappingRelocationsAcrossElements() {
    std::vector<std::uint8_t> bytes = AddMappedFragmentConstants(Artifact, {{0u}, {0u}});
    RequireRejectedWithError(bytes, "different active pixel uniform elements overlap SDK relocation write ranges");
}

void TestTruncatedHeader() {
    TruncateAndReject(11u);
}

void TestTruncatedIdentity() {
    TruncateAndReject(14u);
}

void TestTruncatedVertexStage() {
    WireLayout layout = ParseWireLayout(Artifact);
    TruncateAndReject(layout.Stages[0].Begin + 5u);
}

void TestTruncatedVertexBlob() {
    WireLayout layout = ParseWireLayout(Artifact);
    TruncateAndReject(layout.Stages[0].Blob + layout.Stages[0].BlobSize / 2u);
}

void TestTruncatedPixelStage() {
    WireLayout layout = ParseWireLayout(Artifact);
    TruncateAndReject(layout.Stages[1].Begin + 5u);
}

void TestTruncatedAttributes() {
    WireLayout layout = ParseWireLayout(Artifact);
    TruncateAndReject(layout.AttributesBegin + 2u);
}

void TestTruncatedSamplers() {
    WireLayout layout = ParseWireLayout(Artifact);
    TruncateAndReject(layout.SamplersBegin + 2u);
}

void TestNullInput() {
    helengine::ps3::Ps3CookedShaderProgram expected = MakeSentinelProgram();
    helengine::ps3::Ps3CookedShaderProgram actual = expected;
    std::string error;
    bool accepted = helengine::ps3::Ps3ShaderArtifactReader::Read(nullptr, Artifact.size(), actual, error);
    Require(!accepted && !error.empty(), "reader accepted a null input pointer");
    Require(SameProgram(expected, actual), "null input changed the caller's output program");
}

} // namespace

int main() {
    std::string error;
    if (!LoadBoundedFile("/app_home/USRDIR/content/fixture.ps3sp", 12u, MaximumArtifactBytes, Artifact, error)
        || !LoadBoundedFile("/app_home/USRDIR/content/vertex.bin", 1u, MaximumStageBytes, RawVertex, error)
        || !LoadBoundedFile("/app_home/USRDIR/content/pixel.bin", 1u, MaximumStageBytes, RawPixel, error)) {
        std::printf("FAIL fixture-load: %s\n", error.c_str());
        std::printf("PS3_SHADER_READER_TESTS passed=0 failed=1\n");
        return 1;
    }

    RunCase("producer-fixture-positive", TestProducerFixture);
    RunCase("wrong-magic", TestWrongMagic);
    RunCase("wrong-version", TestWrongVersion);
    RunCase("wrong-total-length", TestWrongTotalLength);
    RunCase("trailing-byte", TestTrailingByte);
    RunCase("input-over-sixteen-megabytes", TestDeclaredInputTooLarge);
    RunCase("oversized-string", TestOversizedString);
    RunCase("oversized-stage-blob", TestOversizedBlob);
    RunCase("oversized-list-count", TestOversizedList);
    RunCase("embedded-nul", TestNulString);
    RunCase("overlong-utf8", TestOverlongUtf8);
    RunCase("utf8-surrogate", TestSurrogateUtf8);
    RunCase("truncated-utf8-sequence", TestTruncatedUtf8);
    RunCase("empty-required-identity", TestEmptyRequiredField);
    RunCase("wrong-vertex-stage", TestWrongVertexStage);
    RunCase("wrong-pixel-stage", TestWrongPixelStage);
    RunCase("invalid-row-major-flag", TestInvalidRowMajorFlag);
    RunCase("unknown-scalar-type", TestUnknownScalarType);
    RunCase("unknown-sampler-dimension", TestUnknownSamplerDimension);
    RunCase("duplicate-buffer", TestDuplicateBuffer);
    RunCase("duplicate-attribute-slot", TestDuplicateAttributeSlot);
    RunCase("duplicate-sampler-unit", TestDuplicateSamplerUnit);
    RunCase("uniform-range-overflow", TestUniformRangeOverflow);
    RunCase("sdk-bad-magic", TestSdkBadMagic);
    RunCase("sdk-table-overlap", TestSdkTableOverlap);
    RunCase("sdk-unterminated-name", TestSdkNameUnterminated);
    RunCase("sdk-constant-continuation-overrun", TestSdkConstantContinuationOverrun);
    RunCase("missing-generated-name", TestMissingGeneratedName);
    RunCase("mismatched-sdk-type", TestMismatchedSdkType);
    RunCase("mismatched-sdk-index", TestMismatchedSdkIndex);
    RunCase("unmapped-non-internal-sdk-constant", TestUnmappedNonInternalSdkConstant);
    RunCase("unmapped-sdk-vertex-input", TestUnmappedSdkVertexInput);
    RunCase("unmapped-sdk-pixel-sampler", TestUnmappedSdkPixelSampler);
    RunCase("fragment-relocation-out-of-bounds", TestFragmentRelocationOutOfBounds);
    RunCase("fragment-relocation-full-width-overflow", TestFragmentFullWidthOverflow);
    RunCase("same-element-duplicate-relocations", TestRepeatedRelocationsWithinOneElement);
    RunCase("cross-element-relocation-overlap", TestOverlappingRelocationsAcrossElements);
    RunCase("truncated-header", TestTruncatedHeader);
    RunCase("truncated-identity", TestTruncatedIdentity);
    RunCase("truncated-vertex-stage", TestTruncatedVertexStage);
    RunCase("truncated-vertex-blob", TestTruncatedVertexBlob);
    RunCase("truncated-pixel-stage", TestTruncatedPixelStage);
    RunCase("truncated-attribute-list", TestTruncatedAttributes);
    RunCase("truncated-sampler-list", TestTruncatedSamplers);
    RunCase("null-input", TestNullInput);

    std::printf("PS3_SHADER_READER_TESTS passed=%lu failed=%lu\n",
        static_cast<unsigned long>(PassedCases), static_cast<unsigned long>(FailedCases));
    return FailedCases == 0u ? 0 : 1;
}
