#pragma once

#include <ppu-types.h>

#include "platform/ps3/Ps3RunOptions.hpp"

namespace helengine::ps3 {
    class Ps3Video;

    /// Native PS3 boot host: registers the system exit callback, owns the video and
    /// renderer lifetimes in order, drives the frame loop with a monotonic clock, and
    /// funnels every failure into one ordered cleanup path (drain, renderer shutdown,
    /// video shutdown). Startup stages, the first submission, periodic aggregate counters
    /// and shutdown are logged; individual frames are not.
    class Ps3BootHost {
    public:
        /// Runs the host with the given options and returns the process exit code:
        /// zero for a normal (timed or system-requested) exit, nonzero for any failure,
        /// including the deliberate failure points used for lifecycle validation.
        static int Run(const Ps3RunOptions& options);

    private:
        /// Rotation speed in radians per second for the animated pose.
        static constexpr float RotationRadiansPerSecond = 0.5f;

        /// Pose used when the fixed-pose option is set.
        static constexpr float FixedPoseRadians = 0.6f;

        /// Largest animation step accepted per frame, so pauses do not jump the pose.
        static constexpr float MaximumStepSeconds = 0.1f;

        /// Full turn used to wrap the animated pose.
        static constexpr float TwoPi = 6.28318530717958647692f;

        /// Interval between aggregate frame/allocation log lines, in seconds.
        static constexpr unsigned StatisticsIntervalSeconds = 5;

        /// Set by the system callback when GameOS requests exit; read by the frame loop.
        static volatile bool ExitRequested;

        /// System event callback: records an exit request and does nothing else.
        static void HandleSystemEvent(u64 status, u64 parameter, void* userData);

        /// Logs a diagnostic readback of six fixed screen points from the displayed frame
        /// (center, left, right, top, bottom, corner) so face colors and background can be
        /// checked from the log; it never influences rendering.
        static void LogFrameProbe(const Ps3Video& video, const char* label, float pose);
    };
}
