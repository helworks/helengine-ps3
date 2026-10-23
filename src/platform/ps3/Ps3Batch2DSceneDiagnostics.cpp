#include "platform/ps3/Ps3Batch2DSceneDiagnostics.hpp"

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include <algorithm>
#include <cstdio>

#include "Entity.hpp"
#include "FontAsset.hpp"
#include "FontChar.hpp"
#include "IRoundedRectDrawable2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ITextDrawable2D.hpp"
#include "RuntimeTexture.hpp"
#include "TextureAsset.hpp"
#include "byte4.hpp"
#include "float2.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"

namespace helengine::ps3 {

/// Initializes the one-pass recorder without creating any scene-owned state.
Ps3Batch2DSceneDiagnostics::Ps3Batch2DSceneDiagnostics()
    : PassClaimed(false)
    , PassActive(false)
    , TruncationReported(false)
    , PassIndex(0)
    , PassViewport()
    , PassScissor()
    , InitialBatchDrawCount(0)
    , Sequence(0)
    , SpriteCount(0)
    , TextCount(0)
    , RoundedRectCount(0) {
}

/// Starts exactly one diagnostic pass and writes its initial camera state.
void Ps3Batch2DSceneDiagnostics::BeginPass(std::uint32_t pass, const ::float4& viewport,
    const ::float4& scissor, std::uint64_t batchDrawCount) {
    if (pass != 0 || PassClaimed) return;

    PassClaimed = true;
    PassActive = true;
    PassIndex = pass;
    PassViewport = viewport;
    PassScissor = scissor;
    InitialBatchDrawCount = batchDrawCount;
    std::printf(
        "PS3_SCENE_INPUT_PASS_BEGIN pass=%u viewport=%.9g,%.9g,%.9g,%.9g "
        "scissor=%.9g,%.9g,%.9g,%.9g visited_sprite=0 visited_text=0 "
        "visited_rounded_rect=0 batch_draw_count=%llu\n",
        PassIndex, static_cast<double>(viewport.X), static_cast<double>(viewport.Y), static_cast<double>(viewport.Z), static_cast<double>(viewport.W),
        static_cast<double>(scissor.X), static_cast<double>(scissor.Y), static_cast<double>(scissor.Z), static_cast<double>(scissor.W),
        static_cast<unsigned long long>(InitialBatchDrawCount));
}

/// Writes first-pass totals and the renderer's actual draw-count delta.
void Ps3Batch2DSceneDiagnostics::EndPass(bool completed, std::uint64_t batchDrawCount) {
    if (!PassActive) return;

    const bool drawCountMonotonic = batchDrawCount >= InitialBatchDrawCount;
    const std::uint64_t drawDelta = drawCountMonotonic
        ? batchDrawCount - InitialBatchDrawCount
        : 0;
    std::printf(
        "PS3_SCENE_INPUT_PASS_END pass=%u complete=%u "
        "viewport=%.9g,%.9g,%.9g,%.9g scissor=%.9g,%.9g,%.9g,%.9g "
        "visited_total=%llu "
        "visited_sprite=%llu visited_text=%llu visited_rounded_rect=%llu "
        "drawable_records=%llu truncated=%u batch_draw_count=%llu "
        "batch_draw_delta=%llu batch_draw_delta_valid=%u\n",
        PassIndex, completed ? 1u : 0u,
        static_cast<double>(PassViewport.X), static_cast<double>(PassViewport.Y), static_cast<double>(PassViewport.Z), static_cast<double>(PassViewport.W),
        static_cast<double>(PassScissor.X), static_cast<double>(PassScissor.Y), static_cast<double>(PassScissor.Z), static_cast<double>(PassScissor.W),
        static_cast<unsigned long long>(Sequence),
        static_cast<unsigned long long>(SpriteCount),
        static_cast<unsigned long long>(TextCount),
        static_cast<unsigned long long>(RoundedRectCount),
        static_cast<unsigned long long>(std::min(Sequence, MaximumDrawableRecords)),
        TruncationReported ? 1u : 0u,
        static_cast<unsigned long long>(batchDrawCount),
        static_cast<unsigned long long>(drawDelta), drawCountMonotonic ? 1u : 0u);
    PassActive = false;
}

/// Increments pass/category visitation counts, bounds detailed records, and writes common fields.
bool Ps3Batch2DSceneDiagnostics::BeginDrawable(Category category, const char* categoryName,
    ::Entity* parent, const ::float4& scissor, std::uint64_t& sequence) {
    if (!PassActive) return false;

    sequence = ++Sequence;
    switch (category) {
        case Category::Sprite:
            ++SpriteCount;
            break;
        case Category::Text:
            ++TextCount;
            break;
        case Category::RoundedRect:
            ++RoundedRectCount;
            break;
    }

    if (sequence > MaximumDrawableRecords) {
        if (!TruncationReported) {
            TruncationReported = true;
            std::printf("PS3_SCENE_INPUT_TRUNCATED pass=%u first_omitted_sequence=%llu limit=%llu\n",
                PassIndex, static_cast<unsigned long long>(sequence),
                static_cast<unsigned long long>(MaximumDrawableRecords));
        }
        return false;
    }

    WriteCommonFields(categoryName, sequence, parent, scissor);
    return true;
}

/// Writes the common parent transform and scissor fields for one drawable record.
void Ps3Batch2DSceneDiagnostics::WriteCommonFields(const char* categoryName,
    std::uint64_t sequence, ::Entity* parent, const ::float4& scissor) const {
    std::printf("PS3_SCENE_INPUT_DRAW pass=%u sequence=%llu category=%s parent=%p",
        PassIndex, static_cast<unsigned long long>(sequence), categoryName,
        const_cast<void*>(static_cast<const void*>(parent)));
    if (parent == nullptr) {
        std::printf(" parent_state=null");
    } else {
        const ::float3 position = parent->get_Position();
        const ::float3 scale = parent->get_Scale();
        const ::float4 orientation = parent->get_Orientation();
        std::printf(
            " parent_position=%.9g,%.9g,%.9g parent_scale=%.9g,%.9g,%.9g "
            "parent_orientation=%.9g,%.9g,%.9g,%.9g",
            static_cast<double>(position.X), static_cast<double>(position.Y), static_cast<double>(position.Z), static_cast<double>(scale.X), static_cast<double>(scale.Y), static_cast<double>(scale.Z),
            static_cast<double>(orientation.X), static_cast<double>(orientation.Y), static_cast<double>(orientation.Z), static_cast<double>(orientation.W));
    }
    std::printf(" scissor=%.9g,%.9g,%.9g,%.9g", static_cast<double>(scissor.X), static_cast<double>(scissor.Y), static_cast<double>(scissor.Z), static_cast<double>(scissor.W));
}

/// Writes a byte range as lowercase hexadecimal, preserving embedded zeroes and newlines.
void Ps3Batch2DSceneDiagnostics::WriteHex(const char* bytes, std::size_t length) {
    for (std::size_t index = 0; index < length; ++index) {
        std::printf("%02x", static_cast<unsigned char>(bytes[index]));
    }
}

/// Writes actual FontChar table results for each distinct byte in the bounded text prefix.
void Ps3Batch2DSceneDiagnostics::LogGlyphMetrics(std::uint64_t sequence,
    const std::string& text, ::FontAsset* font) const {
    bool seen[256] = {};
    const std::size_t scanLength = std::min(text.size(), MaximumTextBytes);
    std::uint32_t glyphCount = 0;
    ::Dictionary<char16_t, ::FontChar>* characters = font == nullptr ? nullptr : font->get_Characters();

    for (std::size_t index = 0; index < scanLength; ++index) {
        const unsigned char byte = static_cast<unsigned char>(text[index]);
        if (seen[byte]) continue;
        seen[byte] = true;
        ++glyphCount;

        const char16_t key = static_cast<char16_t>(byte);
        ::FontChar glyph;
        const bool found = characters != nullptr && characters->TryGetValue(key, glyph);
        if (!found) {
            std::printf(
                "PS3_SCENE_INPUT_GLYPH pass=%u sequence=%llu byte=%02x lookup_key_byte=%02x "
                "status=%s font=%p characters=%p\n",
                PassIndex, static_cast<unsigned long long>(sequence),
                static_cast<unsigned>(byte), static_cast<unsigned>(byte),
                characters == nullptr ? "lookup_unavailable" : "missing",
                const_cast<void*>(static_cast<const void*>(font)),
                const_cast<void*>(static_cast<const void*>(characters)));
            continue;
        }

        std::printf(
            "PS3_SCENE_INPUT_GLYPH pass=%u sequence=%llu byte=%02x lookup_key_byte=%02x "
            "status=found font=%p source_rect=%.9g,%.9g,%.9g,%.9g "
            "offset_y=%.9g advance_width=%.9g bearing_x=%.9g bearing_y=%.9g\n",
            PassIndex, static_cast<unsigned long long>(sequence),
            static_cast<unsigned>(byte), static_cast<unsigned>(byte),
            const_cast<void*>(static_cast<const void*>(font)),
            static_cast<double>(glyph.SourceRect.X), static_cast<double>(glyph.SourceRect.Y), static_cast<double>(glyph.SourceRect.Z), static_cast<double>(glyph.SourceRect.W),
            static_cast<double>(glyph.OffsetY), static_cast<double>(glyph.AdvanceWidth), static_cast<double>(glyph.BearingX), static_cast<double>(glyph.BearingY));
    }

    std::printf(
        "PS3_SCENE_INPUT_GLYPH_SUMMARY pass=%u sequence=%llu distinct_bytes=%u "
        "scan_bytes=%llu scan_truncated=%u\n",
        PassIndex, static_cast<unsigned long long>(sequence), glyphCount,
        static_cast<unsigned long long>(scanLength), text.size() > scanLength ? 1u : 0u);
}

/// Records sprite geometry, tint, wrapper identity, and actual runtime texture dimensions.
void Ps3Batch2DSceneDiagnostics::LogSprite(::ISpriteDrawable2D* sprite,
    const ::float4& scissor) {
    if (sprite == nullptr) return;
    ::Entity* parent = sprite->get_Parent();
    std::uint64_t sequence = 0;
    if (!BeginDrawable(Category::Sprite, "sprite", parent, scissor, sequence)) return;

    const ::int2 size = sprite->get_Size();
    const ::float4 sourceRect = sprite->get_SourceRect();
    const ::byte4 color = sprite->get_Color();
    ::RuntimeTexture* texture = sprite->get_Texture();
    std::printf(" texture=%p", const_cast<void*>(static_cast<const void*>(texture)));
    if (texture != nullptr) {
        std::printf(" texture_width=%d texture_height=%d", texture->get_Width(), texture->get_Height());
    } else {
        std::printf(" texture_width=unavailable texture_height=unavailable");
    }
    std::printf(
        " size=%d,%d source_rect=%.9g,%.9g,%.9g,%.9g color_rgba=%u,%u,%u,%u\n",
        size.X, size.Y, static_cast<double>(sourceRect.X), static_cast<double>(sourceRect.Y), static_cast<double>(sourceRect.Z), static_cast<double>(sourceRect.W),
        static_cast<unsigned>(color.X), static_cast<unsigned>(color.Y),
        static_cast<unsigned>(color.Z), static_cast<unsigned>(color.W));
}

/// Records text state, atlas identity/dimensions, byte-safe content, and actual glyph metrics.
void Ps3Batch2DSceneDiagnostics::LogText(::ITextDrawable2D* textDrawable,
    const ::float4& scissor) {
    if (textDrawable == nullptr) return;
    ::Entity* parent = textDrawable->get_Parent();
    std::uint64_t sequence = 0;
    if (!BeginDrawable(Category::Text, "text", parent, scissor, sequence)) return;

    const auto managedText = textDrawable->get_Text();
    const std::string text(managedText.data(), managedText.size());
    const std::size_t loggedBytes = std::min(text.size(), MaximumTextBytes);
    const ::int2 size = textDrawable->get_Size();
    const ::float4 sourceRect = textDrawable->get_SourceRect();
    const ::byte4 color = textDrawable->get_Color();
    const ::byte4 outlineColor = textDrawable->get_OutlineColor();
    const ::float2 shadowOffset = textDrawable->get_ShadowOffset();
    const ::byte4 shadowColor = textDrawable->get_ShadowColor();
    ::FontAsset* font = textDrawable->get_Font();
    ::RuntimeTexture* atlasTexture = font == nullptr ? nullptr : font->get_Texture();

    std::printf(
        " text_bytes=%llu text_recorded_bytes=%llu text_truncated=%u text_hex=",
        static_cast<unsigned long long>(text.size()),
        static_cast<unsigned long long>(loggedBytes), text.size() > loggedBytes ? 1u : 0u);
    WriteHex(text.data(), loggedBytes);
    std::printf(
        " size=%d,%d source_rect=%.9g,%.9g,%.9g,%.9g font_scale=%.9g alignment=%u "
        "wrap_text=%u color_rgba=%u,%u,%u,%u outline_scale=%.9g outline_color_rgba=%u,%u,%u,%u "
        "shadow_offset=%.9g,%.9g shadow_color_rgba=%u,%u,%u,%u font=%p "
        "font_source_texture_asset=%p",
        size.X, size.Y, static_cast<double>(sourceRect.X), static_cast<double>(sourceRect.Y), static_cast<double>(sourceRect.Z), static_cast<double>(sourceRect.W),
        static_cast<double>(textDrawable->get_FontScale()), static_cast<unsigned>(textDrawable->get_Alignment()),
        textDrawable->get_WrapText() ? 1u : 0u,
        static_cast<unsigned>(color.X), static_cast<unsigned>(color.Y),
        static_cast<unsigned>(color.Z), static_cast<unsigned>(color.W),
        static_cast<double>(textDrawable->get_OutlineScale()),
        static_cast<unsigned>(outlineColor.X), static_cast<unsigned>(outlineColor.Y),
        static_cast<unsigned>(outlineColor.Z), static_cast<unsigned>(outlineColor.W),
        static_cast<double>(shadowOffset.X), static_cast<double>(shadowOffset.Y),
        static_cast<unsigned>(shadowColor.X), static_cast<unsigned>(shadowColor.Y),
        static_cast<unsigned>(shadowColor.Z), static_cast<unsigned>(shadowColor.W),
        const_cast<void*>(static_cast<const void*>(font)),
        const_cast<void*>(static_cast<const void*>(font == nullptr ? nullptr : font->get_SourceTextureAsset())));
    if (font != nullptr) {
        std::printf(" font_atlas_width=%d font_atlas_height=%d line_height=%.9g",
            font->get_AtlasWidth(), font->get_AtlasHeight(), static_cast<double>(font->get_LineHeight()));
    } else {
        std::printf(" font_atlas_width=unavailable font_atlas_height=unavailable line_height=unavailable");
    }
    std::printf(" atlas_texture=%p", const_cast<void*>(static_cast<const void*>(atlasTexture)));
    if (atlasTexture != nullptr) {
        std::printf(" atlas_texture_width=%d atlas_texture_height=%d\n",
            atlasTexture->get_Width(), atlasTexture->get_Height());
    } else {
        std::printf(" atlas_texture_width=unavailable atlas_texture_height=unavailable\n");
    }
    LogGlyphMetrics(sequence, text, font);
}

/// Records all rounded-rectangle fields used by the shared shape emitter.
void Ps3Batch2DSceneDiagnostics::LogRoundedRect(::IRoundedRectDrawable2D* shape,
    const ::float4& scissor) {
    if (shape == nullptr) return;
    ::Entity* parent = shape->get_Parent();
    std::uint64_t sequence = 0;
    if (!BeginDrawable(Category::RoundedRect, "rounded_rect", parent, scissor, sequence)) return;

    const ::int2 size = shape->get_Size();
    const ::float4 sourceRect = shape->get_SourceRect();
    const ::byte4 color = shape->get_Color();
    const ::byte4 fillColor = shape->get_FillColor();
    const ::byte4 borderColor = shape->get_BorderColor();
    std::printf(
        " size=%d,%d radius=%.9g border_thickness=%.9g corners=%u "
        "fill_rgba=%u,%u,%u,%u border_rgba=%u,%u,%u,%u color_rgba=%u,%u,%u,%u "
        "source_rect=%.9g,%.9g,%.9g,%.9g rotation=%.9g\n",
        size.X, size.Y, static_cast<double>(shape->get_Radius()), static_cast<double>(shape->get_BorderThickness()),
        static_cast<unsigned>(shape->get_Corners()),
        static_cast<unsigned>(fillColor.X), static_cast<unsigned>(fillColor.Y),
        static_cast<unsigned>(fillColor.Z), static_cast<unsigned>(fillColor.W),
        static_cast<unsigned>(borderColor.X), static_cast<unsigned>(borderColor.Y),
        static_cast<unsigned>(borderColor.Z), static_cast<unsigned>(borderColor.W),
        static_cast<unsigned>(color.X), static_cast<unsigned>(color.Y),
        static_cast<unsigned>(color.Z), static_cast<unsigned>(color.W),
        static_cast<double>(sourceRect.X), static_cast<double>(sourceRect.Y), static_cast<double>(sourceRect.Z), static_cast<double>(sourceRect.W), static_cast<double>(shape->get_Rotation()));
}

/// Records a cooked wrapper/path association only after texture construction succeeded.
void Ps3Batch2DSceneDiagnostics::LogCookedTexture(::RuntimeTexture* wrapper,
    const std::string& cookedPath) const {
    std::printf("PS3_SCENE_INPUT_TEXTURE kind=cooked wrapper=%p path_bytes=%llu path_hex=",
        const_cast<void*>(static_cast<const void*>(wrapper)),
        static_cast<unsigned long long>(cookedPath.size()));
    WriteHex(cookedPath.data(), cookedPath.size());
    std::printf("\n");
}

/// Records a raw wrapper/source-asset association and exact source texture dimensions.
void Ps3Batch2DSceneDiagnostics::LogRawTexture(::RuntimeTexture* wrapper,
    ::TextureAsset* sourceAsset) const {
    if (sourceAsset == nullptr) return;
    std::printf(
        "PS3_SCENE_INPUT_TEXTURE kind=raw wrapper=%p source_texture_asset=%p "
        "width=%u height=%u color_format=%u alpha_precision=%u\n",
        const_cast<void*>(static_cast<const void*>(wrapper)),
        const_cast<void*>(static_cast<const void*>(sourceAsset)),
        static_cast<unsigned>(sourceAsset->Width), static_cast<unsigned>(sourceAsset->Height),
        static_cast<unsigned>(sourceAsset->ColorFormat), static_cast<unsigned>(sourceAsset->AlphaPrecision));
}

} // namespace helengine::ps3

#endif
