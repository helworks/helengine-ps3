#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "Batch2DSink.hpp"

namespace helengine::ps3 {

class Ps3Batch2DPrograms;
class Ps3Batch2DRenderer;

/// <summary>Adapts shared writer callbacks to one borrowed native batch renderer and program pair.</summary>
class Ps3Batch2DSink final : public ::Batch2DSink {
private:
    /// <summary>Borrowed GPU submission owner; it must outlive this generated sink.</summary>
    Ps3Batch2DRenderer& Renderer;

    /// <summary>Borrowed source-scoped shaders selected for every synchronous run submission.</summary>
    Ps3Batch2DPrograms& Programs;

public:
    /// <summary>Creates a non-owning bridge to an initialized renderer and selected program pair.</summary>
    /// <param name="renderer">Native owner receiving camera and run submissions.</param>
    /// <param name="programs">Source-scoped pair used for all shared runs.</param>
    Ps3Batch2DSink(Ps3Batch2DRenderer& renderer, Ps3Batch2DPrograms& programs);

    /// <summary>Sink borrows both native owners and does not retire either GPU resource.</summary>
    ~Ps3Batch2DSink() override;

    /// <summary>Generated sinks cannot be copied because the base is a callback identity.</summary>
    Ps3Batch2DSink(const Ps3Batch2DSink&) = delete;

    /// <summary>Generated sinks cannot be copy-assigned because the base is a callback identity.</summary>
    Ps3Batch2DSink& operator=(const Ps3Batch2DSink&) = delete;

    /// <summary>Forwards the copied camera values to the renderer for validation and SDK packing.</summary>
    /// <param name="projection">Shared Core projection matrix.</param>
    /// <param name="pixelViewport">Shared pixel viewport.</param>
    void SetCamera(::float4x4 projection, ::float4 pixelViewport) override;

    /// <summary>Rejects null buffers and synchronously forwards a generated writer run.</summary>
    /// <param name="buffer">Borrowed shared arrays valid only during this callback.</param>
    /// <param name="run">Run metadata copied or resolved by the renderer.</param>
    void Submit(::Batch2DBuffer* buffer, ::Batch2DRun run) override;
};

} // namespace helengine::ps3

#endif
