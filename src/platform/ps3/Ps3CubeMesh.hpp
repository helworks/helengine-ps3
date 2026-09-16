#pragma once

#include <cstdint>

#include "platform/ps3/Ps3Vertex.hpp"

namespace helengine::ps3 {
    /// Immutable, process-lifetime diagnostic cube geometry: a side-length-2 cube centered
    /// at the origin with 24 face-local vertices (four per face, one constant color per face)
    /// and 36 indices forming two outward-wound triangles per face. Storage is fixed and
    /// static; nothing is allocated and no asset system is involved.
    class Ps3CubeMesh {
    public:
        /// Number of vertices in the fixed vertex array.
        static constexpr int VertexCount = 24;

        /// Number of 16-bit indices in the fixed index array (12 triangles).
        static constexpr int IndexCount = 36;

        /// Returns the 24 face-local vertices, four consecutive vertices per face.
        static const Ps3Vertex* Vertices();

        /// Returns the 36 triangle indices; every index is below VertexCount.
        static const std::uint16_t* Indices();
    };
}
