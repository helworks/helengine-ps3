#pragma once

#include <cstdint>
#include <stdexcept>

#include "RenderManager2D.hpp"
#include "platform/ps3/Ps3Batch2DResources.hpp"

/// <summary>Concrete fixture manager that delegates raw texture creation and retains real base release behavior.</summary>
class Ps3Batch2DOwnerReleaseManager final : public ::RenderManager2D {
private:
    /// <summary>Renderer-owned resource set used by the inherited Core release methods.</summary>
    helengine::ps3::Ps3Batch2DResources& Resources;

public:
    /// <summary>Creates a narrow concrete manager for exercising generated ReleaseTexture and ReleaseFont.</summary>
    /// <param name="resources">Resource owner that creates and retires its wrappers.</param>
    explicit Ps3Batch2DOwnerReleaseManager(helengine::ps3::Ps3Batch2DResources& resources)
        : Resources(resources) {}

    /// <summary>Creates the renderer-owned wrapper through the actual owner implementation.</summary>
    /// <param name="data">Borrowed Core texture asset.</param>
    /// <returns>Distinct Core-owned runtime texture wrapper.</returns>
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override {
        if (data == nullptr) throw std::invalid_argument("release fixture requires a texture asset");
        return Resources.BuildRaw(*data);
    }

    /// <summary>Texture fixture has no visitor submission.</summary>
    void Draw() override {}

    /// <summary>Texture fixture has no rounded-rectangle adapter.</summary>
    void DrawRoundedRect(::IRoundedRectDrawable2D*) override {}

    /// <summary>Texture fixture has no sprite adapter.</summary>
    void DrawSprite(::ISpriteDrawable2D*) override {}

    /// <summary>Texture fixture has no text adapter.</summary>
    void DrawText(::ITextDrawable2D*) override {}

protected:
    /// <summary>Rejects texture-region updates because this fixture only tests Core's ownership dispatch.</summary>
    void UpdateTextureRegionCore(::RuntimeTexture*, std::int32_t, std::int32_t, std::int32_t,
        std::int32_t, ::Array<std::uint8_t>*, std::int32_t) override {
        throw std::logic_error("release fixture does not submit texture updates");
    }
};
