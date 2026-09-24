#include "platform/ps3/Ps3AuthoredContentCache.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>
#include "AssetSerializer.hpp"
#include "IContentStreamSource.hpp"
#include "TextureAsset.hpp"
#include "system/io/stream.hpp"
#include "runtime/native_exceptions.hpp"
#include "platform/ps3/Ps3ArtifactReadUtilities.hpp"
#include "platform/ps3/Ps3AuthoredProgram.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3MaterialArtifactReader.hpp"
#include "platform/ps3/Ps3RuntimeMaterial.hpp"
#include "platform/ps3/Ps3ShaderArtifactReader.hpp"
#include "platform/ps3/Ps3Video.hpp"

namespace helengine::ps3 {
    namespace {
        /// Rejects ambiguous source-relative keys before they can reach content I/O or cache lookup.
        void ValidateContentPath(const std::string& path) {
            if (path.empty() || path.size() > 65536u || path.front() == '/' || path.back() == '/') {
                throw std::runtime_error("content path must be a nonempty bounded relative path");
            }
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(path.data());
            std::size_t cursor = 0, segmentStart = 0;
            bool visible = false;
            while (cursor < path.size()) {
                const std::size_t start = cursor;
                std::uint32_t scalar = 0;
                if (!Ps3ArtifactReadUtilities::ReadUtf8CodePoint(bytes, path.size(), cursor, scalar) ||
                    scalar < 32u || (scalar >= 127u && scalar <= 159u) || scalar == '\\' || scalar == ':') {
                    throw std::runtime_error("content path contains invalid UTF-8, a control, backslash, or colon");
                }
                if (scalar == '/') {
                    const std::string segment = path.substr(segmentStart, start - segmentStart);
                    if (!visible || segment == "." || segment == "..") {
                        throw std::runtime_error("content path contains an empty, blank, or traversal segment");
                    }
                    visible = false;
                    segmentStart = cursor;
                } else if (!Ps3ArtifactReadUtilities::IsWhitespace(scalar)) {
                    visible = true;
                }
            }
            const std::string segment = path.substr(segmentStart);
            if (!visible || segment == "." || segment == "..") {
                throw std::runtime_error("content path contains an empty, blank, or traversal segment");
            }
        }

        /// Disposes and deletes an owned stream exactly once, including when Dispose itself throws.
        void DisposeStream(std::unique_ptr<::Stream>& stream) {
            std::unique_ptr<::Stream> closing = std::move(stream);
            if (closing) closing->Dispose();
        }

        /// Preserves the original read/deserialization exception while still attempting stream disposal.
        void DisposeFailedStream(std::unique_ptr<::Stream>& stream) {
            try { DisposeStream(stream); }
            catch (::Exception* exception) {
                std::printf("PS3_AUTHORED_CONTENT_ERROR stage=stream-dispose-during-failure reason=%s\n",
                    exception == nullptr ? "null generated exception" : exception->what());
                delete exception;
            }
            catch (...) { std::printf("PS3_AUTHORED_CONTENT_ERROR stage=stream-dispose-during-failure\n"); }
        }

        /// Preserves a pending primary failure while releasing temporary texture arrays.
        void DisposeFailedTexture(::TextureAsset* texture, const std::string& path) noexcept {
            if (texture == nullptr) return;
            try { texture->Dispose(); }
            catch (::Exception* exception) {
                std::printf("PS3_AUTHORED_CONTENT_ERROR stage=texture-dispose-during-failure path=%s reason=%s\n",
                    path.c_str(), exception == nullptr ? "null generated exception" : exception->what());
                delete exception;
            } catch (const std::exception& exception) {
                std::printf("PS3_AUTHORED_CONTENT_ERROR stage=texture-dispose-during-failure path=%s reason=%s\n",
                    path.c_str(), exception.what());
            } catch (...) {
                std::printf("PS3_AUTHORED_CONTENT_ERROR stage=texture-dispose-during-failure path=%s reason=unknown-secondary-exception\n",
                    path.c_str());
            }
        }
    }

