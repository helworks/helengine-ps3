#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <vector>

#include "system/io/stream.hpp"

/// <summary>Read-only in-memory generated Core stream that exposes disposal to its fixture source.</summary>
class Ps3Batch2DOwnerStream final : public ::Stream {
private:
    /// <summary>Serialized packaged asset bytes returned by this stream.</summary>
    std::vector<std::uint8_t> Bytes;

    /// <summary>Current read cursor within the serialized asset.</summary>
    std::size_t PositionValue;

    /// <summary>Declared source length, which may exceed fixture bytes for envelope-boundary tests.</summary>
    std::size_t LengthValue;

    /// <summary>Counter owned by the live fixture source and incremented when the stream is disposed.</summary>
    std::size_t* DisposedStreamCount;

public:
    /// <summary>Creates one stream over a copied immutable asset snapshot.</summary>
    /// <param name="bytes">Complete serialized fixture content.</param>
    /// <param name="disposedStreamCount">Live source counter updated by Dispose.</param>
    Ps3Batch2DOwnerStream(const std::vector<std::uint8_t>& bytes, std::size_t& disposedStreamCount,
        std::size_t declaredLength = 0u)
        : Bytes(bytes), PositionValue(0u), LengthValue(declaredLength == 0u ? bytes.size() : declaredLength)
        , DisposedStreamCount(&disposedStreamCount) {}

    /// <summary>Copies at most the remaining bytes into the requested destination offset.</summary>
    /// <param name="buffer">Destination byte buffer.</param>
    /// <param name="offset">Starting destination offset.</param>
    /// <param name="count">Maximum requested bytes.</param>
    /// <returns>Number of bytes copied, or zero at end of stream.</returns>
    std::size_t Read(std::uint8_t* buffer, std::size_t offset, std::size_t count) override {
        if (buffer == nullptr || PositionValue >= Bytes.size()) return 0u;
        const std::size_t copied = std::min(count, Bytes.size() - PositionValue);
        std::memcpy(buffer + offset, Bytes.data() + PositionValue, copied);
        PositionValue += copied;
        return copied;
    }

    /// <summary>Rejects writes because content sources in this fixture are immutable.</summary>
    void Write(const std::uint8_t*, std::size_t, std::size_t) override {
        throw std::runtime_error("owner fixture stream is read-only");
    }

    /// <summary>Rejects seeking because the packaged asset reader consumes this fixture sequentially.</summary>
    std::size_t Seek(std::int64_t, SeekOrigin) override {
        throw std::runtime_error("owner fixture stream is not seekable");
    }

    /// <summary>Rejects resizing because the serialized fixture is immutable.</summary>
    void SetLength(std::size_t) override {
        throw std::runtime_error("owner fixture stream cannot be resized");
    }

    /// <summary>Reports that fixture bytes can be read.</summary>
    bool CanRead() const override { return true; }

    /// <summary>Reports that fixture bytes cannot be modified.</summary>
    bool CanWrite() const override { return false; }

    /// <summary>Reports that this sequential fixture cannot seek.</summary>
    bool CanSeek() const override { return false; }

    /// <summary>Returns the complete serialized asset length.</summary>
    std::size_t Length() const override { return LengthValue; }

    /// <summary>Returns the number of serialized bytes already consumed.</summary>
    std::size_t Position() const override { return PositionValue; }

    /// <summary>Rejects repositioning because reads are deliberately sequential.</summary>
    void SetPosition(std::size_t) override {
        throw std::runtime_error("owner fixture stream is not seekable");
    }

    /// <summary>Rejects capacity requests on this immutable read-only stream.</summary>
    void InternalReserve(std::size_t) override {
        throw std::runtime_error("owner fixture stream is read-only");
    }

    /// <summary>Rejects byte writes on this immutable read-only stream.</summary>
    void InternalWriteByte(std::uint8_t) override {
        throw std::runtime_error("owner fixture stream is read-only");
    }

    /// <summary>Reads one byte or returns negative one at end of stream.</summary>
    int InternalReadByte() override {
        if (PositionValue >= Bytes.size()) return -1;
        return static_cast<int>(Bytes[PositionValue++]);
    }

    /// <summary>Records exact disposal of this source-created stream.</summary>
    void Dispose() override {
        if (DisposedStreamCount != nullptr) ++*DisposedStreamCount;
        DisposedStreamCount = nullptr;
    }
};
