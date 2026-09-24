#include "Ps3RsxProgramValidator.hpp"

#include "Ps3RsxActiveWriteRange.hpp"
#include "Ps3RsxAttributeEntry.hpp"
#include "Ps3RsxConstantElement.hpp"
#include "Ps3RsxConstantGroup.hpp"
#include "Ps3RsxProgramView.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace helengine::ps3 {
namespace {

/// <summary>Fixed byte size of the pinned SDK RSX program header.</summary>
constexpr std::size_t SdkHeaderBytes = 36u;

/// <summary>Fixed byte size of each pinned SDK attribute-table record.</summary>
constexpr std::size_t SdkAttributeBytes = 12u;

/// <summary>Fixed byte size of each pinned SDK constant-table record.</summary>
constexpr std::size_t SdkConstantBytes = 28u;

/// <summary>Fixed byte size of one pinned SDK microcode instruction.</summary>
constexpr std::uint64_t SdkInstructionBytes = 16u;

/// <summary>SDK fragment-constant marker that denotes no relocation table.</summary>
constexpr std::uint32_t NoFragmentRelocations = std::numeric_limits<std::uint32_t>::max();

/// <summary>Decodes one big-endian 16-bit SDK integer without unaligned typed access.</summary>
/// <param name="bytes">Complete bounded SDK program.</param>
/// <param name="size">SDK program length.</param>
/// <param name="offset">Byte offset of the first encoded byte.</param>
/// <param name="resource">Field label included in malformed-program errors.</param>
/// <returns>The decoded unsigned value.</returns>
std::uint16_t ReadU16(const std::uint8_t* bytes, std::size_t size,
    std::size_t offset, const char* resource) {
    if (offset > size || size - offset < 2u) {
        throw std::runtime_error(std::string(resource) + " is truncated");
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8u)
        | static_cast<std::uint16_t>(bytes[offset + 1u]));
}

/// <summary>Decodes one big-endian 32-bit SDK integer without unaligned typed access.</summary>
/// <param name="bytes">Complete bounded SDK program.</param>
/// <param name="size">SDK program length.</param>
/// <param name="offset">Byte offset of the first encoded byte.</param>
/// <param name="resource">Field label included in malformed-program errors.</param>
/// <returns>The decoded unsigned value.</returns>
std::uint32_t ReadU32(const std::uint8_t* bytes, std::size_t size,
    std::size_t offset, const char* resource) {
    if (offset > size || size - offset < 4u) {
        throw std::runtime_error(std::string(resource) + " is truncated");
    }
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u)
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u)
        | static_cast<std::uint32_t>(bytes[offset + 3u]);
}

/// <summary>Decodes and validates one UTF-8 scalar from a bounded SDK name.</summary>
/// <param name="bytes">Name bytes up to but excluding the SDK NUL terminator.</param>
/// <param name="size">Exact name byte length.</param>
/// <param name="cursor">Current byte offset, advanced on successful decoding.</param>
/// <param name="codePoint">Receives the decoded Unicode scalar value.</param>
/// <returns>True when one strict UTF-8 scalar is available.</returns>
bool ReadUtf8CodePoint(const std::uint8_t* bytes, std::size_t size,
    std::size_t& cursor, std::uint32_t& codePoint) {
    if (cursor >= size) return false;
    const std::uint8_t first = bytes[cursor++];
    if (first <= 0x7Fu) {
        codePoint = first;
        return true;
    }
    std::size_t continuationCount = 0u;
    std::uint32_t value = 0u;
    std::uint32_t minimum = 0u;
    if (first >= 0xC2u && first <= 0xDFu) {
        continuationCount = 1u;
        value = first & 0x1Fu;
        minimum = 0x80u;
    } else if (first >= 0xE0u && first <= 0xEFu) {
        continuationCount = 2u;
        value = first & 0x0Fu;
        minimum = 0x800u;
    } else if (first >= 0xF0u && first <= 0xF4u) {
        continuationCount = 3u;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        return false;
    }
    if (continuationCount > size - cursor) return false;
    for (std::size_t index = 0u; index < continuationCount; ++index) {
        const std::uint8_t next = bytes[cursor++];
        if ((next & 0xC0u) != 0x80u) return false;
        value = (value << 6u) | (next & 0x3Fu);
    }
    if (value < minimum || value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) return false;
    codePoint = value;
    return true;
}

