#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <cstddef>
#include <cstdint>
#include <string>

#include "float4.hpp"

class Entity;
class FontAsset;
class IRoundedRectDrawable2D;
class ISpriteDrawable2D;
class ITextDrawable2D;
class RuntimeTexture;
class TextureAsset;

namespace helengine::ps3 {

/// Emits bounded, machine-readable Core drawable inputs for the first 2D camera pass.
/// The helper retains only counters and never owns or caches borrowed scene objects.
class Ps3Batch2DSceneDiagnostics final {
private:
    /// Maximum number of complete drawable records emitted for the first camera pass.
    static constexpr std::uint64_t MaximumDrawableRecords = 256;

    /// Maximum UTF-8 bytes copied into any text record.
    static constexpr std::size_t MaximumTextBytes = 4096;

    /// Identifies a supported drawable category for pass summary accounting.
    enum class Category : std::uint8_t {
        Sprite,
        Text,
        RoundedRect
    };

    /// Prevents a failed first pass from making later cameras look like the first pass.
    bool PassClaimed;

    /// Indicates that drawable records may currently be emitted.
    bool PassActive;

    /// Indicates that the one overflow marker has already been emitted.
    bool TruncationReported;

    /// Camera-pass index captured at the start of the first pass.
    std::uint32_t PassIndex;

    /// Pixel viewport captured at the start of the first pass for its closing record.
    ::float4 PassViewport;

    /// Initial pixel scissor captured at the start of the first pass for its closing record.
    ::float4 PassScissor;

    /// Renderer draw count captured before visiting the first pass queue.
    std::uint64_t InitialBatchDrawCount;

    /// Total number of drawable callbacks visited during the first pass.
    std::uint64_t Sequence;

    /// Number of sprite callbacks visited during the first pass.
    std::uint64_t SpriteCount;

    /// Number of text callbacks visited during the first pass.
    std::uint64_t TextCount;

    /// Number of rounded-rectangle callbacks visited during the first pass.
    std::uint64_t RoundedRectCount;

    /// Starts one drawable record, increments visited counts, and writes its common fields.
    bool BeginDrawable(Category category, const char* categoryName, ::Entity* parent,
                       const ::float4& scissor, std::uint64_t& sequence);

    /// Writes parent transform and current scissor fields without retaining the parent.
    void WriteCommonFields(const char* categoryName, std::uint64_t sequence,
                           ::Entity* parent, const ::float4& scissor) const;

    /// Writes a byte range as lowercase hexadecimal without escaping or truncating it.
    static void WriteHex(const char* bytes, std::size_t length);

    /// Logs actual glyph entries present for distinct bytes in the bounded text prefix.
    void LogGlyphMetrics(std::uint64_t sequence, const std::string& text, ::FontAsset* font) const;

public:
    /// Initializes an inactive diagnostic recorder with empty first-pass counts.
    Ps3Batch2DSceneDiagnostics();

    /// Begins logging only when the manager's successful camera-pass counter is still zero.
    /// @param pass Camera pass index supplied by the owning render manager.
    /// @param viewport Resolved pixel viewport for this camera.
    /// @param scissor Initial camera scissor before drawable clip intersections.
    /// @param batchDrawCount Renderer submission count immediately before queue traversal.
    void BeginPass(std::uint32_t pass, const ::float4& viewport, const ::float4& scissor,
                   std::uint64_t batchDrawCount);

    /// Emits first-pass totals and the completed batch-renderer draw-count delta.
    /// @param completed Whether ordered traversal and its pending flush completed.
    /// @param batchDrawCount Renderer submission count after traversal or failure.
    void EndPass(bool completed, std::uint64_t batchDrawCount);

    /// Records a sprite's actual source state before the shared sprite emitter runs.
    /// @param sprite Borrowed sprite supplied by Core's ordered camera queue.
    /// @param scissor Current resolved drawable scissor.
    void LogSprite(::ISpriteDrawable2D* sprite, const ::float4& scissor);

    /// Records text, atlas, style, and bounded actual glyph-table metrics before emission.
    /// @param text Borrowed text drawable supplied by Core's ordered camera queue.
    /// @param scissor Current resolved drawable scissor.
    void LogText(::ITextDrawable2D* text, const ::float4& scissor);

    /// Records rounded-rectangle geometry and colors before the shared emitter runs.
    /// @param shape Borrowed rounded-rectangle drawable supplied by Core's ordered queue.
    /// @param scissor Current resolved drawable scissor.
    void LogRoundedRect(::IRoundedRectDrawable2D* shape, const ::float4& scissor);

    /// Associates a newly-created wrapper identity with its exact cooked path bytes.
    /// @param wrapper Successfully-created borrowed Core texture wrapper.
    /// @param cookedPath Canonical cooked content path supplied to the texture builder.
    void LogCookedTexture(::RuntimeTexture* wrapper, const std::string& cookedPath) const;

    /// Associates a newly-created wrapper with its raw source asset identity and dimensions.
    /// @param wrapper Successfully-created borrowed Core texture wrapper.
    /// @param sourceAsset Raw asset used to create the wrapper.
    void LogRawTexture(::RuntimeTexture* wrapper, ::TextureAsset* sourceAsset) const;
};

} // namespace helengine::ps3

#endif