    /// Starts an isolated cache whose source pointer is never used by its retirement path.
    Ps3AuthoredContentCache::Ps3AuthoredContentCache(Ps3Video& video, ::IContentStreamSource& source)
        : Video(video), Source(&source), ProgramsCreated(0), ProgramsReused(0),
          TexturesCreated(0), TexturesReused(0), ShutdownComplete(false) {}

    /// GPU-owner destructors deliberately do not infer drain safety from the borrowed video.
    Ps3AuthoredContentCache::~Ps3AuthoredContentCache() = default;

    /// Source pointer equality is the cache namespace boundary while Core is alive.
    bool Ps3AuthoredContentCache::MatchesSource(const ::IContentStreamSource& source) const {
        return Source == &source;
    }

    /// Validates size before allocation, rejects zero progress and growth, and closes every opened stream.
    std::vector<std::uint8_t> Ps3AuthoredContentCache::ReadBounded(const std::string& path, std::size_t maximumBytes) {
        ValidateContentPath(path);
        std::unique_ptr<::Stream> stream(Source->OpenRead(path));
        if (!stream) throw std::runtime_error("could not open required content: " + path);
        try {
            const std::size_t length = stream->Length();
            if (length == 0 || length > maximumBytes) {
                throw std::runtime_error("content length is empty or exceeds its envelope limit: " + path);
            }
            std::vector<std::uint8_t> bytes(length);
            std::size_t offset = 0;
            while (offset < length) {
                const std::size_t count = stream->Read(bytes.data(), offset, length - offset);
                if (count == 0 || count > length - offset) {
                    throw std::runtime_error("short or invalid content read: " + path);
                }
                offset += count;
            }
            if (stream->ReadByte() != -1) throw std::runtime_error("content grew while being read: " + path);
            DisposeStream(stream);
            return bytes;
        } catch (...) {
            DisposeFailedStream(stream);
            throw;
        }
    }

    /// Publishes only parsed, identity-matched and initialized shader pairs within this source namespace.
    Ps3AuthoredProgram& Ps3AuthoredContentCache::ResolveProgram(const Ps3CookedMaterial& material) {
        const auto found = Programs.find(material.ShaderRelativePath);
        std::string error;
        if (found != Programs.end()) {
            if (!Ps3MaterialArtifactReader::MatchesProgram(material, found->second->Program(), error)) {
                throw std::runtime_error(error);
            }
            ++ProgramsReused;
            return *found->second;
        }
        const auto bytes = ReadBounded(material.ShaderRelativePath, 16u * 1024u * 1024u);
        Ps3CookedShaderProgram program;
        if (!Ps3ShaderArtifactReader::Read(bytes.data(), bytes.size(), program, error) ||
            !Ps3MaterialArtifactReader::MatchesProgram(material, program, error)) {
            throw std::runtime_error("shader " + material.ShaderRelativePath + ": " + error);
        }
        // Allocate the map node before acquiring GPU memory; node merge needs no allocation.
        std::map<std::string, std::unique_ptr<Ps3AuthoredProgram>> pending;
        pending.emplace(material.ShaderRelativePath, std::make_unique<Ps3AuthoredProgram>());
        Ps3AuthoredProgram& owner = *pending.begin()->second;
        if (!owner.Initialize(Video, program, error)) throw std::runtime_error(error);
        Programs.merge(pending);
        ++ProgramsCreated;
        return owner;
    }