/// <summary>Identifies Unicode White_Space scalars that cannot form a required SDK parameter name.</summary>
/// <param name="value">Decoded Unicode scalar.</param>
/// <returns>True for Unicode whitespace used by the artifact writer's required-text rule.</returns>
bool IsWhitespace(std::uint32_t value) {
    return (value >= 0x0009u && value <= 0x000Du)
        || value == 0x0020u || value == 0x0085u || value == 0x00A0u
        || value == 0x1680u || (value >= 0x2000u && value <= 0x200Au)
        || value == 0x2028u || value == 0x2029u || value == 0x202Fu
        || value == 0x205Fu || value == 0x3000u;
}

/// <summary>Reads a NUL-terminated SDK name, allowing offset zero only for explicitly optional names.</summary>
/// <param name="bytes">Complete bounded SDK program.</param>
/// <param name="size">SDK program length.</param>
/// <param name="offset">Absolute SDK byte offset stored by the record.</param>
/// <param name="required">Whether zero or blank names are forbidden.</param>
/// <param name="resource">Name role included in malformed-program errors.</param>
/// <returns>An exact owning UTF-8 name, or an empty string for an allowed zero pointer.</returns>
std::string ReadSdkName(const std::uint8_t* bytes, std::size_t size,
    std::uint32_t offset, bool required, const char* resource) {
    if (offset == 0u) {
        if (required) throw std::runtime_error(std::string(resource) + " has a zero name offset");
        return std::string();
    }
    if (static_cast<std::size_t>(offset) >= size) {
        throw std::runtime_error(std::string(resource) + " name offset is outside the SDK program");
    }
    const std::size_t start = static_cast<std::size_t>(offset);
    std::size_t end = start;
    while (end < size && bytes[end] != 0u) ++end;
    if (end == size) throw std::runtime_error(std::string(resource) + " name is not NUL-terminated");
    if (end == start) throw std::runtime_error(std::string(resource) + " name is empty at a nonzero offset");
    bool hasNonWhitespace = false;
    std::size_t cursor = start;
    while (cursor < end) {
        std::uint32_t codePoint = 0u;
        if (!ReadUtf8CodePoint(bytes, end, cursor, codePoint)) {
            throw std::runtime_error(std::string(resource) + " name is not strict UTF-8");
        }
        if (!IsWhitespace(codePoint)) hasNonWhitespace = true;
    }
    if (required && !hasNonWhitespace) throw std::runtime_error(std::string(resource) + " name is blank");
    return std::string(reinterpret_cast<const char*>(bytes + start), end - start);
}

/// <summary>Checks whether two nonempty half-open byte ranges intersect.</summary>
/// <param name="firstBegin">First range start.</param>
/// <param name="firstEnd">First range exclusive end.</param>
/// <param name="secondBegin">Second range start.</param>
/// <param name="secondEnd">Second range exclusive end.</param>
/// <returns>True only when both nonempty ranges overlap.</returns>
bool RangesOverlap(std::uint64_t firstBegin, std::uint64_t firstEnd,
    std::uint64_t secondBegin, std::uint64_t secondEnd) {
    return firstBegin < firstEnd && secondBegin < secondEnd
        && firstBegin < secondEnd && secondBegin < firstEnd;
}

/// <summary>Returns the write width in 32-bit words for a supported SDK constant type.</summary>
/// <param name="dataType">Exact SDK type code from a constant-group head.</param>
/// <returns>One through four writable words, or zero for a non-constant/unknown code.</returns>
std::uint32_t ConstantWordWidth(std::uint8_t dataType) {
    switch (dataType) {
        case 0u: case 1u: case 5u: case 6u: return 1u;
        case 2u: case 7u: return 2u;
        case 3u: case 8u: case 12u: case 13u: return 3u;
        case 4u: case 9u: case 10u: case 11u: return 4u;
        default: return 0u;
    }
}

/// <summary>Returns whether a valid SDK attribute type is one of the pinned texture sampler codes.</summary>
/// <param name="dataType">Exact SDK type code.</param>
/// <returns>True for sampler1D through samplerRect.</returns>
bool IsSamplerType(std::uint8_t dataType) {
    return dataType >= 14u && dataType <= 18u;
}

