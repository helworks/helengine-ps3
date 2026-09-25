#include "platform/ps3/Ps3InputBackend.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <stdexcept>

namespace helengine::ps3 {
    namespace {
        /// Assigns the SDK defaults to the injectable pad function table.
        Ps3PadApi NativePadApi() {
            return { ioPadInit, ioPadEnd, ioPadGetInfo2, ioPadSetPressMode, ioPadGetData };
        }

        /// Returns whether the SDK's pressure-capability bit is present.
        bool HasPressure(const u32 capabilities) {
            return (capabilities & (1u << 1)) != 0u;
        }
    }

    Ps3InputBackend::Ps3InputBackend()
        : Ps3InputBackend(NativePadApi()) {
    }

    Ps3InputBackend::Ps3InputBackend(Ps3PadApi api)
        : Api(api)
        , Snapshots{ ::Array<::InputGamepadState>(MAX_PORT_NUM), ::Array<::InputGamepadState>(MAX_PORT_NUM) }
        , WasConnected{}
        , DeviceTypes{}
        , Capabilities{}
        , PressureModeRequested{}
        , CurrentSnapshot(0u)
        , Initialized(false) {
        if (Api.Init == nullptr || Api.End == nullptr || Api.GetInfo2 == nullptr ||
            Api.SetPressMode == nullptr || Api.GetData == nullptr) {
            throw std::invalid_argument("PS3 pad API function table contains a null function");
        }
    }

    Ps3InputBackend::~Ps3InputBackend() {
        if (Initialized) {
            const s32 result = Api.End();
            if (result != PAD_OK) {
                std::fprintf(stderr, "PS3_INPUT_ERROR operation=ioPadEnd result=0x%08x\n",
                             static_cast<unsigned>(result));
            }
        }
    }

    std::int16_t Ps3InputBackend::ConvertStick(u16 value) {
        if (value > 255u) value = 255u;
        const int centered = static_cast<int>(value) - 128;
        if (centered < 0) {
            return static_cast<std::int16_t>(centered * 256);
        }
        return static_cast<std::int16_t>((centered * 32767) / 127);
    }

    std::int16_t Ps3InputBackend::ConvertPressure(u16 value) {
        const unsigned bounded = value > 255u ? 255u : value;
        return static_cast<std::int16_t>((bounded * 32767u) / 255u);
    }

    void Ps3InputBackend::MapButtons(::InputGamepadState& state, const padData& data) {
        const u16 digital1 = data.button[2];
        const u16 digital2 = data.button[3];
        state.SetButtonDown(::InputGamepadButton::DPadLeft, (digital1 & PAD_CTRL_LEFT) != 0u);
        state.SetButtonDown(::InputGamepadButton::DPadDown, (digital1 & PAD_CTRL_DOWN) != 0u);
        state.SetButtonDown(::InputGamepadButton::DPadRight, (digital1 & PAD_CTRL_RIGHT) != 0u);
        state.SetButtonDown(::InputGamepadButton::DPadUp, (digital1 & PAD_CTRL_UP) != 0u);
        state.SetButtonDown(::InputGamepadButton::Start, (digital1 & PAD_CTRL_START) != 0u);
        state.SetButtonDown(::InputGamepadButton::RightStick, (digital1 & PAD_CTRL_R3) != 0u);
        state.SetButtonDown(::InputGamepadButton::LeftStick, (digital1 & PAD_CTRL_L3) != 0u);
        state.SetButtonDown(::InputGamepadButton::Select, (digital1 & PAD_CTRL_SELECT) != 0u);
        state.SetButtonDown(::InputGamepadButton::West, (digital2 & PAD_CTRL_SQUARE) != 0u);
        state.SetButtonDown(::InputGamepadButton::South, (digital2 & PAD_CTRL_CROSS) != 0u);
        state.SetButtonDown(::InputGamepadButton::East, (digital2 & PAD_CTRL_CIRCLE) != 0u);
        state.SetButtonDown(::InputGamepadButton::North, (digital2 & PAD_CTRL_TRIANGLE) != 0u);
        state.SetButtonDown(::InputGamepadButton::RightShoulder, (digital2 & PAD_CTRL_R1) != 0u);
        state.SetButtonDown(::InputGamepadButton::LeftShoulder, (digital2 & PAD_CTRL_L1) != 0u);
        state.SetButtonDown(::InputGamepadButton::RightTrigger, (digital2 & PAD_CTRL_R2) != 0u);
        state.SetButtonDown(::InputGamepadButton::LeftTrigger, (digital2 & PAD_CTRL_L2) != 0u);
    }

