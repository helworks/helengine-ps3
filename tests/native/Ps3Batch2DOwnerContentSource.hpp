#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "IContentStreamSource.hpp"
#include "Ps3Batch2DOwnerStream.hpp"

/// <summary>Source-scoped packaged texture fixture that counts opens and stream disposal.</summary>
class Ps3Batch2DOwnerContentSource final : public ::IContentStreamSource {
private:
    /// <summary>Distinct serialized snapshots indexed by canonical content-relative path.</summary>
    std::map<std::string, std::vector<std::uint8_t>> Assets;

    /// <summary>Reported content lengths used to exercise bounded-envelope checks without large buffers.</summary>
    std::map<std::string, std::size_t> DeclaredLengths;

public:
    /// <summary>Number of OpenRead calls received from the resource owner.</summary>
    std::size_t OpenCount;

    /// <summary>Number of streams whose Dispose method was called by the resource owner.</summary>
    std::size_t DisposedStreamCount;

    /// <summary>Creates an empty source namespace with observable stream lifetime counts.</summary>
    Ps3Batch2DOwnerContentSource() : OpenCount(0u), DisposedStreamCount(0u) {}

    /// <summary>Stores a complete packaged asset under one fixture-relative path.</summary>
    /// <param name="path">Exact key the production source will receive.</param>
    /// <param name="bytes">Serialized asset snapshot returned by this source.</param>
    void AddAsset(const std::string& path, const std::vector<std::uint8_t>& bytes) {
        AddAssetWithDeclaredLength(path, bytes, bytes.size());
    }

    /// <summary>Stores bytes with an independently declared length for allocation-free boundary regressions.</summary>
    /// <param name="path">Exact source-relative key.</param>
    /// <param name="bytes">Small fixture bytes returned by stream reads.</param>
    /// <param name="declaredLength">Logical source size reported before deserialization.</param>
    void AddAssetWithDeclaredLength(const std::string& path, const std::vector<std::uint8_t>& bytes,
        std::size_t declaredLength) {
        Assets[path] = bytes;
        DeclaredLengths[path] = declaredLength;
    }

    /// <summary>Opens only an exact source-local path and returns an independently owned stream.</summary>
    /// <param name="assetPath">Canonical path requested by BuildCooked.</param>
    /// <returns>A new read-only stream, or null when this source lacks the path.</returns>
    ::Stream* OpenRead(std::string assetPath) override {
        ++OpenCount;
        const auto found = Assets.find(assetPath);
        if (found == Assets.end()) return nullptr;
        return new Ps3Batch2DOwnerStream(found->second, DisposedStreamCount, DeclaredLengths.at(assetPath));
    }
};