/// <summary>Reads one fragment relocation list and validates each complete typed write against microcode.</summary>
/// <param name="bytes">Complete bounded SDK pixel program.</param>
/// <param name="size">SDK program length.</param>
/// <param name="tableOffset">Absolute table offset stored in the constant element.</param>
/// <param name="wordWidth">Number of 32-bit words written by the containing constant type.</param>
/// <param name="attributeOffset">SDK attribute-table start.</param>
/// <param name="attributeEnd">SDK attribute-table exclusive end.</param>
/// <param name="constantOffset">SDK constant-table start.</param>
/// <param name="constantEnd">SDK constant-table exclusive end.</param>
/// <param name="microcodeOffset">SDK microcode start.</param>
/// <param name="microcodeBytes">Exact SDK microcode length.</param>
/// <returns>Validated microcode-relative word offsets, preserving duplicates and order.</returns>
std::vector<std::uint32_t> ReadFragmentRelocations(const std::uint8_t* bytes, std::size_t size,
    std::uint32_t tableOffset, std::uint32_t wordWidth,
    std::uint32_t attributeOffset, std::uint64_t attributeEnd,
    std::uint32_t constantOffset, std::uint64_t constantEnd,
    std::uint32_t microcodeOffset, std::uint64_t microcodeBytes) {
    if (tableOffset == NoFragmentRelocations) return std::vector<std::uint32_t>();
    const std::uint64_t tableStart = tableOffset;
    if (tableStart < SdkHeaderBytes || tableStart > size || size - static_cast<std::size_t>(tableStart) < 4u) {
        throw std::runtime_error("fragment relocation table header is outside the SDK program");
    }
    const std::uint32_t relocationCount = ReadU32(bytes, size, static_cast<std::size_t>(tableStart),
        "fragment relocation count");
    const std::uint64_t available = static_cast<std::uint64_t>(size) - tableStart - 4u;
    if (static_cast<std::uint64_t>(relocationCount) > available / 4u) {
        throw std::runtime_error("fragment relocation table extends outside the SDK program");
    }
    const std::uint64_t tableEnd = tableStart + 4u + static_cast<std::uint64_t>(relocationCount) * 4u;
    if (RangesOverlap(tableStart, tableEnd, attributeOffset, attributeEnd)
        || RangesOverlap(tableStart, tableEnd, constantOffset, constantEnd)
        || RangesOverlap(tableStart, tableEnd, microcodeOffset,
            static_cast<std::uint64_t>(microcodeOffset) + microcodeBytes)) {
        throw std::runtime_error("fragment relocation table overlaps an SDK table or microcode");
    }
    std::vector<std::uint32_t> offsets;
    offsets.reserve(relocationCount);
    for (std::uint32_t ordinal = 0u; ordinal < relocationCount; ++ordinal) {
        const std::uint64_t position = tableStart + 4u + static_cast<std::uint64_t>(ordinal) * 4u;
        const std::uint32_t microcodeOffsetValue = ReadU32(bytes, size,
            static_cast<std::size_t>(position), "fragment relocation offset");
        const std::uint64_t byteOffset = microcodeOffsetValue;
        const std::uint64_t writeBytes = static_cast<std::uint64_t>(wordWidth) * 4u;
        if ((microcodeOffsetValue & 3u) != 0u || byteOffset > microcodeBytes
            || writeBytes > microcodeBytes - byteOffset) {
            throw std::runtime_error("fragment relocation does not reference a complete aligned constant write in microcode");
        }
        offsets.push_back(microcodeOffsetValue);
    }
    return offsets;
}

