#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class IContentStreamSource;

namespace helengine::ps3 {
    class Ps3AuthoredProgram;
    class Ps3AuthoredTexture;
    struct Ps3CookedMaterial;
    class Ps3RuntimeMaterial;
    class Ps3Video;

    /// Owns authored resources for exactly one Core content source until the renderer's terminal drain.
    class Ps3AuthoredContentCache {
    private:
        /// Borrowed video owner, alive through explicit shutdown.
        Ps3Video& Video;
        /// Source identity used only while Core is alive; shutdown never dereferences this pointer.
        ::IContentStreamSource* const Source;
        /// Fully initialized shader pairs keyed by validated path within this source only.
        std::map<std::string, std::unique_ptr<Ps3AuthoredProgram>> Programs;
        /// Fully initialized authored 2D textures keyed by validated path within this source only.
        std::map<std::string, std::unique_ptr<Ps3AuthoredTexture>> Textures;
        /// Explicit white diffuse/roughness/disabled-atlas input, separate from authored path keys.
        std::unique_ptr<Ps3AuthoredTexture> WhiteTexture;
        /// Explicit black input for disabled emissive texturing.
        std::unique_ptr<Ps3AuthoredTexture> BlackTexture;
        /// Explicit white cube shared by the four disabled point-shadow inputs.
        std::unique_ptr<Ps3AuthoredTexture> WhiteCube;
        /// Number of successfully published shader owners.
        std::size_t ProgramsCreated;
        /// Number of validated same-source program reuses.
        std::size_t ProgramsReused;
        /// Number of successfully published texture owners, including defaults.
        std::size_t TexturesCreated;
        /// Number of same-source authored texture reuses.
        std::size_t TexturesReused;
        /// Prevents content loads after successful final resource retirement.
        bool ShutdownComplete;

    public:
        /// Binds a cache to a non-null source and live video without allocating resources.
        Ps3AuthoredContentCache(Ps3Video& video, ::IContentStreamSource& source);
        /// Destroys CPU containers; GPU owners retain storage if explicit drain shutdown was omitted.
        ~Ps3AuthoredContentCache();
        /// Prevents duplicate ownership of authored resources.
        Ps3AuthoredContentCache(const Ps3AuthoredContentCache&) = delete;
        /// Prevents replacing a live resource cache by assignment.
        Ps3AuthoredContentCache& operator=(const Ps3AuthoredContentCache&) = delete;
        /// Compares source identity without opening content or conflating equal relative paths.
        bool MatchesSource(const ::IContentStreamSource& source) const;
        /// Loads the exact P3MT/P3SP pair and returns a Core-owned material only after all inputs succeed.
        Ps3RuntimeMaterial* BuildMaterial(const std::string& path, std::string& error);
        /// Retires all GPU owners after successful drain without accessing the possibly disposed source.
        bool ShutdownAfterDrain(std::string& error);
        /// Sums live shader and texture allocations for renderer shutdown diagnostics.
        std::size_t OwnedGpuBytes() const;

    private:
        /// Reads a nonempty bounded envelope through partial reads and verifies terminal EOF and disposal.
        std::vector<std::uint8_t> ReadBounded(const std::string& path, std::size_t maximumBytes);
        /// Loads or reuses a shader, always matching the complete material identity before GPU initialization.
        Ps3AuthoredProgram& ResolveProgram(const Ps3CookedMaterial& material);
        /// Deserializes a required RGBA32 TextureAsset and publishes it only after successful GPU initialization.
        Ps3AuthoredTexture& ResolveTexture(const std::string& path);
        /// Creates only the schema's explicit white, black, and solid-cube resources.
        void EnsureDefaultTextures();
    };
}
#endif
