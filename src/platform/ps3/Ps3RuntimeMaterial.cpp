#include "platform/ps3/Ps3RuntimeMaterial.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
#include <stdexcept>
#include <utility>
#include "platform/ps3/Ps3AuthoredProgram.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3MaterialArtifactReader.hpp"

namespace helengine::ps3 {
    /// Retains the complete CPU payload and validates every borrowed resource before publication.
    Ps3RuntimeMaterial::Ps3RuntimeMaterial(Ps3CookedMaterial material, Ps3AuthoredProgram& program,
        Ps3AuthoredTexture& diffuse, Ps3AuthoredTexture& roughness,
        Ps3AuthoredTexture& emissive, Ps3AuthoredTexture& shadowAtlas,
        Ps3AuthoredTexture& pointShadow)
        : Material(std::move(material)), Program(&program), DiffuseTexture(&diffuse),
          RoughnessTexture(&roughness), EmissiveTexture(&emissive),
          ShadowAtlasTexture(&shadowAtlas), PointShadowTexture(&pointShadow) {
        std::string error;
        if (!Ps3MaterialArtifactReader::MatchesProgram(Material, program.Program(), error)) {
            throw std::invalid_argument(error);
        }
        if (program.OwnedGpuBytes() == 0 || diffuse.OwnedGpuBytes() == 0 ||
            roughness.OwnedGpuBytes() == 0 || emissive.OwnedGpuBytes() == 0 ||
            shadowAtlas.OwnedGpuBytes() == 0 || pointShadow.OwnedGpuBytes() == 0) {
            throw std::invalid_argument("authored material requires initialized cache-owned resources");
        }
    }

    /// Returns the version-one lit bit without keeping duplicate mutable state.
    bool Ps3RuntimeMaterial::IsLit() const { return (Material.SurfaceFlags & 1u) != 0; }

    /// Returns the version-one double-sided bit without keeping duplicate mutable state.
    bool Ps3RuntimeMaterial::IsDoubleSided() const { return (Material.SurfaceFlags & 2u) != 0; }
}
#endif
