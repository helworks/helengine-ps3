#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace helengine::ps3 {

/// <summary>Provides bounded big-endian and strict UTF-8 decoding shared by PS3 artifact readers.</summary>
class Ps3ArtifactReadUtilities {
public:
    /// <summary>Reads one unsigned 32-bit big-endian value after checking that four bytes remain.</summary>
    /// <param name="data">Complete artifact byte buffer.</param>
    /// <param name="size">Exact bounded artifact length.</param>
    /// <param name="cursor">Current byte offset, advanced only after a successful read.</param>
    /// <param name="resource">Field name included in truncation diagnostics.</param>
    /// <returns>The decoded unsigned value.</returns>
    static std::uint32_t ReadU32(
        const std::uint8_t* data,
        std::size_t size,
        std::size_t& cursor,
        const char* resource) {
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

    /// <summary>Decodes one strict UTF-8 scalar while rejecting NUL, overlong forms and invalid Unicode ranges.</summary>
    /// <param name="data">Start of the bounded UTF-8 field.</param>
    /// <param name="length">Exact encoded field length.</param>
    /// <param name="cursor">Current field offset, advanced as bytes are decoded.</param>
    /// <param name="codePoint">Receives the decoded Unicode scalar on success.</param>
    /// <returns>True when one complete non-NUL Unicode scalar was decoded.</returns>
    static bool ReadUtf8CodePoint(
        const std::uint8_t* data,
        std::size_t length,
        std::size_t& cursor,
        std::uint32_t& codePoint) {
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
        for (std::size_t index = 0u; index < continuationCount; ++index) {
            const std::uint8_t next = data[cursor++];
            if ((next & 0xC0u) != 0x80u) return false;
            value = (value << 6u) | (next & 0x3Fu);
        }
        if (value < minimumValue || value > 0x10FFFFu
            || (value >= 0xD800u && value <= 0xDFFFu)) return false;
        codePoint = value;
        return true;
    }

    /// <summary>Identifies the Unicode White_Space set used by PS3 artifact producers.</summary>
    /// <param name="codePoint">Decoded Unicode scalar.</param>
    /// <returns>True when the scalar is whitespace for required-text and path-segment checks.</returns>
    static bool IsWhitespace(std::uint32_t codePoint) {
        return (codePoint >= 0x0009u && codePoint <= 0x000Du)
            || codePoint == 0x0020u || codePoint == 0x0085u || codePoint == 0x00A0u
            || codePoint == 0x1680u || (codePoint >= 0x2000u && codePoint <= 0x200Au)
            || codePoint == 0x2028u || codePoint == 0x2029u || codePoint == 0x202Fu
            || codePoint == 0x205Fu || codePoint == 0x3000u;
    }

    /// <summary>Reads one bounded strict UTF-8 string and preserves its original bytes.</summary>
    /// <param name="data">Complete artifact byte buffer.</param>
    /// <param name="size">Exact bounded artifact length.</param>
    /// <param name="cursor">Current byte offset, advanced after the entire field validates.</param>
    /// <param name="resource">Field name included in validation diagnostics.</param>
    /// <param name="allowEmpty">Whether a zero-byte encoded string is valid for this field.</param>
    /// <returns>An owning byte-preserving UTF-8 string.</returns>
    static std::string ReadString(
        const std::uint8_t* data,
        std::size_t size,
        std::size_t& cursor,
        const char* resource,
        bool allowEmpty = false) {
        const std::uint32_t length = ReadU32(data, size, cursor, resource);
        if ((length == 0u && !allowEmpty) || length > MaximumStringBytes) {
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
        if (stringLength != 0u && !hasNonWhitespace) {
            throw std::runtime_error(std::string(resource) + " is blank");
        }
        std::string value;
        if (stringLength != 0u) {
            value.assign(reinterpret_cast<const char*>(data + stringStart), stringLength);
        }
        cursor += stringLength;
        return value;
    }

private:
    /// <summary>Maximum UTF-8 byte length accepted for one version-one artifact string.</summary>
    static constexpr std::uint32_t MaximumStringBytes = 65536u;
};

} // namespace helengine::ps3
