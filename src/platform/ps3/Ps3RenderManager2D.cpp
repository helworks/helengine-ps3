#include "platform/ps3/Ps3RenderManager2D.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include "Batch2DCameraProjection.hpp"
#include "Batch2DDrawableEmitter.hpp"
#include "Batch2DRun.hpp"
#include "Batch2DVariant.hpp"
#include "Batch2DWriter.hpp"
#include "CameraViewportResolver.hpp"
#include "ICamera.hpp"
#include "IDrawable2D.hpp"
#include "IRenderQueue2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "IContentStreamSource.hpp"
#include "RuntimeTexture.hpp"
#include "TextureAsset.hpp"
#include "Entity.hpp"
#include "runtime/native_exceptions.hpp"

#include "platform/ps3/Ps3Batch2DPrograms.hpp"
#include "platform/ps3/Ps3Batch2DRenderer.hpp"
#include "platform/ps3/Ps3Batch2DResources.hpp"
#include "platform/ps3/Ps3Batch2DSceneDiagnostics.hpp"
#include "platform/ps3/Ps3Batch2DSink.hpp"
#include "platform/ps3/Ps3ClipScissorStack2D.hpp"
#include "platform/ps3/Ps3RasterRenderer.hpp"
#include "platform/ps3/Ps3RuntimeTexture.hpp"
#include "platform/ps3/Ps3Video.hpp"

namespace helengine::ps3 {
    namespace {
        /// Reports the first real use of a manager category without recurring per-frame log noise.
        void NoteFirstUse(const char* category, bool& reported) {
            if (reported) return;
            reported = true;
            std::printf("PS3_CORE_RENDER2D_FIRST_USE category=%s\n", category);
        }

        /// Logs cleanup exceptions that occur while another failure remains authoritative.
        void LogSecondaryFailure(const char* stage) {
            try {
                throw;
            } catch (::Exception* exception) {
                std::printf("PS3_CORE_RENDER2D_ERROR stage=%s reason=%s\n", stage,
                    exception == nullptr ? "null generated exception" : exception->what());
                delete exception;
            } catch (const std::exception& exception) {
                std::printf("PS3_CORE_RENDER2D_ERROR stage=%s reason=%s\n", stage, exception.what());
            } catch (...) {
                std::printf("PS3_CORE_RENDER2D_ERROR stage=%s reason=unknown-secondary-exception\n", stage);
            }
        }

        /// Converts a finite nonnegative boundary into a checked integer pixel edge.
        int32_t PixelEdge(double value) {
            const double rounded = std::ceil(value - 0.5);
            if (!std::isfinite(rounded) || rounded < 0.0 ||
                rounded > static_cast<double>(std::numeric_limits<int32_t>::max())) {
                throw std::runtime_error("PS3 2D camera viewport edge is outside supported integer pixels");
            }
            return static_cast<int32_t>(rounded);
        }

        /// Resolves normalized/pixel Core values and applies the shared pixel-center edge convention.
        ::float4 ResolvePixelViewport(::float4 requested, unsigned width, unsigned height) {
            const ::float4 resolved = ::CameraViewportResolver::ResolveViewport(
                requested, static_cast<double>(width), static_cast<double>(height));
            if (!std::isfinite(resolved.X) || !std::isfinite(resolved.Y) ||
                !std::isfinite(resolved.Z) || !std::isfinite(resolved.W) ||
                resolved.X < 0.0f || resolved.Y < 0.0f || resolved.Z <= 0.0f || resolved.W <= 0.0f) {
                throw std::runtime_error("PS3 2D camera viewport must be finite and positive");
            }
            const int32_t left = PixelEdge(resolved.X);
            const int32_t top = PixelEdge(resolved.Y);
            const int32_t right = PixelEdge(static_cast<double>(resolved.X) + resolved.Z);
            const int32_t bottom = PixelEdge(static_cast<double>(resolved.Y) + resolved.W);
            if (right <= left || bottom <= top ||
                static_cast<uint64_t>(right) > width || static_cast<uint64_t>(bottom) > height) {
                throw std::runtime_error("PS3 2D camera viewport extends outside the backbuffer");
            }
            return ::float4(static_cast<float>(left), static_cast<float>(top),
                static_cast<float>(right - left), static_cast<float>(bottom - top));
        }
    }

