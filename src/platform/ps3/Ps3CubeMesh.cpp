#include "platform/ps3/Ps3CubeMesh.hpp"

namespace helengine::ps3 {
    namespace {
        /// Builds the four corners of one face as normal - u - v, normal + u - v,
        /// normal + u + v, normal - u + v, where normal, u and v are unit axis vectors with
        /// cross(u, v) == normal. Walking the corners in that order is counter-clockwise when
        /// viewed from outside the cube.
        constexpr Ps3Vertex FaceCorner(float nx, float ny, float nz,
                                       float ux, float uy, float uz,
                                       float vx, float vy, float vz,
                                       float uSign, float vSign,
                                       float r, float g, float b) {
            return Ps3Vertex{
                { nx + uSign * ux + vSign * vx, ny + uSign * uy + vSign * vy, nz + uSign * uz + vSign * vz },
                { nx, ny, nz },
                { r, g, b, 1.0f }
            };
        }

        /// Six faces in the documented frame table: normal, u, v and RGB per face.
        #define HELENGINE_PS3_FACE(nx, ny, nz, ux, uy, uz, vx, vy, vz, r, g, b) \
            FaceCorner(nx, ny, nz, ux, uy, uz, vx, vy, vz, -1.0f, -1.0f, r, g, b), \
            FaceCorner(nx, ny, nz, ux, uy, uz, vx, vy, vz, 1.0f, -1.0f, r, g, b), \
            FaceCorner(nx, ny, nz, ux, uy, uz, vx, vy, vz, 1.0f, 1.0f, r, g, b), \
            FaceCorner(nx, ny, nz, ux, uy, uz, vx, vy, vz, -1.0f, 1.0f, r, g, b)

        /// The fixed 24-vertex array, four vertices per face in face order +X, -X, +Y, -Y, +Z, -Z.
        constexpr Ps3Vertex CubeVertices[Ps3CubeMesh::VertexCount] = {
            HELENGINE_PS3_FACE( 1, 0, 0,   0, 1, 0,   0, 0, 1,   1, 0, 0),
            HELENGINE_PS3_FACE(-1, 0, 0,   0, 0, 1,   0, 1, 0,   0, 1, 1),
            HELENGINE_PS3_FACE( 0, 1, 0,   0, 0, 1,   1, 0, 0,   0, 1, 0),
            HELENGINE_PS3_FACE( 0,-1, 0,   1, 0, 0,   0, 0, 1,   1, 0, 1),
            HELENGINE_PS3_FACE( 0, 0, 1,   1, 0, 0,   0, 1, 0,   0, 0, 1),
            HELENGINE_PS3_FACE( 0, 0,-1,   0, 1, 0,   1, 0, 0,   1, 1, 0)
        };

        #undef HELENGINE_PS3_FACE

        /// Local triangles (0,1,2) and (0,2,3) offset by each face's base vertex index.
        #define HELENGINE_PS3_FACE_INDICES(base) \
            static_cast<std::uint16_t>(base + 0), static_cast<std::uint16_t>(base + 1), static_cast<std::uint16_t>(base + 2), \
            static_cast<std::uint16_t>(base + 0), static_cast<std::uint16_t>(base + 2), static_cast<std::uint16_t>(base + 3)

        /// The fixed 36-entry index array.
        constexpr std::uint16_t CubeIndices[Ps3CubeMesh::IndexCount] = {
            HELENGINE_PS3_FACE_INDICES(0),
            HELENGINE_PS3_FACE_INDICES(4),
            HELENGINE_PS3_FACE_INDICES(8),
            HELENGINE_PS3_FACE_INDICES(12),
            HELENGINE_PS3_FACE_INDICES(16),
            HELENGINE_PS3_FACE_INDICES(20)
        };

        #undef HELENGINE_PS3_FACE_INDICES
    }

    const Ps3Vertex* Ps3CubeMesh::Vertices() {
        return CubeVertices;
    }

    const std::uint16_t* Ps3CubeMesh::Indices() {
        return CubeIndices;
    }
}
