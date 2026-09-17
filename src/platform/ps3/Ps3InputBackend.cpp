#include "platform/ps3/Ps3InputBackend.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

namespace helengine::ps3 {
    ::InputFrameState Ps3InputBackend::CaptureFrame() {
        return ::InputFrameState();
    }
}

#endif
