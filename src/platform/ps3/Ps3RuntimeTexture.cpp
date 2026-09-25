#include "platform/ps3/Ps3RuntimeTexture.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <stdexcept>

#include "platform/ps3/Ps3Batch2DResources.hpp"

namespace helengine::ps3 {

/// <summary>Initializes generated public dimensions/ownership and stores only borrowed registry lifetime state.</summary>
Ps3RuntimeTexture::Ps3RuntimeTexture(Ps3Batch2DResources& owner, std::uint32_t width, std::uint32_t height)
    : OwnerValue(&owner)
    , OwnerLifetime(owner.LifetimeSentinel)
    , NativeDisposed(false) {
    Width = static_cast<std::int32_t>(width);
    Height = static_cast<std::int32_t>(height);
    IsEngineOwned = true;
    IsDisposed = false;
}

/// <summary>Synchronizes the public generated flag with private state after successful registry retirement.</summary>
void Ps3RuntimeTexture::MarkNativeDisposed() {
    NativeDisposed = true;
    IsDisposed = true;
}

/// <summary>Reads the authoritative native state that public Core fields cannot mutate.</summary>
bool Ps3RuntimeTexture::IsNativeDisposed() const {
    return NativeDisposed;
}

/// <summary>Validates the weak owner lifetime before delegating safe logical or GPU retirement.</summary>
void Ps3RuntimeTexture::Dispose() {
    if (NativeDisposed) {
        IsDisposed = true;
        return;
    }
    if (OwnerLifetime.expired()) {
        throw std::runtime_error("PS3 runtime texture owner lifetime has ended before Dispose");
    }
    OwnerValue->Retire(*this);
}

} // namespace helengine::ps3

#endif
