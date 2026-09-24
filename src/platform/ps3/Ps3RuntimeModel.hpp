#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
#include <cstdint>
#include "RuntimeModel.hpp"
#include "platform/ps3/Ps3MeshBuffers.hpp"

namespace helengine::ps3 {
    /// Retains mesh descriptors and input validity while the renderer owns deferred GPU retirement.
    class Ps3RuntimeModel final : public ::RuntimeModel {
    private:
        /// Uploaded geometry descriptor; zero until a successful upload is adopted.
        Ps3MeshBuffers MeshBuffers;
        /// Whether each position has a finite nonzero source normal.
        bool UsableNormals;
        /// Whether each position has a finite source texture coordinate.
        bool UsableTexCoords;
    public:
        /// Creates an unrealized model; the generated base creates its empty submesh collection.
        Ps3RuntimeModel();
        /// Prevents duplicating generated owned containers and geometry descriptors.
        Ps3RuntimeModel(const Ps3RuntimeModel&) = delete;
        /// Prevents assigning generated owned containers and geometry descriptors.
        Ps3RuntimeModel& operator=(const Ps3RuntimeModel&) = delete;
        /// Reports whether uploaded index geometry is available.
        bool HasGeometry() const;
        /// Reports whether normals satisfy the authored standard vertex contract.
        bool HasUsableNormals() const;
        /// Records source normal validity after geometry conversion.
        void SetHasUsableNormals(bool value);
        /// Reports whether UVs satisfy the authored standard vertex contract.
        bool HasUsableTexCoords() const;
        /// Records source UV validity after geometry conversion.
        void SetHasUsableTexCoords(bool value);
        /// Returns the original index count independently of its element width.
        std::uint32_t IndexCount() const;
        /// Borrows the mesh descriptor retained until renderer shutdown after drain.
        const Ps3MeshBuffers& Buffers() const;
        /// Adopts one successful upload descriptor without taking early-free ownership.
        void AdoptBuffers(const Ps3MeshBuffers& buffers);
    };
}
#endif
