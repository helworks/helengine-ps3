#include "platform/ps3/Ps3Video.hpp"

#include <cstdio>
#include <malloc.h>
#include <unistd.h>

#include <lv2/systime.h>
#include <rsx/rsx.h>
#include <sysutil/sysutil.h>
#include <sysutil/video.h>

namespace helengine::ps3 {
    Ps3Video::Ps3Video()
        : ContextValue(nullptr)
        , HostBuffer(nullptr)
        , ContextInitialized(false)
        , DisplayConfigured(false)
        , SurfacesAllocated(false)
        , Ready(false)
        , ShutdownDone(false)
        , Drained(false)
        , DrainFailed(false)
        , ResolutionId(0)
        , WidthValue(0)
        , HeightValue(0)
        , AspectValue(0.0f)
        , ColorPitch(0)
        , DepthPitch(0)
        , ColorBuffers{ nullptr, nullptr }
        , ColorOffsets{ 0, 0 }
        , DepthBuffer(nullptr)
        , DepthOffset(0)
        , RenderTargetIndex(0)
        , FlipPending(false)
        , LabelValue(0)
        , PresentedFrames(0)
        , OwnedBytesValue(0) {
    }

    bool Ps3Video::Initialize() {
        if (Ready || ContextInitialized) {
            std::printf("PS3_VIDEO_ERROR stage=initialize reason=already-initialized\n");
            return false;
        }
        HostBuffer = memalign(HostBufferAlignment, HostBufferBytes);
        if (HostBuffer == nullptr) {
            std::printf("PS3_VIDEO_ERROR stage=host-buffer bytes=%u alignment=%u\n", HostBufferBytes, HostBufferAlignment);
            return false;
        }
        const s32 initResult = rsxInit(&ContextValue, CommandBufferBytes, HostBufferBytes, HostBuffer);
        if (initResult != 0 || ContextValue == nullptr) {
            std::printf("PS3_VIDEO_ERROR stage=rsxInit code=%d commandBytes=%u ioBytes=%u\n", initResult, CommandBufferBytes, HostBufferBytes);
            return false;
        }
        ContextInitialized = true;
        std::printf("PS3_VIDEO_CONTEXT commandBytes=%u ioBytes=%u\n", CommandBufferBytes, HostBufferBytes);
        if (!ConfigureDisplay()) {
            return false;
        }
        if (!WaitForRsxIdle()) {
            return false;
        }
        gcmSetFlipMode(GCM_FLIP_VSYNC);
        if (!AllocateSurfaces()) {
            return false;
        }
        gcmResetFlipStatus();
        RenderTargetIndex = 0;
        FlipPending = false;
        Ready = true;
        std::printf("PS3_VIDEO_READY resolutionId=%u width=%u height=%u aspect=%.4f colorPitch=%u depthPitch=%u ownedBytes=%u\n",
                    ResolutionId, WidthValue, HeightValue, static_cast<double>(AspectValue), ColorPitch, DepthPitch, OwnedBytesValue);
        return true;
    }

