#include "Ps3MaterialArtifactReader.hpp"

#include "Ps3ArtifactReadUtilities.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace helengine::ps3 {
namespace {

/// <summary>Maximum complete P3MT envelope size accepted by version one.</summary>
constexpr std::size_t MaximumMaterialBytes = 1024u * 1024u;

/// <summary>Only the three committed PS3 material surface flag bits are valid in version one.</summary>
constexpr std::uint32_t SupportedSurfaceFlags = 0x7u;

/// <summary>Fixed renderer family selected by the PS3 standard material schema.</summary>
constexpr const char* StandardRendererFamilyId = "ps3-standard";

/// <summary>Fixed shader asset identity selected by the PS3 standard material schema.</summary>
constexpr const char* StandardShaderAssetId = "ForwardStandardShader";

/// <summary>Fixed vertex program identity selected by the PS3 standard material schema.</summary>
constexpr const char* StandardVertexProgramName = "ForwardStandardShader.vs";

/// <summary>Fixed pixel program identity selected by the PS3 standard material schema.</summary>
constexpr const char* StandardPixelProgramName = "ForwardStandardShader.ps";

/// <summary>Fixed shader variant selected by the PS3 standard material schema.</summary>
constexpr const char* StandardVariantName = "ForwardStandard";

/// <summary>Canonical lookup path for the fixed ForwardStandard identity tuple.</summary>
constexpr const char* StandardShaderRelativePath =
    "shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp";

/// <summary>Requires an identity field to equal its one supported version-one value.</summary>
/// <param name="actual">Decoded or caller-owned identity field.</param>
/// <param name="expected">Fixed value selected by the committed material schema.</param>
/// <param name="resource">Field name included in the diagnostic.</param>
void RequireFixedValue(const std::string& actual, const char* expected, const char* resource) {
    if (actual != expected) {
        throw std::runtime_error(std::string(resource) + " is not the fixed PS3 standard identity");
    }
}

/// <summary>Validates a caller-owned required text value with the shared strict UTF-8 scalar decoder.</summary>
/// <param name="value">UTF-8 bytes stored in an owning string.</param>
/// <param name="resource">Field name included in validation diagnostics.</param>
void ValidateRequiredText(const std::string& value, const char* resource) {
    if (value.empty()) throw std::runtime_error(std::string(resource) + " is blank");
    const auto* data = reinterpret_cast<const std::uint8_t*>(value.data());
    std::size_t cursor = 0u;
    bool hasNonWhitespace = false;
    while (cursor < value.size()) {
        std::uint32_t codePoint = 0u;
        if (!Ps3ArtifactReadUtilities::ReadUtf8CodePoint(data, value.size(), cursor, codePoint)) {
            throw std::runtime_error(std::string(resource) + " is not strict UTF-8 or contains NUL");
        }
        if (!Ps3ArtifactReadUtilities::IsWhitespace(codePoint)) hasNonWhitespace = true;
    }
    if (!hasNonWhitespace) throw std::runtime_error(std::string(resource) + " is blank");
}

/// <summary>Validates one slash-separated texture path segment without normalizing its original spelling.</summary>
/// <param name="segment">Original encoded segment between forward slashes.</param>
/// <param name="resource">Field name included in validation diagnostics.</param>
void ValidateTextureSegment(const std::string& segment, const char* resource) {
    if (segment.empty()) throw std::runtime_error(std::string(resource) + " contains an empty path segment");
    if (segment == "." || segment == "..") {
        throw std::runtime_error(std::string(resource) + " contains a dot or dot-dot path segment");
    }
    const auto* data = reinterpret_cast<const std::uint8_t*>(segment.data());
    std::size_t cursor = 0u;
    bool hasNonWhitespace = false;
    while (cursor < segment.size()) {
        std::uint32_t codePoint = 0u;
        if (!Ps3ArtifactReadUtilities::ReadUtf8CodePoint(data, segment.size(), cursor, codePoint)) {
            throw std::runtime_error(std::string(resource) + " is not strict UTF-8 or contains NUL");
        }
        if (Ps3ArtifactReadUtilities::IsWhitespace(codePoint)) continue;
        hasNonWhitespace = true;
    }
    if (!hasNonWhitespace) throw std::runtime_error(std::string(resource) + " contains a whitespace-only path segment");
}

/// <summary>Validates the producer's relative texture path rules while preserving valid UTF-8 spelling.</summary>
/// <param name="path">Texture path decoded from the material or held by a caller-owned DTO.</param>
/// <param name="resource">Field name included in validation diagnostics.</param>
void ValidateTextureRelativePath(const std::string& path, const char* resource) {
    if (path.empty()) return;
    const auto* data = reinterpret_cast<const std::uint8_t*>(path.data());
    std::size_t cursor = 0u;
    std::size_t segmentStart = 0u;
    while (cursor < path.size()) {
        std::uint32_t codePoint = 0u;
        if (!Ps3ArtifactReadUtilities::ReadUtf8CodePoint(data, path.size(), cursor, codePoint)) {
            throw std::runtime_error(std::string(resource) + " is not strict UTF-8 or contains NUL");
        }
        if (codePoint == static_cast<std::uint32_t>('/')) {
            ValidateTextureSegment(path.substr(segmentStart, cursor - segmentStart - 1u), resource);
            segmentStart = cursor;
        } else if (codePoint == static_cast<std::uint32_t>('\\')
            || codePoint == static_cast<std::uint32_t>(':')) {
            throw std::runtime_error(std::string(resource) + " contains a backslash or colon");
        } else if (codePoint <= 0x001Fu || (codePoint >= 0x007Fu && codePoint <= 0x009Fu)) {
            throw std::runtime_error(std::string(resource) + " contains a control character");
        }
    }
    ValidateTextureSegment(path.substr(segmentStart), resource);
}

/// <summary>Checks every fixed schema identity and caller-owned value that could bypass the P3MT reader.</summary>
/// <param name="material">Material values that must still satisfy the fixed standard contract.</param>
void ValidateFixedMaterialContract(const Ps3CookedMaterial& material) {
    ValidateRequiredText(material.MaterialAssetId, "material asset id");
    RequireFixedValue(material.RendererFamilyId, StandardRendererFamilyId, "renderer family id");
    RequireFixedValue(material.ShaderAssetId, StandardShaderAssetId, "shader asset id");
    RequireFixedValue(material.VertexProgramName, StandardVertexProgramName, "vertex program name");
    RequireFixedValue(material.PixelProgramName, StandardPixelProgramName, "pixel program name");
    RequireFixedValue(material.VariantName, StandardVariantName, "variant name");
    RequireFixedValue(material.ShaderRelativePath, StandardShaderRelativePath, "shader relative path");
    if ((material.SurfaceFlags & ~SupportedSurfaceFlags) != 0u) {
        throw std::runtime_error("surface flags contain unsupported bits");
    }
    ValidateTextureRelativePath(material.DiffuseTextureRelativePath, "diffuse texture relative path");
}

/// <summary>Returns one stable nonempty diagnostic for unexpected non-standard exceptions.</summary>
/// <param name="error">Caller-owned diagnostic string.</param>
/// <param name="message">Readable operation-specific fallback.</param>
void SetUnknownFailure(std::string& error, const char* message) {
    error = message;
}

} // namespace

