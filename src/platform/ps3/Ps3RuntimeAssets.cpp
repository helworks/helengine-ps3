#include "platform/ps3/Ps3RuntimeAssets.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "RuntimeSubmesh.hpp"
#include "runtime/array.hpp"

namespace helengine::ps3 {
    Ps3RuntimeMaterial::Ps3RuntimeMaterial(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, bool lit)
        : BaseColorR(red)
        , BaseColorG(green)
        , BaseColorB(blue)
        , BaseColorA(alpha)
        , Lit(lit) {
    }

    bool Ps3RuntimeModel::HasGeometry() const {
        return IndexCounter != 0;
    }

    std::uint32_t Ps3RuntimeModel::IndexCount() const {
        return IndexCounter;
    }

    Ps3RuntimeModel::Ps3RuntimeModel()
        : IndexCounter(0) {
        // Core walks the submesh list during draw, so an empty list is supplied rather than
        // leaving it null. Real geometry replaces this once the RSX buffers are built.
        SetSubmeshes(new Array<::RuntimeSubmesh*>(0));
    }
}

#endif
