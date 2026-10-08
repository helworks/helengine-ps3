#include "platform/ps3/Ps3ClipScissorStack2D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace helengine::ps3 {
    namespace {
        /// Rejects non-finite rectangle values before edge arithmetic or integer conversion.
        void ValidateRect(::float4 rectangle, const char* label, bool requirePositiveSize) {
            if (!std::isfinite(rectangle.X) || !std::isfinite(rectangle.Y) ||
                !std::isfinite(rectangle.Z) || !std::isfinite(rectangle.W)) {
                throw std::invalid_argument(std::string("PS3 2D scissor has non-finite ") + label + ".");
            }
            if (rectangle.Z < 0.0f || rectangle.W < 0.0f ||
                (requirePositiveSize && (rectangle.Z == 0.0f || rectangle.W == 0.0f))) {
                throw std::invalid_argument(std::string("PS3 2D scissor has invalid ") + label + " dimensions.");
            }
        }

        /// Maps a float edge to the first integer pixel whose center is at or beyond it.
        double PixelCenterEdge(double edge) {
            return std::ceil(edge - 0.5);
        }
    }

    Ps3ClipScissorStack2D::Ps3ClipScissorStack2D()
        : Builder()
        , ClipChain() {
        ClipChain.reserve(16u);
    }

    ::float4 Ps3ClipScissorStack2D::Resolve(::IDrawable2D* drawable, ::float4 cameraViewport,
                                            unsigned backbufferWidth, unsigned backbufferHeight) {
        if (drawable == nullptr) {
            throw std::invalid_argument("PS3 2D scissor resolution requires a drawable.");
        }
        ValidateRect(cameraViewport, "camera viewport", true);
        if (backbufferWidth == 0u || backbufferHeight == 0u ||
            backbufferWidth > static_cast<unsigned>(std::numeric_limits<std::int32_t>::max()) ||
            backbufferHeight > static_cast<unsigned>(std::numeric_limits<std::int32_t>::max())) {
            throw std::invalid_argument("PS3 2D scissor backbuffer dimensions are invalid.");
        }

        const double backbufferRight = static_cast<double>(backbufferWidth);
        const double backbufferBottom = static_cast<double>(backbufferHeight);
        const double cameraLeft = std::clamp(static_cast<double>(cameraViewport.X), 0.0, backbufferRight);
        const double cameraTop = std::clamp(static_cast<double>(cameraViewport.Y), 0.0, backbufferBottom);
        const double cameraRight = std::clamp(
            static_cast<double>(cameraViewport.X) + static_cast<double>(cameraViewport.Z), 0.0, backbufferRight);
        const double cameraBottom = std::clamp(
            static_cast<double>(cameraViewport.Y) + static_cast<double>(cameraViewport.W), 0.0, backbufferBottom);

        double left = cameraLeft;
        double top = cameraTop;
        double right = cameraRight;
        double bottom = cameraBottom;

        ClipChain.clear();
        Builder.BuildClipChain(drawable, &ClipChain);
        for (::IClipRegion2D* region : ClipChain) {
            if (region == nullptr) {
                throw std::runtime_error("PS3 2D scissor chain contains a null clip component.");
            }
            const ::float4 clip = region->GetClipRect();
            ValidateRect(clip, "clip rectangle", false);

            const double clipLeft = static_cast<double>(clip.X);
            const double clipTop = static_cast<double>(clip.Y);
            const double clipRight = clipLeft + static_cast<double>(clip.Z);
            const double clipBottom = clipTop + static_cast<double>(clip.W);
            left = std::max(left, clipLeft);
            top = std::max(top, clipTop);
            right = std::min(right, clipRight);
            bottom = std::min(bottom, clipBottom);
        }

        const double cameraPixelLeft = PixelCenterEdge(cameraLeft);
        const double cameraPixelTop = PixelCenterEdge(cameraTop);
        const double cameraPixelRight = PixelCenterEdge(cameraRight);
        const double cameraPixelBottom = PixelCenterEdge(cameraBottom);
        const double pixelLeft = std::clamp(PixelCenterEdge(left), cameraPixelLeft, cameraPixelRight);
        const double pixelTop = std::clamp(PixelCenterEdge(top), cameraPixelTop, cameraPixelBottom);
        const double pixelRight = std::clamp(PixelCenterEdge(right), cameraPixelLeft, cameraPixelRight);
        const double pixelBottom = std::clamp(PixelCenterEdge(bottom), cameraPixelTop, cameraPixelBottom);
        const double boundedLeft = std::clamp(pixelLeft, 0.0, backbufferRight);
        const double boundedTop = std::clamp(pixelTop, 0.0, backbufferBottom);
        const double boundedRight = std::clamp(std::max(pixelRight, boundedLeft), 0.0, backbufferRight);
        const double boundedBottom = std::clamp(std::max(pixelBottom, boundedTop), 0.0, backbufferBottom);

        const auto x = static_cast<std::int32_t>(boundedLeft);
        const auto y = static_cast<std::int32_t>(boundedTop);
        const auto width = static_cast<std::int32_t>(boundedRight - boundedLeft);
        const auto height = static_cast<std::int32_t>(boundedBottom - boundedTop);
        return ::float4(static_cast<float>(x), static_cast<float>(y),
                        static_cast<float>(width), static_cast<float>(height));
    }
}

#endif
