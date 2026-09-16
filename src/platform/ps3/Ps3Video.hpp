#pragma once

#include <ppu-types.h>
#include <rsx/rsx.h>

namespace helengine::ps3 {
    /// Owns the RSX command context, the configured display mode, two 32-bit color
    /// surfaces plus one shared D24S8 depth surface in RSX local memory, and the
    /// double-buffered presentation sequence (bind non-visible target, clear, flip, flush,
    /// wait-flip). Ownership follows the pinned PSL1GHT rsxtest sample ordering.
    ///
    /// Lifetime notes: PSL1GHT has no context teardown or display-buffer unregister API,
    /// so the mapped host command memory and the two registered color buffers are
    /// retained until process exit. Only the depth surface is released, and only after a
    /// successful Drain(). Copying is disabled because the object owns GPU resources.
    class Ps3Video {
    public:
        /// Creates an uninitialized video owner; nothing is allocated until Initialize().
        Ps3Video();

        Ps3Video(const Ps3Video&) = delete;
        Ps3Video& operator=(const Ps3Video&) = delete;

        /// Allocates the host I/O buffer, creates the RSX context, configures the display
        /// (720p when available, else the active mode), allocates and registers the color
        /// surfaces and the depth surface. Logs every failing operation with its code and
        /// returns false; partial state is tracked so Shutdown() stays safe.
        bool Initialize();

        /// Waits for a pending flip while pumping system callbacks (five-second diagnostic
        /// timeout), binds the non-visible color target with the shared depth surface, sets
        /// viewport, scissor, write masks and fixed raster state, and clears both surfaces.
        bool BeginFrame();

        /// Resets the flip status, queues a flip of the current render target, flushes the
        /// command stream, enqueues the GPU-side flip wait and rotates the target index.
        bool Present();

        /// Waits for all queued RSX work to complete through the label write-back primitive
        /// used by the pinned sample, after first waiting for any pending flip. Only valid
        /// after successful context initialization. A timeout is reported and remembered so
        /// GPU-owned memory is never freed afterwards.
        bool Drain();

        /// Releases the depth surface when Drain() succeeded, retains process-lifetime
        /// display and context backing, and logs what was retained. Idempotent and safe
        /// after partial initialization.
        void Shutdown();

        /// Returns the RSX command context (null before Initialize()).
        gcmContextData* Context() const;

        /// Returns the configured frame width in pixels.
        unsigned Width() const;

        /// Returns the configured frame height in pixels.
        unsigned Height() const;

        /// Returns the display aspect ratio reported by the video service (width / height).
        float Aspect() const;

        /// Returns the number of RSX-local bytes owned by this object for diagnostics.
        unsigned OwnedBytes() const;

        /// Returns how many frames Present() has queued.
        unsigned PresentedFrameCount() const;

        /// Returns true when Drain() has completed successfully.
        bool HasDrained() const;

    private:
        /// Size of the main-memory I/O buffer handed to rsxInit (multiple of 1 MiB).
        static constexpr u32 HostBufferBytes = 4u * 1024u * 1024u;

        /// Alignment required by rsxInit for the I/O buffer.
        static constexpr u32 HostBufferAlignment = 1024u * 1024u;

        /// Command buffer size carved out of the I/O buffer by rsxInit.
        static constexpr u32 CommandBufferBytes = 1024u * 1024u;

        /// Number of display color buffers (double buffering).
        static constexpr u32 ColorBufferCount = 2;

        /// Alignment required for linear render surfaces.
        static constexpr u32 SurfaceAlignment = 64;

        /// Bytes per pixel for both X8R8G8B8 color and Z24S8 depth surfaces.
        static constexpr u32 BytesPerPixel = 4;

        /// Label slot used for drain and idle waits (same slot as the pinned sample).
        static constexpr u8 DrainLabelIndex = 255;

        /// Diagnostic timeout for flip and label waits, in microseconds.
        static constexpr s64 WaitTimeoutMicroseconds = 5000000;

        /// Sleep between flip status polls, in microseconds.
        static constexpr u32 FlipPollSleepMicroseconds = 200;

        /// Sleep between label polls, in microseconds.
        static constexpr u32 LabelPollSleepMicroseconds = 30;

        /// Opaque dark clear color (A8R8G8B8).
        static constexpr u32 ClearColor = 0xFF101010u;

        /// Far depth (24-bit) with zero stencil, as consumed by rsxSetClearDepthStencil.
        static constexpr u32 ClearDepthStencil = 0xFFFFFF00u;

        /// RSX command context returned by rsxInit.
        gcmContextData* ContextValue;

        /// 1 MiB-aligned main-memory I/O buffer mapped by rsxInit; retained for the process lifetime.
        void* HostBuffer;

        /// True once rsxInit succeeded.
        bool ContextInitialized;

        /// True once the display mode was configured and its aspect resolved.
        bool DisplayConfigured;

        /// True once every surface was allocated and the display buffers were registered.
        bool SurfacesAllocated;

        /// True once Initialize() completed and frames may be produced.
        bool Ready;

        /// True once Shutdown() ran.
        bool ShutdownDone;

        /// True once a Drain() completed successfully.
        bool Drained;

        /// True once a Drain() or flip wait timed out; GPU-owned memory is then never freed.
        bool DrainFailed;

        /// Selected video resolution id.
        u32 ResolutionId;

        /// Frame width in pixels.
        u32 WidthValue;

        /// Frame height in pixels.
        u32 HeightValue;

        /// Display aspect ratio from the video state.
        float AspectValue;

        /// Bytes per color surface line.
        u32 ColorPitch;

        /// Bytes per depth surface line.
        u32 DepthPitch;

        /// RSX-local color surfaces (effective addresses).
        void* ColorBuffers[ColorBufferCount];

        /// RSX offsets of the color surfaces.
        u32 ColorOffsets[ColorBufferCount];

        /// RSX-local depth surface (effective address).
        void* DepthBuffer;

        /// RSX offset of the depth surface.
        u32 DepthOffset;

        /// Index of the color buffer currently used as render target (not being displayed).
        u32 RenderTargetIndex;

        /// True while a queued flip has not been confirmed complete.
        bool FlipPending;

        /// Monotonic label value for drain waits.
        u32 LabelValue;

        /// Number of frames queued by Present().
        u32 PresentedFrames;

        /// RSX-local bytes owned (two color surfaces plus depth).
        u32 OwnedBytesValue;

        /// Selects the display mode (720p when available, else the active supported mode) and resolves the aspect.
        bool ConfigureDisplay();

        /// Allocates the color and depth surfaces and registers the color buffers with the display.
        bool AllocateSurfaces();

        /// Fills a full surface descriptor (including unused color slots) for the current render target and binds it.
        void BindRenderTarget();

        /// Waits until the last queued flip completed, pumping system callbacks; false on timeout.
        bool WaitForFlip();

        /// Writes a new label value from the GPU back end, flushes, and waits for it on the CPU; false on timeout.
        bool SignalAndWaitLabel();

        /// Makes the GPU wait for the current label and then drains, mirroring the sample's idle wait after video setup.
        bool WaitForRsxIdle();
    };
}
