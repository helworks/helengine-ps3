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
    class Ps3Video;

    /// Minimal 3D render manager for the v0.2 Core boot gate.
    ///
    /// The gate runs Core against an empty scene to prove the lifecycle, so this adapter
    /// accepts being driven and draws nothing. Every request that would require real
    /// rendering resources raises instead of returning a null or empty object, because a
    /// silent failure here would surface much later as an unexplained blank frame.
    ///
    /// Only `BuildModelFromRaw` is abstract on the generated base; the overrides below are
    /// the subset the gate actually needs to control.
    class Ps3RenderManager3D final : public ::RenderManager3D, public ::IRenderVisitor3D {
    public:
        /// Creates an adapter bound to the video owner and raster renderer it draws through.
        /// @param video Video owner supplying the display aspect. Must outlive this.
        /// @param renderer Raster renderer that uploads geometry and submits draws. Must outlive this.
        Ps3RenderManager3D(Ps3Video& video, Ps3RasterRenderer& renderer);

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

        /// Records the draw request and presents nothing, or raises when armed. The boot
        /// gate's visible output is the v0.1 cube, drawn independently of Core.
        void Draw() override;

        /// Builds a renderer material from one cooked platform payload.
        /// @param materialAsset Cooked material Core is asking to realize.
        /// @return An owned material carrying the cooked base colour.
        ::RuntimeMaterial* BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) override;

        /// Reads one cooked material from content and realizes it.
        /// @param cookedAssetPath Content-relative path to the cooked material.
        /// @param contentStreamSource Source the path is resolved against.
        /// @return An owned material carrying the cooked base colour.
        ::RuntimeMaterial* BuildMaterialFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;

        /// Reads one cooked model from content and realizes it.
        /// @param cookedAssetPath Content-relative path to the cooked model.
        /// @param contentStreamSource Source the path is resolved against.
        /// @return An owned model, currently without GPU geometry.
        ::RuntimeModel* BuildModelFromCooked(std::string cookedAssetPath, ::IContentStreamSource* contentStreamSource) override;

        /// Refuses model construction, which belongs to the authored scene milestone.
        /// @param data Raw model asset Core is asking to realize.
        /// @return Never returns normally.
        ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;

        /// Releases nothing, because this adapter never hands out models.
        void Dispose() override;

    private:
        /// Number of draw requests received from Core.
        std::uint32_t DrawCounter;

        /// Whether the first draw has already been logged, so the log stays readable.
        bool LoggedFirstDraw;

        /// Whether the build asked the next draw to raise, for the unwinding gate.
        bool RaiseOnDraw;

        /// Whether the first cooked material has been reported.
        bool LoggedFirstMaterial;

        /// Whether the first cooked model has been reported.
        bool LoggedFirstModel;

        /// Number of cooked materials realized.
        std::uint32_t MaterialCounter;

        /// Number of cooked models realized.
        std::uint32_t ModelCounter;

        /// Raster renderer that uploads geometry and submits draws.
        Ps3RasterRenderer& Renderer;

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

        /// Reads one cooked asset through the content source and reports a clear reason on failure.
        /// @param cookedAssetPath Content-relative path to the cooked asset.
        /// @param contentStreamSource Source the path is resolved against.
        /// @param kind Asset kind named in diagnostics.
        /// @return The deserialized asset; never null.
        static ::Asset* ReadCookedAsset(const std::string& cookedAssetPath, ::IContentStreamSource* contentStreamSource, const char* kind);
    };
}

#endif
