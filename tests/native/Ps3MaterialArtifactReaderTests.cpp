#include "platform/ps3/Ps3MaterialArtifactReader.hpp"
#include "platform/ps3/Ps3ShaderArtifactReader.hpp"

#include <sys/process.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

SYS_PROCESS_PARAM(1001, 0x100000);

namespace {

/// <summary>Exact application-home path for the real production P3MT fixture.</summary>
constexpr const char* MaterialFixturePath = "/app_home/USRDIR/content/material.p3mt";

/// <summary>Maximum envelope length asserted by the committed version-one format.</summary>
constexpr std::size_t MaximumMaterialBytes = 1024u * 1024u;

/// <summary>Expected fixed ForwardStandard P3SP path from the committed identity contract.</summary>
constexpr const char* ExpectedShaderRelativePath = "cooked/shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp";

/// <summary>Names the location and size of one length-prefixed field in a fixture copy.</summary>
struct StringFieldSpan {
    /// <summary>Byte offset of the field's four-byte big-endian length prefix.</summary>
    std::size_t LengthOffset;

    /// <summary>Byte offset of the encoded field contents.</summary>
    std::size_t PayloadOffset;

    /// <summary>Encoded byte length declared by the field prefix.</summary>
    std::uint32_t Length;
};

/// <summary>Throws a descriptive test failure when a native reader behavior differs from the contract.</summary>
/// <param name="condition">Observed condition that should be true.</param>
/// <param name="message">Readable description of the expected behavior.</param>
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

/// <summary>Reads one fixture file from the application-home content directory.</summary>
/// <param name="path">Absolute application-home path to the fixture.</param>
/// <returns>Owned fixture bytes.</returns>
std::vector<std::uint8_t> ReadFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    if (!input) throw std::runtime_error("could not open fixture: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff fileLength = input.tellg();
    if (fileLength <= 0) throw std::runtime_error("fixture is empty: " + path);
    input.seekg(0, std::ios::beg);
    const std::size_t byteLength = static_cast<std::size_t>(fileLength);
    std::vector<std::uint8_t> bytes(byteLength);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(byteLength));
    if (!input) throw std::runtime_error("could not read complete fixture: " + path);
    return bytes;
}

/// <summary>Decodes one test-owned four-byte big-endian integer at a checked offset.</summary>
/// <param name="bytes">Owned mutable envelope copy.</param>
/// <param name="offset">Byte offset of the integer.</param>
/// <returns>The unsigned big-endian value.</returns>
std::uint32_t ReadU32At(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4u, "test field offset is outside the fixture");
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u)
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u)
        | static_cast<std::uint32_t>(bytes[offset + 3u]);
}

/// <summary>Writes a test-owned four-byte big-endian integer at a checked offset.</summary>
/// <param name="bytes">Owned mutable envelope copy.</param>
/// <param name="offset">Byte offset of the integer.</param>
/// <param name="value">Unsigned value encoded in big-endian byte order.</param>
void SetU32At(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    Require(offset <= bytes.size() && bytes.size() - offset >= 4u, "test write offset is outside the fixture");
    bytes[offset] = static_cast<std::uint8_t>(value >> 24u);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value);
}

/// <summary>Finds a serialized material string field by its zero-based wire-order ordinal.</summary>
/// <param name="bytes">Complete producer fixture or an owning in-memory mutation.</param>
/// <param name="ordinal">String field ordinal, from material asset id through texture path.</param>
/// <returns>The checked offsets and encoded length of the requested field.</returns>
StringFieldSpan FindStringField(const std::vector<std::uint8_t>& bytes, std::size_t ordinal) {
    std::size_t cursor = 12u;
    for (std::size_t index = 0u; index <= ordinal; ++index) {
        const std::size_t lengthOffset = cursor;
        const std::uint32_t length = ReadU32At(bytes, lengthOffset);
        const std::size_t payloadOffset = lengthOffset + 4u;
        Require(static_cast<std::size_t>(length) <= bytes.size() - payloadOffset,
            "test fixture string field extends beyond the producer payload");
        if (index == ordinal) return {lengthOffset, payloadOffset, length};
        cursor = payloadOffset + static_cast<std::size_t>(length);
        if (index == 6u) {
            Require(cursor <= bytes.size() && bytes.size() - cursor >= 8u,
                "test fixture color and surface flags are truncated before the texture path");
            cursor += 8u;
        }
    }
    throw std::runtime_error("requested material string field ordinal is unavailable");
}

