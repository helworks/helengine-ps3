#include "AssetSerializer.hpp"
#include "FontAsset.hpp"
#include "TextureAsset.hpp"
#include "TextureAssetColorFormat.hpp"
#include "platform/ps3/Ps3AuthoredTexture.hpp"
#include "platform/ps3/Ps3Batch2DResources.hpp"
#include "platform/ps3/Ps3RuntimeTexture.hpp"
#include "platform/ps3/Ps3Video.hpp"
#include "native/Ps3Batch2DOwnerContentSource.hpp"
#include "native/Ps3Batch2DOwnerReleaseManager.hpp"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

/// <summary>Number of independently named owner assertions emitted to the emulator TTY.</summary>
std::size_t PassedCases = 0u;

/// <summary>Number of failed owner assertions emitted to the emulator TTY.</summary>
std::size_t FailedCases = 0u;

/// <summary>Records one named assertion in the fixture's fresh terminal output.</summary>
/// <param name="passed">Whether the observed owner behavior met the expectation.</param>
/// <param name="name">Stable assertion name consumed by the runner.</param>
void Expect(bool passed, const char* name) {
    if (passed) {
        ++PassedCases;
        std::printf("PASS %s\n", name);
    } else {
        ++FailedCases;
        std::printf("FAIL %s\n", name);
    }
}

/// <summary>Requires one operation to throw instead of silently accepting an invalid resource request.</summary>
/// <param name="operation">Production operation expected to reject the request.</param>
/// <returns>True when the operation failed visibly.</returns>
bool Throws(const std::function<void()>& operation) {
    try {
        operation();
    } catch (...) {
        return true;
    }
    return false;
}

/// <summary>Returns an operation's visible standard diagnostic without allocating GPU state.</summary>
/// <param name="operation">Operation expected to reject fixture input.</param>
/// <returns>The standard exception text, or an empty string when no standard failure occurred.</returns>
std::string FailureMessage(const std::function<void()>& operation) {
    try {
        operation();
    } catch (const std::exception& exception) {
        return exception.what();
    } catch (...) {
        return "non-standard exception";
    }
    return {};
}

/// <summary>Appends a little-endian unsigned 16-bit field to a generated packaged-asset fixture.</summary>
/// <param name="bytes">Serialized bytes receiving the field.</param>
/// <param name="value">Unsigned value to encode.</param>
void AppendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value));
    bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
}

/// <summary>Appends a little-endian unsigned 32-bit field to a generated packaged-asset fixture.</summary>
/// <param name="bytes">Serialized bytes receiving the field.</param>
/// <param name="value">Unsigned value to encode.</param>
void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned shift = 0u; shift < 32u; shift += 8u) bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

