#pragma once

#include <cstddef>

namespace helengine::ps3 {
    /// One cube vertex as consumed directly by the RSX vertex fetch: three F32 position
    /// components at byte offset 0 followed by four F32 color components at byte offset 12,
    /// 28 bytes in total. Native float arrays already have the target's byte order.
    struct Ps3Vertex {
        /// Model-space position (x, y, z).
        float Position[3];

        /// Unlit face color (r, g, b, a) with alpha fixed at 1.
        float Color[4];
    };

    static_assert(sizeof(Ps3Vertex) == 28, "Ps3Vertex must be exactly 28 bytes for the 28-byte RSX stride");
    static_assert(offsetof(Ps3Vertex, Color) == 12, "Ps3Vertex color must start at byte offset 12");
}
