#pragma once

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
class IContentStreamSource;
#endif

#include "platform/ps3/Ps3Matrix4.hpp"
#include "platform/ps3/Ps3MeshBuffers.hpp"

namespace helengine::ps3 {
    class Ps3Video;
    struct Ps3Vertex;

    class Ps3AuthoredContentCache;
    class Ps3RuntimeMaterial;
    struct Ps3StandardDrawData;
#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
    class Ps3Batch2DPrograms;
    class Ps3Batch2DRenderer;
    class Ps3Batch2DResources;
#endif

    /// Owns diagnostic geometry/programs and source-isolated authored GPU caches.
    /// Authored draws preflight reflected semantics, every active uniform and sampler;
    /// the embedded path remains an explicit diagnostic entry point, never a fallback.
    /// All GPU allocations are retained until successful video drain and Shutdown.
    class Ps3RasterRenderer {
    public:
        /// Creates an empty renderer; nothing is acquired until Initialize().
        Ps3RasterRenderer();

        /// Destroys CPU bookkeeping; GPU retirement requires explicit post-drain Shutdown.
        ~Ps3RasterRenderer();

        /// GPU owners cannot be copied.
        Ps3RasterRenderer(const Ps3RasterRenderer&) = delete;
        /// GPU ownership cannot be assigned.
        Ps3RasterRenderer& operator=(const Ps3RasterRenderer&) = delete;

        /// Reflects the embedded programs, copies the fragment microcode, uploads the cube
        /// vertices and indices into RSX memory and records allocation sizes. Fails (false,
        /// logged) if reflection, conversion or any allocation fails. The video object must
        /// outlive this renderer.
        bool Initialize(Ps3Video& video);

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
        /// Creates and retains the native shared-2D GPU owners before initializing their buffers.
        /// @param flushPending Callback that submits queued CPU geometry before texture mutation.
        void InitializeBatch2D(std::function<void()> flushPending);

        /// Returns the raster-owned Core texture registry after its 2D bridge was initialized.
        Ps3Batch2DResources& Batch2DResources();

        /// Returns the raster-owned bounded shared-2D GPU submitter after initialization.
        Ps3Batch2DRenderer& Batch2DRenderer();

        /// Finds or creates a stable shader-pair owner keyed by exact content-source identity.
        /// @param source Content source used to load the manifest paths and P3SP programs.
        Ps3Batch2DPrograms& ResolveBatch2DPrograms(::IContentStreamSource& source);
#endif

        /// Submits the cube with the given row-major model/view/projection matrix into the
        /// frame that Ps3Video::BeginFrame() opened. Returns false if not initialized.
        bool Draw(const Ps3Matrix4& modelViewProjection);

        /// Uploads one authored mesh into RSX memory.
        /// @param vertices Vertex array in the renderer's 48-byte layout.
        /// @param vertexCount Number of vertices.
        /// @param indices Triangle-list indices, 16 or 32 bit as declared below.
        /// @param indexCount Number of indices; must be a multiple of three.
        /// @param uses32BitIndices True when indices are 32 bit. RSX draws either width.
        /// @param buffers Receives the allocated buffers, or stays zeroed on failure.
        /// @return True when both buffers were allocated and filled.
        bool UploadMesh(const Ps3Vertex* vertices, u32 vertexCount, const void* indices, u32 indexCount, bool uses32BitIndices, Ps3MeshBuffers& buffers);

        /// Frees one uploaded mesh. Safe on a zeroed structure. The caller must have drained
        /// GPU work first, exactly as for the renderer's own buffers.
        /// @param buffers Registered buffers; zeroed only after successful post-drain release.
        void ReleaseMesh(Ps3MeshBuffers& buffers);

        /// Submits one uploaded mesh with the given transform and material/light inputs.
        /// @param buffers Mesh to draw.
        /// @param modelViewProjection Row-major model/view/projection matrix.
        /// @param normalTransform Row-major inverse-transpose transform for model-space normals.
        /// @param materialColor Base color as normalized RGBA channels.
        /// @param lit True to shade with scene light values; false preserves vertex color.
        /// @param ambientLight RGBA ambient light intensity.
        /// @param directionalLight RGBA directional light intensity.
        /// @param directionalDirection World-space direction in which light rays travel.
        /// @return True when the draw was submitted.
        bool DrawIndexed(const Ps3MeshBuffers& buffers, const Ps3Matrix4& modelViewProjection,
                         const Ps3Matrix4& normalTransform, const float materialColor[4], bool lit,
                         const float ambientLight[4], const float directionalLight[4],
                         const float directionalDirection[4]);

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
        /// Loads P3MT and its P3SP/textures through the same source into renderer-owned caches.
        Ps3RuntimeMaterial* BuildAuthoredMaterial(const std::string& path, ::IContentStreamSource& source, std::string& error);

        /// Validates every authored binding before submitting an indexed ForwardStandard draw.
        bool DrawAuthored(const Ps3MeshBuffers& buffers, const Ps3RuntimeMaterial& material,
                          const Ps3StandardDrawData& draw, std::string& error);
#endif

