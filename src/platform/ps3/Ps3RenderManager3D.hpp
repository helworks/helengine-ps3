#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstdint>

#include <string>

#include "IRenderVisitor3D.hpp"
#include "RenderManager3D.hpp"
#include "float4x4.hpp"

class Asset;

namespace helengine::ps3 {
    class Ps3RasterRenderer;
    class Ps3RenderManager2D;
    class Ps3Video;

    /// Bridges Core scene data to the authored P3MT/P3SP standard draw path.
    /// GPU resources remain in the renderer until successful drain, beyond Core disposal.
    /// The initial profile supports one whole-model submesh, opaque material and basic lighting.
    class Ps3RenderManager3D final : public ::RenderManager3D, public ::IRenderVisitor3D {
    public:
        /// Creates a camera coordinator bound to the video, shared raster owners, and 2D pass.
        /// @param video Video owner supplying the display surface and dimensions. Must outlive this.
        /// @param renderer Raster renderer that uploads geometry and submits draws. Must outlive this.
        /// @param renderer2D Per-camera 2D pass, called after each camera's 3D queue. Must outlive this.
        Ps3RenderManager3D(Ps3Video& video, Ps3RasterRenderer& renderer,
                           Ps3RenderManager2D& renderer2D);

        /// Draws one scene drawable the camera ordered.
        /// @param drawable Drawable Core ordered for the active camera.
        void Visit(::IDrawable3D* drawable) override;

        /// Counts how many times Core asked this manager to draw.
        std::uint32_t DrawCount() const;

        /// Arms a deliberate raise on the next draw.
        ///
        /// Core calls this manager from inside its own draw, so the raise travels back out
        /// through generated stack frames before the runtime host catches it. That is the
        /// evidence that enabling exceptions for this platform was real rather than nominal.
        /// The 3D manager is the target because Core drives it every frame, while it never
        /// calls the 2D manager for the empty scene this gate runs.
        void ArmDeliberateRaise();

        /// Collects the first camera and supported scene lighting, then visits its ordered drawables.
        void Draw() override;

        /// Rejects legacy material payloads that cannot identify an authored shader pair.
        /// @param materialAsset Cooked material Core is asking to realize.
        /// @return Never returns normally for legacy payloads.
        ::RuntimeMaterial* BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) override;

        /// Reads one cooked material from content and realizes it.
        /// @param cookedAssetPath Content-relative path to the cooked material.
        /// @param contentStreamSource Source the path is resolved against.
        /// @return An owned P3MT material borrowing fully realized cache resources.
        ::RuntimeMaterial* BuildMaterialFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;

        /// Reads one cooked model from content and realizes it.
        /// @param cookedAssetPath Content-relative path to the cooked model.
        /// @param contentStreamSource Source the path is resolved against.
        /// @return An owned Core model borrowing uploaded geometry retained by the renderer.
        ::RuntimeModel* BuildModelFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;

        /// Copies indexed positions, normals and UVs and retains source validity.
        /// @param data Raw model asset Core is asking to realize.
        /// @return An owned model with validated whole-model submesh metadata.
        ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;

        /// Reports disposal without freeing resources that may still be referenced by RSX.
        void Dispose() override;

    private:
        /// Number of draw requests received from Core.
        std::uint32_t DrawCounter;

        /// Whether the first draw has already been logged, so the log stays readable.
        bool LoggedFirstDraw;

        /// Whether the build asked the next draw to raise, for the unwinding gate.
        bool RaiseOnDraw;

        /// Whether the first cooked model has been reported.
        bool LoggedFirstModel;

        /// Number of cooked materials realized.
        std::uint32_t MaterialCounter;

        /// Number of cooked models realized.
        std::uint32_t ModelCounter;

        /// Raster renderer that uploads geometry and submits draws.
        Ps3RasterRenderer& Renderer;

        /// Per-camera 2D pass coordinated after each camera's 3D queue.
        Ps3RenderManager2D& Renderer2D;

        /// Video owner supplying the display aspect.
        Ps3Video& Video;

        /// View-projection for the camera being visited, valid only during Draw.
        ::float4x4 ViewProjection;

        /// Number of drawables visited during the most recent draw.
        std::uint32_t VisitedDrawables;

        /// Number of drawables actually submitted during the most recent draw.
        std::uint32_t SubmittedDrawables;

        /// Whether the camera and its queue were reported once already.
        bool LoggedCamera;

        /// Whether the first visited drawable was reported.
        bool LoggedFirstVisit;

        /// Reports why one frame or drawable was skipped, once per distinct reason.
        /// @param marker Stable reason marker.
        void NoteSkip(const char* marker);

        /// Last skip marker reported, so the same reason is not logged every frame.
        const char* LastSkipMarker;

        /// Scene ambient RGBA intensity prepared before the ordered draw queue is visited.
        float AmbientLight[4];

        /// First supported directional light RGBA intensity for the active scene.
        float DirectionalLight[4];

        /// World-space direction of travel for the active directional light.
        float DirectionalDirection[4];

        /// World position of the active camera, supplied before visiting drawables.
        float CameraPosition[4];

        /// Distinguishes an absent directional light from an authored zero-intensity light.
        bool HasDirectionalLight;

        /// Reads one cooked asset through the content source and reports a clear reason on failure.
        /// @param cookedAssetPath Content-relative path to the cooked asset.
        /// @param contentStreamSource Source the path is resolved against.
        /// @param kind Asset kind named in diagnostics.
        /// @return The deserialized asset; never null.
        static ::Asset* ReadCookedAsset(const std::string& cookedAssetPath, ::IContentStreamSource* contentStreamSource, const char* kind);
    };
}

#endif