/// <summary>Parses and validates the pinned SDK program layout, names, constant groups, and stage references.</summary>
/// <param name="bytecode">Owned stage bytecode copied from the P3SP artifact.</param>
/// <param name="isVertex">True for VP magic/layout, false for FP magic/layout.</param>
/// <returns>A temporary view of validated SDK metadata without modifying the source blob.</returns>
Ps3RsxProgramView ParseSdkProgram(const std::vector<std::uint8_t>& bytecode, bool isVertex) {
    if (bytecode.size() < SdkHeaderBytes) throw std::runtime_error("SDK program header is truncated");
    if (bytecode.size() > 1024u * 1024u) throw std::runtime_error("SDK program exceeds the 1 MiB stage bound");
    const std::uint8_t* bytes = bytecode.data();
    const std::size_t size = bytecode.size();
    const std::uint16_t magic = ReadU16(bytes, size, 0u, "SDK program magic");
    const std::uint16_t expectedMagic = isVertex ? 0x5650u : 0x4650u;
    if (magic != expectedMagic) throw std::runtime_error("SDK program magic does not match its P3SP stage");

    const std::uint32_t attributeCount = ReadU16(bytes, size, 6u, "SDK attribute count");
    const std::uint32_t constantCount = ReadU16(bytes, size, 8u, "SDK constant count");
    const std::uint32_t instructionCount = ReadU16(bytes, size, 10u, "SDK instruction count");
    const std::uint32_t attributeOffset = ReadU32(bytes, size, 12u, "SDK attribute offset");
    const std::uint32_t constantOffset = ReadU32(bytes, size, 16u, "SDK constant offset");
    const std::uint32_t microcodeOffset = ReadU32(bytes, size, 20u, "SDK microcode offset");
    if (instructionCount == 0u) throw std::runtime_error("SDK program has no microcode instructions");
    if ((microcodeOffset & 15u) != 0u) throw std::runtime_error("SDK microcode offset is not 16-byte aligned");

    const std::uint64_t attributeEnd = static_cast<std::uint64_t>(attributeOffset)
        + static_cast<std::uint64_t>(attributeCount) * SdkAttributeBytes;
    const std::uint64_t constantEnd = static_cast<std::uint64_t>(constantOffset)
        + static_cast<std::uint64_t>(constantCount) * SdkConstantBytes;
    const std::uint64_t microcodeBytes = static_cast<std::uint64_t>(instructionCount) * SdkInstructionBytes;
    const std::uint64_t microcodeEnd = static_cast<std::uint64_t>(microcodeOffset) + microcodeBytes;
    if (attributeOffset < SdkHeaderBytes || attributeEnd > size) {
        throw std::runtime_error("SDK attribute table range is outside the program");
    }
    if (constantOffset < SdkHeaderBytes || constantEnd > size) {
        throw std::runtime_error("SDK constant table range is outside the program");
    }
    if (microcodeOffset < SdkHeaderBytes || microcodeEnd > size) {
        throw std::runtime_error("SDK microcode range is outside the program");
    }
    if (attributeEnd > constantOffset || constantEnd > microcodeOffset) {
        throw std::runtime_error("SDK attribute, constant, and microcode ranges overlap or are out of order");
    }

    Ps3RsxProgramView program{};
    program.IsVertex = isVertex;
    program.MicrocodeOffset = microcodeOffset;
    program.MicrocodeBytes = microcodeBytes;
    program.Attributes.reserve(attributeCount);
    std::unordered_set<std::string> parameterNames;
    for (std::uint32_t ordinal = 0u; ordinal < attributeCount; ++ordinal) {
        const std::size_t record = static_cast<std::size_t>(attributeOffset)
            + static_cast<std::size_t>(ordinal) * SdkAttributeBytes;
        const std::string name = ReadSdkName(bytes, size,
            ReadU32(bytes, size, record, "SDK attribute name offset"), true, "SDK attribute");
        const std::uint32_t index = ReadU32(bytes, size, record + 4u, "SDK attribute index");
        const std::uint8_t dataType = bytes[record + 8u];
        if (dataType > 18u) throw std::runtime_error("SDK attribute has an unknown data type");
        if (isVertex && IsSamplerType(dataType)) {
            throw std::runtime_error("vertex SDK program contains an unsupported sampler");
        }
        if (!parameterNames.insert(name).second) {
            throw std::runtime_error("SDK program repeats parameter name " + name);
        }
        Ps3RsxAttributeEntry entry{};
        entry.Name = name;
        entry.Index = index;
        entry.DataType = dataType;
        program.Attributes.push_back(std::move(entry));
    }

    std::uint32_t recordOrdinal = 0u;
    while (recordOrdinal < constantCount) {
        const std::size_t record = static_cast<std::size_t>(constantOffset)
            + static_cast<std::size_t>(recordOrdinal) * SdkConstantBytes;
        const std::string name = ReadSdkName(bytes, size,
            ReadU32(bytes, size, record, "SDK constant name offset"), false, "SDK constant");
        const std::uint8_t dataType = bytes[record + 8u];
        const std::uint8_t internalFlag = bytes[record + 9u];
        const std::uint32_t elementCount = bytes[record + 10u];
        if (internalFlag > 1u) throw std::runtime_error("SDK constant has an invalid internal flag");
        if (elementCount == 0u) throw std::runtime_error("SDK constant continuation was incorrectly treated as a group head");
        if (elementCount > constantCount - recordOrdinal) {
            throw std::runtime_error("SDK constant group extends beyond its constant table");
        }
        const std::uint32_t wordWidth = ConstantWordWidth(dataType);
        if (wordWidth == 0u) throw std::runtime_error("SDK constant group has an unsupported type code");
        if (name.empty() && internalFlag == 0u) {
            throw std::runtime_error("unnamed SDK constant group is not marked internal");
        }
        if (!name.empty() && !parameterNames.insert(name).second) {
            throw std::runtime_error("SDK program repeats parameter name " + name);
        }

        Ps3RsxConstantGroup group{};
        group.Name = name;
        group.DataType = dataType;
        group.Elements.reserve(elementCount);
        for (std::uint32_t elementOrdinal = 0u; elementOrdinal < elementCount; ++elementOrdinal) {
            const std::uint32_t currentOrdinal = recordOrdinal + elementOrdinal;
            const std::size_t currentRecord = static_cast<std::size_t>(constantOffset)
                + static_cast<std::size_t>(currentOrdinal) * SdkConstantBytes;
            const std::string continuationName = ReadSdkName(bytes, size,
                ReadU32(bytes, size, currentRecord, "SDK constant continuation name offset"),
                false, "SDK constant continuation");
            if (elementOrdinal > 0u && !continuationName.empty() && continuationName != name) {
                throw std::runtime_error("SDK constant continuation introduces a different parameter name");
            }
            if (elementOrdinal > 0u && name.empty() && !continuationName.empty()) {
                throw std::runtime_error("unnamed SDK internal constant has a named continuation");
            }
            const std::uint8_t elementInternal = bytes[currentRecord + 9u];
            if (elementInternal > 1u) throw std::runtime_error("SDK constant continuation has an invalid internal flag");

            Ps3RsxConstantElement element{};
            element.Index = ReadU32(bytes, size, currentRecord + 4u, "SDK constant register or relocation index");
            element.IsInternal = elementInternal;
            if (isVertex) {
                if (element.Index >= 512u) throw std::runtime_error("vertex SDK constant register is outside the tested 512-register runtime bound");
            } else {
                element.RelocationOffsets = ReadFragmentRelocations(bytes, size, element.Index, wordWidth,
                    attributeOffset, attributeEnd, constantOffset, constantEnd,
                    microcodeOffset, microcodeBytes);
            }
            group.Elements.push_back(std::move(element));
        }
        program.Constants.push_back(std::move(group));
        recordOrdinal += elementCount;
    }
    return program;
}

