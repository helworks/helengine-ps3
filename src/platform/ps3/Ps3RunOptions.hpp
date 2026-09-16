#pragma once

namespace helengine::ps3 {
    /// Run configuration for the native cube host. Values come from explicit compile
    /// definitions chosen by scripts/build.ps1 (PS3 process arguments are not assumed).
    struct Ps3RunOptions {
        /// True to present only the cleared surface and never initialize the cube renderer.
        bool ClearOnly;

        /// True to hold the documented fixed pose (0.6 radians) instead of rotating over time.
        bool FixedPose;

        /// Wall-clock run duration in seconds; 0 runs until the system requests exit.
        unsigned RunSeconds;

        /// Deliberate failure stage for lifecycle validation: 0 none, 1 after video, 2 after renderer.
        unsigned FailurePoint;
    };
}
