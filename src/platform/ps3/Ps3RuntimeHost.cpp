#include "platform/ps3/Ps3RuntimeHost.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <exception>
#include <malloc.h>
#include <new>

#include "Core.hpp"
#include "PlatformInfo.hpp"

#include "platform/ps3/Ps3InputBackend.hpp"
#include "platform/ps3/Ps3RenderManager2D.hpp"
#include "platform/ps3/Ps3RenderManager3D.hpp"
#include "platform/ps3/Ps3Video.hpp"

namespace helengine::ps3 {
    extern "C" {
    volatile Ps3CoreProbeSnapshot helengine_ps3_core_probe;
    }

    namespace {
        /// Reports free heap bytes, used to show that Core's allocations are returned on
        /// disposal rather than leaked.
        std::uint32_t FreeHeapBytes() {
            const struct mallinfo information = mallinfo();
            return static_cast<std::uint32_t>(information.fordblks);
        }

        /// Human-readable stage name for the log.
        const char* StageName(Ps3CoreStage stage) {
            switch (stage) {
                case Ps3CoreStage::Booting: return "Booting";
                case Ps3CoreStage::AdaptersReady: return "AdaptersReady";
                case Ps3CoreStage::CoreConstructed: return "CoreConstructed";
                case Ps3CoreStage::CoreInitialized: return "CoreInitialized";
                case Ps3CoreStage::FrameComplete: return "FrameComplete";
                case Ps3CoreStage::CoreDisposed: return "CoreDisposed";
                case Ps3CoreStage::CleanShutdown: return "CleanShutdown";
            }
            return "Unknown";
        }
    }

    Ps3RuntimeHost::Ps3RuntimeHost()
        : CoreInstance(nullptr)
        , PlatformInfoInstance(nullptr)
        , RenderManager3DInstance(nullptr)
        , RenderManager2DInstance(nullptr)
        , InputBackendInstance(nullptr)
        , StageValue(Ps3CoreStage::Booting)
        , ErrorValue(Ps3CoreError::None)
        , FrameCounter(0)
        , ShutdownDone(false) {
        volatile Ps3CoreProbeSnapshot& snapshot = helengine_ps3_core_probe;
        snapshot.Magic = Ps3CoreProbeMagic;
        snapshot.Version = Ps3CoreProbeVersion;
        snapshot.Stage = static_cast<std::uint32_t>(Ps3CoreStage::Booting);
        snapshot.ErrorCode = static_cast<std::uint32_t>(Ps3CoreError::None);
        snapshot.FrameCount = 0;
        snapshot.Render3DDrawCount = 0;
        snapshot.Render2DDrawCount = 0;
        snapshot.CoreReadyFreeBytes = 0;
        snapshot.CoreDisposedFreeBytes = 0;
        snapshot.Reserved = 0;
    }

    Ps3RuntimeHost::~Ps3RuntimeHost() {
        if (!ShutdownDone) {
            Shutdown();
        }
    }

    void Ps3RuntimeHost::EnterStage(Ps3CoreStage stage) {
        StageValue = stage;
        helengine_ps3_core_probe.Stage = static_cast<std::uint32_t>(stage);
        std::printf("PS3_CORE_STAGE stage=%s freeBytes=%u\n", StageName(stage), FreeHeapBytes());
    }

    void Ps3RuntimeHost::Fail(Ps3CoreError error, const char* stageName, const char* detail) {
        ErrorValue = error;
        helengine_ps3_core_probe.ErrorCode = static_cast<std::uint32_t>(error);
        std::printf("PS3_CORE_ERROR stage=%s code=%u detail=%s\n",
                    stageName, static_cast<unsigned>(error), detail != nullptr ? detail : "(none)");
    }

    bool Ps3RuntimeHost::Initialize(Ps3Video& video) {
        try {
            RenderManager3DInstance = new Ps3RenderManager3D();
            RenderManager2DInstance = new Ps3RenderManager2D();
            InputBackendInstance = new Ps3InputBackend();
            PlatformInfoInstance = new ::PlatformInfo(std::string("PlayStation 3"), std::string("0.2.0"));
        } catch (const std::exception& error) {
            Fail(Ps3CoreError::AdapterConstruction, "AdapterConstruction", error.what());
            return false;
        } catch (...) {
            Fail(Ps3CoreError::AdapterConstruction, "AdapterConstruction", "unknown exception");
            return false;
        }
        EnterStage(Ps3CoreStage::AdaptersReady);

        try {
            CoreInstance = new ::Core();
        } catch (const std::exception& error) {
            Fail(Ps3CoreError::CoreConstruction, "CoreConstruction", error.what());
            return false;
        } catch (...) {
            Fail(Ps3CoreError::CoreConstruction, "CoreConstruction", "unknown exception");
            return false;
        }
        EnterStage(Ps3CoreStage::CoreConstructed);

        // The window size Core reports comes from the configured display, so a later
        // milestone that renders authored content inherits the real resolution rather than
        // a guess baked in here.
        std::printf("PS3_CORE_DISPLAY width=%u height=%u aspect=%.4f\n",
                    video.Width(), video.Height(), static_cast<double>(video.Aspect()));

        try {
            CoreInstance->Initialize(RenderManager3DInstance, RenderManager2DInstance, InputBackendInstance, PlatformInfoInstance);
        } catch (const std::exception& error) {
            Fail(Ps3CoreError::CoreInitialization, "CoreInitialization", error.what());
            return false;
        } catch (...) {
            Fail(Ps3CoreError::CoreInitialization, "CoreInitialization", "unknown exception");
            return false;
        }

        helengine_ps3_core_probe.CoreReadyFreeBytes = FreeHeapBytes();
        EnterStage(Ps3CoreStage::CoreInitialized);
        std::printf("PS3_CORE_READY freeBytes=%u\n", helengine_ps3_core_probe.CoreReadyFreeBytes);
        return true;
    }