/// <summary>Finds the uniquely named SDK constant group after parser-level duplicate-name rejection.</summary>
/// <param name="program">Validated raw SDK stage view.</param>
/// <param name="name">Exact generated name serialized in the P3SP uniform.</param>
/// <returns>The matching constant group, or null when the artifact mapping is missing.</returns>
const Ps3RsxConstantGroup* FindConstant(const Ps3RsxProgramView& program, const std::string& name) {
    for (const Ps3RsxConstantGroup& constant : program.Constants) {
        if (!constant.Name.empty() && constant.Name == name) return &constant;
    }
    return nullptr;
}

/// <summary>Finds the uniquely named SDK attribute or sampler after parser-level duplicate-name rejection.</summary>
/// <param name="program">Validated raw SDK stage view.</param>
/// <param name="name">Exact generated name serialized in the P3SP mapping.</param>
/// <returns>The matching SDK attribute, or null when the artifact mapping is missing.</returns>
const Ps3RsxAttributeEntry* FindAttribute(const Ps3RsxProgramView& program, const std::string& name) {
    for (const Ps3RsxAttributeEntry& attribute : program.Attributes) {
        if (attribute.Name == name) return &attribute;
    }
    return nullptr;
}

/// <summary>Derives the only supported SDK float type for a serialized physical uniform shape.</summary>
/// <param name="uniform">Serialized uniform metadata with bounded dimensions.</param>
/// <returns>The SDK type code required by that scalar/vector/matrix shape.</returns>
std::uint32_t ExpectedUniformType(const Ps3CookedShaderUniform& uniform) {
    if (uniform.ScalarType != "FP3232") throw std::runtime_error("uniform scalar type is not the producer's FP3232 type");
    if (uniform.Columns == 1u) {
        switch (uniform.VectorWidth) {
            case 1u: return 6u;
            case 2u: return 7u;
            case 3u: return 8u;
            case 4u: return 9u;
            default: break;
        }
    } else {
        if (uniform.ArrayLength != 0u) {
            if (uniform.VectorWidth == 3u && uniform.Columns == 4u) return 10u;
            if (uniform.VectorWidth == 4u && uniform.Columns == 4u) return 11u;
            if (uniform.VectorWidth == 3u && uniform.Columns == 3u) return 12u;
            if (uniform.VectorWidth == 4u && uniform.Columns == 3u) return 13u;
        }
    }
    throw std::runtime_error("uniform physical shape has no supported pinned SDK float type");
}

