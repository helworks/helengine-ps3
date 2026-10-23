#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>
#include <memory>
#include <string>

#include "RenderManager2D.hpp"
#include "IRenderVisitor2D.hpp"
#include "float4.hpp"
#include "runtime/array.hpp"
#include "platform/ps3/Ps3Batch2DSceneDiagnostics.hpp"

class ICamera;
class IDrawable2D;
class Batch2DWriter;
class Batch2DDrawableEmitter;

namespace helengine::ps3 {

class Ps3Batch2DPrograms;
class Ps3Batch2DSink;
class Ps3ClipScissorStack2D;
class Ps3RasterRenderer;
class Ps3Video;

/// Owns the CPU side of Core's ordered 2D submission and borrows Raster's GPU owners.
/// Camera queues are visited exactly once by RenderCamera; Draw remains a compatibility
/// hook and does not replay scene content. Dispose must run while Core and Raster are alive.
class Ps3RenderManager2D final : public ::RenderManager2D, public ::IRenderVisitor2D {
private:
    /// Initialized video dimensions and camera submission context, borrowed from the host.
    Ps3Video& Video;

    /// Long-lived GPU owner for 2D textures, geometry, and shader programs.
    Ps3RasterRenderer& Raster;

    /// Source namespace used lazily when the first camera needs the shader pair.
    ::IContentStreamSource& ShaderSource;

    /// Proves the manager is live before a retained Raster callback dereferences its captured pointer.
    std::shared_ptr<void> CallbackLifetime;

    /// CPU-side bounded quad writer; its callback target is this manager while attached.
    std::unique_ptr<::Batch2DWriter> Writer;

    /// Core drawable adapter sharing the writer and generated RenderManager2D services.
    std::unique_ptr<::Batch2DDrawableEmitter> Emitter;

    /// Non-owning sink bridge to Raster's GPU owners.
    std::unique_ptr<Ps3Batch2DSink> Sink;

    /// Value-owned native resolver for per-drawable ancestor clips.
    std::unique_ptr<Ps3ClipScissorStack2D> ClipResolver;

    /// Source-keyed program pair borrowed from Raster after lazy initialization.
    Ps3Batch2DPrograms* Programs;

    /// Camera whose ordered queue is currently being visited, or null outside a pass.
    ::ICamera* ActiveCamera;

    /// Pixel viewport for the active camera, copied before queue traversal.
    ::float4 ActiveViewport;

    /// Scissor resolved for the drawable currently dispatching through this manager.
    ::float4 ActiveScissor;

    /// Number of real camera passes submitted during this manager lifetime.
    std::uint32_t DrawCounter;

    /// Bounded first-pass Core inputs; retains no drawable, font, texture, or entity pointers.
    Ps3Batch2DSceneDiagnostics SceneDiagnostics;

    /// Counts queued drawables explicitly omitted because their entity hierarchy is disabled.
    std::uint32_t SkippedDrawCounter;

    /// True after the first successful camera pass has been reported.
    bool LoggedCameraPass;

    /// True after the first cooked or raw texture build has been reported.
    bool LoggedTextureBuild;

    /// True after the first rounded-rectangle callback has been reported.
    bool LoggedRoundedRect;

    /// True after the first sprite callback has been reported.
    bool LoggedSprite;

    /// True after the first text callback has been reported.
    bool LoggedText;

    /// True after the first texture update has been reported.
    bool LoggedTextureUpdate;

    /// True after explicit Dispose completed successfully.
    bool Disposed;

    /// Latches a failed lazy setup so partially published CPU state can never be reused.
    bool SubmissionInitializationFailed;

    /// Resolves Raster-owned programs and creates the CPU submission objects once.
    void EnsureSubmissionObjects();

    /// Requires the active queue callback context before emitting one drawable.
    void RequireActiveCamera(const char* operation) const;

public:
    /// Creates the resource callback before GPU allocation, with all callback state initialized.
    /// Constructor failure detaches any callback Raster published before the failure.
    /// @param video Initialized video service borrowed for camera dimensions.
    /// @param raster Raster owner retaining every GPU allocation beyond this manager's lifetime.
    /// @param shaderSource Source namespace used to load the exact generated shader pair lazily.
    Ps3RenderManager2D(Ps3Video& video, Ps3RasterRenderer& raster,
                       ::IContentStreamSource& shaderSource);

    /// Attempts orderly CPU detachment without allowing an exception to escape destruction.
    ~Ps3RenderManager2D() override;

    /// Returns the number of camera passes whose ordered 2D queues were rendered.
    std::uint32_t DrawCount() const;

    /// Returns hierarchy-excluded queued drawables; unsupported input throws instead of being skipped.
    std::uint32_t SkippedDrawCount() const;

    /// Compatibility hook; per-camera queues are coordinated by the 3D manager.
    void Draw() override;

    /// Visits one camera's ordered 2D queue and flushes it before returning.
    /// @param camera Required camera with a backbuffer viewport and non-null 2D queue.
    void RenderCamera(::ICamera* camera);

    /// Flushes queued shared geometry when a texture mutation or lifecycle boundary requires it.
    void FlushPending();

    /// Resolves clipping for one ordered drawable and dispatches its Core draw callback.
    /// @param drawable Drawable supplied by the active generated render queue.
    void Visit(::IDrawable2D* drawable) override;

    /// Builds a unique native texture from the required cooked content source and path.
    /// @param cookedAssetPath Canonical source-relative cooked asset path.
    /// @param contentStreamSource Required source used to resolve the path.
    /// @return Core-owned wrapper for the Raster-owned GPU texture.
    ::RuntimeTexture* BuildTextureFromCooked(::HeCppString cookedAssetPath,
        ::IContentStreamSource* contentStreamSource) override;

    /// Copies and uploads a raw Core texture asset through Raster's texture registry.
    /// @param data Required raw RGBA asset.
    /// @return Core-owned wrapper for the Raster-owned GPU texture.
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;

    /// Emits a rounded rectangle using the active camera scissor.
    /// @param shape Drawable supplied by Core during the active queue visit.
    void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;

    /// Emits a sprite using its real texture and source rectangle.
    /// @param sprite Drawable supplied by Core during the active queue visit.
    void DrawSprite(::ISpriteDrawable2D* sprite) override;

    /// Emits text through the shared glyph-atlas path.
    /// @param text Drawable supplied by Core during the active queue visit.
    void DrawText(::ITextDrawable2D* text) override;

    /// Flushes, releases generated default textures, detaches Raster's callback, then frees CPU adapters.
    void Dispose() override;

protected:
    /// Validates signed bounds and forwards a bounded contiguous byte view to Raster resources.
    /// @param texture Core runtime texture wrapper owned by this Raster.
    /// @param x Destination left coordinate.
    /// @param y Destination top coordinate.
    /// @param width Positive destination width.
    /// @param height Positive destination height.
    /// @param rgba8 Borrowed contiguous RGBA bytes; never retained after this call.
    /// @param sourceRowPitch Bytes between source rows.
    void UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x, int32_t y,
        int32_t width, int32_t height, Array<uint8_t>* rgba8,
        int32_t sourceRowPitch) override;
};

} // namespace helengine::ps3

#endif
