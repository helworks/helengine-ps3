#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstddef>
#include <cstdint>

class Core;
class CoreInitializationOptions;
class PlatformInfo;

namespace helengine::ps3 {
    class Ps3ContentStreamSource;
    class Ps3InputBackend;
    class Ps3RasterRenderer;
    class Ps3RenderManager2D;
    class Ps3RenderManager3D;
    class Ps3Video;

    /// Stage the runtime host has reached. Recorded in the snapshot below so a run that ends
    /// without a console attached can still be diagnosed.
    enum class Ps3CoreStage : std::uint32_t {
        Booting = 0,
        AdaptersReady = 1,
        CoreConstructed = 2,
        CoreInitialized = 3,
        FrameComplete = 4,
        CoreDisposed = 5,
        CleanShutdown = 6,
    };

    /// Why the runtime host stopped, when it stopped early.
    enum class Ps3CoreError : std::uint32_t {
        None = 0,
        AdapterConstruction = 1,
        CoreConstruction = 2,
        CoreInitialization = 3,
        FrameUpdate = 4,
        FrameDraw = 5,
        FrameBoundary = 6,
        CoreDisposal = 7,
        DeliberateFailurePoint = 8,
        SceneLoad = 9,
    };

    /// Versioned snapshot of the Core lifecycle, written as it progresses.
    ///
    /// The emulator captures standard output, so the log is the primary evidence. This
    /// exists because hardware has no such console: a debugger or a memory dump can read
    /// these fields and learn exactly how far the run got and why it stopped.
    struct Ps3CoreProbeSnapshot {
        /// Identifies the structure in a memory dump.
        std::uint32_t Magic;

        /// Layout version, so a reader can reject a snapshot it does not understand.
        std::uint32_t Version;

        /// Furthest stage reached, as a Ps3CoreStage value.
        std::uint32_t Stage;

        /// Why the run stopped, as a Ps3CoreError value.
        std::uint32_t ErrorCode;

        /// Number of completed update, draw and frame-boundary cycles.
        std::uint32_t FrameCount;

        /// Times Core asked the 3D manager to draw.
        std::uint32_t Render3DDrawCount;

        /// Times Core asked the 2D manager to draw.
        std::uint32_t Render2DDrawCount;

        /// Free heap in bytes when Core finished initializing, or zero if it never did.
        std::uint32_t CoreReadyFreeBytes;

        /// Free heap in bytes after Core was disposed, or zero if it never was.
        std::uint32_t CoreDisposedFreeBytes;

        /// Non-zero once the startup scene finished loading. Claims the reserved word, which
        /// is why the layout version below is 2.
        std::uint32_t SceneLoaded;
    };

    static_assert(sizeof(Ps3CoreProbeSnapshot) == 40, "Core probe snapshot layout must remain stable");

    extern "C" {
    /// Snapshot symbol a debugger or memory dump can locate by name.
    extern volatile Ps3CoreProbeSnapshot helengine_ps3_core_probe;
    }

    /// Magic value spelling HEC3, marking a valid snapshot.
    constexpr std::uint32_t Ps3CoreProbeMagic = 0x48454333u;

    /// Layout version of the snapshot structure. Version 2 renamed the reserved word to
    /// SceneLoaded, so a reader of version 1 must not interpret that field.
    constexpr std::uint32_t Ps3CoreProbeVersion = 2u;

    /// Owns the generated Core lifetime for the v0.2 boot gate.
    ///
    /// Core is constructed, bound to four native adapters, driven for whatever run duration
    /// the build requested, and then disposed. Every call into generated code is wrapped, so
    /// an exception escaping the generated tree is reported by message at this boundary
    /// rather than terminating the process silently. That boundary is itself part of what
    /// the gate proves, since the codegen profile enables exceptions.
    ///
    /// The host owns its adapters and Core. Ps3Video outlives it.
    class Ps3RuntimeHost {
    public:
        /// Creates a host that has not yet constructed Core.
        Ps3RuntimeHost();

        /// Releases anything still held, without assuming a successful shutdown ran.
        ~Ps3RuntimeHost();

        Ps3RuntimeHost(const Ps3RuntimeHost&) = delete;
        Ps3RuntimeHost& operator=(const Ps3RuntimeHost&) = delete;

        /// Constructs the adapters and Core, then initializes Core against them.
        /// @param video Video owner, used to describe the platform to Core. Must outlive this host.
        /// @param renderer Raster renderer the 3D adapter draws authored geometry through. Must outlive this host.
        /// @return True when Core reached the initialized stage.
        bool Initialize(Ps3Video& video, Ps3RasterRenderer& renderer);

        /// Advances Core by one step.
        /// @param elapsedSeconds Wall-clock seconds since the previous step.
        /// @return True when the step completed without an escaping exception.
        bool Update(double elapsedSeconds);

        /// Asks Core to draw, which reaches the adapters above.
        /// @return True when the draw completed without an escaping exception.
        bool Draw();

        /// Closes the frame, matching the engine's frame-boundary contract.
        /// @return True when the boundary completed without an escaping exception.
        bool CompleteFrameBoundary();

        /// Arms a deliberate raise inside the 2D adapter, which Core calls during its draw.
        ///
        /// Call before Initialize. The raise then travels back out through generated stack
        /// frames and is caught by the boundary in Draw, which is what proves unwinding.
        void ArmDeliberateDrawFailure();

        /// Records a stop the build asked for, distinguishing it from a real defect.
        /// @param stageName Stage the build asked to stop at, for the log.
        void FailDeliberately(const char* stageName);

        /// Disposes Core and releases the adapters. Safe to call once after a failed start.
        /// @return True when disposal completed without an escaping exception.
        bool Shutdown();

        /// Returns the furthest stage reached.
        Ps3CoreStage Stage() const;

        /// Returns why the host stopped, or None while it is healthy.
        Ps3CoreError Error() const;

        /// Reports whether a startup scene finished loading.
        bool SceneLoaded() const;

        /// Returns the number of completed frame cycles.
        std::uint32_t FrameCount() const;

    private:
        /// Generated engine core; null until Initialize succeeds.
        ::Core* CoreInstance;

        /// Platform identity handed to Core.
        ::PlatformInfo* PlatformInfoInstance;

        /// 3D adapter bound to Core.
        Ps3RenderManager3D* RenderManager3DInstance;

        /// 2D adapter bound to Core.
        Ps3RenderManager2D* RenderManager2DInstance;

        /// Input adapter bound to Core.
        Ps3InputBackend* InputBackendInstance;

        /// Content source Core reads assets through. Core refuses to initialize without one,
        /// even for a run that reads nothing.
        Ps3ContentStreamSource* ContentStreamSourceInstance;

        /// Settings Core is initialized with, carrying the content source above.
        ::CoreInitializationOptions* InitializationOptionsInstance;

        /// Furthest stage reached.
        Ps3CoreStage StageValue;

        /// Why the host stopped.
        Ps3CoreError ErrorValue;

        /// Completed frame cycles.
        std::uint32_t FrameCounter;

        /// Whether Shutdown has already run.
        bool ShutdownDone;

        /// Whether the build asked the 2D adapter to raise once, for the unwinding gate.
        bool ArmDrawFailure;

        /// Records a stage transition in both the log and the snapshot.
        void EnterStage(Ps3CoreStage stage);

        /// Records a failure in both the log and the snapshot.
        void Fail(Ps3CoreError error, const char* stageName, const char* detail);

        /// Releases the adapters and platform identity, leaving Core alone.
        void ReleaseAdapters();
    };
}

#endif
