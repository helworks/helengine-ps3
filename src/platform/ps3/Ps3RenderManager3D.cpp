#include "platform/ps3/Ps3RenderManager3D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>

#include "runtime/native_exceptions.hpp"
#include "runtime/native_runtime.hpp"

namespace helengine::ps3 {
    Ps3RenderManager3D::Ps3RenderManager3D()
        : DrawCounter(0)
        , LoggedFirstDraw(false) {
    }

    std::uint32_t Ps3RenderManager3D::DrawCount() const {
        return DrawCounter;
    }

    void Ps3RenderManager3D::Draw() {
        ++DrawCounter;
        if (!LoggedFirstDraw) {
            LoggedFirstDraw = true;
            std::printf("PS3_CORE_RENDER3D_FIRST_DRAW\n");
        }
    }

    ::RuntimeModel* Ps3RenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
        (void)data;
        std::printf("PS3_CORE_ERROR stage=BuildModelFromRaw reason=not-supported-in-v02-boot-gate\n");
        he_cpp_raise(::NotSupportedException(
            "The PlayStation 3 v0.2 boot gate builds no models. Authored geometry arrives with the scene milestone."));
    }

    void Ps3RenderManager3D::Dispose() {
        std::printf("PS3_CORE_RENDER3D_DISPOSED draws=%u\n", DrawCounter);
    }
}

#endif
