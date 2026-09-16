#include "platform/ps3/Ps3BootHost.hpp"

#include <cstdio>
#include <exception>

#include <lv2/systime.h>
#include <ppu-types.h>
#include <sysutil/sysutil.h>

#include "platform/ps3/Ps3CubeTransform.hpp"
#include "platform/ps3/Ps3Matrix4.hpp"
#include "platform/ps3/Ps3RasterRenderer.hpp"
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

    void Ps3BootHost::LogFrameProbe(const Ps3Video& video, const char* label, float pose) {
        const unsigned width = video.Width();
        const unsigned height = video.Height();
        const unsigned points[6][2] = {
            { width / 2, height / 2 },
            { width / 4, height / 2 },
            { (width * 3) / 4, height / 2 },
            { width / 2, height / 4 },
            { width / 2, (height * 3) / 4 },
            { 8, 8 }
        };
        unsigned colors[6] = { 0, 0, 0, 0, 0, 0 };
        bool readable = true;
        for (int point = 0; point < 6; ++point) {
            if (!video.ReadDisplayedPixel(points[point][0], points[point][1], &colors[point])) {
                readable = false;
            }
        }
        std::printf("PS3_FRAME_PROBE label=%s pose=%.3f readable=%d center=0x%06X left=0x%06X right=0x%06X top=0x%06X bottom=0x%06X corner=0x%06X\n",
                    label, static_cast<double>(pose), readable ? 1 : 0, colors[0], colors[1], colors[2], colors[3], colors[4], colors[5]);
    }

    int Ps3BootHost::Run(const Ps3RunOptions& options) {
        std::printf("PS3_BOOT clearOnly=%d fixedPose=%d fixedPoseRadians=%.4f runSeconds=%u failurePoint=%u\n",
                    options.ClearOnly ? 1 : 0, options.FixedPose ? 1 : 0, static_cast<double>(options.FixedPoseRadians), options.RunSeconds, options.FailurePoint);
        ExitRequested = false;
        const s32 callbackResult = sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, HandleSystemEvent, nullptr);
        if (callbackResult != 0) {
            std::printf("PS3_BOOT_ERROR stage=sysUtilRegisterCallback code=%d\n", callbackResult);
            return 1;
        }

        Ps3Video video;
        Ps3RasterRenderer renderer;
        bool failed = false;
        bool videoReady = false;
        bool rendererReady = false;
        const char* exitReason = "none";
        float pose = options.FixedPose ? options.FixedPoseRadians : 0.0f;

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
            if (!renderer.Initialize(video)) {
                failed = true;
                exitReason = "renderer-initialization-failed";
            } else {
                rendererReady = true;
            }
        }
        if (!failed && options.FailurePoint == 2) {
            std::printf("PS3_FAILURE_POINT stage=AfterRenderer\n");
            failed = true;
            exitReason = "deliberate-failure-after-renderer";
        }

        if (!failed) {
            const s64 startMicroseconds = sysGetSystemTime();
            s64 previousMicroseconds = startMicroseconds;
            s64 nextStatisticsMicroseconds = startMicroseconds + static_cast<s64>(StatisticsIntervalSeconds) * 1000000;
            const s64 runLimitMicroseconds = static_cast<s64>(options.RunSeconds) * 1000000;
            unsigned previousCommandOffset = video.CommandWriteOffset();
            unsigned ringWrapCount = 0;
            while (true) {
                // Diagnostic: the SDK ring is consumed in segments; a write offset that moves
                // backwards means the command stream jumped back toward the ring start.
                const unsigned commandOffset = video.CommandWriteOffset();
                if (commandOffset < previousCommandOffset) {
                    ++ringWrapCount;
                    std::printf("PS3_COMMAND_RING_WRAP wrap=%u frame=%u previousOffset=0x%X offset=0x%X\n",
                                ringWrapCount, video.PresentedFrameCount(), previousCommandOffset, commandOffset);
                }
                previousCommandOffset = commandOffset;
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
                // BeginFrame confirmed the previous flip, so the displayed buffer is complete.
                if (rendererReady && video.PresentedFrameCount() == 1) {
                    LogFrameProbe(video, "first-frame", pose);
                }
                if (rendererReady) {
                    Ps3Matrix4 modelViewProjection;
                    try {
                        modelViewProjection = Ps3CubeTransform::Build(pose, video.Aspect());
                    } catch (const std::exception& error) {
                        std::printf("PS3_TRANSFORM_ERROR what=%s pose=%.3f aspect=%.4f\n", error.what(), static_cast<double>(pose), static_cast<double>(video.Aspect()));
                        failed = true;
                        exitReason = "transform-failed";
                        break;
                    }
                    if (!renderer.Draw(modelViewProjection)) {
                        failed = true;
                        exitReason = "draw-failed";
                        break;
                    }
                }
                if (!video.Present()) {
                    failed = true;
                    exitReason = "present-failed";
                    break;
                }
                if (nowMicroseconds >= nextStatisticsMicroseconds) {
                    nextStatisticsMicroseconds += static_cast<s64>(StatisticsIntervalSeconds) * 1000000;
                    std::printf("PS3_FRAME_STATS frames=%u draws=%u elapsedMilliseconds=%lld pose=%.3f videoOwnedBytes=%u rendererOwnedBytes=%u commandOffset=0x%X\n",
                                video.PresentedFrameCount(), renderer.DrawCount(), static_cast<long long>(elapsedMicroseconds / 1000),
                                static_cast<double>(pose), video.OwnedBytes(), renderer.OwnedBytes(), video.CommandWriteOffset());
                }
            }
        }

        std::printf("PS3_EXIT reason=%s failed=%d frames=%u draws=%u\n", exitReason, failed ? 1 : 0, video.PresentedFrameCount(), renderer.DrawCount());
        bool drained = false;
        if (videoReady) {
            drained = video.Drain();
            if (!drained) {
                std::printf("PS3_DRAIN_FAILED note=renderer-and-gpu-memory-retained\n");
                failed = true;
            }
        }
        if (rendererReady && drained && video.PresentedFrameCount() > 0) {
            LogFrameProbe(video, "final-frame", pose);
        }
        if (rendererReady && drained) {
            renderer.Shutdown();
        }
        video.Shutdown();
        sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT0);
        const int exitCode = failed ? 1 : 0;
        std::printf("PS3_SHUTDOWN exitCode=%d\n", exitCode);
        return exitCode;
    }
}
