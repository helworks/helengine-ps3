#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstddef>
#include <memory>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "PlatformInfo.hpp"
#include "runtime/native_exceptions.hpp"
#include "platform/ps3/Ps3ContentStreamSource.hpp"
#include "platform/ps3/Ps3InputBackend.hpp"
#include "platform/ps3/Ps3RasterRenderer.hpp"
#include "platform/ps3/Ps3RenderManager2D.hpp"
#include "platform/ps3/Ps3RenderManager3D.hpp"
#include "platform/ps3/Ps3Batch2DResources.hpp"
#include "platform/ps3/Ps3Video.hpp"

/// Owns one real generated Core and the native PS3 services used by its camera fixture.
class Ps3Batch2DCameraRuntime final {
private:
    /// Native video context shared by both render managers and all Raster-owned GPU objects.
    helengine::ps3::Ps3Video Video;

    /// GPU owner that outlives Core and both manager adapters through successful video drain.
    helengine::ps3::Ps3RasterRenderer Raster;

    /// Content namespace used by Core and the source-keyed shader-program cache.
    std::unique_ptr<helengine::ps3::Ps3ContentStreamSource> ContentSource;

    /// Native per-camera 2D manager whose callback is detached before destruction.
    std::unique_ptr<helengine::ps3::Ps3RenderManager2D> Manager2D;

    /// Native camera coordinator that dispatches each camera's 3D queue before its 2D queue.
    std::unique_ptr<helengine::ps3::Ps3RenderManager3D> Manager3D;

    /// Neutral platform input adapter required by generated Core initialization.
    helengine::ps3::Ps3InputBackend Input;

    /// Platform identity passed through Core's real adapter initialization boundary.
    ::PlatformInfo Platform;

    /// Generated runtime whose actual ObjectManager registers cameras and drawable queues.
    std::unique_ptr<::Core> CoreInstance;

    /// Settings object passed into Core and bound to its required content source.
    ::CoreInitializationOptions* Options;

    /// True only after Core accepted all native adapters and options.
    bool CoreInitialized;

    /// True after idempotent teardown has completed successfully.
    bool ShutdownComplete;

    /// Exact 2D texture allocation count sampled after manager detach and successful video drain.
    std::size_t TwoDOwnedBytesAfterDrain;

public:
    /// Creates an uninitialized fixture runtime with stable platform identity and empty owners.
    Ps3Batch2DCameraRuntime()
        : Video(), Raster(), ContentSource(), Manager2D(), Manager3D(), Input(),
          Platform("PS3 camera fixture", "1"), CoreInstance(), Options(nullptr),
          CoreInitialized(false), ShutdownComplete(false), TwoDOwnedBytesAfterDrain(0u) {}

    /// Rejects implicit unsafe teardown failures; the fixture calls Shutdown and checks its result.
    ~Ps3Batch2DCameraRuntime() {
        if (!ShutdownComplete) {
            try { Shutdown(); }
            catch (...) { std::printf("PS3_BATCH2D_CAMERA_CLEANUP_ERROR stage=destructor\n"); }
        }
    }

    /// Initializes the real RSX context, Raster owners, native managers, and generated Core.
    /// @return True only when Core initialized against all required native adapters.
    bool Start() {
        if (!Video.Initialize()) return false;
        if (!Raster.Initialize(Video)) return false;
        ContentSource = std::make_unique<helengine::ps3::Ps3ContentStreamSource>(
            "/app_home/USRDIR/content");
        Manager2D = std::make_unique<helengine::ps3::Ps3RenderManager2D>(
            Video, Raster, *ContentSource);
        Manager3D = std::make_unique<helengine::ps3::Ps3RenderManager3D>(
            Video, Raster, *Manager2D);
        Options = new ::CoreInitializationOptions();
        Options->set_ContentStreamSource(ContentSource.get());
        CoreInstance = std::make_unique<::Core>(Options);
        CoreInstance->Initialize(Manager3D.get(), Manager2D.get(), &Input, &Platform, Options);
        CoreInitialized = true;
        return true;
    }

