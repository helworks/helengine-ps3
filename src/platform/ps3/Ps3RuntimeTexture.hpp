#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>
#include <memory>

#include "RuntimeTexture.hpp"

namespace helengine::ps3 {

class Ps3Batch2DResources;

/// <summary>Core-owned runtime handle that borrows one renderer-managed PS3 texture allocation.</summary>
class Ps3RuntimeTexture final : public ::RuntimeTexture {
private:
    /// <summary>Resource registry that owns the GPU object while this wrapper remains live.</summary>
    Ps3Batch2DResources* OwnerValue;

    /// <summary>Non-owning lifetime proof used before touching OwnerValue from late Core disposal.</summary>
    std::weak_ptr<void> OwnerLifetime;

    /// <summary>Private authoritative disposal state, independent of the generated public IsDisposed field.</summary>
    bool NativeDisposed;

    /// <summary>Creates a private Core-facing wrapper bound to an already selected renderer owner.</summary>
    /// <param name="owner">Renderer resource set whose lifetime sentinel is borrowed.</param>
    /// <param name="width">Validated texture width in texels.</param>
    /// <param name="height">Validated texture height in texels.</param>
    explicit Ps3RuntimeTexture(Ps3Batch2DResources& owner, std::uint32_t width, std::uint32_t height);

    /// <summary>Records successful native retirement after the resource registry releases the GPU owner.</summary>
    void MarkNativeDisposed();

    /// <summary>Reports the native authoritative state to the resource registry without trusting public fields.</summary>
    bool IsNativeDisposed() const;

    friend class Ps3Batch2DResources;

public:
    /// <summary>Destroys only the CPU wrapper; it never flushes, retires GPU state, or calls the borrowed owner.</summary>
    ~Ps3RuntimeTexture() override = default;

    /// <summary>Prevents copying a handle whose membership identity is its stable wrapper address.</summary>
    Ps3RuntimeTexture(const Ps3RuntimeTexture&) = delete;

    /// <summary>Prevents assigning a handle whose membership identity is its stable wrapper address.</summary>
    Ps3RuntimeTexture& operator=(const Ps3RuntimeTexture&) = delete;

    /// <summary>Asks the live resource registry to flush and retire this allocation exactly once.</summary>
    void Dispose() override;
};

} // namespace helengine::ps3

#endif
