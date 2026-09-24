#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
#include "RuntimeMaterial.hpp"
#include "platform/ps3/Ps3CookedMaterial.hpp"

namespace helengine::ps3 {
    class Ps3AuthoredProgram;
    class Ps3AuthoredTexture;

    /// Owns immutable cooked material state and borrows renderer-cache resources through Core disposal.
    class Ps3RuntimeMaterial final : public ::RuntimeMaterial {
    public:
        /// Exact validated P3MT state; the sole source of color and surface flags.
        const Ps3CookedMaterial Material;
        /// Initialized shader pair retained by the renderer's source-specific cache.
        Ps3AuthoredProgram* const Program;
        /// Authored diffuse image, or the schema's intentional white texture.
        Ps3AuthoredTexture* const DiffuseTexture;
        /// Explicit white roughness input for the version-one surface schema.
        Ps3AuthoredTexture* const RoughnessTexture;
        /// Explicit black emissive input for the no-emission schema.
        Ps3AuthoredTexture* const EmissiveTexture;
        /// Explicit white 2D input for disabled atlas shadows.
        Ps3AuthoredTexture* const ShadowAtlasTexture;
        /// One explicit white cube shared by all four disabled point-shadow samplers.
        Ps3AuthoredTexture* const PointShadowTexture;

        /// Requires validated cooked state and initialized cache-owned resources; never acquires GPU ownership.
        Ps3RuntimeMaterial(Ps3CookedMaterial material, Ps3AuthoredProgram& program,
            Ps3AuthoredTexture& diffuse, Ps3AuthoredTexture& roughness,
            Ps3AuthoredTexture& emissive, Ps3AuthoredTexture& shadowAtlas,
            Ps3AuthoredTexture& pointShadow);
        /// Prevents copying the generated base's owned material containers.
        Ps3RuntimeMaterial(const Ps3RuntimeMaterial&) = delete;
        /// Prevents assigning the generated base's owned material containers.
        Ps3RuntimeMaterial& operator=(const Ps3RuntimeMaterial&) = delete;
        /// Reads the authored lighting choice directly from P3MT surface flags.
        bool IsLit() const;
        /// Reads the authored culling choice directly from P3MT surface flags.
        bool IsDoubleSided() const;
    };
}
#endif
