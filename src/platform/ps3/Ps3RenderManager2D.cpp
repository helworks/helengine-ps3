#include "platform/ps3/Ps3RenderManager2D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>

#include "RuntimeTexture.hpp"
#include "TextureAsset.hpp"

namespace helengine::ps3 {
    namespace {
        /// Reports the first use of one 2D entry point and then stays quiet.
        ///
        /// PlayStation 3 has no 2D pipeline yet, and authored scenes carry overlay content
        /// that Core will drive every frame. These entry points accept the work and draw
        /// nothing rather than raising, so a scene's 3D content still runs. One line per
        /// entry point keeps that absence visible in the console log without flooding it,
        /// because an overlay that is merely missing looks exactly like one that is broken.
        void NoteFirstUse(const char* stage, bool& logged) {
            if (logged) {
                return;
            }
            logged = true;
            std::printf("PS3_CORE_RENDER2D_SKIPPED stage=%s reason=no-2d-pipeline-yet\n", stage);
        }
    }

    Ps3RenderManager2D::Ps3RenderManager2D()
        : DrawCounter(0)
        , LoggedFirstDraw(false)
        , LoggedBuildTexture(false)
        , LoggedRoundedRect(false)
        , LoggedSprite(false)
        , LoggedText(false)
        , LoggedTextureUpdate(false)
        , SkippedDrawCounter(0) {
    }

    std::uint32_t Ps3RenderManager2D::DrawCount() const {
        return DrawCounter;
    }

    std::uint32_t Ps3RenderManager2D::SkippedDrawCount() const {
        return SkippedDrawCounter;
    }

    void Ps3RenderManager2D::Draw() {
        ++DrawCounter;
        if (!LoggedFirstDraw) {
            LoggedFirstDraw = true;
            std::printf("PS3_CORE_RENDER2D_FIRST_DRAW\n");
        }
    }

    ::RuntimeTexture* Ps3RenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
        NoteFirstUse("BuildTextureFromRaw", LoggedBuildTexture);

        // A real texture object with no GPU memory behind it. Core stores and measures
        // textures, so handing back null here would fault long after the cause.
        ::RuntimeTexture* texture = new ::RuntimeTexture();
        if (data != nullptr) {
            texture->Width = static_cast<int32_t>(data->Width);
            texture->Height = static_cast<int32_t>(data->Height);
        }
        return texture;
    }

    void Ps3RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
        (void)shape;
        ++SkippedDrawCounter;
        NoteFirstUse("DrawRoundedRect", LoggedRoundedRect);
    }

    void Ps3RenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
        (void)sprite;
        ++SkippedDrawCounter;
        NoteFirstUse("DrawSprite", LoggedSprite);
    }

    void Ps3RenderManager2D::DrawText(::ITextDrawable2D* text) {
        (void)text;
        ++SkippedDrawCounter;
        NoteFirstUse("DrawText", LoggedText);
    }

    void Ps3RenderManager2D::UpdateTextureRegionCore(
        ::RuntimeTexture* texture,
        int32_t x,
        int32_t y,
        int32_t width,
        int32_t height,
        Array<uint8_t>* rgba8,
        int32_t sourceRowPitch) {
        (void)texture;
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)rgba8;
        (void)sourceRowPitch;
        NoteFirstUse("UpdateTextureRegionCore", LoggedTextureUpdate);
    }

    void Ps3RenderManager2D::Dispose() {
        std::printf("PS3_CORE_RENDER2D_DISPOSED draws=%u skippedDraws=%u\n", DrawCounter, SkippedDrawCounter);
    }
}

#endif
