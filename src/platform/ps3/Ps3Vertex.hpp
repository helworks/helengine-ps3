#pragma once

#include <cstddef>

namespace helengine::ps3 {
    /// One mesh vertex as consumed directly by the RSX vertex fetch: three F32 position
    /// components, three F32 normal components, four F32 colors and two F32 UVs, 48 bytes
    /// total. Native float arrays already have the target's byte order.
    struct Ps3Vertex {
        /// Model-space position (x, y, z).
        float Position[3];

        /// Model-space surface normal (x, y, z).
        float Normal[3];

        /// Unlit face color (r, g, b, a) with alpha fixed at 1.
        float Color[4];

        /// Authored texture coordinate (u, v); diagnostic geometry supplies its own face UVs.
        float TexCoord[2];
    };

    static_assert(sizeof(Ps3Vertex) == 48, "Ps3Vertex must be exactly 48 bytes for the RSX stride");
    static_assert(offsetof(Ps3Vertex, Position) == 0, "Ps3Vertex position must start at byte offset 0");
    static_assert(offsetof(Ps3Vertex, Normal) == 12, "Ps3Vertex normal must start at byte offset 12");
    static_assert(offsetof(Ps3Vertex, Color) == 24, "Ps3Vertex color must start at byte offset 24");
    static_assert(offsetof(Ps3Vertex, TexCoord) == 40, "Ps3Vertex UV must start at byte offset 40");
}
