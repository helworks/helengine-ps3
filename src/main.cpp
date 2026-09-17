#include <sys/process.h>

#include "platform/ps3/Ps3BootHost.hpp"
#include "platform/ps3/Ps3RunOptions.hpp"

// Run settings are explicit compile definitions supplied by scripts/build.ps1; a build
// without them is a configuration error, not a default.
#if !defined(HELENGINE_PS3_CLEAR_ONLY) || !defined(HELENGINE_PS3_FIXED_POSE) || !defined(HELENGINE_PS3_RUN_SECONDS) || !defined(HELENGINE_PS3_FAILURE_POINT) || !defined(HELENGINE_PS3_FIXED_POSE_RADIANS)
#error "HELENGINE_PS3_CLEAR_ONLY, HELENGINE_PS3_FIXED_POSE, HELENGINE_PS3_FIXED_POSE_RADIANS, HELENGINE_PS3_RUN_SECONDS and HELENGINE_PS3_FAILURE_POINT must be defined by the build wrapper"
#endif

// The Core failure point is meaningful only where a generated tree was compiled in, so a
// native build neither requires it nor silently accepts one.
#if defined(HELENGINE_PS3_HAS_GENERATED_CORE) && !defined(HELENGINE_PS3_CORE_FAILURE_POINT)
#error "A Core-mode build must define HELENGINE_PS3_CORE_FAILURE_POINT"
#endif
#if !defined(HELENGINE_PS3_HAS_GENERATED_CORE) && defined(HELENGINE_PS3_CORE_FAILURE_POINT)
#error "HELENGINE_PS3_CORE_FAILURE_POINT was defined for a build with no generated Core"
#endif

/// Declares the PPU process parameters read by GameOS: normal priority (1001) and a 1 MiB main stack.
SYS_PROCESS_PARAM(1001, 0x100000)

/// Native PS3 entry point: builds the run options from the compile definitions and enters the boot host.
int main() {
    helengine::ps3::Ps3RunOptions options;
    options.ClearOnly = (HELENGINE_PS3_CLEAR_ONLY) != 0;
    options.FixedPose = (HELENGINE_PS3_FIXED_POSE) != 0;
    options.FixedPoseRadians = static_cast<float>(HELENGINE_PS3_FIXED_POSE_RADIANS);
    options.RunSeconds = static_cast<unsigned>(HELENGINE_PS3_RUN_SECONDS);
    options.FailurePoint = static_cast<unsigned>(HELENGINE_PS3_FAILURE_POINT);
#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
    options.CoreEnabled = true;
    options.CoreFailurePoint = static_cast<unsigned>(HELENGINE_PS3_CORE_FAILURE_POINT);
#else
    options.CoreEnabled = false;
    options.CoreFailurePoint = 0;
#endif
    return helengine::ps3::Ps3BootHost::Run(options);
}
