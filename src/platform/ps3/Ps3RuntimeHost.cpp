#include "platform/ps3/Ps3RuntimeHost.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <exception>
#include <malloc.h>
#include <new>
#include <string>
#include <utility>

#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "PlatformInfo.hpp"

#include "SceneLoadMode.hpp"
#include "SceneManager.hpp"

#include "platform/ps3/Ps3ContentStreamSource.hpp"
#include "platform/ps3/Ps3InputBackend.hpp"
#include "platform/ps3/Ps3RasterRenderer.hpp"
#include "platform/ps3/Ps3SceneBootstrap.hpp"
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

        /// Outcome of one guarded call into generated code.
        struct GuardedResult {
            /// True when the call returned without raising.
            bool Succeeded;

            /// Why the call failed. Meaningful only when Succeeded is false.
            std::string Detail;
        };

        /// Runs one call into generated code and captures whatever it raises.
        ///
        /// Generated code raises by pointer, as in throw new ArgumentNullException(...),
        /// which a std::exception reference never matches. Catching only that reference
        /// would send every generated failure to the catch-all and discard its message,
        /// so both shapes are handled here. The pointer form is deleted once its message
        /// has been copied, so reporting a failure does not also leak its cause.
        template <typename TCall>
        GuardedResult Guard(TCall&& call) {
            try {
                call();
                return GuardedResult{ true, std::string() };
            } catch (::Exception* error) {
                std::string detail = error == nullptr
                    ? std::string("a null exception pointer was raised")
                    : std::string(error->what());
                delete error;
                return GuardedResult{ false, std::move(detail) };
            } catch (const std::exception& error) {
                return GuardedResult{ false, std::string(error.what()) };
            } catch (...) {
                return GuardedResult{ false, std::string("an exception of an unrecognized type") };
            }
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
        , ContentStreamSourceInstance(nullptr)
        , InitializationOptionsInstance(nullptr)
        , StageValue(Ps3CoreStage::Booting)
        , ErrorValue(Ps3CoreError::None)
        , FrameCounter(0)
        , ShutdownDone(false)
        , ArmDrawFailure(false) {
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
        snapshot.SceneLoaded = 0;
    }

    Ps3RuntimeHost::~Ps3RuntimeHost() {
        if (!ShutdownDone) {
            Shutdown();
        }
    }

    void Ps3RuntimeHost::ArmDeliberateDrawFailure() {
        ArmDrawFailure = true;
    }

    void Ps3RuntimeHost::FailDeliberately(const char* stageName) {
        Fail(Ps3CoreError::DeliberateFailurePoint, stageName, "the build requested a deliberate stop");
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

    bool Ps3RuntimeHost::Initialize(Ps3Video& video, Ps3RasterRenderer& renderer) {
        const GuardedResult adapters = Guard([this, &video, &renderer] {
            RenderManager3DInstance = new Ps3RenderManager3D(video, renderer);
            RenderManager2DInstance = new Ps3RenderManager2D();
            InputBackendInstance = new Ps3InputBackend();
            PlatformInfoInstance = new ::PlatformInfo(std::string("PlayStation 3"), std::string("0.2.0"));
            ContentStreamSourceInstance = new Ps3ContentStreamSource();
            InitializationOptionsInstance = new ::CoreInitializationOptions();
            InitializationOptionsInstance->set_ContentStreamSource(ContentStreamSourceInstance);

            // A build that packaged no scenes has no catalog, and Core is happy without one.
            // The boot gate is exactly that case, so this stays optional rather than required.
            ::RuntimeSceneCatalog* sceneCatalog = BuildPs3RuntimeSceneCatalog();
            if (sceneCatalog != nullptr) {
                InitializationOptionsInstance->set_SceneCatalog(sceneCatalog);
            }
        });
        if (!adapters.Succeeded) {
            Fail(Ps3CoreError::AdapterConstruction, "AdapterConstruction", adapters.Detail.c_str());
            return false;
        }
        if (ArmDrawFailure) {
            RenderManager3DInstance->ArmDeliberateRaise();
        }
        EnterStage(Ps3CoreStage::AdaptersReady);

        const GuardedResult construction = Guard([this] { CoreInstance = new ::Core(); });
        if (!construction.Succeeded) {
            Fail(Ps3CoreError::CoreConstruction, "CoreConstruction", construction.Detail.c_str());
            return false;
        }
        EnterStage(Ps3CoreStage::CoreConstructed);

        // The window size Core reports comes from the configured display, so a later
        // milestone that renders authored content inherits the real resolution rather than
        // a guess baked in here.
        std::printf("PS3_CORE_DISPLAY width=%u height=%u aspect=%.4f\n",
                    video.Width(), video.Height(), static_cast<double>(video.Aspect()));

        const GuardedResult initialization = Guard([this] {
            CoreInstance->Initialize(RenderManager3DInstance, RenderManager2DInstance, InputBackendInstance, PlatformInfoInstance, InitializationOptionsInstance);
        });
        if (!initialization.Succeeded) {
            Fail(Ps3CoreError::CoreInitialization, "CoreInitialization", initialization.Detail.c_str());
            return false;
        }

        helengine_ps3_core_probe.CoreReadyFreeBytes = FreeHeapBytes();
        EnterStage(Ps3CoreStage::CoreInitialized);
        std::printf("PS3_CORE_READY freeBytes=%u\n", helengine_ps3_core_probe.CoreReadyFreeBytes);

        const char* startupSceneId = Ps3StartupSceneId();
        if (startupSceneId == nullptr) {
            std::printf("PS3_CORE_SCENE none=1 reason=build-packaged-no-scenes\n");
            return true;
        }

        std::printf("PS3_CORE_SCENE_LOADING sceneId=%s\n", startupSceneId);
        const GuardedResult sceneLoad = Guard([this, startupSceneId] {
            CoreInstance->get_SceneManager()->LoadScene(std::string(startupSceneId), ::SceneLoadMode::Single);
        });
        if (!sceneLoad.Succeeded) {
            Fail(Ps3CoreError::SceneLoad, "SceneLoad", sceneLoad.Detail.c_str());
            return false;
        }

        helengine_ps3_core_probe.SceneLoaded = 1;
        std::printf("PS3_CORE_SCENE_LOADED sceneId=%s freeBytes=%u\n", startupSceneId, FreeHeapBytes());
        return true;
    }

    bool Ps3RuntimeHost::Update(double elapsedSeconds) {
        if (CoreInstance == nullptr) {
            Fail(Ps3CoreError::FrameUpdate, "Update", "core is not initialized");
            return false;
        }
        const GuardedResult update = Guard([this, elapsedSeconds] { CoreInstance->Update(elapsedSeconds); });
        if (!update.Succeeded) {
            Fail(Ps3CoreError::FrameUpdate, "Update", update.Detail.c_str());
            return false;
        }
        return true;
    }

    bool Ps3RuntimeHost::Draw() {
        if (CoreInstance == nullptr) {
            Fail(Ps3CoreError::FrameDraw, "Draw", "core is not initialized");
            return false;
        }
        const GuardedResult draw = Guard([this] { CoreInstance->Draw(); });
        if (!draw.Succeeded) {
            Fail(Ps3CoreError::FrameDraw, "Draw", draw.Detail.c_str());
            return false;
        }
        return true;
    }

    bool Ps3RuntimeHost::CompleteFrameBoundary() {
        if (CoreInstance == nullptr) {
            Fail(Ps3CoreError::FrameBoundary, "CompleteFrameBoundary", "core is not initialized");
            return false;
        }
        const GuardedResult boundary = Guard([this] { CoreInstance->CompleteFrameBoundary(); });
        if (!boundary.Succeeded) {
            Fail(Ps3CoreError::FrameBoundary, "CompleteFrameBoundary", boundary.Detail.c_str());
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
        // The options object only borrows the content source, so the source outlives it and
        // is released afterwards.
        delete InitializationOptionsInstance;
        InitializationOptionsInstance = nullptr;
        delete ContentStreamSourceInstance;
        ContentStreamSourceInstance = nullptr;
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
            const GuardedResult disposal = Guard([this] { CoreInstance->Dispose(); });
            if (!disposal.Succeeded) {
                Fail(Ps3CoreError::CoreDisposal, "Dispose", disposal.Detail.c_str());
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

    bool Ps3RuntimeHost::SceneLoaded() const {
        return helengine_ps3_core_probe.SceneLoaded != 0;
    }

    std::uint32_t Ps3RuntimeHost::FrameCount() const {
        return FrameCounter;
    }
}

#endif
