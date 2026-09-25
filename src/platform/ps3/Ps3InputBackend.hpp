#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <array>
#include <cstdint>

#include <io/pad.h>

#include "InputGamepadState.hpp"
#include "IInputBackend.hpp"
#include "platform/ps3/Ps3PadApi.hpp"
#include "runtime/array.hpp"

namespace helengine::ps3 {
    /// Captures stable PS3 pad ports into alternating, backend-owned Core input snapshots.
    class Ps3InputBackend final : public ::IInputBackend {
    private:
        /// SDK or fixture API used for all pad service operations.
        Ps3PadApi Api;

        /// Two immutable-to-Core snapshots alternate so shallow previous frames stay intact.
        std::array<::Array<::InputGamepadState>, 2> Snapshots;

        /// Last stable connection bit for each PS3 port.
        std::array<bool, MAX_PORT_NUM> WasConnected;

        /// Last device type reported for each PS3 port.
        std::array<u32, MAX_PORT_NUM> DeviceTypes;

        /// Last capability word reported for each PS3 port.
        std::array<u32, MAX_PORT_NUM> Capabilities;

        /// True after pressure mode has been requested for the current port assignment.
        std::array<bool, MAX_PORT_NUM> PressureModeRequested;

        /// Snapshot index returned by the preceding capture.
        std::uint8_t CurrentSnapshot;

        /// True after this instance successfully initialized the process pad service.
        bool Initialized;

        /// Converts the SDK's unsigned byte stick coordinate to the shared signed-short range.
        static std::int16_t ConvertStick(u16 value);

        /// Converts a real pressure byte to a signed-short trigger value.
        static std::int16_t ConvertPressure(u16 value);

        /// Updates face, shoulder, stick-click, d-pad, and menu button bits for one pad.
        static void MapButtons(::InputGamepadState& state, const padData& data);

        /// Reports the SDK operation and port that failed.
        [[noreturn]] static void ThrowSdkError(const char* operation, s32 result, int port = -1);

    public:
        /// Creates an uninitialized backend using the native PSL1GHT pad functions.
        Ps3InputBackend();

        /// Prevents copying a live service owner and its frame snapshots.
        Ps3InputBackend(const Ps3InputBackend&) = delete;

        /// Prevents assigning a second owner to the process pad service.
        Ps3InputBackend& operator=(const Ps3InputBackend&) = delete;

        /// Prevents moving a live service owner while Core may retain its snapshot pointers.
        Ps3InputBackend(Ps3InputBackend&&) = delete;

        /// Prevents move assignment from invalidating Core's retained snapshot pointers.
        Ps3InputBackend& operator=(Ps3InputBackend&&) = delete;

        /// Creates an uninitialized backend with an explicit SDK-compatible function table.
        explicit Ps3InputBackend(Ps3PadApi api);

        /// Ends the pad service only when this instance successfully initialized it.
        ~Ps3InputBackend();

        /// Polls every stable port and returns its connected state and analog/button values.
        ::InputFrameState CaptureFrame() override;
    };
}

#endif