    /// Attempts Core disposal, detaches the 2D callback while Core and its source remain alive,
    /// then destroys Core/managers and drains RSX before retiring Raster owners.
    /// @return True when each required shutdown stage succeeds.
    bool Shutdown() {
        if (ShutdownComplete) return true;
        bool succeeded = true;
        const auto cleanupStage = [&succeeded](const char* stage, const auto& action) {
            try {
                action();
            } catch (::Exception* exception) {
                std::printf("PS3_BATCH2D_CAMERA_CLEANUP_ERROR stage=%s reason=%s\n", stage,
                    exception == nullptr ? "null generated exception" : exception->what());
                delete exception;
                succeeded = false;
            } catch (const std::exception& exception) {
                std::printf("PS3_BATCH2D_CAMERA_CLEANUP_ERROR stage=%s reason=%s\n",
                    stage, exception.what());
                succeeded = false;
            } catch (...) {
                std::printf("PS3_BATCH2D_CAMERA_CLEANUP_ERROR stage=%s reason=unknown exception\n", stage);
                succeeded = false;
            }
        };

        if (CoreInstance != nullptr && CoreInitialized) {
            cleanupStage("core-dispose", [this]() { CoreInstance->Dispose(); });
        }
        CoreInitialized = false;
        if (Manager2D != nullptr) {
            cleanupStage("manager2d-dispose", [this]() { Manager2D->Dispose(); });
            cleanupStage("manager2d-idempotent-dispose", [this]() { Manager2D->Dispose(); });
        }
        CoreInstance.reset();
        if (Manager3D != nullptr) {
            cleanupStage("manager3d-dispose", [this]() { Manager3D->Dispose(); });
        }
        Manager3D.reset();
        Manager2D.reset();
        ContentSource.reset();
        const bool drained = Video.Drain();
        if (!drained) {
            std::printf("PS3_BATCH2D_CAMERA_CLEANUP_ERROR stage=video-drain reason=drain failed\n");
            succeeded = false;
        }
        if (drained) {
            TwoDOwnedBytesAfterDrain = Raster.Batch2DResources().OwnedGpuBytes();
            cleanupStage("raster-shutdown", [this]() { Raster.Shutdown(); });
            cleanupStage("video-shutdown", [this]() { Video.Shutdown(); });
        }
        ShutdownComplete = succeeded;
        return succeeded;
    }

    /// Provides the live Core so fixture entities use its real ObjectManager registries.
    /// @return Initialized generated runtime Core.
    ::Core& Core() {
        if (!CoreInitialized || CoreInstance == nullptr) throw std::logic_error("camera fixture Core is not initialized");
        return *CoreInstance;
    }

    /// Provides video operations for an explicit begin/draw/present sequence.
    /// @return Initialized native video owner.
    helengine::ps3::Ps3Video& VideoService() { return Video; }

    /// Provides native Raster operations for owned textures and the post-2D 3D-state check.
    /// @return Raster owner retained through final GPU drain.
    helengine::ps3::Ps3RasterRenderer& RasterService() { return Raster; }

    /// Provides the native 3D adapter for cooked cube/model submission through generated queues.
    /// @return Core's active native 3D render manager.
    helengine::ps3::Ps3RenderManager3D& Manager3DService() { return *Manager3D; }

    /// Provides the exact source namespace used by generated Core and cooked GPU asset loading.
    /// @return Live content source rooted at the staged standard build content directory.
    helengine::ps3::Ps3ContentStreamSource& ContentSourceService() { return *ContentSource; }

    /// Reports texture GPU bytes remaining after manager disposal and the final video drain.
    /// @return Exact Raster-owned 2D texture bytes sampled before Raster shutdown.
    std::size_t Owned2DBytesAfterDrain() const { return TwoDOwnedBytesAfterDrain; }

    /// Provides the exact 2D adapter bound into generated Core.
    /// @return Core's active native 2D render manager.
    helengine::ps3::Ps3RenderManager2D& Manager2DService() { return *Manager2D; }
};

#endif