/// <summary>Appends one generated EngineBinary UTF-8 string field to a fixture.</summary>
/// <param name="bytes">Serialized bytes receiving the string.</param>
/// <param name="value">Exact string content to encode.</param>
void AppendString(std::vector<std::uint8_t>& bytes, const std::string& value) {
    AppendU32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

/// <summary>Creates real packaged EngineBinary bytes that the generated AssetSerializer deserializes.</summary>
/// <param name="width">Texture width recorded by the serialized asset.</param>
/// <param name="height">Texture height recorded by the serialized asset.</param>
/// <param name="rgba">Serialized row-major RGBA32 texels.</param>
/// <param name="colorFormat">Serialized texture format enum value.</param>
/// <param name="valueKind">Serialized asset value kind, allowing a wrong-type fixture.</param>
/// <returns>Complete packaged asset bytes for the fixture source.</returns>
std::vector<std::uint8_t> MakePackagedAsset(std::uint16_t width, std::uint16_t height,
    const std::vector<std::uint8_t>& rgba, std::uint8_t colorFormat = 0u, std::uint16_t valueKind = 1u) {
    std::vector<std::uint8_t> bytes = { 'H', 'E', 'L', 'E', 1u, 25u, 1u, 0u, 1u, 0u,
        static_cast<std::uint8_t>(valueKind), 0u };
    AppendString(bytes, "owner-fixture-texture");
    AppendU32(bytes, 0u); // RuntimeAssetId low word.
    AppendU32(bytes, 0u); // RuntimeAssetId high word.
    AppendString(bytes, "owner-fixture-authoring-id");
    AppendU32(bytes, 0u); // Empty FormerAuthoringAssetIds array.
    if (valueKind != 1u) {
        AppendString(bytes, "wrong serialized asset kind");
        return bytes;
    }
    AppendU16(bytes, width);
    AppendU16(bytes, height);
    bytes.push_back(colorFormat);
    bytes.push_back(0u); // TextureAssetAlphaPrecision::Opaque.
    AppendU32(bytes, 0u); // Empty palette byte array.
    AppendU32(bytes, static_cast<std::uint32_t>(rgba.size()));
    bytes.insert(bytes.end(), rgba.begin(), rgba.end());
    return bytes;
}

/// <summary>Creates a borrowed raw TextureAsset with caller-controlled dimensions, format and bytes.</summary>
/// <param name="width">Raw texture width.</param>
/// <param name="height">Raw texture height.</param>
/// <param name="rgba">Raw row-major texture bytes, or null to test missing storage.</param>
/// <param name="colorFormat">Raw texture enum value.</param>
/// <returns>Heap asset whose caller retains ownership throughout BuildRaw.</returns>
::TextureAsset* MakeRawAsset(std::uint16_t width, std::uint16_t height,
    const std::vector<std::uint8_t>* rgba, std::uint8_t colorFormat = 0u) {
    ::TextureAsset* asset = new ::TextureAsset();
    asset->Width = width;
    asset->Height = height;
    asset->ColorFormat = static_cast<::TextureAssetColorFormat>(colorFormat);
    if (rgba != nullptr) {
        asset->Colors = new Array<std::uint8_t>(static_cast<std::int32_t>(rgba->size()));
        for (std::size_t index = 0u; index < rgba->size(); ++index) {
            (*asset->Colors)[static_cast<std::int32_t>(index)] = (*rgba)[index];
        }
    }
    return asset;
}

/// <summary>Creates a borrowed raw TextureAsset from a temporary-safe byte vector reference.</summary>
/// <param name="width">Raw texture width.</param>
/// <param name="height">Raw texture height.</param>
/// <param name="rgba">Raw row-major texture bytes copied into the new asset.</param>
/// <returns>Heap asset whose caller retains ownership throughout BuildRaw.</returns>
::TextureAsset* MakeRawAsset(std::uint16_t width, std::uint16_t height,
    const std::vector<std::uint8_t>& rgba) {
    return MakeRawAsset(width, height, &rgba);
}

/// <summary>Releases a fixture-owned Core TextureAsset using its generated array-disposal contract.</summary>
/// <param name="asset">Heap TextureAsset created by MakeRawAsset.</param>
void DisposeRawAsset(::TextureAsset* asset) {
    if (asset == nullptr) return;
    asset->Dispose();
    delete asset;
}

/// <summary>Checks invalid raw assets fail before callback execution or any GPU allocation growth.</summary>
void TestRawValidation(::helengine::ps3::Ps3Batch2DResources& resources,
    std::size_t& flushCount, const std::vector<std::uint8_t>& pixels) {
    const std::size_t startingBytes = resources.OwnedGpuBytes();
    ::TextureAsset* invalidFormat = MakeRawAsset(2u, 2u, &pixels, 1u);
    Expect(Throws([&] { (void)resources.BuildRaw(*invalidFormat); }), "raw-rgba4444-rejected");
    DisposeRawAsset(invalidFormat);

    ::TextureAsset* zeroDimension = MakeRawAsset(0u, 2u, &pixels);
    Expect(Throws([&] { (void)resources.BuildRaw(*zeroDimension); }), "raw-zero-dimension-rejected");
    DisposeRawAsset(zeroDimension);

    ::TextureAsset* overLimit = MakeRawAsset(4097u, 1u, &pixels);
    Expect(Throws([&] { (void)resources.BuildRaw(*overLimit); }), "raw-over-limit-dimension-rejected");
    DisposeRawAsset(overLimit);

    const std::vector<std::uint8_t> partialPixels = { 1u, 2u, 3u, 4u };
    ::TextureAsset* wrongCount = MakeRawAsset(2u, 2u, &partialPixels);
    Expect(Throws([&] { (void)resources.BuildRaw(*wrongCount); }), "raw-wrong-color-count-rejected");
    DisposeRawAsset(wrongCount);

    ::TextureAsset* nullColors = MakeRawAsset(2u, 2u, nullptr);
    Expect(Throws([&] { (void)resources.BuildRaw(*nullColors); }), "raw-null-colors-rejected");
    DisposeRawAsset(nullColors);
    Expect(resources.OwnedGpuBytes() == startingBytes && flushCount == 0u,
        "raw-invalid-input-does-not-grow-gpu-or-flush");
}

} // namespace