/// <summary>Replaces one string in an owning producer-fixture copy and repairs only its enclosing lengths.</summary>
/// <param name="bytes">Producer bytes copied into test-owned memory.</param>
/// <param name="ordinal">Zero-based string field ordinal.</param>
/// <param name="replacement">Exact replacement bytes, including any deliberately invalid UTF-8.</param>
/// <returns>A new fixture copy containing the selected mutation.</returns>
std::vector<std::uint8_t> ReplaceStringField(
    const std::vector<std::uint8_t>& bytes, std::size_t ordinal, const std::vector<std::uint8_t>& replacement) {
    const StringFieldSpan field = FindStringField(bytes, ordinal);
    const std::size_t oldEnd = field.PayloadOffset + static_cast<std::size_t>(field.Length);
    std::vector<std::uint8_t> updated;
    updated.reserve(bytes.size() - static_cast<std::size_t>(field.Length) + replacement.size());
    updated.insert(updated.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(field.PayloadOffset));
    updated.insert(updated.end(), replacement.begin(), replacement.end());
    updated.insert(updated.end(), bytes.begin() + static_cast<std::ptrdiff_t>(oldEnd), bytes.end());
    Require(replacement.size() <= std::numeric_limits<std::uint32_t>::max(), "test replacement exceeds U32 string length");
    SetU32At(updated, field.LengthOffset, static_cast<std::uint32_t>(replacement.size()));
    Require(updated.size() <= std::numeric_limits<std::uint32_t>::max(), "test replacement exceeds U32 envelope length");
    SetU32At(updated, 8u, static_cast<std::uint32_t>(updated.size()));
    return updated;
}

/// <summary>Encodes the bytes of a test string without assuming that char is unsigned.</summary>
/// <param name="value">Byte-preserving test value, possibly containing NUL or UTF-8 sequences.</param>
/// <returns>Unsigned byte sequence used by an in-memory envelope mutation.</returns>
std::vector<std::uint8_t> BytesOf(const std::string& value) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(value.size());
    for (unsigned char byte : value) bytes.push_back(static_cast<std::uint8_t>(byte));
    return bytes;
}

/// <summary>Truncates one encoded string inside its payload and updates the exact envelope-size header.</summary>
/// <param name="bytes">Producer fixture bytes.</param>
/// <param name="ordinal">Zero-based string field ordinal.</param>
/// <returns>Truncated owning byte sequence ending one byte before the field completes.</returns>
std::vector<std::uint8_t> TruncateStringPayload(const std::vector<std::uint8_t>& bytes, std::size_t ordinal) {
    const StringFieldSpan field = FindStringField(bytes, ordinal);
    Require(field.Length != 0u, "truncation test requires a nonempty producer string");
    const std::size_t truncatedSize = field.PayloadOffset + static_cast<std::size_t>(field.Length) - 1u;
    std::vector<std::uint8_t> truncated(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(truncatedSize));
    SetU32At(truncated, 8u, static_cast<std::uint32_t>(truncated.size()));
    return truncated;
}

/// <summary>Returns the byte offset immediately after the seven required material identity strings.</summary>
/// <param name="bytes">Complete producer fixture envelope.</param>
/// <returns>Offset of the four raw base-color bytes.</returns>
std::size_t ColorOffset(const std::vector<std::uint8_t>& bytes) {
    const StringFieldSpan field = FindStringField(bytes, 6u);
    return field.PayloadOffset + static_cast<std::size_t>(field.Length);
}