    /// Publishes the flush callback only after all callback-visible fields have valid initial values.
    Ps3RenderManager2D::Ps3RenderManager2D(Ps3Video& video, Ps3RasterRenderer& raster,
        ::IContentStreamSource& shaderSource)
        : Video(video)
        , Raster(raster)
        , ShaderSource(shaderSource)
        , CallbackLifetime(std::make_shared<unsigned char>(0))
        , Writer()
        , Emitter()
        , Sink()
        , ClipResolver()
        , Programs(nullptr)
        , ActiveCamera(nullptr)
        , ActiveViewport()
        , ActiveScissor()
        , DrawCounter(0)
        , SceneDiagnostics()
        , SkippedDrawCounter(0)
        , LoggedCameraPass(false)
        , LoggedTextureBuild(false)
        , LoggedRoundedRect(false)
        , LoggedSprite(false)
        , LoggedText(false)
        , LoggedTextureUpdate(false)
        , Disposed(false)
        , SubmissionInitializationFailed(false) {
        try {
            const std::weak_ptr<void> lifetime(CallbackLifetime);
            Raster.InitializeBatch2D([this, lifetime]() {
                const std::shared_ptr<void> live = lifetime.lock();
                if (!live) throw std::runtime_error("PS3 2D flush callback invoked after its manager was destroyed");
                FlushPending();
            });
        } catch (...) {
            const std::exception_ptr primary = std::current_exception();
            try {
                // InitializeBatch2D publishes its resource owner before any fallible GPU allocation.
                Raster.Batch2DResources().DetachCpuSubmission();
            } catch (...) {
                LogSecondaryFailure("constructor-callback-detach");
            }
            std::rethrow_exception(primary);
        }
    }

    /// Uses explicit Dispose for normal cleanup; failures are reported without escaping destruction.
    Ps3RenderManager2D::~Ps3RenderManager2D() {
        if (!Disposed) {
            try {
                Dispose();
            } catch (...) {
                LogSecondaryFailure("destructor-dispose");
            }
        }
        // A failed explicit detach may leave Raster holding the closure; expire its guard before this dies.
        CallbackLifetime.reset();
        if (Emitter != nullptr) Emitter.reset();
        if (Writer != nullptr) {
            try {
                // Writer memory is CPU-owned; release it after the callback has become inert.
                Writer->Dispose();
            } catch (...) {
                LogSecondaryFailure("destructor-writer-dispose");
            }
            Writer.reset();
        }
        Sink.reset();
        ClipResolver.reset();
    }

    /// Returns successful ordered camera-pass submissions, not draw callback count.
    std::uint32_t Ps3RenderManager2D::DrawCount() const {
        return DrawCounter;
    }

    /// Unsupported draws throw, so this compatibility count remains zero.
    std::uint32_t Ps3RenderManager2D::SkippedDrawCount() const {
        return SkippedDrawCounter;
    }

    /// Leaves camera replay to the 3D frame coordinator to preserve multi-camera ordering.
    void Ps3RenderManager2D::Draw() {
        if (Disposed) throw std::logic_error("PS3 2D manager Draw called after Dispose");
    }

