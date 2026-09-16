#include "platform/ps3/Ps3BootHost.hpp"

#include <cstdio>
#include <exception>

#include <lv2/systime.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>

#include "platform/ps3/Ps3CubeTransform.hpp"
#include "platform/ps3/Ps3Matrix4.hpp"
#include "platform/ps3/Ps3Video.hpp"

namespace helengine::ps3 {
    volatile bool Ps3BootHost::ExitRequested = false;

    void Ps3BootHost::HandleSystemEvent(u64 status, u64 parameter, void* userData) {
        (void)parameter;
        (void)userData;
        if (status == SYSUTIL_EXIT_GAME) {
            ExitRequested = true;
        }
    }

    int Ps3BootHost::Run(const Ps3RunOptions& options) {
        std::printf("PS3_BOOT clearOnly=%d fixedPose=%d runSeconds=%u failurePoint=%u\n",
                    options.ClearOnly ? 1 : 0, options.FixedPose ? 1 : 0, options.RunSeconds, options.FailurePoint);
        ExitRequested = false;
        const s32 callbackResult = sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, HandleSystemEvent, nullptr);
        if (callbackResult != 0) {
            std::printf("PS3_BOOT_ERROR stage=sysUtilRegisterCallback code=%d\n", callbackResult);
            return 1;
        }

        Ps3Video video;
        bool failed = false;
        bool videoReady = false;
        const char* exitReason = "none";

        if (!video.Initialize()) {
            failed = true;
            exitReason = "video-initialization-failed";
        } else {
            videoReady = true;
        }
        if (!failed && options.FailurePoint == 1) {
            std::printf("PS3_FAILURE_POINT stage=AfterVideo\n");
            failed = true;
            exitReason = "deliberate-failure-after-video";
        }
        if (!failed && !options.ClearOnly) {
            std::printf("PS3_BOOT_ERROR stage=renderer reason=cube-renderer-not-available-in-this-build\n");
            failed = true;
            exitReason = "renderer-unavailable";
        }

        if (!failed) {
            const s64 startMicroseconds = sysGetSystemTime();
            s64 previousMicroseconds = startMicroseconds;
            s64 nextStatisticsMicroseconds = startMicroseconds + static_cast<s64>(StatisticsIntervalSeconds) * 1000000;
            float pose = options.FixedPose ? FixedPoseRadians : 0.0f;
            const s64 runLimitMicroseconds = static_cast<s64>(options.RunSeconds) * 1000000;
            bool running = true;
            while (running) {
                sysUtilCheckCallback();
                if (ExitRequested) {
                    exitReason = "system-exit-request";
                    break;
                }
                const s64 nowMicroseconds = sysGetSystemTime();
                const s64 elapsedMicroseconds = nowMicroseconds - startMicroseconds;
                if (runLimitMicroseconds > 0 && elapsedMicroseconds >= runLimitMicroseconds) {
                    exitReason = "run-duration-reached";
                    break;
                }
                float stepSeconds = static_cast<float>(nowMicroseconds - previousMicroseconds) * 0.000001f;
                previousMicroseconds = nowMicroseconds;
                if (stepSeconds > MaximumStepSeconds) {
                    stepSeconds = MaximumStepSeconds;
                }
                if (!options.FixedPose) {
                    pose += RotationRadiansPerSecond * stepSeconds;
                    while (pose >= TwoPi) {
                        pose -= TwoPi;
                    }
                }
                if (!video.BeginFrame()) {
                    failed = true;
                    exitReason = "begin-frame-failed";
                    break;
                }
                if (!video.Present()) {
                    failed = true;
                    exitReason = "present-failed";
                    break;
                }
                if (nowMicroseconds >= nextStatisticsMicroseconds) {
                    nextStatisticsMicroseconds += static_cast<s64>(StatisticsIntervalSeconds) * 1000000;
                    std::printf("PS3_FRAME_STATS frames=%u elapsedMilliseconds=%lld pose=%.3f videoOwnedBytes=%u\n",
                                video.PresentedFrameCount(), static_cast<long long>(elapsedMicroseconds / 1000),
                                static_cast<double>(pose), video.OwnedBytes());
                }
            }
        }

        std::printf("PS3_EXIT reason=%s failed=%d frames=%u\n", exitReason, failed ? 1 : 0, video.PresentedFrameCount());
        if (videoReady) {
            const bool drained = video.Drain();
            if (!drained) {
                std::printf("PS3_DRAIN_FAILED note=renderer-and-gpu-memory-retained\n");
                failed = true;
            }
        }
        video.Shutdown();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        const int exitCode = failed ? 1 : 0;
        std::printf("PS3_SHUTDOWN exitCode=%d\n", exitCode);
        return exitCode;
    }
}