/// <summary>Returns the byte offset of the serialized surface-flags integer.</summary>
/// <param name="bytes">Complete producer fixture envelope.</param>
/// <returns>Offset of the flags field.</returns>
std::size_t FlagsOffset(const std::vector<std::uint8_t>& bytes) {
    return ColorOffset(bytes) + 4u;
}

/// <summary>Returns the byte offset of the optional final texture string's length prefix.</summary>
/// <param name="bytes">Complete producer fixture envelope.</param>
/// <returns>Offset of the final string length field.</returns>
std::size_t TextureLengthOffset(const std::vector<std::uint8_t>& bytes) {
    return FlagsOffset(bytes) + 4u;
}

/// <summary>Builds a destination with distinct sentinel values for every public material field.</summary>
/// <returns>Caller output that must remain unchanged after every rejected read.</returns>
helengine::ps3::Ps3CookedMaterial SentinelMaterial() {
    helengine::ps3::Ps3CookedMaterial material;
    material.MaterialAssetId = "sentinel-material";
    material.RendererFamilyId = "sentinel-renderer";
    material.ShaderAssetId = "sentinel-shader";
    material.VertexProgramName = "sentinel-vertex";
    material.PixelProgramName = "sentinel-pixel";
    material.VariantName = "sentinel-variant";
    material.ShaderRelativePath = "sentinel/path";
    material.BaseColorR = 0x11u;
    material.BaseColorG = 0x22u;
    material.BaseColorB = 0x33u;
    material.BaseColorA = 0x44u;
    material.SurfaceFlags = 0x55667788u;
    material.DiffuseTextureRelativePath = "sentinel-texture";
    return material;
}

/// <summary>Compares every material DTO field so rejection tests can prove transactional output behavior.</summary>
/// <param name="left">Actual caller-owned output after the read attempt.</param>
/// <param name="right">Original sentinel value.</param>
/// <returns>True only when all serialized and derived fields are unchanged.</returns>
bool SameMaterial(
    const helengine::ps3::Ps3CookedMaterial& left,
    const helengine::ps3::Ps3CookedMaterial& right) {
    return left.MaterialAssetId == right.MaterialAssetId
        && left.RendererFamilyId == right.RendererFamilyId
        && left.ShaderAssetId == right.ShaderAssetId
        && left.VertexProgramName == right.VertexProgramName
        && left.PixelProgramName == right.PixelProgramName
        && left.VariantName == right.VariantName
        && left.ShaderRelativePath == right.ShaderRelativePath
        && left.BaseColorR == right.BaseColorR
        && left.BaseColorG == right.BaseColorG
        && left.BaseColorB == right.BaseColorB
        && left.BaseColorA == right.BaseColorA
        && left.SurfaceFlags == right.SurfaceFlags
        && left.DiffuseTextureRelativePath == right.DiffuseTextureRelativePath;
}

/// <summary>Requires one malformed input to fail with field context and preserve the caller's DTO.</summary>
/// <param name="data">Input bytes to reject.</param>
/// <param name="size">Exact supplied input length.</param>
/// <param name="diagnosticFragment">Field-specific substring expected in the error.</param>
void ExpectRejected(const std::uint8_t* data, std::size_t size, const std::string& diagnosticFragment) {
    helengine::ps3::Ps3CookedMaterial output = SentinelMaterial();
    const helengine::ps3::Ps3CookedMaterial sentinel = output;
    std::string error;
    const bool accepted = helengine::ps3::Ps3MaterialArtifactReader::Read(data, size, output, error);
    Require(!accepted, "malformed P3MT input was accepted");
    Require(!error.empty(), "rejected P3MT input returned an empty diagnostic");
    Require(error.find(diagnosticFragment) != std::string::npos,
        "P3MT diagnostic did not identify '" + diagnosticFragment + "': " + error);
    Require(SameMaterial(output, sentinel), "failed P3MT read changed the caller's output DTO");
}

