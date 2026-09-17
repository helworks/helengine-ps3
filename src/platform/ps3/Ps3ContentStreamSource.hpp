#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <string>

#include "IContentStreamSource.hpp"
#include "system/io/file-stream.hpp"

namespace helengine::ps3 {
    /// Resolves generated Core content reads against the PlayStation 3 application's content
    /// directory.
    ///
    /// The codegen profile binds generated file access to this type, so the static members
    /// below are a seam the generated File helpers call before falling back to ordinary
    /// file I/O. PlayStation 3 has a real filesystem through newlib, so paths this type
    /// declines are served perfectly well by the default path; it only claims paths that are
    /// content-relative, which is where a packaged game keeps its cooked assets.
    ///
    /// The v0.2 boot gate runs Core against an empty scene and therefore never reads content.
    /// This exists because the generated tree requires the bound type to be present, and it
    /// is written to behave correctly rather than to be a placeholder.
    class Ps3ContentStreamSource final : public ::IContentStreamSource {
    public:
        /// Creates a source rooted at the application's content directory.
        /// @param contentRootPath Absolute directory that content-relative paths resolve against.
        explicit Ps3ContentStreamSource(std::string contentRootPath = DefaultContentRootPath());

        /// Opens one content-relative asset for reading.
        /// @param assetPath Path relative to the content root.
        /// @return An owned stream, or null when the asset does not exist.
        ::Stream* OpenRead(std::string assetPath) override;

        /// Returns the default content root for an installed PlayStation 3 application.
        static std::string DefaultContentRootPath();

        /// Reports whether generated file access should route this path through this type.
        /// @param path Path the generated code is about to open.
        /// @return True when the path is content-relative and this type owns it.
        static bool CanHandlePath(const char* path);

        /// Reports whether a path this type owns exists on disk.
        /// @param path Path the generated code is testing.
        /// @return True when the resolved file exists.
        static bool Exists(const char* path);

        /// Opens a path this type owns.
        /// @param path Path the generated code is opening.
        /// @return An owned file stream, or null when the file does not exist.
        static ::FileStream* OpenRead(const char* path);

    private:
        /// Absolute directory that content-relative paths resolve against.
        std::string ContentRootPath;

        /// Joins the content root and a relative path with a single separator.
        /// @param contentRootPath Absolute content root.
        /// @param assetPath Path relative to that root.
        /// @return The absolute path to the asset.
        static std::string ResolveAssetPath(const std::string& contentRootPath, const std::string& assetPath);
    };
}

#endif
