#include "platform/ps3/Ps3Batch2DResources.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

#include "AssetSerializer.hpp"
#include "IContentStreamSource.hpp"
#include "TextureAsset.hpp"
#include "platform/ps3/Ps3ArtifactReadUtilities.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3Batch2DFlushGuard.hpp"
#include "platform/ps3/Ps3RuntimeTexture.hpp"
#include "platform/ps3/Ps3Video.hpp"
#include "platform/ps3/Ps3GtfTexture.hpp"
#include "platform/ps3/Ps3TexturePrefixStream.hpp"
#include "runtime/native_exceptions.hpp"
#include "system/io/stream.hpp"

namespace helengine::ps3 {
namespace {

/// <summary>Maximum serialized RGBA bytes for one 4096-by-4096 texture.</summary>
constexpr std::size_t MaximumTexturePayloadBytes = 64u * 1024u * 1024u;

/// <summary>Bounded serializer and identity metadata allowance above the maximum texture payload.</summary>
constexpr std::size_t MaximumCookedMetadataBytes = 1u * 1024u * 1024u;

/// <summary>Maximum complete packaged texture size, including bounded serialized metadata.</summary>
constexpr std::size_t MaximumCookedEnvelopeBytes = MaximumTexturePayloadBytes + MaximumCookedMetadataBytes;

/// <summary>Rejects source paths that are absolute, ambiguous, noncanonical or unsafe before opening a stream.</summary>
void ValidateContentPath(const std::string& path) {
    if (path.empty() || path.size() > 65536u || path.front() == '/' || path.back() == '/') {
        throw std::runtime_error("content path must be a nonempty bounded relative path");
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(path.data());
    std::size_t cursor = 0u;
    std::size_t segmentStart = 0u;
    bool visibleInSegment = false;
    while (cursor < path.size()) {
        const std::size_t characterStart = cursor;
        std::uint32_t scalar = 0u;
        if (!Ps3ArtifactReadUtilities::ReadUtf8CodePoint(bytes, path.size(), cursor, scalar)
            || scalar < 32u || (scalar >= 127u && scalar <= 159u) || scalar == '\\' || scalar == ':') {
            throw std::runtime_error("content path contains invalid UTF-8, a control, backslash, or colon");
        }
        if (scalar == '/') {
            const std::string segment = path.substr(segmentStart, characterStart - segmentStart);
            if (!visibleInSegment || segment == "." || segment == "..") {
                throw std::runtime_error("content path contains an empty, blank, or traversal segment");
            }
            segmentStart = cursor;
            visibleInSegment = false;
        } else if (!Ps3ArtifactReadUtilities::IsWhitespace(scalar)) {
            visibleInSegment = true;
        }
    }
    const std::string finalSegment = path.substr(segmentStart);
    if (!visibleInSegment || finalSegment == "." || finalSegment == "..") {
        throw std::runtime_error("content path contains an empty, blank, or traversal segment");
    }
}

/// <summary>Disposes and deletes an owned stream once, even when generated Dispose reports an error.</summary>
void DisposeStream(std::unique_ptr<::Stream>& stream) {
    std::unique_ptr<::Stream> closing = std::move(stream);
    if (closing) closing->Dispose();
}

/// <summary>Attempts stream disposal while preserving the read/deserialization exception already in flight.</summary>
void DisposeFailedStream(std::unique_ptr<::Stream>& stream) noexcept {
    try {
        DisposeStream(stream);
    } catch (::Exception* exception) {
        std::printf("PS3_BATCH2D_RESOURCES_ERROR stage=stream-dispose-during-failure reason=%s\n",
            exception == nullptr ? "null generated exception" : exception->what());
        delete exception;
    } catch (const std::exception& exception) {
        std::printf("PS3_BATCH2D_RESOURCES_ERROR stage=stream-dispose-during-failure reason=%s\n", exception.what());
    } catch (...) {
        std::printf("PS3_BATCH2D_RESOURCES_ERROR stage=stream-dispose-during-failure reason=unknown\n");
    }
}

/// <summary>Attempts temporary TextureAsset disposal without replacing the primary cooked-input failure.</summary>
void DisposeFailedTexture(::TextureAsset* texture, const std::string& path) noexcept {
    if (texture == nullptr) return;
    try {
        texture->Dispose();
    } catch (::Exception* exception) {
        std::printf("PS3_BATCH2D_RESOURCES_ERROR stage=texture-dispose-during-failure path=%s reason=%s\n",
            path.c_str(), exception == nullptr ? "null generated exception" : exception->what());
        delete exception;
    } catch (const std::exception& exception) {
        std::printf("PS3_BATCH2D_RESOURCES_ERROR stage=texture-dispose-during-failure path=%s reason=%s\n",
            path.c_str(), exception.what());
    } catch (...) {
        std::printf("PS3_BATCH2D_RESOURCES_ERROR stage=texture-dispose-during-failure path=%s reason=unknown\n",
            path.c_str());
    }
}

} // namespace

/// <summary>Stores borrowed video and callback services while creating the wrapper lifetime token.</summary>
Ps3Batch2DResources::Ps3Batch2DResources(Ps3Video& video, std::function<void()> flushPending)
    : Video(video)
    , FlushPending(std::move(flushPending))
    , LifetimeSentinel(std::make_shared<unsigned char>(0u))
    , OwnedGpuBytesValue(0u)
    , FlushInProgress(false)
    , CpuSubmissionDetached(false)
    , ShutdownComplete(false) {
    if (!FlushPending) throw std::invalid_argument("PS3 2D resources require a live CPU flush callback");
}

/// <summary>Expires wrappers before member destruction and deliberately leaves undrained GPU allocations untouched.</summary>
Ps3Batch2DResources::~Ps3Batch2DResources() {
    LifetimeSentinel.reset();
}

/// <summary>Rejects GPU-facing mutations once submission, video context, or callback safety has ended.</summary>
void Ps3Batch2DResources::RequireMutable(const char* operation) const {
    if (ShutdownComplete) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": owner is shut down");
    if (CpuSubmissionDetached) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": CPU submission is detached");
    if (Video.HasDrained() || Video.Context() == nullptr) {
        throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": video is not available for submission");
    }
    if (FlushInProgress) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": reentrant mutation during CPU flush");
}

