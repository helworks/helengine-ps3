#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "InputSystem.hpp"
#include "platform/ps3/Ps3InputBackend.hpp"

namespace {
    /// Stable port inventory supplied at the external SDK boundary.
    padInfo2 info;
    /// Raw SDK packets, including unchanged and malformed packets.
    padData pads[MAX_PORT_NUM];
    /// Initialization result used to exercise ownership failure.
    int initResult = PAD_OK;
    /// Shutdown result returned by the owned pad service.
    int endResult = PAD_OK;
    /// Inventory read result used to exercise visible failure.
    int infoResult = PAD_OK;
    /// Pressure setup result used to exercise visible failure.
    int pressModeResult = PAD_OK;
    /// Per-port packet read status.
    int dataResult[MAX_PORT_NUM];
    /// Number of service initialization attempts made by the real backend.
    int initCalls;
    /// Number of owned-service shutdown calls.
    int endCalls;
    /// Number of inventory polls.
    int infoCalls;
    /// Per-port pressure setup attempts.
    int pressModeCalls[MAX_PORT_NUM];
    /// Per-port packet polls.
    int dataCalls[MAX_PORT_NUM];
    /// Number of completed behavioral checks.
    int checkCount;

    /// Accepts the real maximum port count and returns controlled service status.
    int FakeInit(u32 maxPorts) {
        ++initCalls;
        return maxPorts == MAX_PORT_NUM ? initResult : PAD_ERROR_INVALID_PARAMETER;
    }

    /// Records the backend releasing its owned service.
    int FakeEnd() {
        ++endCalls;
        return endResult;
    }

    /// Copies the controlled physical port inventory into the SDK output.
    int FakeInfo(padInfo2* result) {
        ++infoCalls;
        if (infoResult == PAD_OK) *result = info;
        return infoResult;
    }

    /// Models pressure-mode activation becoming visible on the next inventory poll.
    int FakeSetPressMode(u32 port, u32 mode) {
        ++pressModeCalls[port];
        if (pressModeResult == PAD_OK) {
            info.port_setting[port] = mode == PAD_PRESS_MODE_ON ? PAD_SETTINGS_PRESS_ON : PAD_SETTINGS_PRESS_OFF;
        }
        return pressModeResult;
    }

    /// Returns a controlled packet or SDK read failure for the requested physical port.
    int FakeData(u32 port, padData* result) {
        ++dataCalls[port];
        if (dataResult[port] == PAD_OK) *result = pads[port];
        return dataResult[port];
    }

    /// Replaces only external pad calls; generated input processing remains real.
    helengine::ps3::Ps3PadApi FakeApi() {
        return { FakeInit, FakeEnd, FakeInfo, FakeSetPressMode, FakeData };
    }

    /// Emits a named result and fails the executable at the first violated contract.
    void Check(bool condition, const char* name) {
        if (!condition) {
            std::printf("FAIL %s\n", name);
            std::exit(1);
        }
        ++checkCount;
        std::printf("PASS %s\n", name);
    }

    /// Reads a public captured port state without reaching into backend internals.
    ::InputGamepadState StateAt(const ::InputFrameState& frame, int port) {
        return frame.Gamepads == nullptr ? ::InputGamepadState() : (*frame.Gamepads)[port];
    }

    /// Connects a neutral standard pad with the requested reported capabilities.
    void Connect(int port, u32 capabilities = 0u) {
        info.port_status[port] = 1u;
        info.port_setting[port] = 0u;
        info.device_capability[port] = capabilities;
        info.device_type[port] = PAD_TYPE_STANDARD;
        pads[port] = {};
        pads[port].len = capabilities == 0u ? 8 : 20;
        pads[port].button[4] = 128;
        pads[port].button[5] = 128;
        pads[port].button[6] = 128;
        pads[port].button[7] = 128;
    }

    /// One independently specified raw-button to engine-button mapping.
    struct ButtonCase {
        /// Single SDK button bit.
        u16 raw;
        /// SDK digital word containing the bit.
        int offset;
        /// Expected abstract button, with all other button bits clear.
        ::InputGamepadButton expected;
        /// Diagnostic name for the contract being tested.
        const char* name;
    };
}