    bool Ps3Video::ConfigureDisplay() {
        u32 candidate = VIDEO_RESOLUTION_720;
        const s32 preferredAvailability = videoGetResolutionAvailability(VIDEO_PRIMARY, candidate, VIDEO_ASPECT_AUTO, 0);
        if (preferredAvailability != 1) {
            videoState activeState = {};
            const s32 stateResult = videoGetState(VIDEO_PRIMARY, 0, &activeState);
            if (stateResult != 0) {
                std::printf("PS3_VIDEO_ERROR stage=videoGetState code=%d\n", stateResult);
                return false;
            }
            candidate = activeState.displayMode.resolution;
            const s32 fallbackAvailability = videoGetResolutionAvailability(VIDEO_PRIMARY, candidate, VIDEO_ASPECT_AUTO, 0);
            std::printf("PS3_VIDEO_MODE_FALLBACK reason=720p-unavailable availability720=%d activeResolutionId=%u activeAvailability=%d\n",
                        preferredAvailability, candidate, fallbackAvailability);
            if (fallbackAvailability != 1) {
                std::printf("PS3_VIDEO_ERROR stage=resolution-availability reason=no-usable-resolution\n");
                return false;
            }
        }
        videoResolution resolution = {};
        const s32 resolutionResult = videoGetResolution(candidate, &resolution);
        if (resolutionResult != 0) {
            std::printf("PS3_VIDEO_ERROR stage=videoGetResolution code=%d resolutionId=%u\n", resolutionResult, candidate);
            return false;
        }
        videoConfiguration configuration = {};
        configuration.resolution = static_cast<u8>(candidate);
        configuration.format = VIDEO_BUFFER_FORMAT_XRGB;
        configuration.aspect = VIDEO_ASPECT_AUTO;
        configuration.pitch = static_cast<u32>(resolution.width) * BytesPerPixel;
        const s32 configureResult = videoConfigure(VIDEO_PRIMARY, &configuration, nullptr, 0);
        if (configureResult != 0) {
            std::printf("PS3_VIDEO_ERROR stage=videoConfigure code=%d resolutionId=%u\n", configureResult, candidate);
            return false;
        }
        videoState state = {};
        const s32 stateResult = videoGetState(VIDEO_PRIMARY, 0, &state);
        if (stateResult != 0) {
            std::printf("PS3_VIDEO_ERROR stage=videoGetState-after-configure code=%d\n", stateResult);
            return false;
        }
        if (state.displayMode.aspect == VIDEO_ASPECT_4_3) {
            AspectValue = 4.0f / 3.0f;
        } else if (state.displayMode.aspect == VIDEO_ASPECT_16_9) {
            AspectValue = 16.0f / 9.0f;
        } else {
            std::printf("PS3_VIDEO_ERROR stage=aspect reason=unknown-aspect value=%u\n", static_cast<unsigned>(state.displayMode.aspect));
            return false;
        }
        ResolutionId = candidate;
        WidthValue = resolution.width;
        HeightValue = resolution.height;
        DisplayConfigured = true;
        std::printf("PS3_VIDEO_MODE resolutionId=%u width=%u height=%u aspectCode=%u scanMode=%u\n",
                    ResolutionId, WidthValue, HeightValue, static_cast<unsigned>(state.displayMode.aspect), static_cast<unsigned>(state.displayMode.scanMode));
        return true;
    }

    bool Ps3Video::AllocateSurfaces() {
        ColorPitch = WidthValue * BytesPerPixel;
        DepthPitch = WidthValue * BytesPerPixel;
        const u32 colorBytes = ColorPitch * HeightValue;
        const u32 depthBytes = DepthPitch * HeightValue;
        for (u32 index = 0; index < ColorBufferCount; ++index) {
            ColorBuffers[index] = rsxMemalign(SurfaceAlignment, colorBytes);
            if (ColorBuffers[index] == nullptr) {
                std::printf("PS3_VIDEO_ERROR stage=color-alloc index=%u bytes=%u\n", index, colorBytes);
                return false;
            }
            OwnedBytesValue += colorBytes;
            const s32 offsetResult = rsxAddressToOffset(ColorBuffers[index], &ColorOffsets[index]);
            if (offsetResult != 0) {
                std::printf("PS3_VIDEO_ERROR stage=color-offset index=%u code=%d\n", index, offsetResult);
                return false;
            }
            const s32 registerResult = gcmSetDisplayBuffer(static_cast<u8>(index), ColorOffsets[index], ColorPitch, WidthValue, HeightValue);
            if (registerResult != 0) {
                std::printf("PS3_VIDEO_ERROR stage=gcmSetDisplayBuffer index=%u code=%d\n", index, registerResult);
                return false;
            }
        }
        DepthBuffer = rsxMemalign(SurfaceAlignment, depthBytes);
        if (DepthBuffer == nullptr) {
            std::printf("PS3_VIDEO_ERROR stage=depth-alloc bytes=%u\n", depthBytes);
            return false;
        }
        OwnedBytesValue += depthBytes;
        const s32 depthOffsetResult = rsxAddressToOffset(DepthBuffer, &DepthOffset);
        if (depthOffsetResult != 0) {
            std::printf("PS3_VIDEO_ERROR stage=depth-offset code=%d\n", depthOffsetResult);
            return false;
        }
        SurfacesAllocated = true;
        std::printf("PS3_VIDEO_SURFACES colorBuffers=%u colorBytesEach=%u depthBytes=%u totalBytes=%u\n",
                    ColorBufferCount, colorBytes, depthBytes, OwnedBytesValue);
        return true;
    }