    /// Consumes TextureAsset through the same source and requires exact RGBA32 bytes before RSX allocation.
    Ps3AuthoredTexture& Ps3AuthoredContentCache::ResolveTexture(const std::string& path) {
        ValidateContentPath(path);
        const auto found = Textures.find(path);
        if (found != Textures.end()) { ++TexturesReused; return *found->second; }
        std::unique_ptr<::Stream> stream(Source->OpenRead(path));
        if (!stream) throw std::runtime_error("could not open required diffuse texture: " + path);
        std::unique_ptr<::Asset> asset;
        try {
            asset.reset(::AssetSerializer::Deserialize(stream.get()));
            DisposeStream(stream);
        } catch (...) {
            DisposeFailedStream(stream);
            auto* failedTexture = dynamic_cast<::TextureAsset*>(asset.get());
            DisposeFailedTexture(failedTexture, path);
            throw;
        }
        auto* texture = dynamic_cast<::TextureAsset*>(asset.get());
        if (texture == nullptr) throw std::runtime_error("diffuse content is not a TextureAsset: " + path);
        std::vector<std::uint8_t> rgba;
        const std::uint32_t width = texture->Width;
        const std::uint32_t height = texture->Height;
        try {
            if (static_cast<unsigned>(texture->ColorFormat) != 0u || width == 0 || height == 0 ||
                width > 4096u || height > 4096u) {
                throw std::runtime_error("diffuse TextureAsset requires Rgba32 and dimensions 1..4096: " + path);
            }
            const std::size_t byteCount = static_cast<std::size_t>(width) * height * 4u;
            if (texture->Colors == nullptr || static_cast<std::size_t>(texture->Colors->get_Length()) != byteCount) {
                throw std::runtime_error("diffuse TextureAsset RGBA byte count differs from width*height*4: " + path);
            }
            rgba.resize(byteCount);
            for (std::size_t index = 0; index < byteCount; ++index) {
                rgba[index] = (*texture->Colors)[static_cast<std::int32_t>(index)];
            }
        } catch (...) {
            DisposeFailedTexture(texture, path);
            throw;
        }
        texture->Dispose();
        std::map<std::string, std::unique_ptr<Ps3AuthoredTexture>> pending;
        pending.emplace(path, std::make_unique<Ps3AuthoredTexture>());
        Ps3AuthoredTexture& owner = *pending.begin()->second;
        std::string error;
        if (!owner.InitializeRgba2D(Video, width, height, rgba.data(), rgba.size(), error)) {
            throw std::runtime_error("diffuse texture " + path + ": " + error);
        }
        Textures.merge(pending);
        ++TexturesCreated;
        return owner;
    }

    /// Creates explicit schema inputs; none is substituted for a nonempty authored texture path.
    void Ps3AuthoredContentCache::EnsureDefaultTextures() {
        const std::uint8_t white[4] = {255u, 255u, 255u, 255u};
        const std::uint8_t black[4] = {0u, 0u, 0u, 255u};
        std::string error;
        if (!WhiteTexture) {
            auto candidate = std::make_unique<Ps3AuthoredTexture>();
            if (!candidate->InitializeRgba2D(Video, 1u, 1u, white, sizeof(white), error)) throw std::runtime_error(error);
            WhiteTexture = std::move(candidate);
            ++TexturesCreated;
        }
        if (!BlackTexture) {
            auto candidate = std::make_unique<Ps3AuthoredTexture>();
            if (!candidate->InitializeRgba2D(Video, 1u, 1u, black, sizeof(black), error)) throw std::runtime_error(error);
            BlackTexture = std::move(candidate);
            ++TexturesCreated;
        }
        if (!WhiteCube) {
            auto candidate = std::make_unique<Ps3AuthoredTexture>();
            if (!candidate->InitializeSolidCube(Video, white, error)) throw std::runtime_error(error);
            WhiteCube = std::move(candidate);
            ++TexturesCreated;
        }
    }

