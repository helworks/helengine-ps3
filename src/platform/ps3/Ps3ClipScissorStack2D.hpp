#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "ClipRegionStackBuilder2D.hpp"
#include "IClipRegion2D.hpp"
#include "IDrawable2D.hpp"
#include "float4.hpp"
#include "runtime/native_list.hpp"

namespace helengine::ps3 {

/// Resolves generated ancestor clip chains into bounded RSX integer scissors.
/// The builder and list are values; list elements are borrowed Core components.
class Ps3ClipScissorStack2D final {
private:
    /// Generated chain builder reused for every drawable in the current camera pass.
    ::ClipRegionStackBuilder2D Builder;

    /// Borrowed clip components in generated ancestor-first order; this list owns no elements.
    ::List<::IClipRegion2D*> ClipChain;

public:
    /// Creates reusable clipping state with storage for common shallow UI hierarchies.
    Ps3ClipScissorStack2D();

    /// Resolves camera bounds and all ancestor clips using the integer pixel-center rule.
    /// @param drawable Drawable whose parent chain supplies clip components.
    /// @param cameraViewport Camera scissor as pixel-space x, y, width, height.
    /// @param backbufferWidth Full backbuffer width used to clamp final pixel bounds.
    /// @param backbufferHeight Full backbuffer height used to clamp final pixel bounds.
    /// @return Integer-aligned x, y, width, height represented as float components for Core APIs.
    ::float4 Resolve(::IDrawable2D* drawable, ::float4 cameraViewport,
                     unsigned backbufferWidth, unsigned backbufferHeight);
};

} // namespace helengine::ps3

#endif
