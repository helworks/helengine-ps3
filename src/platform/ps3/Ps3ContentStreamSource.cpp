#include "platform/ps3/Ps3ContentStreamSource.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/stat.h>

namespace helengine::ps3 {
    namespace {
        /// Prefix that marks a path as belonging to the application's content directory.
        /// A packaged PlayStation 3 title keeps its files under the running application's
        /// USRDIR, which GameOS exposes through this mount point.
        constexpr const char* ContentPathPrefix = "/app_home/USRDIR/content/";

        /// Reports whether a path already names an absolute GameOS location, in which case
        /// ordinary file access handles it and this type must not claim it.
        bool IsAbsoluteDevicePath(const char* path) {
            return path != nullptr && path[0] == '/';
        }
    }

    Ps3ContentStreamSource::Ps3ContentStreamSource(std::string contentRootPath)
        : ContentRootPath(std::move(contentRootPath)) {
    }

    std::string Ps3ContentStreamSource::DefaultContentRootPath() {
        return std::string(ContentPathPrefix);
    }

    std::string Ps3ContentStreamSource::ResolveAssetPath(const std::string& contentRootPath, const std::string& assetPath) {
        if (assetPath.empty()) {
            return contentRootPath;
        }
        if (IsAbsoluteDevicePath(assetPath.c_str())) {
            return assetPath;
        }
        std::string resolved = contentRootPath;
        if (!resolved.empty() && resolved.back() != '/') {
            resolved.push_back('/');
        }
        std::string relative = assetPath;
        for (char& character : relative) {
            if (character == '\\') {
                character = '/';
            }
        }
        if (!relative.empty() && relative.front() == '/') {
            relative.erase(relative.begin());
        }
        resolved += relative;
        return resolved;
    }

    bool Ps3ContentStreamSource::CanHandlePath(const char* path) {
        if (path == nullptr) {
            return false;
        }
        // Only content-relative paths are claimed. An absolute device path is a deliberate
        // request for a specific GameOS location and belongs to ordinary file access, which
        // newlib serves correctly on this platform.
        return !IsAbsoluteDevicePath(path);
    }

    bool Ps3ContentStreamSource::Exists(const char* path) {
        if (path == nullptr) {
            return false;
        }
        const std::string resolved = ResolveAssetPath(DefaultContentRootPath(), path);
        struct stat status;
        return stat(resolved.c_str(), &status) == 0;
    }

    ::FileStream* Ps3ContentStreamSource::OpenRead(const char* path) {
        if (path == nullptr) {
            return nullptr;
        }
        const std::string resolved = ResolveAssetPath(DefaultContentRootPath(), path);
        struct stat status;
        if (stat(resolved.c_str(), &status) != 0) {
            std::printf("PS3_CONTENT_MISSING path=%s\n", resolved.c_str());
            return nullptr;
        }
        return new ::FileStream(resolved.c_str(), FileMode::Open, FileAccess::Read, FileShare::Read);
    }

    ::Stream* Ps3ContentStreamSource::OpenRead(HeCppString assetPath) {
        if (assetPath.is_null()) throw std::invalid_argument("PS3 content asset path cannot be null.");
        const std::string resolved = ResolveAssetPath(ContentRootPath, std::string(assetPath.data(), assetPath.size()));
        struct stat status;
        if (stat(resolved.c_str(), &status) != 0) {
            std::printf("PS3_CONTENT_MISSING path=%s\n", resolved.c_str());
            return nullptr;
        }
        std::printf("PS3_CONTENT_OPEN path=%s bytes=%ld\n", resolved.c_str(), static_cast<long>(status.st_size));
        return new ::FileStream(resolved.c_str(), FileMode::Open, FileAccess::Read, FileShare::Read);
    }
}

#endif
