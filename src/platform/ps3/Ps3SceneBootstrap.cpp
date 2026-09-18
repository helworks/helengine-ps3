#include "platform/ps3/Ps3SceneBootstrap.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstddef>
#include <cstdint>

#if defined(HELENGINE_PS3_HAS_SCENE_CATALOG)
#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "ps3_runtime_scene_catalog.hpp"
#include "runtime/array.hpp"
#endif

namespace helengine::ps3 {
#if defined(HELENGINE_PS3_HAS_SCENE_CATALOG)
    ::RuntimeSceneCatalog* BuildPs3RuntimeSceneCatalog() {
        std::size_t sceneCount = 0;
        const HEPs3RuntimeSceneCatalogEntry* entries = he_ps3_runtime_scene_catalog_entries(&sceneCount);
        if (entries == nullptr || sceneCount == 0) {
            return nullptr;
        }

        Array<::RuntimeSceneCatalogEntry*>* catalogEntries =
            new Array<::RuntimeSceneCatalogEntry*>(static_cast<int32_t>(sceneCount));
        for (std::size_t index = 0; index < sceneCount; ++index) {
            const HEPs3RuntimeSceneCatalogEntry& sourceEntry = entries[index];
            (*catalogEntries)[static_cast<int32_t>(index)] =
                new ::RuntimeSceneCatalogEntry(sourceEntry.SceneId, sourceEntry.CookedRelativePath);
        }
        return new ::RuntimeSceneCatalog(catalogEntries);
    }

    const char* Ps3StartupSceneId() {
        const char* startupSceneId = he_ps3_runtime_startup_scene_id();
        return startupSceneId == nullptr || startupSceneId[0] == '\0' ? nullptr : startupSceneId;
    }
#else
    ::RuntimeSceneCatalog* BuildPs3RuntimeSceneCatalog() {
        return nullptr;
    }

    const char* Ps3StartupSceneId() {
        return nullptr;
    }
#endif
}

#endif
