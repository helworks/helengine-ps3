#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

class IContentStreamSource;
class TextureAsset;

namespace helengine::ps3 {

class Ps3AuthoredTexture;
class Ps3RuntimeTexture;
class Ps3Video;

/// <summary>Owns renderer GPU textures while Core owns and disposes the corresponding lightweight wrappers.</summary>
class Ps3Batch2DResources final {
private:
    /// <summary>Borrowed video service that remains alive through explicit post-drain shutdown.</summary>
    Ps3Video& Video;

    /// <summary>CPU submission barrier cleared on successful detach so no dead manager callback can be invoked.</summary>
    std::function<void()> FlushPending;

    /// <summary>Wrapper-address keyed registry; wrapper keys are never dereferenced during final shutdown.</summary>
    std::map<Ps3RuntimeTexture*, std::unique_ptr<Ps3AuthoredTexture>> Records;

    /// <summary>Strong token whose expiration proves that wrapper owner pointers must no longer be dereferenced.</summary>
    std::shared_ptr<void> LifetimeSentinel;

    /// <summary>Total RSX bytes retained by all registered texture allocations.</summary>
    std::size_t OwnedGpuBytesValue;

    /// <summary>True while a callback barrier is executing and resource mutation must be rejected.</summary>
    bool FlushInProgress;

    /// <summary>True after CPU submission was successfully flushed and detached.</summary>
    bool CpuSubmissionDetached;

    /// <summary>True after all GPU owners completed safe post-drain shutdown.</summary>
    bool ShutdownComplete;

    /// <summary>Validates bytes and creates an unpublished owner/wrapper pair before publishing the allocation.</summary>
    /// <param name="width">Validated source texture width.</param>
    /// <param name="height">Validated source texture height.</param>
    /// <param name="rgba">Copied row-major RGBA32 data independent of Core asset lifetime.</param>
    /// <param name="byteCount">Exact source byte length.</param>
    /// <returns>New distinct Core-owned wrapper.</returns>
    Ps3RuntimeTexture* CreateOwned(std::uint32_t width, std::uint32_t height,
        const std::uint8_t* rgba, std::size_t byteCount);

    /// <summary>Checks owner state before a new texture or mutation can reach the GPU.</summary>
    /// <param name="operation">Operation included in the explicit diagnostic.</param>
    void RequireMutable(const char* operation) const;

    /// <summary>Invokes the live CPU barrier exactly once and guards all reentrant mutations.</summary>
    void FlushBeforeMutation(const char* operation);

    /// <summary>Finds a wrapper only after verifying that it belongs to this exact resource owner.</summary>
    /// <param name="texture">Live wrapper reference to validate.</param>
    /// <param name="operation">Operation included in the explicit diagnostic.</param>
    /// <returns>Registered GPU texture owner.</returns>
    Ps3AuthoredTexture& FindLive(Ps3RuntimeTexture& texture, const char* operation);

    friend class Ps3RuntimeTexture;

public:
    /// <summary>Creates an empty renderer resource owner with an explicit CPU submission callback.</summary>
    /// <param name="video">Initialized video service, borrowed until all GPU ownership is retired.</param>
    /// <param name="flushPending">Required callback that submits pending CPU-side runs before mutation.</param>
    Ps3Batch2DResources(Ps3Video& video, std::function<void()> flushPending);

    /// <summary>Expires wrapper access without flushing, touching video, or force-releasing undrained GPU owners.</summary>
    ~Ps3Batch2DResources();

    /// <summary>Resource ownership cannot be copied because records own unique RSX allocations.</summary>
    Ps3Batch2DResources(const Ps3Batch2DResources&) = delete;

    /// <summary>Resource ownership cannot be copy-assigned because records own unique RSX allocations.</summary>
    Ps3Batch2DResources& operator=(const Ps3Batch2DResources&) = delete;

    /// <summary>Copies a validated caller-owned RGBA32 asset into a new unique RSX allocation.</summary>
    /// <param name="asset">Borrowed Core asset; this method never disposes it.</param>
    /// <returns>New Core-owned wrapper with engine ownership enabled.</returns>
    Ps3RuntimeTexture* BuildRaw(const ::TextureAsset& asset);

    /// <summary>Deserializes a required source-scoped packaged TextureAsset and copies it into a unique GPU owner.</summary>
    /// <param name="source">Explicit content namespace used for this read.</param>
    /// <param name="path">Canonical nonempty content-relative asset path.</param>
    /// <returns>New Core-owned wrapper independent of source path aliases.</returns>
    Ps3RuntimeTexture* BuildCooked(::IContentStreamSource& source, const std::string& path);

    /// <summary>Resolves a live wrapper to its unique mutable RSX texture owner.</summary>
    /// <param name="texture">Wrapper created by this resource set.</param>
    /// <returns>Its initialized GPU allocation.</returns>
    Ps3AuthoredTexture& Resolve(const Ps3RuntimeTexture& texture);

    /// <summary>Flushes pending CPU work and updates a checked subrectangle without reallocating storage.</summary>
    /// <param name="texture">Live wrapper created by this resource set.</param>
    /// <param name="x">Destination rectangle left edge.</param>
    /// <param name="y">Destination rectangle top edge.</param>
    /// <param name="width">Nonzero destination rectangle width.</param>
    /// <param name="height">Nonzero destination rectangle height.</param>
    /// <param name="rgba">Source bytes in RGBA order.</param>
    /// <param name="byteCount">Available bytes in the supplied row storage.</param>
    /// <param name="rowPitch">Byte distance between source rows.</param>
    void Update(Ps3RuntimeTexture& texture, std::uint32_t x, std::uint32_t y,
        std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba,
        std::size_t byteCount, std::size_t rowPitch);

    /// <summary>Flushes and retires one owned allocation, or performs logical disposal after final shutdown.</summary>
    /// <param name="texture">Wrapper being disposed by Core or a caller.</param>
    void Retire(Ps3RuntimeTexture& texture);

    /// <summary>Flushes pending CPU runs and removes the callback before its manager can be destroyed.</summary>
    void DetachCpuSubmission();

    /// <summary>Releases all remaining owners only after CPU detach and the video's successful terminal drain.</summary>
    /// <param name="error">Receives an explicit lifecycle or low-level retirement failure.</param>
    /// <returns>True after all records are safely cleared; false retains any unresolved owners.</returns>
    bool ShutdownAfterDrain(std::string& error);

    /// <summary>Returns the exact total GPU texture bytes retained by this resource set.</summary>
    std::size_t OwnedGpuBytes() const;
};

} // namespace helengine::ps3

#endif