    /// Creates the shader pair and CPU-only adapters on the first actual camera pass.
    void Ps3RenderManager2D::EnsureSubmissionObjects() {
        if (Disposed) throw std::logic_error("PS3 2D submission requested after Dispose");
        if (Writer != nullptr && Emitter != nullptr && Sink != nullptr && ClipResolver != nullptr) return;
        if (SubmissionInitializationFailed) {
            throw std::logic_error("PS3 2D lazy submission setup previously failed and is not retryable");
        }
        SubmissionInitializationFailed = true;
        std::unique_ptr<Ps3Batch2DSink> pendingSink;
        std::unique_ptr<::Batch2DWriter> pendingWriter;
        std::unique_ptr<::Batch2DDrawableEmitter> pendingEmitter;
        std::unique_ptr<Ps3ClipScissorStack2D> pendingClipResolver;
        try {
            Programs = &Raster.ResolveBatch2DPrograms(ShaderSource);
            pendingSink = std::make_unique<Ps3Batch2DSink>(Raster.Batch2DRenderer(), *Programs);
            pendingWriter = std::make_unique<::Batch2DWriter>(pendingSink.get(), 1024);
            pendingEmitter = std::make_unique<::Batch2DDrawableEmitter>(pendingWriter.get(), this);
            pendingClipResolver = std::make_unique<Ps3ClipScissorStack2D>();
            Sink = std::move(pendingSink);
            Writer = std::move(pendingWriter);
            Emitter = std::move(pendingEmitter);
            ClipResolver = std::move(pendingClipResolver);
            SubmissionInitializationFailed = false;
        } catch (...) {
            const std::exception_ptr primary = std::current_exception();
            if (pendingWriter != nullptr) {
                try { pendingWriter->Dispose(); }
                catch (...) { LogSecondaryFailure("lazy-setup-writer-dispose"); }
            }
            std::rethrow_exception(primary);
        }
    }

    /// Requires a camera queue callback context for direct Core draw dispatch.
    void Ps3RenderManager2D::RequireActiveCamera(const char* operation) const {
        if (Disposed) throw std::logic_error(std::string(operation) + " called after PS3 2D manager disposal");
        if (ActiveCamera == nullptr || Writer == nullptr || Emitter == nullptr || ClipResolver == nullptr) {
            throw std::logic_error(std::string(operation) + " requires an active PS3 2D camera pass");
        }
    }

    /// Applies the canonical Core viewport resolver and submits the ordered camera queue once.
    void Ps3RenderManager2D::RenderCamera(::ICamera* camera) {
        if (camera == nullptr) throw new ArgumentNullException("camera");
        if (Disposed) throw std::logic_error("PS3 2D RenderCamera called after Dispose");
        if (ActiveCamera != nullptr) throw std::logic_error("PS3 2D camera passes cannot be nested");
        if (camera->get_RenderTarget() != nullptr) {
            throw std::runtime_error("PS3 2D manager supports only the video backbuffer, not camera RenderTarget");
        }
        ::IRenderQueue2D* queue = camera->get_RenderQueue2D();
        if (queue == nullptr) throw std::runtime_error("PS3 camera has no 2D render queue");

        ActiveViewport = ResolvePixelViewport(camera->get_Viewport(), Video.Width(), Video.Height());
        const ::float4x4 projection = ::Batch2DCameraProjection::Create(ActiveViewport);
        EnsureSubmissionObjects();
        Ps3Batch2DRenderer& batchRenderer = Raster.Batch2DRenderer();
        ActiveCamera = camera;
        ActiveScissor = ActiveViewport;
        try {
            Writer->ConfigureCamera(projection, ActiveViewport);
            SceneDiagnostics.BeginPass(DrawCounter, ActiveViewport, ActiveScissor, batchRenderer.DrawCount());
            queue->VisitOrdered(this);
            FlushPending();
            SceneDiagnostics.EndPass(true, batchRenderer.DrawCount());
            ++DrawCounter;
            NoteFirstUse("camera", LoggedCameraPass);
        } catch (...) {
            SceneDiagnostics.EndPass(false, batchRenderer.DrawCount());
            ActiveCamera = nullptr;
            ActiveViewport = ::float4();
            ActiveScissor = ::float4();
            throw;
        }
        ActiveCamera = nullptr;
        ActiveViewport = ::float4();
        ActiveScissor = ::float4();
    }