    bool Ps3RuntimeHost::Update(double elapsedSeconds) {
        if (CoreInstance == nullptr) {
            Fail(Ps3CoreError::FrameUpdate, "Update", "core is not initialized");
            return false;
        }
        try {
            CoreInstance->Update(elapsedSeconds);
        } catch (const std::exception& error) {
            Fail(Ps3CoreError::FrameUpdate, "Update", error.what());
            return false;
        } catch (...) {
            Fail(Ps3CoreError::FrameUpdate, "Update", "unknown exception");
            return false;
        }
        return true;
    }

    bool Ps3RuntimeHost::Draw() {
        if (CoreInstance == nullptr) {
            Fail(Ps3CoreError::FrameDraw, "Draw", "core is not initialized");
            return false;
        }
        try {
            CoreInstance->Draw();
        } catch (const std::exception& error) {
            Fail(Ps3CoreError::FrameDraw, "Draw", error.what());
            return false;
        } catch (...) {
            Fail(Ps3CoreError::FrameDraw, "Draw", "unknown exception");
            return false;
        }
        return true;
    }

    bool Ps3RuntimeHost::CompleteFrameBoundary() {
        if (CoreInstance == nullptr) {
            Fail(Ps3CoreError::FrameBoundary, "CompleteFrameBoundary", "core is not initialized");
            return false;
        }
        try {
            CoreInstance->CompleteFrameBoundary();
        } catch (const std::exception& error) {
            Fail(Ps3CoreError::FrameBoundary, "CompleteFrameBoundary", error.what());
            return false;
        } catch (...) {
            Fail(Ps3CoreError::FrameBoundary, "CompleteFrameBoundary", "unknown exception");
            return false;
        }

        ++FrameCounter;
        helengine_ps3_core_probe.FrameCount = FrameCounter;
        if (RenderManager3DInstance != nullptr) {
            helengine_ps3_core_probe.Render3DDrawCount = RenderManager3DInstance->DrawCount();
        }
        if (RenderManager2DInstance != nullptr) {
            helengine_ps3_core_probe.Render2DDrawCount = RenderManager2DInstance->DrawCount();
        }
        if (FrameCounter == 1) {
            EnterStage(Ps3CoreStage::FrameComplete);
        }
        return true;
    }

    void Ps3RuntimeHost::ReleaseAdapters() {
        delete PlatformInfoInstance;
        PlatformInfoInstance = nullptr;
        delete InputBackendInstance;
        InputBackendInstance = nullptr;
        delete RenderManager2DInstance;
        RenderManager2DInstance = nullptr;
        delete RenderManager3DInstance;
        RenderManager3DInstance = nullptr;
    }

    bool Ps3RuntimeHost::Shutdown() {
        if (ShutdownDone) {
            return ErrorValue == Ps3CoreError::None;
        }
        ShutdownDone = true;

        bool disposed = true;
        if (CoreInstance != nullptr) {
            try {
                CoreInstance->Dispose();
            } catch (const std::exception& error) {
                Fail(Ps3CoreError::CoreDisposal, "Dispose", error.what());
                disposed = false;
            } catch (...) {
                Fail(Ps3CoreError::CoreDisposal, "Dispose", "unknown exception");
                disposed = false;
            }
            delete CoreInstance;
            CoreInstance = nullptr;
            if (disposed) {
                helengine_ps3_core_probe.CoreDisposedFreeBytes = FreeHeapBytes();
                EnterStage(Ps3CoreStage::CoreDisposed);
                std::printf("PS3_CORE_DISPOSED freeBytes=%u frames=%u\n",
                            helengine_ps3_core_probe.CoreDisposedFreeBytes, FrameCounter);
            }
        }

        ReleaseAdapters();
        if (disposed && ErrorValue == Ps3CoreError::None) {
            EnterStage(Ps3CoreStage::CleanShutdown);
        }
        return disposed;
    }

    Ps3CoreStage Ps3RuntimeHost::Stage() const {
        return StageValue;
    }

    Ps3CoreError Ps3RuntimeHost::Error() const {
        return ErrorValue;
    }

    std::uint32_t Ps3RuntimeHost::FrameCount() const {
        return FrameCounter;
    }
}

#endif