    [[noreturn]] void Ps3InputBackend::ThrowSdkError(const char* operation, s32 result, int port) {
        char message[128];
        if (port >= 0) {
            ::snprintf(message, sizeof(message), "PS3 pad %s failed on port %d (0x%08x)",
                          operation, port, static_cast<unsigned>(result));
        } else {
            ::snprintf(message, sizeof(message), "PS3 pad %s failed (0x%08x)",
                          operation, static_cast<unsigned>(result));
        }
        throw std::runtime_error(message);
    }

    ::InputFrameState Ps3InputBackend::CaptureFrame() {
        if (!Initialized) {
            const s32 initResult = Api.Init(MAX_PORT_NUM);
            if (initResult != PAD_OK) {
                ThrowSdkError("ioPadInit", initResult);
            }
            Initialized = true;
        }

        padInfo2 information{};
        const s32 infoResult = Api.GetInfo2(&information);
        if (infoResult != PAD_OK) {
            ThrowSdkError("ioPadGetInfo2", infoResult);
        }

        const std::uint8_t nextSnapshot = static_cast<std::uint8_t>(1u - CurrentSnapshot);
        ::Array<::InputGamepadState>& previous = Snapshots[CurrentSnapshot];
        ::Array<::InputGamepadState>& current = Snapshots[nextSnapshot];
        ::Array<::InputGamepadState>::Copy(&previous, &current, MAX_PORT_NUM);

        ::InputFrameState frame;
        frame.Gamepads = &current;
        frame.GamepadCount = 0;

        for (u32 port = 0; port < MAX_PORT_NUM; ++port) {
            ::InputGamepadState& state = current[static_cast<int>(port)];
            const bool connected = (information.port_status[port] & 1u) != 0u;
            const bool assignmentChanged = (information.port_status[port] & 2u) != 0u;
            const bool identityChanged = WasConnected[port] && connected &&
                (DeviceTypes[port] != information.device_type[port] ||
                 Capabilities[port] != information.device_capability[port]);

            if (!connected) {
                state = ::InputGamepadState();
                WasConnected[port] = false;
                DeviceTypes[port] = 0u;
                Capabilities[port] = 0u;
                PressureModeRequested[port] = false;
                continue;
            }

            if (frame.GamepadCount < static_cast<int>(port + 1u)) {
                frame.GamepadCount = static_cast<int>(port + 1u);
            }

            const bool newConnection = !WasConnected[port];
            if (newConnection || assignmentChanged || identityChanged) {
                state = ::InputGamepadState();
                state.Connected = true;
                WasConnected[port] = true;
                DeviceTypes[port] = information.device_type[port];
                Capabilities[port] = information.device_capability[port];
                PressureModeRequested[port] = false;
            }
            if (HasPressure(Capabilities[port]) && !PressureModeRequested[port]) {
                const s32 pressResult = Api.SetPressMode(port, PAD_PRESS_MODE_ON);
                if (pressResult != PAD_OK) {
                    ThrowSdkError("ioPadSetPressMode", pressResult, static_cast<int>(port));
                }
                PressureModeRequested[port] = true;
            }

            padData data{};
            const s32 dataResult = Api.GetData(port, &data);
            if (dataResult != PAD_OK) {
                ThrowSdkError("ioPadGetData", dataResult, static_cast<int>(port));
            }
            if (data.len == 0) {
                continue;
            }
            if (data.len < 8 || data.len > MAX_PAD_CODES) {
                ThrowSdkError("ioPadGetData invalid packet length", data.len, static_cast<int>(port));
            }

            state.Connected = true;
            MapButtons(state, data);
            state.RightStickX = ConvertStick(data.button[4]);
            // Shared input uses screen-style Y (up negative), also used by the Vita backend.
            state.RightStickY = ConvertStick(data.button[5]);
            state.LeftStickX = ConvertStick(data.button[6]);
            state.LeftStickY = ConvertStick(data.button[7]);
            const bool pressurePacketAvailable = data.len > 19;
            const bool pressureModeEnabled = (information.port_setting[port] & PAD_SETTINGS_PRESS_ON) != 0u;
            if (HasPressure(Capabilities[port]) && pressureModeEnabled && pressurePacketAvailable) {
                state.LeftTrigger = ConvertPressure(data.button[18]);
                state.RightTrigger = ConvertPressure(data.button[19]);
            } else {
                state.LeftTrigger = (data.button[3] & PAD_CTRL_L2) != 0u ? 32767 : 0;
                state.RightTrigger = (data.button[3] & PAD_CTRL_R2) != 0u ? 32767 : 0;
            }
        }

        CurrentSnapshot = nextSnapshot;
        return frame;
    }
}

#endif