    /// Flushes only when a writer exists; the constructor-installed callback is safe before lazy setup.
    void Ps3RenderManager2D::FlushPending() {
        if (Writer != nullptr) Writer->Flush();
    }

    /// Resolves the generated ancestor clip chain before invoking the drawable's ordinary Core dispatch.
    void Ps3RenderManager2D::Visit(::IDrawable2D* drawable) {
        RequireActiveCamera("Visit");
        if (drawable == nullptr) throw new ArgumentNullException("drawable");
        ::Entity* parent = drawable->get_Parent();
        if (parent == nullptr) throw std::runtime_error("PS3 camera queue contains a drawable with no parent entity");
        if (!parent->get_IsHierarchyEnabled()) {
            ++SkippedDrawCounter;
            return;
        }
        ActiveScissor = ClipResolver->Resolve(drawable, ActiveViewport, Video.Width(), Video.Height());
        drawable->Draw();
    }

    /// Loads a real cooked RGBA texture into the Raster-owned registry.
    ::RuntimeTexture* Ps3RenderManager2D::BuildTextureFromCooked(std::string cookedAssetPath,
        ::IContentStreamSource* contentStreamSource) {
        if (contentStreamSource == nullptr) throw new ArgumentNullException("contentStreamSource");
        if (cookedAssetPath.empty()) throw new ArgumentException("Cooked texture asset path is required.", "cookedAssetPath");
        ::RuntimeTexture* texture = Raster.Batch2DResources().BuildCooked(*contentStreamSource, cookedAssetPath);
        SceneDiagnostics.LogCookedTexture(texture, cookedAssetPath);
        NoteFirstUse("texture-build", LoggedTextureBuild);
        return texture;
    }

    /// Copies the required raw asset into a unique Raster-owned texture.
    ::RuntimeTexture* Ps3RenderManager2D::BuildTextureFromRaw(::TextureAsset* data) {
        if (data == nullptr) throw new ArgumentNullException("data");
        ::RuntimeTexture* texture = Raster.Batch2DResources().BuildRaw(*data);
        SceneDiagnostics.LogRawTexture(texture, data);
        NoteFirstUse("texture-build", LoggedTextureBuild);
        return texture;
    }