/// <summary>Runs the active callback under an exception-safe guard so nested resource writes fail visibly.</summary>
void Ps3Batch2DResources::FlushBeforeMutation(const char* operation) {
    if (CpuSubmissionDetached) return;
    if (!FlushPending) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": CPU flush callback is unavailable");
    if (FlushInProgress) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": reentrant mutation during CPU flush");
    Ps3Batch2DFlushGuard guard(FlushInProgress);
    FlushPending();
}

/// <summary>Validates owner identity, private disposal state, and exact wrapper registry membership.</summary>
Ps3AuthoredTexture& Ps3Batch2DResources::FindLive(Ps3RuntimeTexture& texture, const char* operation) {
    if (texture.OwnerValue != this) throw std::invalid_argument(std::string("PS3 2D resources ") + operation + ": foreign wrapper");
    if (texture.IsNativeDisposed()) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": wrapper is retired");
    const auto found = Records.find(&texture);
    if (found == Records.end()) throw std::runtime_error(std::string("PS3 2D resources ") + operation + ": wrapper has no live GPU record");
    return *found->second;
}

/// <summary>Preallocates CPU publication state, initializes one GPU texture, then publishes without further allocation.</summary>
Ps3RuntimeTexture* Ps3Batch2DResources::CreateOwned(std::uint32_t width, std::uint32_t height,
    const std::uint8_t* rgba, std::size_t byteCount) {
    RequireMutable("create texture");
    if (width == 0u || height == 0u || width > 4096u || height > 4096u || rgba == nullptr) {
        throw std::invalid_argument("PS3 2D texture creation requires nonnull RGBA data and dimensions 1..4096");
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (byteCount != expected) throw std::invalid_argument("PS3 2D texture byte count must equal width*height*4");
    if (OwnedGpuBytesValue > std::numeric_limits<std::size_t>::max() - expected) {
        throw std::overflow_error("PS3 2D texture total GPU byte count overflows size_t");
    }

    // Allocate wrapper, owner and stable map node before initializing the GPU allocation.
    std::unique_ptr<Ps3RuntimeTexture> wrapper(new Ps3RuntimeTexture(*this, width, height));
    std::map<Ps3RuntimeTexture*, std::unique_ptr<Ps3AuthoredTexture>> pending;
    std::unique_ptr<Ps3AuthoredTexture> textureOwner(new Ps3AuthoredTexture());
    auto inserted = pending.emplace(wrapper.get(), std::move(textureOwner));
    std::string error;
    if (!inserted.first->second->InitializeRgba2D(Video, width, height, rgba, byteCount, error)) {
        throw std::runtime_error("PS3 2D texture initialization failed: " + error);
    }
    Records.merge(pending);
    OwnedGpuBytesValue += expected;
    return wrapper.release();
}

/// <summary>Preserves native RSX storage and publishes its wrapper only after all allocations and upload checks pass.</summary>
Ps3RuntimeTexture* Ps3Batch2DResources::CreateOwnedGtf(const std::uint8_t* bytes, std::size_t byteCount) {
    RequireMutable("create GTF texture");
    Ps3GtfTexture descriptor;
    std::string error;
    if (!Ps3GtfTexture::TryParse(bytes, byteCount, descriptor, error)) throw std::invalid_argument("PS3 GTF texture is invalid: " + error);
    if (OwnedGpuBytesValue > std::numeric_limits<std::size_t>::max() - descriptor.PixelBytes)
        throw std::overflow_error("PS3 GTF total GPU byte count overflows size_t");
    std::unique_ptr<Ps3RuntimeTexture> wrapper(new Ps3RuntimeTexture(*this, descriptor.Width, descriptor.Height));
    std::map<Ps3RuntimeTexture*, std::unique_ptr<Ps3AuthoredTexture>> pending;
    auto inserted = pending.emplace(wrapper.get(), std::unique_ptr<Ps3AuthoredTexture>(new Ps3AuthoredTexture()));
    if (!inserted.first->second->InitializeGtf2D(Video, bytes, byteCount, error)) throw std::runtime_error("PS3 GTF upload failed: " + error);
    Records.merge(pending);
    OwnedGpuBytesValue += descriptor.PixelBytes;
    return wrapper.release();
}

/// <summary>Validates and snapshots a caller-owned generated asset before creating renderer-owned GPU storage.</summary>
Ps3RuntimeTexture* Ps3Batch2DResources::BuildRaw(const ::TextureAsset& asset) {
    RequireMutable("build raw texture");
    const std::uint32_t width = asset.Width;
    const std::uint32_t height = asset.Height;
    if (static_cast<unsigned>(asset.ColorFormat) != 0u) {
        throw std::invalid_argument("PS3 raw texture requires RGBA32 color format");
    }
    if (width == 0u || height == 0u || width > 4096u || height > 4096u) {
        throw std::invalid_argument("PS3 raw texture dimensions must each be between one and 4096");
    }
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (asset.Colors == nullptr || asset.Colors->get_Length() < 0
        || static_cast<std::size_t>(asset.Colors->get_Length()) != expected) {
        throw std::invalid_argument("PS3 raw texture RGBA byte count must equal width*height*4");
    }
    std::vector<std::uint8_t> rgba(expected);
    for (std::size_t index = 0u; index < expected; ++index) {
        rgba[index] = (*asset.Colors)[static_cast<std::int32_t>(index)];
    }
    return CreateOwned(width, height, rgba.data(), rgba.size());
}

/// <summary>Deserializes one explicitly source-scoped packaged texture and releases every temporary CPU object.</summary>
Ps3RuntimeTexture* Ps3Batch2DResources::BuildCooked(::IContentStreamSource& source, const std::string& path) {
    RequireMutable("build cooked texture");
    ValidateContentPath(path);
    std::unique_ptr<::Stream> input(source.OpenRead(path));
    if (!input) throw std::runtime_error("could not open required texture content: " + path);
    std::unique_ptr<::Asset> asset;
    try {
        const std::size_t length = input->Length();
        if (length < 4u || length > MaximumCookedEnvelopeBytes) {
            throw std::runtime_error("texture content is empty or exceeds its 64 MiB payload plus 1 MiB metadata envelope: " + path);
        }
        std::array<std::uint8_t, 4u> prefix{};
        std::size_t cursor = 0u;
        while (cursor < prefix.size()) {
            const std::size_t read = input->Read(prefix.data(), cursor, prefix.size() - cursor);
            if (read == 0u || read > prefix.size() - cursor) throw std::runtime_error("texture content signature is truncated: " + path);
            cursor += read;
        }
        if (prefix[0] == 2u) {
            std::vector<std::uint8_t> gtf(length);
            std::copy(prefix.begin(), prefix.end(), gtf.begin());
            while (cursor < length) {
                const std::size_t read = input->Read(gtf.data(), cursor, length - cursor);
                if (read == 0u || read > length - cursor) throw std::runtime_error("GTF content payload is truncated: " + path);
                cursor += read;
            }
            DisposeStream(input);
            return CreateOwnedGtf(gtf.data(), gtf.size());
        }
        Ps3TexturePrefixStream replay(*input, prefix);
        asset.reset(::AssetSerializer::Deserialize(&replay));
        DisposeStream(input);
    } catch (::Exception* exception) {
        DisposeFailedStream(input);
        DisposeFailedTexture(dynamic_cast<::TextureAsset*>(asset.get()), path);
        const std::string reason = exception == nullptr ? "null generated exception" : exception->what();
        delete exception;
        throw std::runtime_error("cooked texture deserialization failed for " + path + ": " + reason);
    } catch (...) {
        DisposeFailedStream(input);
        DisposeFailedTexture(dynamic_cast<::TextureAsset*>(asset.get()), path);
        throw;
    }

    auto* texture = dynamic_cast<::TextureAsset*>(asset.get());
    if (texture == nullptr) throw std::runtime_error("cooked texture content is not a TextureAsset: " + path);
    const std::uint32_t width = texture->Width;
    const std::uint32_t height = texture->Height;
    std::vector<std::uint8_t> rgba;
    try {
        if (static_cast<unsigned>(texture->ColorFormat) != 0u || width == 0u || height == 0u
            || width > 4096u || height > 4096u) {
            throw std::runtime_error("cooked TextureAsset requires RGBA32 and dimensions 1..4096: " + path);
        }
        const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        if (texture->Colors == nullptr || texture->Colors->get_Length() < 0
            || static_cast<std::size_t>(texture->Colors->get_Length()) != expected) {
            throw std::runtime_error("cooked TextureAsset RGBA byte count differs from width*height*4: " + path);
        }
        rgba.resize(expected);
        for (std::size_t index = 0u; index < expected; ++index) {
            rgba[index] = (*texture->Colors)[static_cast<std::int32_t>(index)];
        }
    } catch (...) {
        DisposeFailedTexture(texture, path);
        throw;
    }
    try {
        texture->Dispose();
    } catch (::Exception* exception) {
        const std::string reason = exception == nullptr ? "null generated exception" : exception->what();
        delete exception;
        throw std::runtime_error("cooked texture disposal failed for " + path + ": " + reason);
    }
    return CreateOwned(width, height, rgba.data(), rgba.size());
}

/// <summary>Returns the allocation only for a live wrapper owned by this registry before CPU detach.</summary>
Ps3AuthoredTexture& Ps3Batch2DResources::Resolve(const Ps3RuntimeTexture& texture) {
    if (texture.OwnerValue != this) throw std::invalid_argument("PS3 2D resources resolve: foreign wrapper");
    if (ShutdownComplete || CpuSubmissionDetached) throw std::runtime_error("PS3 2D resources resolve: submission is detached or shut down");
    if (texture.IsNativeDisposed()) throw std::runtime_error("PS3 2D resources resolve: wrapper is retired");
    const auto found = Records.find(const_cast<Ps3RuntimeTexture*>(&texture));
    if (found == Records.end()) throw std::runtime_error("PS3 2D resources resolve: wrapper has no live GPU record");
    return *found->second;
}

/// <summary>Flushes earlier CPU submissions before validating and synchronizing a checked in-place texture update.</summary>
void Ps3Batch2DResources::Update(Ps3RuntimeTexture& texture, std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba,
    std::size_t byteCount, std::size_t rowPitch) {
    RequireMutable("update texture");
    Ps3AuthoredTexture& textureOwner = FindLive(texture, "update texture");
    FlushBeforeMutation("update texture");
    std::string error;
    if (!textureOwner.UpdateRgbaRegion(x, y, width, height, rgba, byteCount, rowPitch, error)) {
        throw std::runtime_error("PS3 2D texture update failed: " + error);
    }
}

/// <summary>Retires one allocation after pending CPU and RSX work, retaining it on every failure.</summary>
void Ps3Batch2DResources::Retire(Ps3RuntimeTexture& texture) {
    if (texture.OwnerValue != this) throw std::invalid_argument("PS3 2D resources retire: foreign wrapper");
    if (ShutdownComplete) {
        texture.MarkNativeDisposed();
        return;
    }
    if (texture.IsNativeDisposed()) {
        texture.IsDisposed = true;
        return;
    }
    (void)FindLive(texture, "retire texture");
    const auto found = Records.find(&texture);
    Ps3AuthoredTexture& textureOwner = *found->second;
    const std::size_t bytesToRelease = textureOwner.OwnedGpuBytes();
    if (!CpuSubmissionDetached) FlushBeforeMutation("retire texture");
    std::string error;
    const bool retired = Video.HasDrained()
        ? textureOwner.ShutdownAfterDrain(error)
        : textureOwner.RetireAfterIdle(error);
    if (!retired) throw std::runtime_error("PS3 2D texture retirement failed: " + error);
    OwnedGpuBytesValue -= bytesToRelease;
    Records.erase(found);
    texture.MarkNativeDisposed();
}

/// <summary>Flushes once and clears the manager callback before its borrowed lifetime ends.</summary>
void Ps3Batch2DResources::DetachCpuSubmission() {
    if (CpuSubmissionDetached) return;
    if (ShutdownComplete) throw std::runtime_error("PS3 2D resources detach: owner is already shut down");
    if (FlushInProgress) throw std::runtime_error("PS3 2D resources detach: reentrant mutation during CPU flush");
    FlushBeforeMutation("detach CPU submission");
    CpuSubmissionDetached = true;
    FlushPending = std::function<void()>();
}

/// <summary>Releases remaining texture owners only after both CPU detach and successful terminal video drain.</summary>
bool Ps3Batch2DResources::ShutdownAfterDrain(std::string& error) {
    error.clear();
    if (ShutdownComplete) return true;
    if (!Video.HasDrained()) {
        error = "PS3 2D resources require successful final video drain";
        return false;
    }
    if (!CpuSubmissionDetached) {
        error = "PS3 2D resources require CPU submission detach before final shutdown";
        return false;
    }
    auto entry = Records.begin();
    while (entry != Records.end()) {
        auto retiring = entry++;
        const std::size_t bytesToRelease = retiring->second->OwnedGpuBytes();
        if (bytesToRelease > OwnedGpuBytesValue) {
            error = "PS3 2D resource GPU byte accounting is inconsistent before final shutdown";
            return false;
        }
        if (!retiring->second->ShutdownAfterDrain(error)) return false;
        OwnedGpuBytesValue -= bytesToRelease;
        Records.erase(retiring);
    }
    if (OwnedGpuBytesValue != 0u) {
        error = "PS3 2D resource final shutdown left unregistered GPU bytes";
        return false;
    }
    ShutdownComplete = true;
    return true;
}

/// <summary>Returns the cached sum without traversing wrappers, sources, or generated Core objects.</summary>
std::size_t Ps3Batch2DResources::OwnedGpuBytes() const {
    return OwnedGpuBytesValue;
}

} // namespace helengine::ps3

#endif