    void Ps3Video::BindRenderTarget() {
        gcmSurface surface = {};
        surface.type = GCM_SURFACE_TYPE_LINEAR;
        surface.antiAlias = GCM_SURFACE_CENTER_1;
        surface.colorFormat = GCM_SURFACE_X8R8G8B8;
        surface.colorTarget = GCM_SURFACE_TARGET_0;
        for (u32 slot = 0; slot < GCM_MAX_MRT_COUNT; ++slot) {
            surface.colorLocation[slot] = GCM_LOCATION_RSX;
            surface.colorOffset[slot] = 0;
            surface.colorPitch[slot] = SurfaceAlignment;
        }
        surface.colorOffset[0] = ColorOffsets[RenderTargetIndex];
        surface.colorPitch[0] = ColorPitch;
        surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
        surface.depthLocation = GCM_LOCATION_RSX;
        surface.depthOffset = DepthOffset;
        surface.depthPitch = DepthPitch;
        surface.width = static_cast<u16>(WidthValue);
        surface.height = static_cast<u16>(HeightValue);
        surface.x = 0;
        surface.y = 0;
        rsxSetSurface(ContextValue, &surface);
    }

    bool Ps3Video::BeginFrame(bool deferClear) {
        if (!Ready || ShutdownDone || Drained || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=BeginFrame reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        if (FlipPending) {
            if (!WaitForFlip()) {
                return false;
            }
            FlipPending = false;
        }
        BindRenderTarget();
        rsxSetColorMask(ContextValue, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
        rsxSetColorMaskMrt(ContextValue, 0);
        const u16 width = static_cast<u16>(WidthValue);
        const u16 height = static_cast<u16>(HeightValue);
        // Maps clip space x/y in -1..1 to pixels with +Y up on screen and depth -1..1 into 0..1.
        const f32 scale[4] = { width * 0.5f, height * -0.5f, 0.5f, 0.0f };
        const f32 offset[4] = { width * 0.5f, height * 0.5f, 0.5f, 0.0f };
        rsxSetViewport(ContextValue, 0, 0, width, height, 0.0f, 1.0f, scale, offset);
        rsxSetScissor(ContextValue, 0, 0, width, height);
        for (u8 clipIndex = 0; clipIndex < 8; ++clipIndex) {
            rsxSetViewportClip(ContextValue, clipIndex, width, height);
        }
        rsxSetZMinMaxControl(ContextValue, GCM_FALSE, GCM_TRUE, GCM_FALSE);
        rsxSetBlendEnable(ContextValue, GCM_FALSE);
        rsxSetStencilTestEnable(ContextValue, GCM_FALSE);
        rsxSetAlphaTestEnable(ContextValue, GCM_FALSE);
        rsxSetLogicOpEnable(ContextValue, GCM_FALSE);
        rsxSetDepthWriteEnable(ContextValue, GCM_TRUE);
        return deferClear || ClearDefaultSurface();
    }

    bool Ps3Video::ClearSurface(bool clearColorEnabled, bool clearDepthEnabled, bool clearStencilEnabled,
                                u32 packedColor, u32 packedDepthStencil) {
        if (!Ready || ShutdownDone || Drained || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=ClearSurface reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        u32 mask = 0;
        if (clearColorEnabled) {
            mask |= GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A;
            rsxSetClearColor(ContextValue, packedColor);
        }
        if (clearDepthEnabled || clearStencilEnabled) {
            if (clearDepthEnabled) {
                mask |= GCM_CLEAR_Z;
            }
            if (clearStencilEnabled) {
                mask |= GCM_CLEAR_S;
            }
            rsxSetClearDepthStencil(ContextValue, packedDepthStencil);
        }
        if (mask != 0) {
            rsxClearSurface(ContextValue, mask);
        }
        return true;
    }

    bool Ps3Video::ClearDefaultSurface() {
        return ClearSurface(true, true, true, ClearColor, ClearDepthStencil);
    }

    bool Ps3Video::Present() {
        if (!Ready || ShutdownDone || Drained || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=Present reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        // Flush the frame's commands before queuing the flip. gcmSetFlip appends the flip
        // command and advances the PUT pointer by that command's size on the assumption that
        // PUT already equals the write position; queuing the flip with unflushed commands
        // behind it lets the GPU run past the write position into stale ring data (observed
        // in RPCS3 as an RSX FIFO desync on the previous lap's constant floats).
        rsxFlushBuffer(ContextValue);
        gcmResetFlipStatus();
        const s32 flipResult = gcmSetFlip(ContextValue, static_cast<u8>(RenderTargetIndex));
        if (flipResult != 0) {
            std::printf("PS3_VIDEO_ERROR stage=gcmSetFlip code=%d target=%u\n", flipResult, RenderTargetIndex);
            return false;
        }
        rsxFlushBuffer(ContextValue);
        gcmSetWaitFlip(ContextValue);
        FlipPending = true;
        RenderTargetIndex ^= 1u;
        ++PresentedFrames;
        if (PresentedFrames == 1) {
            std::printf("PS3_FIRST_PRESENT target=%u\n", RenderTargetIndex ^ 1u);
        }
        return true;
    }

    bool Ps3Video::WaitForFlip() {
        if (!ContextInitialized || ContextValue == nullptr || ShutdownDone || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=wait-flip reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        const s64 start = sysGetSystemTime();
        while (gcmGetFlipStatus() != 0) {
            sysUtilCheckCallback();
            const s64 waited = sysGetSystemTime() - start;
            if (waited > WaitTimeoutMicroseconds) {
                std::printf("PS3_FLIP_TIMEOUT waitedMicroseconds=%lld frames=%u\n", static_cast<long long>(waited), PresentedFrames);
                DrainFailed = true;
                return false;
            }
            usleep(FlipPollSleepMicroseconds);
        }
        return true;
    }

    bool Ps3Video::SignalAndWaitLabel() {
        if (!ContextInitialized || ContextValue == nullptr || ShutdownDone || Drained || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=wait-label reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        ++LabelValue;
#if defined(HELENGINE_PS3_TEST_WITHHOLD_RUNTIME_LABEL)
        const bool withholdRuntimeLabel = Ready;
        if (withholdRuntimeLabel) {
            std::printf("PS3_TEST_FAULT_WITHHELD_RUNTIME_LABEL label=%u\n", LabelValue);
        } else {
            rsxSetWriteBackendLabel(ContextValue, DrainLabelIndex, LabelValue);
        }
#else
        rsxSetWriteBackendLabel(ContextValue, DrainLabelIndex, LabelValue);
#endif
        rsxFlushBuffer(ContextValue);
        volatile u32* label = gcmGetLabelAddress(DrainLabelIndex);
        const s64 start = sysGetSystemTime();
        while (*label != LabelValue) {
            sysUtilCheckCallback();
            const s64 waited = sysGetSystemTime() - start;
            if (waited > WaitTimeoutMicroseconds) {
                std::printf("PS3_DRAIN_TIMEOUT label=%u observed=%u waitedMicroseconds=%lld\n", LabelValue, *label, static_cast<long long>(waited));
                DrainFailed = true;
                return false;
            }
            usleep(LabelPollSleepMicroseconds);
        }
        return true;
    }

    bool Ps3Video::WaitForRsxIdle() {
        if (!ContextInitialized || ContextValue == nullptr || ShutdownDone || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=initial-idle reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        ++LabelValue;
        rsxSetWriteBackendLabel(ContextValue, DrainLabelIndex, LabelValue);
        rsxSetWaitLabel(ContextValue, DrainLabelIndex, LabelValue);
        return SignalAndWaitLabel();
    }

    bool Ps3Video::WaitForSubmittedWork() {
        if (!Ready || !ContextInitialized || ContextValue == nullptr || ShutdownDone || Drained || DrainFailed) {
            std::printf("PS3_VIDEO_ERROR stage=wait-submitted-work reason=video-unavailable drainFailed=%d\n",
                        DrainFailed ? 1 : 0);
            return false;
        }
        if (FlipPending) {
            if (!WaitForFlip()) {
                return false;
            }
            FlipPending = false;
        }
        if (!SignalAndWaitLabel()) {
            return false;
        }
        std::printf("PS3_SUBMITTED_WORK_COMPLETE label=%u frames=%u drained=0\n", LabelValue, PresentedFrames);
        return true;
    }

    bool Ps3Video::Drain() {
        if (!ContextInitialized || ContextValue == nullptr) {
            std::printf("PS3_DRAIN_SKIPPED reason=no-context\n");
            return false;
        }
        if (ShutdownDone || DrainFailed) {
            std::printf("PS3_DRAIN_SKIPPED reason=video-unavailable drainFailed=%d\n", DrainFailed ? 1 : 0);
            return false;
        }
        if (Drained) {
            return true;
        }
        if (FlipPending) {
            if (!WaitForFlip()) {
                return false;
            }
            FlipPending = false;
        }
        if (!SignalAndWaitLabel()) {
            return false;
        }
        Drained = true;
        std::printf("PS3_DRAIN_COMPLETE label=%u frames=%u\n", LabelValue, PresentedFrames);
        return true;
    }

    void Ps3Video::Shutdown() {
        if (ShutdownDone) {
            return;
        }
        ShutdownDone = true;
        Ready = false;
        u32 freedBytes = 0;
        if (DepthBuffer != nullptr && Drained && !DrainFailed) {
            rsxFree(DepthBuffer);
            freedBytes += DepthPitch * HeightValue;
            DepthBuffer = nullptr;
        }
        const u32 retainedBytes = OwnedBytesValue - freedBytes;
        std::printf("PS3_VIDEO_SHUTDOWN drained=%d drainFailed=%d freedBytes=%u retainedRsxBytes=%u retainedHostBytes=%u frames=%u "
                    "note=display-buffers-and-context-retained-for-process-lifetime\n",
                    Drained ? 1 : 0, DrainFailed ? 1 : 0, freedBytes, retainedBytes, ContextInitialized ? HostBufferBytes : 0, PresentedFrames);
    }

    gcmContextData* Ps3Video::Context() const {
        if (!ContextInitialized || ShutdownDone || Drained || DrainFailed) {
            return nullptr;
        }
        return ContextValue;
    }

    unsigned Ps3Video::Width() const {
        return WidthValue;
    }

    unsigned Ps3Video::Height() const {
        return HeightValue;
    }

    float Ps3Video::Aspect() const {
        return AspectValue;
    }

    unsigned Ps3Video::OwnedBytes() const {
        return OwnedBytesValue;
    }

    unsigned Ps3Video::PresentedFrameCount() const {
        return PresentedFrames;
    }

    unsigned Ps3Video::CommandWriteOffset() const {
        if (ContextValue == nullptr) {
            return 0;
        }
        u32 offset = 0;
        if (rsxAddressToOffset(ContextValue->current, &offset) != 0) {
            return 0xFFFFFFFFu;
        }
        return offset;
    }

    bool Ps3Video::HasDrained() const {
        return Drained && !DrainFailed;
    }

    bool Ps3Video::ReadDisplayedPixel(unsigned x, unsigned y, unsigned* colorOut) const {
        if (!Ready || PresentedFrames == 0 || FlipPending || colorOut == nullptr || x >= WidthValue || y >= HeightValue) {
            return false;
        }
        // After Present() rotated the index, the displayed buffer is the other one.
        const u32 displayedIndex = RenderTargetIndex ^ 1u;
        const volatile u8* row = static_cast<const volatile u8*>(ColorBuffers[displayedIndex]) + static_cast<u32>(y) * ColorPitch;
        const volatile u32* pixel = reinterpret_cast<const volatile u32*>(row + static_cast<u32>(x) * BytesPerPixel);
        *colorOut = (*pixel) & 0x00FFFFFFu;
        return true;
    }
}