    /// Emits a rounded-shape variant using the scissor resolved by Visit.
    void Ps3RenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D* shape) {
        RequireActiveCamera("DrawRoundedRect");
        if (shape == nullptr) throw new ArgumentNullException("shape");
        const ::Batch2DRun run(::Batch2DVariant::RoundedShape, nullptr,
            static_cast<int32_t>(ActiveScissor.X), static_cast<int32_t>(ActiveScissor.Y),
            static_cast<int32_t>(ActiveScissor.Z), static_cast<int32_t>(ActiveScissor.W));
        SceneDiagnostics.LogRoundedRect(shape, ActiveScissor);
        Emitter->EmitRoundedRect(shape, run);
        NoteFirstUse("rounded-rect", LoggedRoundedRect);
    }

    /// Emits a textured sprite without substituting a fallback texture.
    void Ps3RenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
        RequireActiveCamera("DrawSprite");
        if (sprite == nullptr) throw new ArgumentNullException("sprite");
        const ::Batch2DRun run(::Batch2DVariant::Textured, sprite->get_Texture(),
            static_cast<int32_t>(ActiveScissor.X), static_cast<int32_t>(ActiveScissor.Y),
            static_cast<int32_t>(ActiveScissor.Z), static_cast<int32_t>(ActiveScissor.W));
        SceneDiagnostics.LogSprite(sprite, ActiveScissor);
        Emitter->EmitSprite(sprite, run);
        NoteFirstUse("sprite", LoggedSprite);
    }

    /// Emits text through the shared text and glyph-atlas implementation.
    void Ps3RenderManager2D::DrawText(::ITextDrawable2D* text) {
        RequireActiveCamera("DrawText");
        if (text == nullptr) throw new ArgumentNullException("text");
        const ::Batch2DRun run(::Batch2DVariant::Textured, nullptr,
            static_cast<int32_t>(ActiveScissor.X), static_cast<int32_t>(ActiveScissor.Y),
            static_cast<int32_t>(ActiveScissor.Z), static_cast<int32_t>(ActiveScissor.W));
        SceneDiagnostics.LogText(text, ActiveScissor);
        Emitter->EmitText(text, run);
        NoteFirstUse("text", LoggedText);
    }

    /// Validates Core texture-region arguments before forwarding a borrowed contiguous byte span.
    void Ps3RenderManager2D::UpdateTextureRegionCore(::RuntimeTexture* texture, int32_t x,
        int32_t y, int32_t width, int32_t height, Array<uint8_t>* rgba8, int32_t sourceRowPitch) {
        if (texture == nullptr) throw new ArgumentNullException("texture");
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || sourceRowPitch <= 0) {
            throw new ArgumentOutOfRangeException("region", "Texture region coordinates, extents, and row pitch must be positive.");
        }
        if (rgba8 == nullptr) throw new ArgumentNullException("rgba8");
        auto* nativeTexture = dynamic_cast<Ps3RuntimeTexture*>(texture);
        if (nativeTexture == nullptr) throw std::invalid_argument("PS3 2D texture update received a foreign RuntimeTexture");
        if (rgba8->Length < 0 || (rgba8->Length > 0 && rgba8->Data == nullptr)) {
            throw std::invalid_argument("PS3 2D texture update received an invalid managed byte array");
        }
        const uint64_t rowBytes = static_cast<uint64_t>(width) * 4u;
        if (static_cast<uint64_t>(sourceRowPitch) < rowBytes) {
            throw std::invalid_argument("PS3 2D texture update row pitch is smaller than one RGBA row");
        }
        const uint64_t requiredBytes = static_cast<uint64_t>(height - 1) *
            static_cast<uint64_t>(sourceRowPitch) + rowBytes;
        if (requiredBytes > static_cast<uint64_t>(rgba8->Length)) {
            throw std::invalid_argument("PS3 2D texture update array does not contain all requested rows");
        }
        Raster.Batch2DResources().Update(*nativeTexture, static_cast<uint32_t>(x),
            static_cast<uint32_t>(y), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
            rgba8->Data, static_cast<std::size_t>(rgba8->Length),
            static_cast<std::size_t>(sourceRowPitch));
        NoteFirstUse("texture-update", LoggedTextureUpdate);
    }

    /// Performs the ordered CPU lifetime handoff while leaving every GPU owner with Raster.
    void Ps3RenderManager2D::Dispose() {
        if (Disposed) return;
        std::exception_ptr primary;
        try {
            FlushPending();
        } catch (...) {
            primary = std::current_exception();
        }

        try {
            ::RenderManager2D::Dispose();
        } catch (...) {
            if (primary == nullptr) primary = std::current_exception();
            else LogSecondaryFailure("base-dispose");
        }

        bool detached = false;
        try {
            Raster.Batch2DResources().DetachCpuSubmission();
            detached = true;
        } catch (...) {
            if (primary == nullptr) primary = std::current_exception();
            else LogSecondaryFailure("callback-detach");
        }

        if (detached) {
            if (Writer != nullptr) {
                try {
                    Writer->Dispose();
                } catch (...) {
                    if (primary == nullptr) primary = std::current_exception();
                    else LogSecondaryFailure("writer-dispose");
                }
            }
            Emitter.reset();
            Writer.reset();
            Sink.reset();
            ClipResolver.reset();
            ActiveCamera = nullptr;
            Disposed = true;
        }
        if (primary != nullptr) std::rethrow_exception(primary);
    }
}

#endif
