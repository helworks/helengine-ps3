#include "platform/ps3/Ps3RuntimeModel.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
namespace helengine::ps3 {
    /// Starts without realized geometry or invented normal/UV validity.
    Ps3RuntimeModel::Ps3RuntimeModel()
        : MeshBuffers{nullptr, 0, nullptr, 0, 0, false, 0},
          UsableNormals(false), UsableTexCoords(false) {}
    /// Tests whether an upload populated the descriptor.
    bool Ps3RuntimeModel::HasGeometry() const { return MeshBuffers.IndexCount != 0; }
    /// Returns the recorded source normal validity.
    bool Ps3RuntimeModel::HasUsableNormals() const { return UsableNormals; }
    /// Stores the source normal validation result.
    void Ps3RuntimeModel::SetHasUsableNormals(bool value) { UsableNormals = value; }
    /// Returns the recorded source UV validity.
    bool Ps3RuntimeModel::HasUsableTexCoords() const { return UsableTexCoords; }
    /// Stores the source UV validation result.
    void Ps3RuntimeModel::SetHasUsableTexCoords(bool value) { UsableTexCoords = value; }
    /// Returns the number of indices supplied by the original model asset.
    std::uint32_t Ps3RuntimeModel::IndexCount() const { return MeshBuffers.IndexCount; }
    /// Exposes the renderer-retained geometry for indexed draws.
    const Ps3MeshBuffers& Ps3RuntimeModel::Buffers() const { return MeshBuffers; }
    /// Records a successfully uploaded descriptor; material/model disposal never frees its buffers.
    void Ps3RuntimeModel::AdoptBuffers(const Ps3MeshBuffers& buffers) { MeshBuffers = buffers; }
}
#endif
