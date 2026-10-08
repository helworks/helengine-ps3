#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstddef>
#include <cstdint>
#include <string>

class Batch2DBuffer;
class Batch2DRun;
class float4;
class float4x4;

namespace helengine::ps3 {

class Ps3Batch2DPrograms;
class Ps3Batch2DResources;
class Ps3Video;

/// <summary>Submits bounded shared 2D runs through reflected SpriteBatchShader programs and RSX-local geometry.</summary>
class Ps3Batch2DRenderer final {
private:
    /// <summary>Borrowed initialized video service, retained until successful post-drain shutdown.</summary>
    Ps3Video& Video;

    /// <summary>Borrowed texture registry used to reject foreign runtime wrappers.</summary>
    Ps3Batch2DResources& Resources;

    /// <summary>Owned RSX-local geometry allocation for at most 4096 vertices.</summary>
    void* VertexBuffer;

    /// <summary>Owned RSX-local index allocation for at most 6144 ushort indices.</summary>
    void* IndexBuffer;

    /// <summary>Resolved RSX offset of VertexBuffer.</summary>
    std::uint32_t VertexOffset;

    /// <summary>Resolved RSX offset of IndexBuffer.</summary>
    std::uint32_t IndexOffset;

    /// <summary>Total live GPU bytes owned by the two bounded geometry allocations.</summary>
    std::size_t OwnedGpuBytesValue;

    /// <summary>Copied SDK-column-packed camera matrix for the most recent valid camera.</summary>
    float CameraColumns[16];

    /// <summary>Pixel viewport copied from Core and validated against the active backbuffer.</summary>
    float Viewport[4];

    /// <summary>True after geometry and offsets are fully initialized.</summary>
    bool Initialized;

    /// <summary>True after SetCamera has accepted a projection and viewport.</summary>
    bool CameraConfigured;

    /// <summary>True after an unsafe GPU submission failure permanently poisons this renderer.</summary>
    bool Failed;

    /// <summary>True after successful explicit post-drain release.</summary>
    bool ShutdownComplete;

    /// <summary>True after the first submitted opaque-black textured quad is traced once.</summary>
    bool ShadowDiagnosticLogged;

    /// <summary>Number of completed indexed draw commands submitted by this renderer.</summary>
    std::uint64_t DrawCountValue;

public:
    /// <summary>Creates a renderer that borrows the live video and texture registry without allocating GPU memory.</summary>
    /// <param name="video">Initialized video owner which outlives this renderer.</param>
    /// <param name="resources">Live texture registry used to resolve run texture wrappers.</param>
    Ps3Batch2DRenderer(Ps3Video& video, Ps3Batch2DResources& resources);

    /// <summary>Retains GPU allocations when explicit successful post-drain shutdown was omitted.</summary>
    ~Ps3Batch2DRenderer();

    /// <summary>GPU buffers cannot be copied because each renderer owns unique RSX allocations.</summary>
    Ps3Batch2DRenderer(const Ps3Batch2DRenderer&) = delete;

    /// <summary>GPU buffers cannot be copy-assigned because each renderer owns unique RSX allocations.</summary>
    Ps3Batch2DRenderer& operator=(const Ps3Batch2DRenderer&) = delete;

    /// <summary>Atomically allocates aligned bounded vertex/index buffers and validates both RSX offsets.</summary>
    void Initialize();

    /// <summary>Validates and copies a finite projection and integral pixel viewport for subsequent submissions.</summary>
    /// <param name="projection">Core row-vector projection matrix copied into SDK DP4 column order.</param>
    /// <param name="pixelViewport">Pixel viewport x, y, width, height.</param>
    void SetCamera(const ::float4x4& projection, const ::float4& pixelViewport);

    /// <summary>Validates, copies, reflects, and submits one shared writer run synchronously.</summary>
    /// <param name="programs">Initialized source-scoped Textured and RoundedShape program pair.</param>
    /// <param name="buffer">Shared generated vertex/index arrays, borrowed only for this call.</param>
    /// <param name="run">Shared variant, texture, and scissor description.</param>
    void Submit(Ps3Batch2DPrograms& programs, ::Batch2DBuffer& buffer, const ::Batch2DRun& run);

    /// <summary>Releases only these geometry allocations after the video's successful terminal drain.</summary>
    /// <param name="error">Receives the lifecycle or drain failure while storage remains retained.</param>
    /// <returns>True when both allocations are released; repeated successful shutdown is idempotent.</returns>
    bool ShutdownAfterDrain(std::string& error);

    /// <summary>Returns the exact RSX-local bytes retained by this renderer.</summary>
    std::size_t OwnedGpuBytes() const;

    /// <summary>Returns completed indexed draw submissions.</summary>
    std::uint64_t DrawCount() const;
};

} // namespace helengine::ps3

#endif