/// <summary>Runs generated-Core texture wrapper ownership, failure and source-isolation checks on PS3.</summary>
int main() {
    static const std::vector<std::uint8_t> pixels = {
        255u, 0u, 0u, 255u, 0u, 255u, 0u, 255u,
        0u, 0u, 255u, 255u, 255u, 255u, 0u, 255u
    };

    ::helengine::ps3::Ps3Video video;
    bool videoInitialized = false;
    std::size_t flushCount = 0u;
    std::size_t observedLiveFlushes = 0u;
    std::size_t rejectedReentrantMutations = 0u;
    bool throwOnFlush = false;
    bool attemptReentrantMutation = false;
    ::helengine::ps3::Ps3Batch2DResources* currentOwner = nullptr;
    ::helengine::ps3::Ps3RuntimeTexture* callbackTexture = nullptr;
    try {
        if (!video.Initialize()) throw std::runtime_error("Ps3Video initialization failed");
        videoInitialized = true;
        ::helengine::ps3::Ps3Batch2DResources resources(video, [&] {
            ++flushCount;
            if (callbackTexture != nullptr && currentOwner != nullptr) {
                if (!callbackTexture->IsDisposed && currentOwner->Resolve(*callbackTexture).OwnedGpuBytes() > 0u) {
                    ++observedLiveFlushes;
                }
                if (attemptReentrantMutation) {
                    try {
                        const std::uint8_t replacement[4] = { 9u, 8u, 7u, 6u };
                        currentOwner->Update(*callbackTexture, 0u, 0u, 1u, 1u,
                            replacement, sizeof(replacement), sizeof(replacement));
                    } catch (...) {
                        ++rejectedReentrantMutations;
                    }
                }
            }
            if (throwOnFlush) throw std::runtime_error("injected CPU flush failure");
        });
        currentOwner = &resources;
        TestRawValidation(resources, flushCount, pixels);

        ::TextureAsset* borrowed = MakeRawAsset(2u, 2u, &pixels);
        ::helengine::ps3::Ps3RuntimeTexture* texture = resources.BuildRaw(*borrowed);
        Expect(texture != nullptr && texture->Width == 2 && texture->Height == 2
            && resources.OwnedGpuBytes() == 16u, "raw-two-by-two-creates-real-owner-and-wrapper");
        Expect(borrowed->Colors != nullptr && borrowed->Colors->get_Length() == 16
            && (*borrowed->Colors)[0] == 255u && (*borrowed->Colors)[5] == 255u,
            "raw-asset-remains-caller-owned-and-usable");
        DisposeRawAsset(borrowed);

        callbackTexture = texture;
        texture->Dispose();
        Expect(observedLiveFlushes == 1u && texture->IsDisposed
            && resources.OwnedGpuBytes() == 0u, "dispose-flushes-while-live-then-retires");
        const std::size_t disposedFlushCount = flushCount;
        texture->Dispose();
        Expect(flushCount == disposedFlushCount, "direct-dispose-is-idempotent");
        delete texture;
        callbackTexture = nullptr;

        ::TextureAsset* mutableFlagAsset = MakeRawAsset(2u, 2u, &pixels);
        ::helengine::ps3::Ps3RuntimeTexture* mutableFlagTexture = resources.BuildRaw(*mutableFlagAsset);
        DisposeRawAsset(mutableFlagAsset);
        callbackTexture = mutableFlagTexture;
        const std::size_t beforeMutableFlagDispose = flushCount;
        mutableFlagTexture->IsDisposed = true;
        mutableFlagTexture->Dispose();
        Expect(flushCount == beforeMutableFlagDispose + 1u && mutableFlagTexture->IsDisposed,
            "public-disposed-flag-cannot-suppress-native-retirement");
        mutableFlagTexture->IsDisposed = false;
        Expect(Throws([&] { (void)resources.Resolve(*mutableFlagTexture); }),
            "public-flag-cannot-resurrect-retired-owner");
        mutableFlagTexture->Dispose();
        Expect(mutableFlagTexture->IsDisposed && flushCount == beforeMutableFlagDispose + 1u,
            "private-retirement-state-remains-authoritative");
        delete mutableFlagTexture;
        callbackTexture = nullptr;

        ::TextureAsset* updateAsset = MakeRawAsset(2u, 2u, &pixels);
        ::helengine::ps3::Ps3RuntimeTexture* updateTexture = resources.BuildRaw(*updateAsset);
        DisposeRawAsset(updateAsset);
        callbackTexture = updateTexture;
        attemptReentrantMutation = true;
        const std::size_t beforeUpdateFlush = flushCount;
        const std::uint8_t replacement[4] = { 4u, 3u, 2u, 1u };
        resources.Update(*updateTexture, 1u, 1u, 1u, 1u, replacement, sizeof(replacement), sizeof(replacement));
        attemptReentrantMutation = false;
        Expect(flushCount == beforeUpdateFlush + 1u && observedLiveFlushes >= 2u,
            "update-flushes-while-resolvable");
        Expect(rejectedReentrantMutations == 1u && resources.OwnedGpuBytes() == 16u,
            "reentrant-mutation-rejected-without-losing-owner");
        callbackTexture = nullptr;

        ::TextureAsset* retryAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 1u, 2u, 3u, 4u });
        ::helengine::ps3::Ps3RuntimeTexture* retryTexture = resources.BuildRaw(*retryAsset);
        DisposeRawAsset(retryAsset);
        callbackTexture = retryTexture;
        throwOnFlush = true;
        const std::size_t beforeThrowBytes = resources.OwnedGpuBytes();
        const bool flushFailureVisible = Throws([&] { retryTexture->Dispose(); });
        Expect(flushFailureVisible && !retryTexture->IsDisposed
            && resources.OwnedGpuBytes() == beforeThrowBytes, "flush-exception-retains-live-owner");
        throwOnFlush = false;
        retryTexture->Dispose();
        Expect(retryTexture->IsDisposed && resources.OwnedGpuBytes() == 16u,
            "cpu-flush-failure-can-be-retried-safely");
        delete retryTexture;
        callbackTexture = nullptr;

        const std::size_t loopBaselineBytes = resources.OwnedGpuBytes();
        for (unsigned iteration = 0u; iteration < 4u; ++iteration) {
            ::TextureAsset* loopAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{
                static_cast<std::uint8_t>(iteration), 2u, 3u, 4u });
            ::helengine::ps3::Ps3RuntimeTexture* loopTexture = resources.BuildRaw(*loopAsset);
            DisposeRawAsset(loopAsset);
            resources.Update(*loopTexture, 0u, 0u, 1u, 1u, replacement, sizeof(replacement), sizeof(replacement));
            loopTexture->Dispose();
            delete loopTexture;
        }
        Expect(resources.OwnedGpuBytes() == loopBaselineBytes,
            "repeated-create-update-dispose-loops-retain-no-extra-gpu-bytes");

        Ps3Batch2DOwnerContentSource sourceA;
        Ps3Batch2DOwnerContentSource sourceB;
        const std::vector<std::uint8_t> sourceAPixels = { 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u };
        const std::vector<std::uint8_t> sourceBPixels = { 90u, 91u, 92u, 93u };
        sourceA.AddAsset("cooked/shared.texture", MakePackagedAsset(2u, 1u, sourceAPixels));
        sourceB.AddAsset("cooked/shared.texture", MakePackagedAsset(1u, 1u, sourceBPixels));
        ::helengine::ps3::Ps3RuntimeTexture* sourceATexture = resources.BuildCooked(sourceA, "cooked/shared.texture");
        ::helengine::ps3::Ps3RuntimeTexture* sourceBTexture = resources.BuildCooked(sourceB, "cooked/shared.texture");
        Expect(sourceATexture != sourceBTexture && sourceATexture->Width == 2 && sourceATexture->Height == 1
            && sourceBTexture->Width == 1 && sourceBTexture->Height == 1,
            "same-path-distinct-sources-create-independent-cooked-owners");
        Expect(sourceA.OpenCount == 1u && sourceA.DisposedStreamCount == 1u
            && sourceB.OpenCount == 1u && sourceB.DisposedStreamCount == 1u,
            "cooked-source-streams-are-disposed-on-success");
        const std::size_t beforeInvalidCooked = sourceA.OpenCount;
        Expect(Throws([&] { (void)resources.BuildCooked(sourceA, "../escape.texture"); })
            && sourceA.OpenCount == beforeInvalidCooked, "cooked-traversal-rejected-before-source-open");
        Expect(Throws([&] { (void)resources.BuildCooked(sourceA, "cooked/missing.texture"); })
            && sourceA.OpenCount == beforeInvalidCooked + 1u, "cooked-missing-content-fails-visibly");
        Ps3Batch2DOwnerContentSource wrongAssetSource;
        wrongAssetSource.AddAsset("cooked/wrong.texture", MakePackagedAsset(0u, 0u, {}, 0u, 4u));
        Expect(Throws([&] { (void)resources.BuildCooked(wrongAssetSource, "cooked/wrong.texture"); })
            && wrongAssetSource.DisposedStreamCount == 1u, "cooked-wrong-asset-fails-and-disposes-stream");
        Ps3Batch2DOwnerContentSource malformedSource;
        malformedSource.AddAsset("cooked/malformed.texture", { 'H', 'E', 'L', 'E' });
        Expect(Throws([&] { (void)resources.BuildCooked(malformedSource, "cooked/malformed.texture"); })
            && malformedSource.DisposedStreamCount == 1u, "cooked-malformed-asset-fails-and-disposes-stream");
        Ps3Batch2DOwnerContentSource boundarySource;
        const std::vector<std::uint8_t> shortMalformedHeader = { 'H', 'E', 'L', 'E' };
        const std::size_t maximumCookedEnvelope = 65u * 1024u * 1024u;
        boundarySource.AddAssetWithDeclaredLength("cooked/at-envelope-limit.texture", shortMalformedHeader,
            maximumCookedEnvelope);
        const std::size_t beforeEnvelopeBoundaryBytes = resources.OwnedGpuBytes();
        const std::string atLimitError = FailureMessage([&] {
            (void)resources.BuildCooked(boundarySource, "cooked/at-envelope-limit.texture");
        });
        Expect(!atLimitError.empty() && atLimitError.find("texture content is empty or exceeds its") == std::string::npos
            && boundarySource.DisposedStreamCount == 1u && resources.OwnedGpuBytes() == beforeEnvelopeBoundaryBytes,
            "cooked-64mib-texture-plus-bounded-metadata-limit-reaches-deserializer-without-gpu-allocation");
        boundarySource.AddAssetWithDeclaredLength("cooked/over-envelope-limit.texture", shortMalformedHeader,
            maximumCookedEnvelope + 1u);
        const std::string overLimitError = FailureMessage([&] {
            (void)resources.BuildCooked(boundarySource, "cooked/over-envelope-limit.texture");
        });
        Expect(overLimitError.find("envelope") != std::string::npos
            && boundarySource.DisposedStreamCount == 2u && resources.OwnedGpuBytes() == beforeEnvelopeBoundaryBytes,
            "cooked-content-over-texture-plus-metadata-envelope-rejected-before-gpu-allocation");

        ::helengine::ps3::Ps3Batch2DResources foreignOwner(video, [] {});
        ::TextureAsset* foreignAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 7u, 6u, 5u, 4u });
        ::helengine::ps3::Ps3RuntimeTexture* foreignTexture = foreignOwner.BuildRaw(*foreignAsset);
        DisposeRawAsset(foreignAsset);
        Expect(Throws([&] { (void)resources.Resolve(*foreignTexture); })
            && Throws([&] { resources.Update(*foreignTexture, 0u, 0u, 1u, 1u, replacement, 4u, 4u); }),
            "foreign-owner-wrapper-rejected");

        Ps3Batch2DOwnerReleaseManager releaseManager(resources);
        ::TextureAsset* managerAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 3u, 2u, 1u, 255u });
        ::RuntimeTexture* managerTexture = releaseManager.BuildTextureFromRaw(managerAsset);
        DisposeRawAsset(managerAsset);
        callbackTexture = static_cast<::helengine::ps3::Ps3RuntimeTexture*>(managerTexture);
        const std::size_t beforeBaseRelease = flushCount;
        releaseManager.ReleaseTexture(managerTexture);
        Expect(flushCount == beforeBaseRelease + 1u, "base-release-texture-disposes-then-deletes-wrapper");
        callbackTexture = nullptr;

        ::TextureAsset* fontSourceAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 3u, 2u, 1u, 255u });
        ::RuntimeTexture* fontTexture = releaseManager.BuildTextureFromRaw(fontSourceAsset);
        DisposeRawAsset(fontSourceAsset);
        ::FontAsset* font = new ::FontAsset(nullptr, fontTexture, nullptr, 0.0f, 0, 0);
        callbackTexture = static_cast<::helengine::ps3::Ps3RuntimeTexture*>(fontTexture);
        const std::size_t beforeFontRelease = flushCount;
        releaseManager.ReleaseFont(font);
        Expect(flushCount == beforeFontRelease + 1u, "base-release-font-releases-owned-atlas-once");
        callbackTexture = nullptr;

        const std::size_t beforeDetach = flushCount;
        resources.DetachCpuSubmission();
        resources.DetachCpuSubmission();
        Expect(flushCount == beforeDetach + 1u, "detach-flushes-exactly-once");
        ::TextureAsset* detachedAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 1u, 1u, 1u, 1u });
        Expect(Throws([&] { (void)resources.BuildRaw(*detachedAsset); }), "detached-owner-rejects-creation");
        DisposeRawAsset(detachedAsset);
        Expect(Throws([&] { resources.Update(*sourceATexture, 0u, 0u, 1u, 1u, replacement, 4u, 4u); })
            && Throws([&] { (void)resources.Resolve(*sourceATexture); }),
            "detached-owner-rejects-update-and-resolve");
        const std::size_t afterDetach = flushCount;
        sourceATexture->Dispose();
        Expect(flushCount == afterDetach, "detached-wrapper-retires-without-dead-callback");

        ::helengine::ps3::Ps3Batch2DResources shutdownGateOwner(video, [] {});
        ::TextureAsset* shutdownGateAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 8u, 7u, 6u, 5u });
        ::helengine::ps3::Ps3RuntimeTexture* shutdownGateTexture = shutdownGateOwner.BuildRaw(*shutdownGateAsset);
        DisposeRawAsset(shutdownGateAsset);

        ::helengine::ps3::Ps3RuntimeTexture* lateWrapper = nullptr;
        {
            ::helengine::ps3::Ps3Batch2DResources temporaryOwner(video, [] {});
            ::TextureAsset* orphanAsset = MakeRawAsset(1u, 1u, std::vector<std::uint8_t>{ 8u, 8u, 8u, 8u });
            lateWrapper = temporaryOwner.BuildRaw(*orphanAsset);
            DisposeRawAsset(orphanAsset);
        }
        Expect(Throws([&] { lateWrapper->Dispose(); }), "late-dispose-after-owner-destruction-fails-safely");
        delete lateWrapper;
        lateWrapper = nullptr;

        if (!video.Drain()) throw std::runtime_error("final video drain failed");
        std::string shutdownError;
        foreignOwner.DetachCpuSubmission();
        shutdownError.clear();
        Expect(foreignOwner.ShutdownAfterDrain(shutdownError) && foreignOwner.OwnedGpuBytes() == 0u,
            "detached-owner-shutdown-after-drain-clears-gpu-bytes");

        shutdownError.clear();
        Expect(!shutdownGateOwner.ShutdownAfterDrain(shutdownError) && !shutdownError.empty()
            && shutdownGateOwner.OwnedGpuBytes() == 4u, "final-shutdown-requires-cpu-detach");
        shutdownGateOwner.DetachCpuSubmission();
        shutdownError.clear();
        Expect(shutdownGateOwner.ShutdownAfterDrain(shutdownError), "detached-owner-shuts-down-after-drain");
        shutdownGateTexture->Dispose();
        delete shutdownGateTexture;

        shutdownError.clear();
        Expect(resources.ShutdownAfterDrain(shutdownError) && resources.OwnedGpuBytes() == 0u,
            "primary-owner-shutdown-after-drain-clears-gpu-bytes");
        shutdownError.clear();
        Expect(resources.ShutdownAfterDrain(shutdownError) && resources.OwnedGpuBytes() == 0u,
            "primary-owner-final-shutdown-is-idempotent");
        Expect(Throws([&] { resources.Retire(*foreignTexture); }),
            "foreign-wrapper-rejected-by-post-shutdown-logical-retire");
        foreignTexture->Dispose();
        delete foreignTexture;
        const std::size_t postShutdownFlushCount = flushCount;
        sourceBTexture->Dispose();
        Expect(sourceBTexture->IsDisposed && flushCount == postShutdownFlushCount,
            "owned-wrapper-can-logically-dispose-after-final-shutdown");
        updateTexture->Dispose();
        delete sourceATexture;
        delete sourceBTexture;
        delete updateTexture;
        sourceATexture = nullptr;
        sourceBTexture = nullptr;
        video.Shutdown();
    } catch (const std::exception& exception) {
        ++FailedCases;
        std::printf("FAIL texture-owner-fatal reason=%s\n", exception.what());
        if (videoInitialized && !video.HasDrained()) (void)video.Drain();
        if (videoInitialized) video.Shutdown();
    } catch (...) {
        ++FailedCases;
        std::printf("FAIL texture-owner-fatal reason=unknown\n");
        if (videoInitialized && !video.HasDrained()) (void)video.Drain();
        if (videoInitialized) video.Shutdown();
    }

    std::printf("PS3_BATCH2D_TEXTURE_OWNERS_TESTS passed=%lu failed=%lu\n",
        static_cast<unsigned long>(PassedCases), static_cast<unsigned long>(FailedCases));
    return FailedCases == 0u ? 0 : 1;
}