        /// Releases only the resources this renderer acquired. The caller must have drained
        /// GPU work successfully first; embedded program storage is static and never freed.
        void Shutdown();

        /// Returns the RSX-local bytes owned by this renderer for diagnostics.
        unsigned OwnedBytes() const;

        /// Returns how many diagnostic or authored indexed draws were submitted.
        unsigned DrawCount() const;

    private:
        /// Alignment for vertex and index buffers in RSX memory.
        static constexpr u32 GeometryAlignment = 128;

        /// Alignment required for fragment microcode in RSX local memory.
        static constexpr u32 FragmentUcodeAlignment = 64;

        /// Bytes between consecutive vertices (sizeof(Ps3Vertex)).
        static constexpr u8 VertexStride = 48;

        /// Byte offset of the position inside a vertex.
        static constexpr u32 PositionByteOffset = 0;

        /// Byte offset of the normal inside a vertex.
        static constexpr u32 NormalByteOffset = 12;

        /// Byte offset of the color inside a vertex.
        static constexpr u32 ColorByteOffset = 24;

        /// Byte offset of authored TEXCOORD0.
        static constexpr u32 TexCoordByteOffset = 40;

        /// Video owner consulted before GPU allocation retirement.
        Ps3Video* VideoValue;

        /// Uploaded mesh allocations retained beyond Core model disposal.
        std::vector<Ps3MeshBuffers> UploadedMeshes;

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)
        /// Source-isolated caches whose GPU allocations survive Core disposal.
        std::vector<std::unique_ptr<Ps3AuthoredContentCache>> ContentCaches;

        /// Shared 2D texture allocations retained until successful post-drain shutdown.
        std::unique_ptr<Ps3Batch2DResources> Batch2DResourcesValue;

        /// Shared 2D geometry buffers retained until successful post-drain shutdown.
        std::unique_ptr<Ps3Batch2DRenderer> Batch2DRendererValue;

        /// Source-identity shader pairs retained until successful post-drain shutdown.
        std::vector<std::unique_ptr<Ps3Batch2DPrograms>> Batch2DProgramSets;
#endif

        /// RSX command context borrowed from the video owner.
        gcmContextData* ContextValue;

        /// Embedded vertex program (static storage generated by the build; never freed).
        const rsxVertexProgram* VertexProgram;

        /// Vertex microcode pointer inside the embedded vertex program.
        void* VertexUcode;

        /// Vertex microcode size in bytes.
        u32 VertexUcodeBytes;

        /// Embedded fragment program (static storage generated by the build; never freed).
        const rsxFragmentProgram* FragmentProgram;

        /// RSX-local copy of the fragment microcode.
        void* FragmentUcodeBuffer;

        /// RSX offset of the fragment microcode copy.
        u32 FragmentUcodeOffset;

        /// Fragment microcode size in bytes.
        u32 FragmentUcodeBytes;

        /// Reflected mvp constant of the vertex program.
        const rsxProgramConst* ModelViewProjectionConstant;

        /// Reflected inverse-transpose normal matrix constant of the vertex program.
        const rsxProgramConst* NormalTransformConstant;

        /// Reflected material base color constant.
        const rsxProgramConst* MaterialColorConstant;

        /// Reflected ambient light constant.
        const rsxProgramConst* AmbientLightConstant;

        /// Reflected directional light color/intensity constant.
        const rsxProgramConst* DirectionalLightConstant;

        /// Reflected direction-of-travel constant for the directional light.
        const rsxProgramConst* DirectionalDirectionConstant;

        /// Reflected material lighting selector constant.
        const rsxProgramConst* LitConstant;

        /// Hardware attribute index reflected for POSITION.
        u8 PositionAttributeIndex;

        /// Hardware attribute index reflected for COLOR0.
        u8 ColorAttributeIndex;

        /// Hardware attribute index reflected for NORMAL.
        u8 NormalAttributeIndex;

        /// RSX-local vertex buffer (24 vertices).
        void* VertexBuffer;

        /// RSX offset of the vertex buffer.
        u32 VertexBufferOffset;

        /// Vertex buffer size in bytes.
        u32 VertexBufferBytes;

        /// RSX-local index buffer (36 16-bit indices).
        void* IndexBuffer;

        /// RSX offset of the index buffer.
        u32 IndexBufferOffset;

        /// Index buffer size in bytes.
        u32 IndexBufferBytes;

        /// RSX-local bytes owned by this renderer.
        u32 OwnedBytesValue;

        /// Number of submitted draws.
        u32 DrawCounter;

        /// True once Initialize() completed.
        bool Initialized;

        /// Finds the reflected attribute indices for POSITION, NORMAL and COLOR0 and logs the attribute table.
        bool ReflectAttributes();

        /// Uploads the matrix to the vertex program's mvp constant. The pinned SDK writes the
        /// sixteen floats to four consecutive constant registers in order, and cgcomp/Cg store
        /// a float4x4 as one row per register, so the row-major Ps3Matrix4 is uploaded as-is
        /// (no transpose). This is the only place the matrix layout is adapted.
        void UploadModelViewProjection(const Ps3Matrix4& modelViewProjection);
    };
}