bool Ps3MaterialArtifactReader::Read(
    const std::uint8_t* data,
    std::size_t size,
    Ps3CookedMaterial& result,
    std::string& error) {
    try {
        if (data == nullptr) throw std::runtime_error("P3MT input is null");
        if (size > MaximumMaterialBytes) throw std::runtime_error("P3MT input exceeds 1 MiB");
        if (size < 12u) throw std::runtime_error("P3MT header is truncated");
        if (data[0] != static_cast<std::uint8_t>('P')
            || data[1] != static_cast<std::uint8_t>('3')
            || data[2] != static_cast<std::uint8_t>('M')
            || data[3] != static_cast<std::uint8_t>('T')) {
            throw std::runtime_error("P3MT magic is invalid");
        }

        std::size_t cursor = 4u;
        const std::uint32_t version = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, "P3MT version");
        if (version != 1u) throw std::runtime_error("P3MT version is unsupported");
        const std::uint32_t totalLength = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, "P3MT total length");
        if (static_cast<std::size_t>(totalLength) != size) {
            throw std::runtime_error("P3MT total length does not match the exact input size");
        }

        Ps3CookedMaterial candidate;
        candidate.MaterialAssetId = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "material asset id");
        candidate.RendererFamilyId = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "renderer family id");
        candidate.ShaderAssetId = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "shader asset id");
        candidate.VertexProgramName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "vertex program name");
        candidate.PixelProgramName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "pixel program name");
        candidate.VariantName = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "variant name");
        candidate.ShaderRelativePath = Ps3ArtifactReadUtilities::ReadString(data, size, cursor, "shader relative path");

        if (cursor > size || size - cursor < 4u) {
            throw std::runtime_error("base color is truncated");
        }
        candidate.BaseColorR = data[cursor++];
        candidate.BaseColorG = data[cursor++];
        candidate.BaseColorB = data[cursor++];
        candidate.BaseColorA = data[cursor++];
        candidate.SurfaceFlags = Ps3ArtifactReadUtilities::ReadU32(data, size, cursor, "surface flags");
        if ((candidate.SurfaceFlags & ~SupportedSurfaceFlags) != 0u) {
            throw std::runtime_error("surface flags contain unsupported bits");
        }
        candidate.DiffuseTextureRelativePath = Ps3ArtifactReadUtilities::ReadString(
            data, size, cursor, "diffuse texture relative path", true);
        ValidateTextureRelativePath(candidate.DiffuseTextureRelativePath, "diffuse texture relative path");
        if (cursor != size) throw std::runtime_error("P3MT contains trailing bytes");

        ValidateFixedMaterialContract(candidate);
        error.clear();
        result = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        SetUnknownFailure(error, "P3MT reader encountered an unknown validation failure");
        return false;
    }
}

bool Ps3MaterialArtifactReader::MatchesProgram(
    const Ps3CookedMaterial& material,
    const Ps3CookedShaderProgram& program,
    std::string& error) {
    try {
        ValidateFixedMaterialContract(material);
        if (program.ShaderAssetId != material.ShaderAssetId) {
            throw std::runtime_error("shader asset id does not match the material");
        }
        if (program.VertexProgramName != material.VertexProgramName) {
            throw std::runtime_error("vertex program name does not match the material");
        }
        if (program.PixelProgramName != material.PixelProgramName) {
            throw std::runtime_error("pixel program name does not match the material");
        }
        if (program.VariantName != material.VariantName) {
            throw std::runtime_error("variant name does not match the material");
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        SetUnknownFailure(error, "P3MT and P3SP identity comparison failed unexpectedly");
        return false;
    }
}

} // namespace helengine::ps3
