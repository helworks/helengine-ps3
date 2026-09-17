#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>

#include "RenderManager2D.hpp"
#include "runtime/array.hpp"

namespace helengine::ps3 {
    /// Minimal 2D render manager for the v0.2 Core boot gate.
    ///
    /// Core requires a 2D manager to initialize, so this adapter exists to satisfy that
    /// contract while the gate proves the lifecycle. It draws nothing and refuses every
    /// request that would need real texture resources, so an unimplemented path is never
    /// mistaken for an empty overlay.
    ///
    /// The five members overridden here are exactly the abstract ones on the generated base.
    class Ps3RenderManager2D final : public ::RenderManager2D {
    public:
        /// Creates an adapter that has drawn nothing yet.
        Ps3RenderManager2D();

        /// Counts how many times Core asked this manager to draw.
        std::uint32_t DrawCount() const;

        /// Records the draw request and presents nothing.
        void Draw() override;

        /// Refuses texture construction, which belongs to a later milestone.
        /// @param data Raw texture asset Core is asking to realize.
        /// @return Never returns normally.
        ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;

        /// Refuses rounded-rectangle drawing, which the gate has no 2D pipeline for.
        /// @param shape Drawable Core is asking to render.
        void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;

        /// Refuses sprite drawing, which the gate has no 2D pipeline for.
        /// @param sprite Drawable Core is asking to render.
        void DrawSprite(::ISpriteDrawable2D* sprite) override;

        /// Refuses text drawing, which the gate has no font pipeline for.
        /// @param text Drawable Core is asking to render.
        void DrawText(::ITextDrawable2D* text) override;

        /// Releases nothing, because this adapter never hands out textures.
        void Dispose() override;

    protected:
        /// Refuses texture updates, since no texture can have been created to update.
        /// @param texture Texture Core is asking to update.
        /// @param x Left edge of the updated region.
        /// @param y Top edge of the updated region.
        /// @param width Width of the updated region.
        /// @param height Height of the updated region.
        /// @param rgba8 Source pixels.
        /// @param sourceRowPitch Bytes between source rows.
        void UpdateTextureRegionCore(
            ::RuntimeTexture* texture,
            int32_t x,
            int32_t y,
            int32_t width,
            int32_t height,
            Array<uint8_t>* rgba8,
            int32_t sourceRowPitch) override;

    private:
        /// Number of draw requests received from Core.
        std::uint32_t DrawCounter;

        /// Whether the first draw has already been logged, so the log stays readable.
        bool LoggedFirstDraw;
    };
}

#endif