/// <summary>Checks owning buffer declarations for nonzero sizes and unique names and bindings.</summary>
/// <param name="stage">Stage whose buffers are being checked.</param>
/// <param name="stageName">Diagnostic stage label.</param>
void ValidateBuffers(const Ps3CookedShaderStage& stage, const char* stageName) {
    if (stage.Buffers.size() > 4096u) throw std::runtime_error(std::string(stageName) + " buffer list exceeds 4096 records");
    std::unordered_set<std::string> names;
    std::unordered_set<std::uint32_t> bindings;
    for (const Ps3CookedShaderBuffer& buffer : stage.Buffers) {
        if (buffer.BufferName.empty()) throw std::runtime_error(std::string(stageName) + " buffer name is empty");
        if (buffer.DeclaredByteSize == 0u) throw std::runtime_error(std::string(stageName) + " buffer has zero declared size");
        if (!names.insert(buffer.BufferName).second) throw std::runtime_error(std::string(stageName) + " repeats a buffer name");
        if (!bindings.insert(buffer.AuthoredBinding).second) throw std::runtime_error(std::string(stageName) + " repeats a buffer binding");
    }
}

/// <summary>Validates one serialized uniform's source range, SDK identity, and shape against its raw stage program.</summary>
/// <param name="stage">Owning stage with buffers and mappings.</param>
/// <param name="sdk">Parsed raw SDK program for that stage.</param>
/// <param name="stageName">Diagnostic stage label.</param>
/// <param name="activeVertexRegisters">Registers already owned by mapped vertex elements.</param>
/// <param name="activePixelWrites">Fragment writes already owned by mapped pixel elements.</param>
void ValidateUniforms(const Ps3CookedShaderStage& stage, const Ps3RsxProgramView& sdk,
    const char* stageName, std::unordered_set<std::uint32_t>& activeVertexRegisters,
    std::vector<Ps3RsxActiveWriteRange>& activePixelWrites) {
    if (stage.Uniforms.size() > 4096u) throw std::runtime_error(std::string(stageName) + " uniform list exceeds 4096 records");
    std::set<std::pair<std::string, std::string>> memberPaths;
    std::unordered_set<std::string> generatedNames;
    for (const Ps3CookedShaderUniform& uniform : stage.Uniforms) {
        if (uniform.BufferName.empty() || uniform.MemberPath.empty() || uniform.GeneratedName.empty()) {
            throw std::runtime_error(std::string(stageName) + " uniform contains an empty source or generated name");
        }
        if (uniform.ByteSize == 0u || uniform.VectorWidth < 1u || uniform.VectorWidth > 4u
            || uniform.Columns < 1u || uniform.Columns > 4u || uniform.ArrayLength == 0u) {
            throw std::runtime_error(std::string(stageName) + " uniform has zero size or unsupported shape metadata");
        }
        if (uniform.RowMajor > 1u) throw std::runtime_error(std::string(stageName) + " uniform has a row-major flag other than zero or one");
        const std::uint64_t rangeEnd = static_cast<std::uint64_t>(uniform.ByteOffset) + uniform.ByteSize;
        const Ps3CookedShaderBuffer* matchingBuffer = nullptr;
        for (const Ps3CookedShaderBuffer& buffer : stage.Buffers) {
            if (buffer.BufferName == uniform.BufferName && buffer.AuthoredBinding == uniform.AuthoredBinding) {
                matchingBuffer = &buffer;
                break;
            }
        }
        if (matchingBuffer == nullptr) throw std::runtime_error(std::string(stageName) + " uniform does not match one declared buffer name and binding");
        if (rangeEnd > matchingBuffer->DeclaredByteSize) throw std::runtime_error(std::string(stageName) + " uniform extends beyond its declared buffer size");
        if (!memberPaths.insert(std::make_pair(uniform.BufferName, uniform.MemberPath)).second) {
            throw std::runtime_error(std::string(stageName) + " repeats a uniform member path in one buffer");
        }
        if (!generatedNames.insert(uniform.GeneratedName).second) {
            throw std::runtime_error(std::string(stageName) + " repeats a generated uniform name");
        }
        const std::uint64_t expectedElements = static_cast<std::uint64_t>(uniform.Columns) * uniform.ArrayLength;
        if (expectedElements == 0u || expectedElements > std::numeric_limits<std::uint32_t>::max()
            || uniform.SdkElementCount != expectedElements) {
            throw std::runtime_error(std::string(stageName) + " uniform SDK element count disagrees with its columns and array length");
        }
        const std::uint32_t expectedType = ExpectedUniformType(uniform);
        if (uniform.SdkDataType != expectedType) throw std::runtime_error(std::string(stageName) + " uniform SDK type disagrees with its source shape");

        const Ps3RsxConstantGroup* constant = FindConstant(sdk, uniform.GeneratedName);
        if (constant == nullptr) throw std::runtime_error(std::string(stageName) + " uniform generated name is missing from SDK constants");
        if (constant->DataType != uniform.SdkDataType || constant->Elements.size() != uniform.SdkElementCount) {
            throw std::runtime_error(std::string(stageName) + " uniform SDK type or element count disagrees with its constant group");
        }
        const std::uint32_t wordWidth = ConstantWordWidth(constant->DataType);
        for (std::size_t elementOrdinal = 0u; elementOrdinal < constant->Elements.size(); ++elementOrdinal) {
            const Ps3RsxConstantElement& element = constant->Elements[elementOrdinal];
            if (sdk.IsVertex) {
                if (!activeVertexRegisters.insert(element.Index).second) {
                    throw std::runtime_error("active vertex uniforms overlap an SDK constant register");
                }
                continue;
            }
            std::unordered_set<std::uint32_t> offsetsInElement;
            for (std::uint32_t offset : element.RelocationOffsets) {
                if (!offsetsInElement.insert(offset).second) continue;
                const std::uint64_t begin = offset;
                const std::uint64_t end = begin + static_cast<std::uint64_t>(wordWidth) * 4u;
                for (const Ps3RsxActiveWriteRange& prior : activePixelWrites) {
                    if (prior.Group == constant && prior.ElementOrdinal == elementOrdinal) continue;
                    if (RangesOverlap(begin, end, prior.Begin, prior.End)) {
                        throw std::runtime_error("different active pixel uniform elements overlap SDK relocation write ranges");
                    }
                }
                Ps3RsxActiveWriteRange range{};
                range.Begin = begin;
                range.End = end;
                range.Group = constant;
                range.ElementOrdinal = elementOrdinal;
                activePixelWrites.push_back(range);
            }
        }
    }

    for (const Ps3RsxConstantGroup& constant : sdk.Constants) {
        bool containsNonInternalElement = false;
        for (const Ps3RsxConstantElement& element : constant.Elements) {
            if (!element.IsInternal) {
                containsNonInternalElement = true;
                break;
            }
        }
        if (containsNonInternalElement && generatedNames.find(constant.Name) == generatedNames.end()) {
            throw std::runtime_error(std::string(stageName) + " SDK constant group " + constant.Name
                + " has an unmapped non-internal element");
        }
    }
}

