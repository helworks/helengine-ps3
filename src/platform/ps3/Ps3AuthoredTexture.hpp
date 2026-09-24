#pragma once

#include <rsx/gcm_sys.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace helengine::ps3 {

class Ps3Video;

/// <summary>Owns immutable RSX-local texels and binds an explicitly configured authored-GPU fixture texture.</summary>
class Ps3AuthoredTexture {
private:
    /// <summary>Base alignment used by linear and swizzled RSX texture storage.</summary>
    static constexpr std::size_t TextureAlignment = 128u;

    /// <summary>Borrowed video owner; it must outlive every live-owner call and explicit post-drain shutdown.</summary>
    Ps3Video* VideoValue;

    /// <summary>Explicit SDK descriptor for the owned normalized RGBA texture.</summary>
    gcmTexture TextureValue;

    /// <summary>RSX-local allocation containing converted A8R8G8B8 texels.</summary>
    void* TextureBuffer;

    /// <summary>Exact allocation size retained until the associated video has drained.</summary>
    std::size_t OwnedGpuBytesValue;

    /// <summary>True after a successful 2D or solid-cube initialization.</summary>
    bool Initialized;

    /// <summary>True after safe post-drain resource release.</summary>
    bool ShutdownComplete;

public:
    /// <summary>Creates an empty owner and does not invent a default texture.</summary>
    Ps3AuthoredTexture();

    /// <summary>Never accesses the borrowed video; logs and retains RSX storage if explicit shutdown was omitted.</summary>
    ~Ps3AuthoredTexture();

    /// <summary>Texture ownership cannot be copied because it contains a unique RSX allocation.</summary>
    Ps3AuthoredTexture(const Ps3AuthoredTexture&) = delete;

    /// <summary>Texture ownership cannot be copy-assigned because it contains a unique RSX allocation.</summary>
    Ps3AuthoredTexture& operator=(const Ps3AuthoredTexture&) = delete;

    /// <summary>Validates, converts, and owns a bounded normalized RGBA8 linear 2D texture.</summary>
    /// <param name="video">Initialized video owner; it must outlive this live owner and its explicit shutdown.</param>
    /// <param name="width">Texture width from one through 4096 texels.</param>
    /// <param name="height">Texture height from one through 4096 texels.</param>
    /// <param name="rgba">Non-null row-major source texels in explicit R,G,B,A byte order.</param>
    /// <param name="byteCount">Must exactly equal checked width times height times four.</param>
    /// <param name="error">Receives validation or allocation detail on failure.</param>
    /// <returns>True after every source texel has been copied into owned RSX storage.</returns>
    bool InitializeRgba2D(Ps3Video& video, std::uint32_t width, std::uint32_t height,
        const std::uint8_t* rgba, std::size_t byteCount, std::string& error);

    /// <summary>Creates six 128-byte-spaced swizzled 1x1 faces containing the explicitly supplied RGBA color.</summary>
    /// <param name="video">Initialized video owner; it must outlive this live owner and its explicit shutdown.</param>
    /// <param name="rgba">Non-null face color in explicit R,G,B,A byte order.</param>
    /// <param name="error">Receives validation or allocation detail on failure.</param>
    /// <returns>True after all six face texels have been copied into owned RSX storage.</returns>
    bool InitializeSolidCube(Ps3Video& video, const std::uint8_t rgba[4], std::string& error);

    /// <summary>Configures the descriptor and sampling state, then binds it at an actual RSX unit zero through fifteen.</summary>
    /// <param name="textureUnit">Physical texture unit mapped by the validated SDK pixel program.</param>
    /// <param name="error">Receives the lifecycle, unit, or context error without submitting rejected commands.</param>
    /// <returns>True when cache invalidation and texture state commands are queued.</returns>
    bool Bind(std::uint32_t textureUnit, std::string& error);

    /// <summary>Releases storage only after Ps3Video reports a successful final drain.</summary>
    /// <param name="error">Receives the lifecycle reason if the resource is still referenced or never initialized.</param>
    /// <returns>True when the owner is retired and reports zero owned RSX bytes.</returns>
    bool ShutdownAfterDrain(std::string& error);

    /// <summary>Returns the exact RSX-local texture allocation size while it remains owned.</summary>
    /// <returns>Owned texel bytes, or zero after successful drain shutdown.</returns>
    std::size_t OwnedGpuBytes() const;

private:
    /// <summary>Releases RSX-local memory and clears its descriptor after lifetime checks have succeeded.</summary>
    void ReleaseOwnedResources();
};

} // namespace helengine::ps3
