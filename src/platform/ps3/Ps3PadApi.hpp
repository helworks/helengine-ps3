#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <io/pad.h>

namespace helengine::ps3 {
    /// Function table for the owned PSL1GHT pad service, injectable for deterministic fixtures.
    struct Ps3PadApi final {
        /// Initializes the process pad service with the requested maximum port count.
        s32 (*Init)(u32);

        /// Shuts down the pad service initialized by the backend.
        s32 (*End)();

        /// Reads stable port status and device capabilities.
        s32 (*GetInfo2)(padInfo2*);

        /// Enables pressure data for one capable port.
        s32 (*SetPressMode)(u32, u32);

        /// Reads one port's current button and analog data.
        s32 (*GetData)(u32, padData*);
    };
}

#endif