/// <summary>Requires one malformed owning byte vector to fail transactionally.</summary>
/// <param name="bytes">Malformed fixture copy.</param>
/// <param name="diagnosticFragment">Field-specific substring expected in the error.</param>
void ExpectRejected(const std::vector<std::uint8_t>& bytes, const std::string& diagnosticFragment) {
    ExpectRejected(bytes.data(), bytes.size(), diagnosticFragment);
}

/// <summary>Parses the real material fixture or throws its reader diagnostic as a test failure.</summary>
/// <param name="bytes">Actual producer-generated P3MT fixture.</param>
/// <returns>Owned decoded standard material.</returns>
helengine::ps3::Ps3CookedMaterial ReadFixtureMaterial(const std::vector<std::uint8_t>& bytes) {
    helengine::ps3::Ps3CookedMaterial material;
    std::string error;
    Require(helengine::ps3::Ps3MaterialArtifactReader::Read(bytes.data(), bytes.size(), material, error),
        "real P3MT fixture failed to decode: " + error);
    return material;
}

/// <summary>Runs one named native acceptance case and records its pass or failure line.</summary>
/// <param name="name">Stable case name emitted to the emulator TTY.</param>
/// <param name="test">Case body using the real PPU reader implementation.</param>
/// <param name="passed">Pass count updated on success.</param>
/// <param name="failed">Failure count updated on error.</param>
void RunCase(const char* name, const std::function<void()>& test, int& passed, int& failed) {
    try {
        test();
        ++passed;
        std::printf("PASS %s\n", name);
        std::fflush(stdout);
    } catch (const std::exception& exception) {
        ++failed;
        std::printf("FAIL %s: %s\n", name, exception.what());
        std::fflush(stdout);
    } catch (...) {
        ++failed;
        std::printf("FAIL %s: unknown native test failure\n", name);
        std::fflush(stdout);
    }
}

/// <summary>Verifies all authored material fields and the real referenced shader pair survive the PPU read.</summary>
/// <param name="materialBytes">Material bytes written by the production P3MT cooker.</param>
void TestRealProducerPair(const std::vector<std::uint8_t>& materialBytes) {
    const helengine::ps3::Ps3CookedMaterial material = ReadFixtureMaterial(materialBytes);
    Require(material.MaterialAssetId == std::string("Materials/Native-") + "\xC3\xA9",
        "material identifier lost its exact UTF-8 spelling");
    Require(material.RendererFamilyId == "ps3-standard", "renderer family identity changed");
    Require(material.ShaderAssetId == "ForwardStandardShader", "shader asset identity changed");
    Require(material.VertexProgramName == "ForwardStandardShader.vs", "vertex program identity changed");
    Require(material.PixelProgramName == "ForwardStandardShader.ps", "pixel program identity changed");
    Require(material.VariantName == "ForwardStandard", "shader variant identity changed");
    Require(material.ShaderRelativePath == ExpectedShaderRelativePath, "shader lookup path changed");
    Require(material.BaseColorR == 0xC0u && material.BaseColorG == 0x20u
        && material.BaseColorB == 0x10u && material.BaseColorA == 0x40u,
        "raw authored RGBA channels changed");
    Require(material.SurfaceFlags == 7u, "lit, double-sided and vertex-color flags were not preserved");
    Require(material.DiffuseTextureRelativePath == "cooked/textures/fixture.hasset",
        "texture path spelling changed");

    const std::string shaderPath = std::string("/app_home/USRDIR/content/") + material.ShaderRelativePath;
    const std::vector<std::uint8_t> shaderBytes = ReadFile(shaderPath);
    helengine::ps3::Ps3CookedShaderProgram program;
    std::string error;
    Require(helengine::ps3::Ps3ShaderArtifactReader::Read(shaderBytes.data(), shaderBytes.size(), program, error),
        "real referenced P3SP failed to decode: " + error);
    Require(helengine::ps3::Ps3MaterialArtifactReader::MatchesProgram(material, program, error),
        "real material and shader pair did not match: " + error);
}

