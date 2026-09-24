#include "Ps3ShaderArtifactReader.hpp"

#include "Ps3RsxProgramValidator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace helengine::ps3 {
namespace {

/// <summary>Maximum encoded byte length accepted for one P3SP string.</summary>
constexpr std::uint32_t MaximumStringBytes = 65536u;

/// <summary>Maximum SDK program blob size accepted for one shader stage.</summary>
constexpr std::uint32_t MaximumStageBytes = 1024u * 1024u;

/// <summary>Maximum count accepted for each serialized metadata list.</summary>
constexpr std::uint32_t MaximumRecordCount = 4096u;

/// <summary>Maximum total artifact size accepted by the version-one reader.</summary>
constexpr std::size_t MaximumArtifactBytes = 16u * 1024u * 1024u;

/// <summary>Decodes one unsigned 32-bit big-endian value after proving its bytes remain in the input.</summary>
/// <param name="data">Complete P3SP input bytes.</param>
/// <param name="size">Bounded input size.</param>
/// <param name="cursor">Current offset, advanced by four bytes on success.</param>
/// <param name="resource">Field name included in truncation diagnostics.</param>
/// <returns>The decoded unsigned value.</returns>
std::uint32_t ReadU32(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, const char* resource) {
    if (cursor > size || size - cursor < 4u) {
        throw std::runtime_error(std::string(resource) + " is truncated");
    }
    const std::uint32_t value = (static_cast<std::uint32_t>(data[cursor]) << 24u)
        | (static_cast<std::uint32_t>(data[cursor + 1u]) << 16u)
        | (static_cast<std::uint32_t>(data[cursor + 2u]) << 8u)
        | static_cast<std::uint32_t>(data[cursor + 3u]);
    cursor += 4u;
    return value;
}

/// <summary>Decodes one Unicode scalar from a previously bounded UTF-8 field.</summary>
/// <param name="data">Start of the encoded field.</param>
/// <param name="length">Exact field byte length.</param>
/// <param name="cursor">Current field offset, advanced on success.</param>
/// <param name="codePoint">Receives the decoded Unicode scalar.</param>
/// <returns>True when a strict UTF-8 scalar was decoded.</returns>
bool ReadUtf8CodePoint(const std::uint8_t* data, std::size_t length,
    std::size_t& cursor, std::uint32_t& codePoint) {
    if (cursor >= length) return false;
    const std::uint8_t first = data[cursor++];
    if (first == 0u) return false;
    if (first <= 0x7Fu) {
        codePoint = first;
        return true;
    }

    std::size_t continuationCount = 0u;
    std::uint32_t value = 0u;
    std::uint32_t minimumValue = 0u;
    if (first >= 0xC2u && first <= 0xDFu) {
        continuationCount = 1u;
        value = first & 0x1Fu;
        minimumValue = 0x80u;
    } else if (first >= 0xE0u && first <= 0xEFu) {
        continuationCount = 2u;
        value = first & 0x0Fu;
        minimumValue = 0x800u;
    } else if (first >= 0xF0u && first <= 0xF4u) {
        continuationCount = 3u;
        value = first & 0x07u;
        minimumValue = 0x10000u;
    } else {
        return false;
    }
    if (continuationCount > length - cursor) return false;
    for (std::size_t index = 0; index < continuationCount; ++index) {
        const std::uint8_t next = data[cursor++];
        if ((next & 0xC0u) != 0x80u) return false;
        value = (value << 6u) | (next & 0x3Fu);
    }
    if (value < minimumValue || value > 0x10FFFFu
        || (value >= 0xD800u && value <= 0xDFFFu)) return false;
    codePoint = value;
    return true;
}

/// <summary>Identifies Unicode whitespace code points that make a required field blank.</summary>
/// <param name="codePoint">Decoded Unicode scalar.</param>
/// <returns>True for the Unicode White_Space set used by the artifact writer.</returns>
bool IsWhitespace(std::uint32_t codePoint) {
    return (codePoint >= 0x0009u && codePoint <= 0x000Du)
        || codePoint == 0x0020u || codePoint == 0x0085u || codePoint == 0x00A0u
        || codePoint == 0x1680u || (codePoint >= 0x2000u && codePoint <= 0x200Au)
        || codePoint == 0x2028u || codePoint == 0x2029u || codePoint == 0x202Fu
        || codePoint == 0x205Fu || codePoint == 0x3000u;
}

/// <summary>Validates and copies one required strict UTF-8 string with a bounded byte length.</summary>
/// <param name="data">Complete artifact bytes.</param>
/// <param name="size">Bounded artifact size.</param>
/// <param name="cursor">Current offset, advanced past the string on success.</param>
/// <param name="resource">Field name included in diagnostics.</param>
/// <returns>An owning byte-preserving UTF-8 string.</returns>
std::string ReadString(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, const char* resource) {
    const std::uint32_t length = ReadU32(data, size, cursor, resource);
    if (length == 0u || length > MaximumStringBytes) {
        throw std::runtime_error(std::string(resource) + " length is outside 1 through 65536 bytes");
    }
    if (cursor > size || static_cast<std::size_t>(length) > size - cursor) {
        throw std::runtime_error(std::string(resource) + " bytes are truncated");
    }
    const std::size_t stringStart = cursor;
    const std::size_t stringLength = static_cast<std::size_t>(length);
    std::size_t utf8Cursor = 0u;
    bool hasNonWhitespace = false;
    while (utf8Cursor < stringLength) {
        std::uint32_t codePoint = 0u;
        if (!ReadUtf8CodePoint(data + stringStart, stringLength, utf8Cursor, codePoint)) {
            throw std::runtime_error(std::string(resource) + " is not strict UTF-8 or contains NUL");
        }
        if (!IsWhitespace(codePoint)) hasNonWhitespace = true;
    }
    if (!hasNonWhitespace) throw std::runtime_error(std::string(resource) + " is blank");
    std::string value(reinterpret_cast<const char*>(data + stringStart), stringLength);
    cursor += stringLength;
    return value;
}

/// <summary>Reads a bounded list count before reserving storage for its records.</summary>
/// <param name="data">Complete artifact bytes.</param>
/// <param name="size">Bounded artifact size.</param>
/// <param name="cursor">Current offset, advanced by the count field.</param>
/// <param name="resource">List name included in diagnostics.</param>
/// <returns>A count no larger than the version-one list cap.</returns>
std::uint32_t ReadCount(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, const char* resource) {
    const std::uint32_t count = ReadU32(data, size, cursor, resource);
    if (count > MaximumRecordCount) {
        throw std::runtime_error(std::string(resource) + " count exceeds 4096 records");
    }
    return count;
}

/// <summary>Reads one required SDK program blob without allocating until its length is bounded.</summary>
/// <param name="data">Complete artifact bytes.</param>
/// <param name="size">Bounded artifact size.</param>
/// <param name="cursor">Current offset, advanced past the blob on success.</param>
/// <param name="resource">Stage label included in diagnostics.</param>
/// <returns>An owned byte-for-byte copy of the program blob.</returns>
std::vector<std::uint8_t> ReadBlob(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, const char* resource) {
    const std::uint32_t length = ReadU32(data, size, cursor, resource);
    if (length == 0u || length > MaximumStageBytes) {
        throw std::runtime_error(std::string(resource) + " blob length is outside 1 through 1048576 bytes");
    }
    if (cursor > size || static_cast<std::size_t>(length) > size - cursor) {
        throw std::runtime_error(std::string(resource) + " blob bytes are truncated");
    }
    const std::size_t blobLength = static_cast<std::size_t>(length);
    std::vector<std::uint8_t> blob(data + cursor, data + cursor + blobLength);
    cursor += blobLength;
    return blob;
}

/// <summary>Decodes one fixed-order stage record and all of its owned buffer and uniform metadata.</summary>
/// <param name="data">Complete artifact bytes.</param>
/// <param name="size">Bounded artifact size.</param>
/// <param name="cursor">Current body offset, advanced past the record.</param>
/// <param name="expectedStage">Required stage identity for this position.</param>
/// <param name="stageName">Human-readable stage label for errors.</param>
/// <returns>A fully populated stage candidate.</returns>
Ps3CookedShaderStage ReadStage(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, std::uint32_t expectedStage, const char* stageName) {
    Ps3CookedShaderStage stage{};
    const std::string stageContext = std::string(stageName) + " stage";
    stage.Stage = ReadU32(data, size, cursor, (stageContext + " identity").c_str());
    if (stage.Stage != expectedStage) {
        throw std::runtime_error(stageContext + " identity is not in vertex/pixel order");
    }
    stage.EntryPoint = ReadString(data, size, cursor, (stageContext + " entry point").c_str());
    stage.Bytecode = ReadBlob(data, size, cursor, (stageContext + " SDK").c_str());

    const std::uint32_t bufferCount = ReadCount(data, size, cursor, (stageContext + " buffer list").c_str());
    stage.Buffers.reserve(bufferCount);
    for (std::uint32_t index = 0u; index < bufferCount; ++index) {
        Ps3CookedShaderBuffer buffer{};
        const std::string resource = stageContext + " buffer " + std::to_string(index);
        buffer.BufferName = ReadString(data, size, cursor, (resource + " name").c_str());
        buffer.AuthoredBinding = ReadU32(data, size, cursor, (resource + " authored binding").c_str());
        buffer.DeclaredByteSize = ReadU32(data, size, cursor, (resource + " declared byte size").c_str());
        stage.Buffers.push_back(std::move(buffer));
    }

    const std::uint32_t uniformCount = ReadCount(data, size, cursor, (stageContext + " uniform list").c_str());
    stage.Uniforms.reserve(uniformCount);
    for (std::uint32_t index = 0u; index < uniformCount; ++index) {
        Ps3CookedShaderUniform uniform{};
        const std::string resource = stageContext + " uniform " + std::to_string(index);
        uniform.BufferName = ReadString(data, size, cursor, (resource + " buffer name").c_str());
        uniform.AuthoredBinding = ReadU32(data, size, cursor, (resource + " authored binding").c_str());
        uniform.MemberPath = ReadString(data, size, cursor, (resource + " member path").c_str());
        uniform.ByteOffset = ReadU32(data, size, cursor, (resource + " byte offset").c_str());
        uniform.ByteSize = ReadU32(data, size, cursor, (resource + " byte size").c_str());
        uniform.ArrayStride = ReadU32(data, size, cursor, (resource + " array stride").c_str());
        uniform.MatrixStride = ReadU32(data, size, cursor, (resource + " matrix stride").c_str());
        uniform.RowMajor = ReadU32(data, size, cursor, (resource + " row-major flag").c_str());
        uniform.ScalarType = ReadString(data, size, cursor, (resource + " scalar type").c_str());
        uniform.VectorWidth = ReadU32(data, size, cursor, (resource + " vector width").c_str());
        uniform.Columns = ReadU32(data, size, cursor, (resource + " matrix columns").c_str());
        uniform.ArrayLength = ReadU32(data, size, cursor, (resource + " array length").c_str());
        uniform.GeneratedName = ReadString(data, size, cursor, (resource + " generated name").c_str());
        uniform.SdkDataType = ReadU32(data, size, cursor, (resource + " SDK data type").c_str());
        uniform.SdkElementCount = ReadU32(data, size, cursor, (resource + " SDK element count").c_str());
        stage.Uniforms.push_back(std::move(uniform));
    }
    return stage;
}

/// <summary>Reads the final pair-level attribute and pixel-sampler mapping lists.</summary>
/// <param name="data">Complete artifact bytes.</param>
/// <param name="size">Bounded artifact size.</param>
/// <param name="cursor">Current body offset, advanced past both lists.</param>
/// <param name="program">Candidate receiving decoded mapping records.</param>
void ReadPairMappings(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, Ps3CookedShaderProgram& program) {
    const std::uint32_t attributeCount = ReadCount(data, size, cursor, "attribute list");
    program.Attributes.reserve(attributeCount);
    for (std::uint32_t index = 0u; index < attributeCount; ++index) {
        Ps3CookedShaderAttribute attribute{};
        const std::string resource = "attribute " + std::to_string(index);
        attribute.FullAuthoredSemantic = ReadString(data, size, cursor, (resource + " authored semantic").c_str());
        attribute.GeneratedName = ReadString(data, size, cursor, (resource + " generated name").c_str());
        attribute.ScalarType = ReadString(data, size, cursor, (resource + " scalar type").c_str());
        attribute.VectorWidth = ReadU32(data, size, cursor, (resource + " vector width").c_str());
        attribute.Columns = ReadU32(data, size, cursor, (resource + " columns").c_str());
        attribute.ArrayLength = ReadU32(data, size, cursor, (resource + " array length").c_str());
        attribute.HardwareSlot = ReadU32(data, size, cursor, (resource + " hardware slot").c_str());
        attribute.SdkDataType = ReadU32(data, size, cursor, (resource + " SDK data type").c_str());
        program.Attributes.push_back(std::move(attribute));
    }

    const std::uint32_t samplerCount = ReadCount(data, size, cursor, "pixel sampler list");
    program.PixelSamplers.reserve(samplerCount);
    for (std::uint32_t index = 0u; index < samplerCount; ++index) {
        Ps3CookedShaderSampler sampler{};
        const std::string resource = "pixel sampler " + std::to_string(index);
        sampler.TextureName = ReadString(data, size, cursor, (resource + " texture name").c_str());
        sampler.TextureBinding = ReadU32(data, size, cursor, (resource + " texture binding").c_str());
        sampler.SamplerName = ReadString(data, size, cursor, (resource + " sampler name").c_str());
        sampler.SamplerBinding = ReadU32(data, size, cursor, (resource + " sampler binding").c_str());
        sampler.GeneratedName = ReadString(data, size, cursor, (resource + " generated name").c_str());
        sampler.Dimension = ReadString(data, size, cursor, (resource + " dimension").c_str());
        sampler.TextureUnit = ReadU32(data, size, cursor, (resource + " texture unit").c_str());
        sampler.SdkDataType = ReadU32(data, size, cursor, (resource + " SDK data type").c_str());
        program.PixelSamplers.push_back(std::move(sampler));
    }
}

} // namespace

