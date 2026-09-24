#pragma once

#include <ppu-types.h>

namespace helengine::ps3 {
    /// RSX geometry descriptor; allocations remain retained by the renderer until successful drain.
    struct Ps3MeshBuffers {
        /// Host pointer to the RSX-local vertex buffer, or null.
        void* VertexBuffer;
        /// RSX offset of the vertex buffer.
        u32 VertexBufferOffset;
        /// Host pointer to the RSX-local index buffer, or null.
        void* IndexBuffer;
        /// RSX offset of the index buffer.
        u32 IndexBufferOffset;
        /// Number of uploaded indices.
        u32 IndexCount;
        /// True when indices are 32-bit rather than 16-bit.
        bool Uses32BitIndices;
        /// RSX-local bytes occupied by both buffers.
        u32 OwnedBytes;
    };
}
