#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "IInputBackend.hpp"

namespace helengine::ps3 {
    /// Supplies a neutral input frame to the generated Core.
    ///
    /// The v0.2 boot gate reads no pads. Returning a default-constructed frame every tick
    /// is honest rather than a placeholder: it reports no devices and no activity, which is
    /// exactly the state of a host that has not initialized pad input. Real input arrives
    /// with the authored scene milestone.
    class Ps3InputBackend final : public ::IInputBackend {
    public:
        /// Returns a frame reporting no connected devices and no activity.
        ::InputFrameState CaptureFrame() override;
    };
}

#endif
