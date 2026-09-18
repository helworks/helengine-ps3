#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>
#include <string>

#include "RenderManager2D.hpp"
#include "runtime/array.hpp"

namespace helengine::ps3 {
    /// Accepting 2D render manager for PlayStation 3, which has no 2D pipeline yet.
    ///
    /// Authored scenes carry overlay content that Core drives every frame, so these entry
    /// points accept the work and draw nothing rather than raising. That lets a scene's 3D
    /// content run while its overlay is absent. Each entry point reports its first use once,
    /// so the gap stays visible in the console log without flooding it.
    ///
    /// The five members overridden here are exactly the abstract ones on the generated base.
    class Ps3RenderManager2D final : public ::RenderManager2D {
    public:
        /// Creates an adapter that has drawn nothing yet.
        Ps3RenderManager2D();

        /// Counts how many times Core asked this manager to draw.
        std::uint32_t DrawCount() const;

        /// Counts individual 2D draw requests accepted and not drawn.
        std::uint32_t SkippedDrawCount() const;

        /// Records the draw request and presents nothing.
        void Draw() override;

        /// Accepts a cooked texture and returns an object with no GPU memory behind it.
        /// @param cookedAssetPath Content-relative path to the cooked texture.
        /// @param contentStreamSource Source the path would be resolved against.
        /// @return An owned texture with no pixels.
        ::RuntimeTexture* BuildTextureFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;

        /// Returns a texture object with no GPU memory behind it.
        /// @param data Raw texture asset Core is asking to realize.
        /// @return An owned texture carrying the source dimensions and nothing else.
        ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;

        /// Accepts rounded-rectangle drawing and draws nothing.
        /// @param shape Drawable Core is asking to render.
        void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;

        /// Accepts sprite drawing and draws nothing.
        /// @param sprite Drawable Core is asking to render.
        void DrawSprite(::ISpriteDrawable2D* sprite) override;

        /// Accepts text drawing and draws nothing.
        /// @param text Drawable Core is asking to render.
        void DrawText(::ITextDrawable2D* text) override;

        /// Reports the accepted and skipped draw counts.
        void Dispose() override;

    protected:
        /// Accepts texture updates and changes nothing.
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

        /// Whether the first texture build has been reported.
        bool LoggedBuildTexture;

        /// Whether the first rounded-rectangle request has been reported.
        bool LoggedRoundedRect;

        /// Whether the first sprite request has been reported.
        bool LoggedSprite;

        /// Whether the first text request has been reported.
        bool LoggedText;

        /// Whether the first texture update has been reported.
        bool LoggedTextureUpdate;

        /// Number of 2D draw requests accepted and not drawn.
        std::uint32_t SkippedDrawCounter;
    };
}

#endif
