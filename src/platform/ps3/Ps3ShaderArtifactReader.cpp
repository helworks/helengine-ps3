#include "Ps3ShaderArtifactReader.hpp"

#include "Ps3ArtifactReadUtilities.hpp"
#include "Ps3RsxProgramValidator.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace helengine::ps3 {
namespace {

/// <summary>Maximum SDK program blob size accepted for one shader stage.</summary>
constexpr std::uint32_t MaximumStageBytes = 1024u * 1024u;

/// <summary>Maximum count accepted for each serialized metadata list.</summary>
constexpr std::uint32_t MaximumRecordCount = 4096u;

/// <summary>Maximum total artifact size accepted by the version-one reader.</summary>
constexpr std::size_t MaximumArtifactBytes = 16u * 1024u * 1024u;

/// <summary>Reads a bounded list count before reserving storage for its records.</summary>
/// <param name="data">Complete artifact bytes.</param>
/// <param name="size">Bounded artifact size.</param>
/// <param name="cursor">Current offset, advanced by the count field.</param>
/// <param name="resource">List name included in diagnostics.</param>
/// <returns>A count no larger than the version-one list cap.</returns>
std::uint32_t ReadCount(const std::uint8_t* data, std::size_t size,
    std::size_t& cursor, const char* resource) {
    const std::uint32_t count = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, resource);
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
    const std::uint32_t length = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, resource);
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
    stage.Stage = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (stageContext + " identity").c_str());
    if (stage.Stage != expectedStage) {
        throw std::runtime_error(stageContext + " identity is not in vertex/pixel order");
    }
    stage.EntryPoint = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (stageContext + " entry point").c_str());
    stage.Bytecode = ReadBlob(data, size, cursor, (stageContext + " SDK").c_str());

    const std::uint32_t bufferCount = ReadCount(data, size, cursor, (stageContext + " buffer list").c_str());
    stage.Buffers.reserve(bufferCount);
    for (std::uint32_t index = 0u; index < bufferCount; ++index) {
        Ps3CookedShaderBuffer buffer{};
        const std::string resource = stageContext + " buffer " + std::to_string(index);
        buffer.BufferName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " name").c_str());
        buffer.AuthoredBinding = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " authored binding").c_str());
        buffer.DeclaredByteSize = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " declared byte size").c_str());
        stage.Buffers.push_back(std::move(buffer));
    }

    const std::uint32_t uniformCount = ReadCount(data, size, cursor, (stageContext + " uniform list").c_str());
    stage.Uniforms.reserve(uniformCount);
    for (std::uint32_t index = 0u; index < uniformCount; ++index) {
        Ps3CookedShaderUniform uniform{};
        const std::string resource = stageContext + " uniform " + std::to_string(index);
        uniform.BufferName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " buffer name").c_str());
        uniform.AuthoredBinding = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " authored binding").c_str());
        uniform.MemberPath = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " member path").c_str());
        uniform.ByteOffset = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " byte offset").c_str());
        uniform.ByteSize = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " byte size").c_str());
        uniform.ArrayStride = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " array stride").c_str());
        uniform.MatrixStride = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " matrix stride").c_str());
        uniform.RowMajor = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " row-major flag").c_str());
        uniform.ScalarType = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " scalar type").c_str());
        uniform.VectorWidth = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " vector width").c_str());
        uniform.Columns = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " matrix columns").c_str());
        uniform.ArrayLength = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " array length").c_str());
        uniform.GeneratedName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " generated name").c_str());
        uniform.SdkDataType = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " SDK data type").c_str());
        uniform.SdkElementCount = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " SDK element count").c_str());
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
        attribute.FullAuthoredSemantic = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " authored semantic").c_str());
        attribute.GeneratedName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " generated name").c_str());
        attribute.ScalarType = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " scalar type").c_str());
        attribute.VectorWidth = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " vector width").c_str());
        attribute.Columns = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " columns").c_str());
        attribute.ArrayLength = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " array length").c_str());
        attribute.HardwareSlot = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " hardware slot").c_str());
        attribute.SdkDataType = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " SDK data type").c_str());
        program.Attributes.push_back(std::move(attribute));
    }

    const std::uint32_t samplerCount = ReadCount(data, size, cursor, "pixel sampler list");
    program.PixelSamplers.reserve(samplerCount);
    for (std::uint32_t index = 0u; index < samplerCount; ++index) {
        Ps3CookedShaderSampler sampler{};
        const std::string resource = "pixel sampler " + std::to_string(index);
        sampler.TextureName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " texture name").c_str());
        sampler.TextureBinding = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " texture binding").c_str());
        sampler.SamplerName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " sampler name").c_str());
        sampler.SamplerBinding = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " sampler binding").c_str());
        sampler.GeneratedName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " generated name").c_str());
        sampler.Dimension = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, (resource + " dimension").c_str());
        sampler.TextureUnit = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " texture unit").c_str());
        sampler.SdkDataType = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, (resource + " SDK data type").c_str());
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
        const std::uint32_t version = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, "P3SP version");
        if (version != 1u) throw std::runtime_error("P3SP version is unsupported");
        const std::uint32_t totalLength = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, "P3SP total length");
        if (totalLength != size) throw std::runtime_error("P3SP total length does not match the exact input size");

        Ps3CookedShaderProgram candidate{};
        candidate.ShaderAssetId = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "shader asset id");
        candidate.VertexProgramName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "vertex program name");
        candidate.PixelProgramName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "pixel program name");
        candidate.VariantName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "variant name");
        candidate.SourceHash = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "source hash");
        candidate.ToolchainIdentity = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "toolchain identity");
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