/// <summary>Verifies the schema's zero-byte optional texture string is accepted and preserved.</summary>
/// <param name="materialBytes">Actual producer fixture copied into test-owned memory.</param>
void TestOptionalTextureMayBeEmpty(const std::vector<std::uint8_t>& materialBytes) {
    const std::vector<std::uint8_t> emptyTexture = ReplaceStringField(materialBytes, 7u, {});
    const helengine::ps3::Ps3CookedMaterial material = ReadFixtureMaterial(emptyTexture);
    Require(material.DiffuseTextureRelativePath.empty(), "empty texture path was not preserved");
}

/// <summary>Reads the shader program at the exact path decoded from the real material fixture.</summary>
/// <param name="material">Previously decoded real material.</param>
/// <returns>Fully validated owned CPU shader program.</returns>
helengine::ps3::Ps3CookedShaderProgram ReadFixtureProgram(const helengine::ps3::Ps3CookedMaterial& material) {
    const std::string path = std::string("/app_home/USRDIR/content/") + material.ShaderRelativePath;
    const std::vector<std::uint8_t> bytes = ReadFile(path);
    helengine::ps3::Ps3CookedShaderProgram program;
    std::string error;
    Require(helengine::ps3::Ps3ShaderArtifactReader::Read(bytes.data(), bytes.size(), program, error),
        "referenced real P3SP failed to decode: " + error);
    return program;
}

/// <summary>Requires MatchesProgram to reject a material that no longer carries the fixed PS3 schema pair.</summary>
/// <param name="material">Owning material copy changed in one identity field.</param>
/// <param name="program">Real P3SP program decoded from the producer fixture.</param>
/// <param name="diagnosticFragment">Expected contract field named in the diagnostic.</param>
void ExpectMaterialContractRejected(
    const helengine::ps3::Ps3CookedMaterial& material,
    const helengine::ps3::Ps3CookedShaderProgram& program,
    const std::string& diagnosticFragment) {
    std::string error;
    Require(!helengine::ps3::Ps3MaterialArtifactReader::MatchesProgram(material, program, error),
        "material outside the fixed ForwardStandard contract matched a P3SP program");
    Require(!error.empty() && error.find(diagnosticFragment) != std::string::npos,
        "material pair diagnostic did not identify '" + diagnosticFragment + "': " + error);
}

/// <summary>Verifies every shader identity component is compared by exact string equality.</summary>
/// <param name="material">Valid parsed producer material.</param>
/// <param name="program">Valid parsed producer shader program copied before each mutation.</param>
/// <param name="identityField">Identity component to change in the owning copy.</param>
/// <param name="caseName">Stable identity case name used in a failure message.</param>
void TestChangedProgramIdentity(
    const helengine::ps3::Ps3CookedMaterial& material,
    const helengine::ps3::Ps3CookedShaderProgram& program,
    std::size_t identityField,
    const char* caseName) {
    helengine::ps3::Ps3CookedShaderProgram changed = program;
    switch (identityField) {
        case 0u: changed.ShaderAssetId = "OtherShader"; break;
        case 1u: changed.VertexProgramName = "OtherVertex"; break;
        case 2u: changed.PixelProgramName = "OtherPixel"; break;
        case 3u: changed.VariantName = "OtherVariant"; break;
        default: throw std::runtime_error("test requested an unknown program identity field");
    }
    std::string error;
    Require(!helengine::ps3::Ps3MaterialArtifactReader::MatchesProgram(material, changed, error),
        std::string("changed P3SP identity was accepted: ") + caseName);
    Require(!error.empty(), std::string("changed P3SP identity returned no diagnostic: ") + caseName);
}