/// <summary>Checks source vertex attributes against their exact generated SDK names, types, and physical slots.</summary>
/// <param name="program">Complete candidate shader pair.</param>
/// <param name="sdk">Parsed raw vertex SDK program.</param>
void ValidateAttributes(const Ps3CookedShaderProgram& program, const Ps3RsxProgramView& sdk) {
    if (program.Attributes.size() > 4096u) throw std::runtime_error("attribute list exceeds 4096 records");
    std::unordered_set<std::uint32_t> slots;
    std::unordered_set<std::string> generatedNames;
    for (const Ps3CookedShaderAttribute& attribute : program.Attributes) {
        if (attribute.FullAuthoredSemantic.empty() || attribute.GeneratedName.empty()) {
            throw std::runtime_error("vertex attribute has an empty authored semantic or generated name");
        }
        if (attribute.ScalarType != "FP3232" || attribute.VectorWidth < 1u || attribute.VectorWidth > 4u
            || attribute.Columns != 1u || attribute.ArrayLength != 1u) {
            throw std::runtime_error("vertex attribute has an unsupported scalar or shape");
        }
        if (attribute.HardwareSlot > 15u) throw std::runtime_error("vertex attribute slot exceeds the RSX input range 0 through 15");
        if (!slots.insert(attribute.HardwareSlot).second) throw std::runtime_error("vertex attributes repeat a physical slot");
        if (!generatedNames.insert(attribute.GeneratedName).second) throw std::runtime_error("vertex attributes repeat a generated name");
        const Ps3RsxAttributeEntry* entry = FindAttribute(sdk, attribute.GeneratedName);
        if (entry == nullptr) throw std::runtime_error("vertex attribute generated name is missing from SDK attributes");
        if (entry->DataType != attribute.SdkDataType || entry->Index != attribute.HardwareSlot) {
            throw std::runtime_error("vertex attribute SDK type or physical slot disagrees with its SDK record");
        }
        if (IsSamplerType(entry->DataType)) throw std::runtime_error("vertex attribute mapping resolved to an SDK sampler");
    }
    for (const Ps3RsxAttributeEntry& entry : sdk.Attributes) {
        if (!IsSamplerType(entry.DataType) && generatedNames.count(entry.Name) == 0u) {
            throw std::runtime_error("vertex SDK input " + entry.Name + " has no artifact mapping");
        }
    }
}