bool Ps3ShaderArtifactReader::Read(const std::uint8_t* data, std::size_t size,
    Ps3CookedShaderProgram& result, std::string& error) {
    try {
        if (data == nullptr) throw std::runtime_error("P3SP input pointer is null");
        if (size < 12u || size > MaximumArtifactBytes) {
            throw std::runtime_error("P3SP artifact length is outside 12 through 16777216 bytes");
        }
        if (data[0] != 0x50u || data[1] != 0x33u || data[2] != 0x53u || data[3] != 0x50u) {
            throw std::runtime_error("P3SP header magic is invalid");
        }
        std::size_t cursor = 4u;
        const std::uint32_t version = ReadU32(data, size, cursor, "P3SP version");
        if (version != 1u) throw std::runtime_error("P3SP version is unsupported");
        const std::uint32_t totalLength = ReadU32(data, size, cursor, "P3SP total length");
        if (totalLength != size) throw std::runtime_error("P3SP total length does not match the exact input size");

        Ps3CookedShaderProgram candidate{};
        candidate.ShaderAssetId = ReadString(data, size, cursor, "shader asset id");
        candidate.VertexProgramName = ReadString(data, size, cursor, "vertex program name");
        candidate.PixelProgramName = ReadString(data, size, cursor, "pixel program name");
        candidate.VariantName = ReadString(data, size, cursor, "variant name");
        candidate.SourceHash = ReadString(data, size, cursor, "source hash");
        candidate.ToolchainIdentity = ReadString(data, size, cursor, "toolchain identity");
        candidate.Vertex = ReadStage(data, size, cursor, 0u, "vertex");
        candidate.Pixel = ReadStage(data, size, cursor, 1u, "pixel");
        ReadPairMappings(data, size, cursor, candidate);
        if (cursor != size) throw std::runtime_error("P3SP artifact has trailing bytes after the pixel sampler list");

        std::string validationError;
        if (!Ps3RsxProgramValidator::Validate(candidate, validationError)) {
            throw std::runtime_error(validationError);
        }
        result = std::move(candidate);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "P3SP reader failed with a non-standard exception";
        return false;
    }
}

} // namespace helengine::ps3
