#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>

#include "RuntimeMaterial.hpp"
#include "RuntimeModel.hpp"

#include "platform/ps3/Ps3RasterRenderer.hpp"

namespace helengine::ps3 {
    /// Cooked material as the PlayStation 3 renderer needs it.
    ///
    /// The generated base carries shading flags but no base color, because colour lives in
    /// whatever payload a platform cooks for itself. This subclass holds the four channels
    /// the PlayStation 3 cooker writes, so the renderer can read them back without going
    /// through the asset again.
    class Ps3RuntimeMaterial final : public ::RuntimeMaterial {
    public:
        /// Creates a material carrying one cooked base colour.
        /// @param red Base colour red channel.
        /// @param green Base colour green channel.
        /// @param blue Base colour blue channel.
        /// @param alpha Base colour alpha channel.
        /// @param lit Whether the cooked material asked for lighting.
        Ps3RuntimeMaterial(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, bool lit);

        /// Base colour red channel.
        std::uint8_t BaseColorR;

        /// Base colour green channel.
        std::uint8_t BaseColorG;

        /// Base colour blue channel.
        std::uint8_t BaseColorB;

        /// Base colour alpha channel.
        std::uint8_t BaseColorA;

        /// Whether the cooked material asked for lighting.
        bool Lit;
    };

    /// Model owned by the PlayStation 3 renderer.
    ///
    /// The generated base constructor is protected, so a platform supplies its own type.
    /// This one exists to carry geometry once the RSX buffers are built; for now it only
    /// gives Core a valid object with an empty submesh list.
    class Ps3RuntimeModel final : public ::RuntimeModel {
    public:
        /// Creates an empty model that owns no GPU geometry yet.
        Ps3RuntimeModel();

        /// Reports whether RSX buffers have been uploaded for this model.
        bool HasGeometry() const;

        /// Returns the number of indices the model would draw.
        std::uint32_t IndexCount() const;

        /// Returns the RSX buffers backing this model.
        const Ps3MeshBuffers& Buffers() const;

        /// Takes ownership of uploaded RSX buffers.
        /// @param buffers Buffers this model becomes responsible for.
        void AdoptBuffers(const Ps3MeshBuffers& buffers);

    private:
        /// RSX buffers this model owns, zeroed until geometry is uploaded.
        Ps3MeshBuffers MeshBuffers;
    };
}

#endif
