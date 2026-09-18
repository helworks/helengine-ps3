#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

// The catalog translation unit is written by the platform builder into the generated Core
// root, so it exists only for a build that packaged a project. A Core build made directly
// against a generated tree, which is how the boot gate is exercised, has no catalog and
// must still compile and run.
#if defined(__has_include)
#if __has_include("ps3_runtime_scene_catalog.hpp")
#define HELENGINE_PS3_HAS_SCENE_CATALOG 1
#endif
#endif

class RuntimeSceneCatalog;

namespace helengine::ps3 {
    /// Builds Core's scene catalog from the table the platform builder generated.
    /// @return An owned catalog, or null when this build packaged no scenes.
    ::RuntimeSceneCatalog* BuildPs3RuntimeSceneCatalog();

    /// Returns the scene the host should load at boot.
    /// @return Scene id, or null when this build packaged no scenes.
    const char* Ps3StartupSceneId();
}

#endif
