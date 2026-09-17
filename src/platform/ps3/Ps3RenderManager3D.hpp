#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>

#include "RenderManager3D.hpp"

namespace helengine::ps3 {
    /// Minimal 3D render manager for the v0.2 Core boot gate.
    ///
    /// The gate runs Core against an empty scene to prove the lifecycle, so this adapter
    /// accepts being driven and draws nothing. Every request that would require real
    /// rendering resources raises instead of returning a null or empty object, because a
    /// silent failure here would surface much later as an unexplained blank frame.
    ///
    /// Only `BuildModelFromRaw` is abstract on the generated base; the overrides below are
    /// the subset the gate actually needs to control.
    class Ps3RenderManager3D final : public ::RenderManager3D {
    public:
        /// Creates an adapter that has drawn nothing yet.
        Ps3RenderManager3D();

        /// Counts how many times Core asked this manager to draw.
        std::uint32_t DrawCount() const;

        /// Arms a deliberate raise on the next draw.
        ///
        /// Core calls this manager from inside its own draw, so the raise travels back out
        /// through generated stack frames before the runtime host catches it. That is the
        /// evidence that enabling exceptions for this platform was real rather than nominal.
        /// The 3D manager is the target because Core drives it every frame, while it never
        /// calls the 2D manager for the empty scene this gate runs.
        void ArmDeliberateRaise();

        /// Records the draw request and presents nothing, or raises when armed. The boot
        /// gate's visible output is the v0.1 cube, drawn independently of Core.
        void Draw() override;

        /// Refuses model construction, which belongs to the authored scene milestone.
        /// @param data Raw model asset Core is asking to realize.
        /// @return Never returns normally.
        ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;

        /// Releases nothing, because this adapter never hands out models.
        void Dispose() override;

    private:
        /// Number of draw requests received from Core.
        std::uint32_t DrawCounter;

        /// Whether the first draw has already been logged, so the log stays readable.
        bool LoggedFirstDraw;

        /// Whether the build asked the next draw to raise, for the unwinding gate.
        bool RaiseOnDraw;
    };
}

#endif
