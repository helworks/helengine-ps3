#include "platform/ps3/Ps3RenderManager2D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>

#include "runtime/native_exceptions.hpp"
#include "runtime/native_runtime.hpp"

namespace helengine::ps3 {
    namespace {
        /// Shared refusal for every 2D operation the boot gate cannot serve. Raising keeps a
        /// missing pipeline from looking like a blank overlay.
        [[noreturn]] void RefuseUnsupported(const char* stage) {
            std::printf("PS3_CORE_ERROR stage=%s reason=not-supported-in-v02-boot-gate\n", stage);
            he_cpp_raise(new ::NotSupportedException(
                "The PlayStation 3 v0.2 boot gate has no 2D pipeline. Overlay rendering arrives with a later milestone."));
        }
    }

    Ps3RenderManager2D::Ps3RenderManager2D()
        : DrawCounter(0)
        , LoggedFirstDraw(false) {
    }

    std::uint32_t Ps3RenderManager2D::DrawCount() const {
        return DrawCounter;
    }

    void Ps3RenderManager2D::Draw() {
        ++DrawCounter;
        if (!LoggedFirstDraw) {
            LoggedFirstDraw = true;
            std::printf("PS3_CORE_RENDER2D_FIRST_DRAW\n");
        }
    }

    ::RuntimeTexture* Ps3RenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
        (void)data;
        RefuseUnsupported("BuildTextureFromRaw");
    }

    void Ps3RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
        (void)shape;
        RefuseUnsupported("DrawRoundedRect");
    }

    void Ps3RenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
        (void)sprite;
        RefuseUnsupported("DrawSprite");
    }

    void Ps3RenderManager2D::DrawText(::ITextDrawable2D* text) {
        (void)text;
        RefuseUnsupported("DrawText");
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
        RefuseUnsupported("UpdateTextureRegionCore");
    }

    void Ps3RenderManager2D::Dispose() {
        std::printf("PS3_CORE_RENDER2D_DISPOSED draws=%u\n", DrawCounter);
    }
}

#endif