/// <summary>Checks authored pixel samplers against exact SDK sampler names, types, dimensions, and units.</summary>
/// <param name="program">Complete candidate shader pair.</param>
/// <param name="sdk">Parsed raw pixel SDK program.</param>
void ValidateSamplers(const Ps3CookedShaderProgram& program, const Ps3RsxProgramView& sdk) {
    if (program.PixelSamplers.size() > 4096u) throw std::runtime_error("pixel sampler list exceeds 4096 records");
    std::unordered_set<std::uint32_t> units;
    std::unordered_set<std::string> generatedNames;
    for (const Ps3CookedShaderSampler& sampler : program.PixelSamplers) {
        if (sampler.TextureName.empty() || sampler.SamplerName.empty() || sampler.GeneratedName.empty()) {
            throw std::runtime_error("pixel sampler has an empty authored or generated name");
        }
        if (sampler.TextureUnit > 15u) throw std::runtime_error("pixel sampler unit exceeds the RSX range 0 through 15");
        if (!units.insert(sampler.TextureUnit).second) throw std::runtime_error("pixel samplers repeat a physical texture unit");
        if (!generatedNames.insert(sampler.GeneratedName).second) throw std::runtime_error("pixel samplers repeat a generated name");
        std::uint32_t expectedType = 0u;
        if (sampler.Dimension == "Dim2D") expectedType = 15u;
        else if (sampler.Dimension == "DimCube") expectedType = 17u;
        else throw std::runtime_error("pixel sampler dimension is outside the supported Dim2D/DimCube subset");
        if (sampler.SdkDataType != expectedType) throw std::runtime_error("pixel sampler SDK type disagrees with its authored dimension");
        const Ps3RsxAttributeEntry* entry = FindAttribute(sdk, sampler.GeneratedName);
        if (entry == nullptr) throw std::runtime_error("pixel sampler generated name is missing from SDK attributes");
        if (entry->DataType != sampler.SdkDataType || entry->Index != sampler.TextureUnit || !IsSamplerType(entry->DataType)) {
            throw std::runtime_error("pixel sampler SDK type or physical unit disagrees with its SDK record");
        }
    }
    for (const Ps3RsxAttributeEntry& entry : sdk.Attributes) {
        if (IsSamplerType(entry.DataType) && generatedNames.count(entry.Name) == 0u) {
            throw std::runtime_error("pixel SDK sampler " + entry.Name + " has no artifact mapping");
        }
    }
}

} // namespace

bool Ps3RsxProgramValidator::Validate(const Ps3CookedShaderProgram& program, std::string& error) {
    try {
        if (program.ShaderAssetId.empty() || program.VertexProgramName.empty()
            || program.PixelProgramName.empty() || program.VariantName.empty()
            || program.SourceHash.empty() || program.ToolchainIdentity.empty()) {
            throw std::runtime_error("shader pair identity contains an empty required field");
        }
        if (program.Vertex.Stage != 0u || program.Pixel.Stage != 1u) {
            throw std::runtime_error("shader pair stages are not in fixed vertex/pixel order");
        }
        if (program.Vertex.EntryPoint.empty() || program.Pixel.EntryPoint.empty()) {
            throw std::runtime_error("shader stage entry point is empty");
        }
        if (program.Vertex.Bytecode.empty() || program.Pixel.Bytecode.empty()
            || program.Vertex.Bytecode.size() > 1024u * 1024u || program.Pixel.Bytecode.size() > 1024u * 1024u) {
            throw std::runtime_error("shader stage blob length is outside 1 through 1048576 bytes");
        }

        ValidateBuffers(program.Vertex, "vertex");
        ValidateBuffers(program.Pixel, "pixel");
        const Ps3RsxProgramView vertexSdk = ParseSdkProgram(program.Vertex.Bytecode, true);
        const Ps3RsxProgramView pixelSdk = ParseSdkProgram(program.Pixel.Bytecode, false);
        std::unordered_set<std::uint32_t> activeVertexRegisters;
        std::vector<Ps3RsxActiveWriteRange> activePixelWrites;
        ValidateUniforms(program.Vertex, vertexSdk, "vertex", activeVertexRegisters, activePixelWrites);
        ValidateUniforms(program.Pixel, pixelSdk, "pixel", activeVertexRegisters, activePixelWrites);
        ValidateAttributes(program, vertexSdk);
        ValidateSamplers(program, pixelSdk);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    } catch (...) {
        error = "RSX program validation failed with a non-standard exception";
        return false;
    }
}

} // namespace helengine::ps3