/// <summary>Runs the full producer-pair and malformed-envelope PPU acceptance suite.</summary>
/// <returns>Zero only when every named native case passes.</returns>
int RunTests() {
    const std::vector<std::uint8_t> materialBytes = ReadFile(MaterialFixturePath);
    const helengine::ps3::Ps3CookedMaterial fixtureMaterial = ReadFixtureMaterial(materialBytes);
    const helengine::ps3::Ps3CookedShaderProgram fixtureProgram = ReadFixtureProgram(fixtureMaterial);
    int passed = 0;
    int failed = 0;

    RunCase("reads_real_producer_pair", [&] { TestRealProducerPair(materialBytes); }, passed, failed);
    RunCase("accepts_empty_optional_texture", [&] { TestOptionalTextureMayBeEmpty(materialBytes); }, passed, failed);
    RunCase("rejects_null_input", [&] { ExpectRejected(nullptr, 0u, "null"); }, passed, failed);

    RunCase("rejects_short_header", [&] {
        std::vector<std::uint8_t> bytes(materialBytes.begin(), materialBytes.begin() + 11);
        ExpectRejected(bytes, "header");
    }, passed, failed);
    RunCase("rejects_wrong_magic", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        bytes[0] = static_cast<std::uint8_t>('X');
        ExpectRejected(bytes, "magic");
    }, passed, failed);
    RunCase("rejects_wrong_version", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        SetU32At(bytes, 4u, 2u);
        ExpectRejected(bytes, "version");
    }, passed, failed);
    RunCase("rejects_wrong_total_length", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        SetU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size() - 1u));
        ExpectRejected(bytes, "total length");
    }, passed, failed);
    RunCase("rejects_trailing_byte", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        bytes.push_back(0u);
        SetU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size()));
        ExpectRejected(bytes, "trailing bytes");
    }, passed, failed);

    const char* stringCaseNames[] = {
        "material_asset_id", "renderer_family_id", "shader_asset_id", "vertex_program_name",
        "pixel_program_name", "variant_name", "shader_relative_path", "diffuse_texture_path"
    };
    const char* stringDiagnostics[] = {
        "material asset id", "renderer family id", "shader asset id", "vertex program name",
        "pixel program name", "variant name", "shader relative path", "diffuse texture relative path"
    };
    for (std::size_t index = 0u; index < 8u; ++index) {
        RunCase((std::string("rejects_truncated_") + stringCaseNames[index]).c_str(), [&, index] {
            ExpectRejected(TruncateStringPayload(materialBytes, index), stringDiagnostics[index]);
        }, passed, failed);
    }
    RunCase("rejects_truncated_color", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        bytes.resize(ColorOffset(bytes) + 3u);
        SetU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size()));
        ExpectRejected(bytes, "base color");
    }, passed, failed);
    RunCase("rejects_truncated_surface_flags", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        bytes.resize(FlagsOffset(bytes) + 3u);
        SetU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size()));
        ExpectRejected(bytes, "surface flags");
    }, passed, failed);
    RunCase("rejects_truncated_texture_length", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        bytes.resize(TextureLengthOffset(bytes) + 3u);
        SetU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size()));
        ExpectRejected(bytes, "diffuse texture relative path");
    }, passed, failed);
    RunCase("rejects_truncated_texture_payload", [&] {
        ExpectRejected(TruncateStringPayload(materialBytes, 7u), "diffuse texture relative path");
    }, passed, failed);

    RunCase("rejects_string_over_65536_bytes", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        SetU32At(bytes, 12u, 65537u);
        ExpectRejected(bytes, "material asset id");
    }, passed, failed);
    RunCase("rejects_total_over_1_mib", [&] {
        std::vector<std::uint8_t> bytes(MaximumMaterialBytes + 1u, 0u);
        bytes[0] = static_cast<std::uint8_t>('P');
        bytes[1] = static_cast<std::uint8_t>('3');
        bytes[2] = static_cast<std::uint8_t>('M');
        bytes[3] = static_cast<std::uint8_t>('T');
        SetU32At(bytes, 4u, 1u);
        SetU32At(bytes, 8u, static_cast<std::uint32_t>(bytes.size()));
        ExpectRejected(bytes, "1 MiB");
    }, passed, failed);

    RunCase("rejects_nul_in_required_text", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 0u, {static_cast<std::uint8_t>('A'), 0u,
            static_cast<std::uint8_t>('B')}), "material asset id");
    }, passed, failed);
    RunCase("rejects_overlong_utf8", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 0u, {0xC0u, 0xAFu}), "material asset id");
    }, passed, failed);
    RunCase("rejects_utf8_surrogate", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 0u, {0xEDu, 0xA0u, 0x80u}), "material asset id");
    }, passed, failed);
    RunCase("rejects_truncated_utf8_scalar", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 0u, {0xE2u, 0x82u}), "material asset id");
    }, passed, failed);
    RunCase("rejects_out_of_range_unicode", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 0u, {0xF4u, 0x90u, 0x80u, 0x80u}), "material asset id");
    }, passed, failed);

    for (std::size_t index = 0u; index < 7u; ++index) {
        RunCase((std::string("rejects_blank_") + stringCaseNames[index]).c_str(), [&, index] {
            ExpectRejected(ReplaceStringField(materialBytes, index, BytesOf(" \t ")), stringDiagnostics[index]);
        }, passed, failed);
    }

    RunCase("rejects_foreign_renderer", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 1u, BytesOf("other-renderer")), "renderer family id");
    }, passed, failed);
    RunCase("rejects_foreign_shader_asset", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 2u, BytesOf("OtherShader")), "shader asset id");
    }, passed, failed);
    RunCase("rejects_foreign_vertex_program", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 3u, BytesOf("OtherVertex")), "vertex program name");
    }, passed, failed);
    RunCase("rejects_foreign_pixel_program", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 4u, BytesOf("OtherPixel")), "pixel program name");
    }, passed, failed);
    RunCase("rejects_foreign_variant", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 5u, BytesOf("OtherVariant")), "variant name");
    }, passed, failed);
    RunCase("rejects_syntactically_valid_foreign_shader_path", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 6u,
            BytesOf(std::string("cooked/shaders/") + std::string(64u, 'a') + ".ps3sp")), "shader relative path");
    }, passed, failed);
    RunCase("rejects_legacy_cook_relative_shader_path", [&] {
        ExpectRejected(ReplaceStringField(materialBytes, 6u,
            BytesOf(std::string(ExpectedShaderRelativePath).substr(7u))), "shader relative path");
    }, passed, failed);
    RunCase("rejects_unknown_surface_flag_bit", [&] {
        std::vector<std::uint8_t> bytes = materialBytes;
        SetU32At(bytes, FlagsOffset(bytes), 8u);
        ExpectRejected(bytes, "surface flags");
    }, passed, failed);

    const struct InvalidTexturePath {
        /// <summary>Readable test-case suffix.</summary>
        const char* Name;
        /// <summary>Invalid path bytes supplied to the optional texture field.</summary>
        const char* Value;
    } invalidTexturePaths[] = {
        {"absolute", "/textures/file.hasset"},
        {"trailing_slash", "textures/file.hasset/"},
        {"drive_relative", "C:textures/file.hasset"},
        {"backslash", "textures\\file.hasset"},
        {"colon", "textures/file:name.hasset"},
        {"dot_segment", "textures/./file.hasset"},
        {"dotdot_segment", "textures/../file.hasset"},
        {"empty_segment", "textures//file.hasset"},
        {"whitespace_segment", "textures/ \t /file.hasset"},
        {"unicode_whitespace_segment", "textures/\xC2\xA0/file.hasset"},
        {"ascii_control", "textures/\x01/file.hasset"},
        {"unicode_control", "textures/\xC2\x85/file.hasset"},
        {"nul", "textures/\0/file.hasset"}
    };
    for (const InvalidTexturePath& invalidPath : invalidTexturePaths) {
        RunCase((std::string("rejects_texture_") + invalidPath.Name).c_str(), [&, invalidPath] {
            ExpectRejected(ReplaceStringField(materialBytes, 7u, BytesOf(invalidPath.Value)),
                "diffuse texture relative path");
        }, passed, failed);
    }

    RunCase("matcher_rejects_changed_program_shader_asset", [&] {
        TestChangedProgramIdentity(fixtureMaterial, fixtureProgram, 0u, "shader asset id");
    }, passed, failed);
    RunCase("matcher_rejects_changed_program_vertex", [&] {
        TestChangedProgramIdentity(fixtureMaterial, fixtureProgram, 1u, "vertex program name");
    }, passed, failed);
    RunCase("matcher_rejects_changed_program_pixel", [&] {
        TestChangedProgramIdentity(fixtureMaterial, fixtureProgram, 2u, "pixel program name");
    }, passed, failed);
    RunCase("matcher_rejects_changed_program_variant", [&] {
        TestChangedProgramIdentity(fixtureMaterial, fixtureProgram, 3u, "variant name");
    }, passed, failed);
    RunCase("matcher_rejects_case_changed_identity", [&] {
        helengine::ps3::Ps3CookedShaderProgram changed = fixtureProgram;
        changed.VariantName = "forwardstandard";
        std::string error;
        Require(!helengine::ps3::Ps3MaterialArtifactReader::MatchesProgram(fixtureMaterial, changed, error),
            "case-changed shader identity was accepted");
        Require(!error.empty(), "case-changed shader identity returned no diagnostic");
    }, passed, failed);

    RunCase("matcher_rejects_changed_material_renderer", [&] {
        helengine::ps3::Ps3CookedMaterial changed = fixtureMaterial;
        changed.RendererFamilyId = "other-renderer";
        ExpectMaterialContractRejected(changed, fixtureProgram, "renderer family id");
    }, passed, failed);
    RunCase("matcher_rejects_changed_material_shader_asset", [&] {
        helengine::ps3::Ps3CookedMaterial changed = fixtureMaterial;
        changed.ShaderAssetId = "OtherShader";
        ExpectMaterialContractRejected(changed, fixtureProgram, "shader asset id");
    }, passed, failed);
    RunCase("matcher_rejects_changed_material_vertex", [&] {
        helengine::ps3::Ps3CookedMaterial changed = fixtureMaterial;
        changed.VertexProgramName = "OtherVertex";
        ExpectMaterialContractRejected(changed, fixtureProgram, "vertex program name");
    }, passed, failed);
    RunCase("matcher_rejects_changed_material_pixel", [&] {
        helengine::ps3::Ps3CookedMaterial changed = fixtureMaterial;
        changed.PixelProgramName = "OtherPixel";
        ExpectMaterialContractRejected(changed, fixtureProgram, "pixel program name");
    }, passed, failed);
    RunCase("matcher_rejects_changed_material_variant", [&] {
        helengine::ps3::Ps3CookedMaterial changed = fixtureMaterial;
        changed.VariantName = "OtherVariant";
        ExpectMaterialContractRejected(changed, fixtureProgram, "variant name");
    }, passed, failed);
    RunCase("matcher_rejects_changed_material_path", [&] {
        helengine::ps3::Ps3CookedMaterial changed = fixtureMaterial;
        changed.ShaderRelativePath = std::string("cooked/shaders/") + std::string(64u, 'a') + ".ps3sp";
        ExpectMaterialContractRejected(changed, fixtureProgram, "shader relative path");
    }, passed, failed);

    std::printf("PS3_MATERIAL_READER_TESTS passed=%d failed=%d\n", passed, failed);
    std::fflush(stdout);
    return failed == 0 && passed >= 25 ? 0 : 1;
}

} // namespace

/// <summary>Starts the real-fixture native material reader acceptance suite.</summary>
/// <returns>Zero only when at least twenty-five named PPU cases pass.</returns>
int main() {
    std::printf("PS3_MATERIAL_MAIN_ENTERED\n");
    std::fflush(stdout);
    try {
        return RunTests();
    } catch (const std::exception& exception) {
        std::printf("FAIL fixture_prerequisite: %s\n", exception.what());
        std::fflush(stdout);
        std::printf("PS3_MATERIAL_READER_TESTS passed=0 failed=1\n");
        std::fflush(stdout);
        return 1;
    }
}
