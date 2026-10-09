#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include "system/io/stream.hpp"

namespace helengine::ps3 {

/// <summary>Replays the four sniffed texture signature bytes without requiring a seekable content source.</summary>
class Ps3TexturePrefixStream final : public ::Stream {
    /// <summary>Borrowed source owned and disposed by the cooked texture loader.</summary>
    ::Stream& Source;
    /// <summary>Signature bytes already consumed from the original stream.</summary>
    std::array<std::uint8_t, 4u> Prefix;
    /// <summary>Read position including the replayed prefix.</summary>
    std::size_t Cursor = 0u;
public:
    /// <summary>Creates a read-only view while the caller retains source lifetime and disposal responsibility.</summary>
    Ps3TexturePrefixStream(::Stream& source, const std::array<std::uint8_t, 4u>& prefix) : Source(source), Prefix(prefix) {}
    /// <summary>Copies the remaining prefix followed by at most the requested source bytes.</summary>
    std::size_t Read(std::uint8_t* buffer, std::size_t offset, std::size_t count) override {
        if (buffer == nullptr && count != 0u) throw std::invalid_argument("texture stream destination is null");
        if (count == 0u) return 0u;
        const std::size_t prefixBytes = Cursor < Prefix.size() ? std::min(count, Prefix.size() - Cursor) : 0u;
        if (prefixBytes != 0u) std::memcpy(buffer + offset, Prefix.data() + Cursor, prefixBytes);
        Cursor += prefixBytes;
        const std::size_t read = prefixBytes == count ? 0u : Source.Read(buffer, offset + prefixBytes, count - prefixBytes);
        if (read > count - prefixBytes) throw std::runtime_error("texture source returned an invalid read count");
        Cursor += read;
        return prefixBytes + read;
    }
    /// <summary>Rejects writes to the immutable content view.</summary>
    void Write(const std::uint8_t*, std::size_t, std::size_t) override { throw std::runtime_error("texture prefix stream is read-only"); }
    /// <summary>Rejects seeking because the source may be sequential.</summary>
    std::size_t Seek(std::int64_t, SeekOrigin) override { throw std::runtime_error("texture prefix stream cannot seek"); }
    /// <summary>Rejects resizing of packaged input.</summary>
    void SetLength(std::size_t) override { throw std::runtime_error("texture prefix stream cannot resize"); }
    /// <summary>Reports read support inherited from the borrowed source.</summary>
    bool CanRead() const override { return Source.CanRead(); }
    /// <summary>Reports that the view rejects writes.</summary>
    bool CanWrite() const override { return false; }
    /// <summary>Reports that the replay view does not expose seeking.</summary>
    bool CanSeek() const override { return false; }
    /// <summary>Returns the complete original content length including the replayed signature.</summary>
    std::size_t Length() const override { return Source.Length(); }
    /// <summary>Returns the deserializer-visible read cursor.</summary>
    std::size_t Position() const override { return Cursor; }
    /// <summary>Rejects cursor assignment on a sequential view.</summary>
    void SetPosition(std::size_t) override { throw std::runtime_error("texture prefix stream cannot seek"); }
    /// <summary>Rejects capacity requests on the borrowed read-only source.</summary>
    void InternalReserve(std::size_t) override { throw std::runtime_error("texture prefix stream is read-only"); }
    /// <summary>Rejects single-byte writes on packaged input.</summary>
    void InternalWriteByte(std::uint8_t) override { throw std::runtime_error("texture prefix stream is read-only"); }
    /// <summary>Reads one replay or source byte and returns negative one at EOF.</summary>
    int InternalReadByte() override { std::uint8_t value = 0u; return Read(&value, 0u, 1u) == 1u ? value : -1; }
    /// <summary>Leaves source disposal with its sole owning loader.</summary>
    void Dispose() override {}
};
} // namespace helengine::ps3
#endif