/// Exercises captured frames, shared input edges and SDK ownership on the PPU.
int main() {
    std::memset(&info, 0, sizeof(info));
    std::memset(pads, 0, sizeof(pads));
    std::memset(dataResult, 0, sizeof(dataResult));
    std::memset(pressModeCalls, 0, sizeof(pressModeCalls));
    std::memset(dataCalls, 0, sizeof(dataCalls));
    Connect(0, 1u << 1);

    {
        helengine::ps3::Ps3InputBackend backend(FakeApi());
        ::InputSystem input;
        input.SetBackend(&backend);

        ::InputFrameState frame = backend.CaptureFrame();
        Check(initCalls == 1 && infoCalls == 1 && dataCalls[0] == 1,
              "initializes-and-polls-connected-port");
        Check(StateAt(frame, 0).Connected && frame.GamepadCount == 1, "connected-state-and-count");
        Check(pressModeCalls[0] == 1, "requests-pressure-mode-for-capable-pad");

        const ButtonCase cases[] = {
            { PAD_CTRL_TRIANGLE, 3, ::InputGamepadButton::North, "triangle-is-north" },
            { PAD_CTRL_CIRCLE, 3, ::InputGamepadButton::East, "circle-is-east" },
            { PAD_CTRL_CROSS, 3, ::InputGamepadButton::South, "cross-is-south" },
            { PAD_CTRL_SQUARE, 3, ::InputGamepadButton::West, "square-is-west" },
            { PAD_CTRL_UP, 2, ::InputGamepadButton::DPadUp, "dpad-up" },
            { PAD_CTRL_DOWN, 2, ::InputGamepadButton::DPadDown, "dpad-down" },
            { PAD_CTRL_LEFT, 2, ::InputGamepadButton::DPadLeft, "dpad-left" },
            { PAD_CTRL_RIGHT, 2, ::InputGamepadButton::DPadRight, "dpad-right" },
            { PAD_CTRL_L1, 3, ::InputGamepadButton::LeftShoulder, "left-shoulder" },
            { PAD_CTRL_R1, 3, ::InputGamepadButton::RightShoulder, "right-shoulder" },
            { PAD_CTRL_L2, 3, ::InputGamepadButton::LeftTrigger, "left-trigger-button" },
            { PAD_CTRL_R2, 3, ::InputGamepadButton::RightTrigger, "right-trigger-button" },
            { PAD_CTRL_L3, 2, ::InputGamepadButton::LeftStick, "left-stick-click" },
            { PAD_CTRL_R3, 2, ::InputGamepadButton::RightStick, "right-stick-click" },
            { PAD_CTRL_START, 2, ::InputGamepadButton::Start, "start" },
            { PAD_CTRL_SELECT, 2, ::InputGamepadButton::Select, "select" }
        };
        for (const ButtonCase& test : cases) {
            pads[0].button[2] = 0;
            pads[0].button[3] = 0;
            pads[0].button[test.offset] = test.raw;
            frame = backend.CaptureFrame();
            const std::uint64_t expected = std::uint64_t{1} << static_cast<int>(test.expected);
            Check(StateAt(frame, 0).Buttons == expected, test.name);
        }

        pads[0].button[2] = 0;
        pads[0].button[3] = PAD_CTRL_L2;
        pads[0].button[18] = 127;
        pads[0].button[19] = 255;
        info.port_setting[0] = 0u;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 0).LeftTrigger == 32767 && StateAt(frame, 0).RightTrigger == 0,
              "pressure-capability-with-disabled-setting-uses-digital-trigger");
        info.port_setting[0] = PAD_SETTINGS_PRESS_ON;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 0).LeftTrigger == 16319 && StateAt(frame, 0).RightTrigger == 32767,
              "enabled-pressure-data-uses-precision-values");
        pads[0].len = 10;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 0).LeftTrigger == 32767 && StateAt(frame, 0).RightTrigger == 0,
              "incomplete-pressure-packet-uses-digital-endpoints");
        pads[0].len = 7;
        bool shortPacketFailedVisibly = false;
        try { (void)backend.CaptureFrame(); }
        catch (const std::runtime_error&) { shortPacketFailedVisibly = true; }
        Check(shortPacketFailedVisibly, "short-standard-packet-fails-visibly");
        pads[0].len = MAX_PAD_CODES + 1;
        bool oversizedPacketFailedVisibly = false;
        try { (void)backend.CaptureFrame(); }
        catch (const std::runtime_error&) { oversizedPacketFailedVisibly = true; }
        Check(oversizedPacketFailedVisibly, "oversized-packet-fails-visibly");
        pads[0].len = 64;

        pads[0].button[3] = PAD_CTRL_TRIANGLE;
        input.Update();
        Check(input.WasGamepadButtonPressed(0, InputGamepadButton::North), "shared-press-edge");
        pads[0] = {}; // SDK returns an empty payload when there is no new packet.
        input.Update();
        Check(StateAt(input.CurrentFrame, 0).IsButtonDown(InputGamepadButton::North) &&
              !input.WasGamepadButtonPressed(0, InputGamepadButton::North), "shared-held-edge-on-len-zero");
        pads[0].len = 64;
        pads[0].button[4] = pads[0].button[5] = pads[0].button[6] = pads[0].button[7] = 128;
        pads[0].button[3] = 0;
        input.Update();
        Check(!input.GetGamepadState(0).IsButtonDown(InputGamepadButton::North) &&
              input.GetPreviousGamepadState(0).IsButtonDown(InputGamepadButton::North), "shared-release-edge");
        pads[0].button[3] = PAD_CTRL_TRIANGLE;
        input.Update();
        Check(input.WasGamepadButtonPressed(0, InputGamepadButton::North), "shared-press-after-release");

        pads[0].button[4] = 255;
        pads[0].button[5] = 0;
        pads[0].button[6] = 0;
        pads[0].button[7] = 255;
        frame = backend.CaptureFrame();
        ::InputGamepadState state = StateAt(frame, 0);
        Check(state.RightStickX == 32767 && state.LeftStickX == -32768, "stick-x-signed-endpoints");
        Check(state.RightStickY == -32768 && state.LeftStickY == 32767, "stick-y-matches-shared-camera-up-negative");
        pads[0].button[5] = 128;
        pads[0].button[7] = 128;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 0).RightStickY == 0 && StateAt(frame, 0).LeftStickY == 0,
              "stick-y-center-is-zero");

        Connect(3, 0u);
        pads[3].button[3] = PAD_CTRL_CROSS | PAD_CTRL_L2 | PAD_CTRL_R2;
        frame = backend.CaptureFrame();
        Check(frame.GamepadCount == 4 && !StateAt(frame, 1).Connected &&
              StateAt(frame, 3).IsButtonDown(InputGamepadButton::South), "sparse-stable-port-index");
        Check(StateAt(frame, 3).LeftTrigger == 32767 && StateAt(frame, 3).RightTrigger == 32767,
              "digital-only-trigger-endpoints");
        info.port_status[3] = 0u;
        frame = backend.CaptureFrame();
        Check(!StateAt(frame, 3).Connected && !StateAt(frame, 3).IsButtonDown(InputGamepadButton::South),
              "disconnect-clears-port-state");
        Connect(3, 0u);
        pads[3].button[3] = PAD_CTRL_SQUARE;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 3).IsButtonDown(InputGamepadButton::West) &&
              !StateAt(frame, 3).IsButtonDown(InputGamepadButton::South), "reconnect-clears-old-device-state");

        info.port_status[3] = 3u;
        pads[3].button[3] = PAD_CTRL_CROSS;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 3).IsButtonDown(InputGamepadButton::South) &&
              !StateAt(frame, 3).IsButtonDown(InputGamepadButton::West), "assignment-change-reads-fresh-state");
        pads[3].len = 0;
        info.port_status[3] = 1u;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 3).IsButtonDown(InputGamepadButton::South), "len-zero-preserves-reassigned-state");
        info.port_status[3] = 3u;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 3).Connected && StateAt(frame, 3).Buttons == 0,
              "reassignment-with-no-packet-clears-old-buttons");
        pads[3].len = 64;
        info.port_status[3] = 1u;
        info.device_type[3] = PAD_TYPE_LDD;
        pads[3].button[3] = PAD_CTRL_TRIANGLE;
        frame = backend.CaptureFrame();
        Check(StateAt(frame, 3).IsButtonDown(InputGamepadButton::North) &&
              !StateAt(frame, 3).IsButtonDown(InputGamepadButton::South), "identity-change-reads-fresh-state");
    }
    Check(endCalls == 1, "ends-owned-pad-service");

    initResult = PAD_ERROR_ALREADY_INITIALIZED;
    bool initializationFailedVisibly = false;
    {
        helengine::ps3::Ps3InputBackend backend(FakeApi());
        try { (void)backend.CaptureFrame(); }
        catch (const std::runtime_error&) { initializationFailedVisibly = true; }
    }
    Check(initializationFailedVisibly && endCalls == 1, "already-initialized-service-is-not-adopted");

    initResult = PAD_OK;
    infoResult = PAD_ERROR_FATAL;
    bool infoFailedVisibly = false;
    {
        helengine::ps3::Ps3InputBackend backend(FakeApi());
        try { (void)backend.CaptureFrame(); }
        catch (const std::runtime_error&) { infoFailedVisibly = true; }
    }
    Check(infoFailedVisibly && endCalls == 2, "info-error-releases-owned-service");

    infoResult = PAD_OK;
    Connect(0, 1u << 1);
    pressModeResult = PAD_ERROR_FATAL;
    bool pressureSetupFailedVisibly = false;
    {
        helengine::ps3::Ps3InputBackend backend(FakeApi());
        try { (void)backend.CaptureFrame(); }
        catch (const std::runtime_error&) { pressureSetupFailedVisibly = true; }
    }
    Check(pressureSetupFailedVisibly && endCalls == 3, "pressure-setup-error-releases-owned-service");

    pressModeResult = PAD_OK;
    dataResult[0] = PAD_ERROR_FATAL;
    bool dataFailedVisibly = false;
    {
        helengine::ps3::Ps3InputBackend backend(FakeApi());
        try { (void)backend.CaptureFrame(); }
        catch (const std::runtime_error&) { dataFailedVisibly = true; }
    }
    Check(dataFailedVisibly && endCalls == 4, "connected-port-read-error-releases-owned-service");
    std::printf("PS3_INPUT_BACKEND_TESTS passed=%d failed=0\n", checkCount);
    return 0;
}