    /// Publishes a material only after validating the complete pair, supported surface flags and texture inputs.
    Ps3RuntimeMaterial* Ps3AuthoredContentCache::BuildMaterial(const std::string& path, std::string& error) {
        error.clear();
        try {
            if (ShutdownComplete || Video.HasDrained()) throw std::runtime_error("content cache is retired");
            const auto bytes = ReadBounded(path, 1024u * 1024u);
            Ps3CookedMaterial material;
            if (!Ps3MaterialArtifactReader::Read(bytes.data(), bytes.size(), material, error)) throw std::runtime_error(error);
            if ((material.SurfaceFlags & 4u) != 0) {
                throw std::runtime_error("UseVertexColor is unsupported: ModelAsset has no colors and ForwardStandard VS has no COLOR input");
            }
            if (material.BaseColorA != 255u) throw std::runtime_error("translucent BaseColorA is unsupported by the opaque PS3 standard profile");
            Ps3AuthoredProgram& program = ResolveProgram(material);
            Ps3AuthoredTexture* diffuse = material.DiffuseTextureRelativePath.empty()
                ? nullptr : &ResolveTexture(material.DiffuseTextureRelativePath);
            EnsureDefaultTextures();
            if (diffuse == nullptr) diffuse = WhiteTexture.get();
            auto result = std::make_unique<Ps3RuntimeMaterial>(material, program, *diffuse,
                *WhiteTexture, *BlackTexture, *WhiteTexture, *WhiteCube);
            const auto& identity = program.Program();
            std::printf("PS3_AUTHORED_MATERIAL path=%s asset=%s shader=%s vertex=%s pixel=%s variant=%s sourceHash=%s rgba=%02X%02X%02X%02X flags=%u diffuse=%s\n",
                path.c_str(), material.MaterialAssetId.c_str(), identity.ShaderAssetId.c_str(),
                identity.VertexProgramName.c_str(), identity.PixelProgramName.c_str(), identity.VariantName.c_str(),
                identity.SourceHash.c_str(), material.BaseColorR, material.BaseColorG, material.BaseColorB,
                material.BaseColorA, material.SurfaceFlags, material.DiffuseTextureRelativePath.c_str());
            std::printf("PS3_AUTHORED_CACHE source=%p programsCreated=%lu programsReused=%lu texturesCreated=%lu texturesReused=%lu ownedGpuBytes=%lu\n",
                static_cast<void*>(Source), static_cast<unsigned long>(ProgramsCreated), static_cast<unsigned long>(ProgramsReused),
                static_cast<unsigned long>(TexturesCreated), static_cast<unsigned long>(TexturesReused),
                static_cast<unsigned long>(OwnedGpuBytes()));
            return result.release();
        } catch (::Exception* exception) {
            std::unique_ptr<::Exception> ownedException(exception);
            error = "material " + path + ": " +
                (exception == nullptr ? "null generated exception" : exception->what());
            return nullptr;
        } catch (const std::exception& exception) {
            error = "material " + path + ": " + exception.what();
            return nullptr;
        } catch (...) {
            error = "material " + path + ": unrecognized content exception";
            return nullptr;
        }
    }

    /// Adds current allocations without reading the borrowed content source.
    std::size_t Ps3AuthoredContentCache::OwnedGpuBytes() const {
        std::size_t total = 0;
        for (const auto& entry : Programs) total += entry.second->OwnedGpuBytes();
        for (const auto& entry : Textures) total += entry.second->OwnedGpuBytes();
        if (WhiteTexture) total += WhiteTexture->OwnedGpuBytes();
        if (BlackTexture) total += BlackTexture->OwnedGpuBytes();
        if (WhiteCube) total += WhiteCube->OwnedGpuBytes();
        return total;
    }

    /// Refuses early retirement and preserves owners on failure; no disposed Core source is dereferenced.
    bool Ps3AuthoredContentCache::ShutdownAfterDrain(std::string& error) {
        error.clear();
        if (ShutdownComplete) return true;
        if (!Video.HasDrained()) { error = "authored content cache requires successful video drain"; return false; }
        const std::size_t before = OwnedGpuBytes();
        for (auto& entry : Programs) if (!entry.second->ShutdownAfterDrain(error)) return false;
        for (auto& entry : Textures) if (!entry.second->ShutdownAfterDrain(error)) return false;
        if (WhiteTexture && !WhiteTexture->ShutdownAfterDrain(error)) return false;
        if (BlackTexture && !BlackTexture->ShutdownAfterDrain(error)) return false;
        if (WhiteCube && !WhiteCube->ShutdownAfterDrain(error)) return false;
        if (OwnedGpuBytes() != 0) { error = "authored cache retirement left GPU allocations"; return false; }
        ShutdownComplete = true;
        std::printf("PS3_AUTHORED_CACHE_SHUTDOWN freedGpuBytes=%lu ownedGpuBytes=0 programsCreated=%lu texturesCreated=%lu\n",
            static_cast<unsigned long>(before), static_cast<unsigned long>(ProgramsCreated), static_cast<unsigned long>(TexturesCreated));
        return true;
    }
}
#endif
