#pragma once

#if defined(HELENGINE_PS3_HAS_GENERATED_CORE)

#include "Ps3AuthoredProgram.hpp"
#include "Ps3CookedShaderProgram.hpp"

#include "IContentStreamSource.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace helengine::ps3 {

class Ps3Video;

/// <summary>Loads, validates, and owns the exact textured and rounded native batch shader pair for one content source.</summary>
class Ps3Batch2DPrograms {
private:
    /// <summary>Borrowed video owner that must outlive both programs and their post-drain retirement.</summary>
    Ps3Video* VideoValue;

    /// <summary>Borrowed content source defining this pair owner's identity namespace.</summary>
    ::IContentStreamSource* SourceValue;

    /// <summary>Textured variant owner, retained after a partial initialization failure until final drain.</summary>
    std::unique_ptr<Ps3AuthoredProgram> TexturedValue;

    /// <summary>Rounded-shape variant owner, retained after a partial initialization failure until final drain.</summary>
    std::unique_ptr<Ps3AuthoredProgram> RoundedShapeValue;

    /// <summary>True after one initialization attempt, including a latched failed attempt.</summary>
    bool InitializationAttempted;

    /// <summary>True only while both program owners are initialized and accessors may be used.</summary>
    bool Ready;

    /// <summary>True after successful explicit post-drain retirement.</summary>
    bool ShutdownComplete;

public:
    /// <summary>Creates a source-scoped owner without reading content or allocating GPU memory.</summary>
    /// <param name="video">Initialized video owner borrowed for each authored program.</param>
    /// <param name="source">Content source whose object identity scopes this pair.</param>
    Ps3Batch2DPrograms(Ps3Video& video, ::IContentStreamSource& source);

    /// <summary>Retains any live RSX allocations if explicit final-drain retirement was omitted.</summary>
    ~Ps3Batch2DPrograms();

    /// <summary>Program-pair ownership cannot be copied because it contains unique RSX program owners.</summary>
    Ps3Batch2DPrograms(const Ps3Batch2DPrograms&) = delete;

    /// <summary>Program-pair ownership cannot be copy-assigned because it contains unique RSX program owners.</summary>
    Ps3Batch2DPrograms& operator=(const Ps3Batch2DPrograms&) = delete;

    /// <summary>Reads and validates both exact cooked paths before initializing either authored GPU program.</summary>
    /// <param name="texturedPath">Manifest-provided content-relative P3SP path for the Textured variant.</param>
    /// <param name="roundedPath">Manifest-provided content-relative P3SP path for the RoundedShape variant.</param>
    /// <exception cref="std::runtime_error">Thrown for a repeated attempt, missing/invalid artifact, or failed program initialization.</exception>
    void Initialize(const std::string& texturedPath, const std::string& roundedPath);

    /// <summary>Compares the source object by identity without reading from either source.</summary>
    /// <param name="source">Candidate source namespace.</param>
    /// <returns>True only when source is the exact object supplied to this owner.</returns>
    bool MatchesSource(const ::IContentStreamSource& source) const;

    /// <summary>Returns the initialized Textured program before retirement.</summary>
    /// <returns>The live textured authored program.</returns>
    /// <exception cref="std::logic_error">Thrown before both programs are ready or after shutdown.</exception>
    Ps3AuthoredProgram& Textured();

    /// <summary>Returns the initialized RoundedShape program before retirement.</summary>
    /// <returns>The live rounded-shape authored program.</returns>
    /// <exception cref="std::logic_error">Thrown before both programs are ready or after shutdown.</exception>
    Ps3AuthoredProgram& RoundedShape();

    /// <summary>Retires both owners after the borrowed video reports its successful final drain.</summary>
    /// <param name="error">Receives the first concrete retirement failure.</param>
    /// <returns>True when both owners have retired and no pair-owned GPU bytes remain.</returns>
    bool ShutdownAfterDrain(std::string& error);

    /// <summary>Returns the sum of RSX local memory retained by either program, including a partial failed attempt.</summary>
    /// <returns>Owned GPU bytes for both program owners.</returns>
    std::size_t OwnedGpuBytes() const;

    /// <summary>Validates the exact batch pair contract against two already decoded CPU snapshots.</summary>
    /// <param name="textured">Candidate Textured artifact.</param>
    /// <param name="rounded">Candidate RoundedShape artifact.</param>
    /// <param name="error">Receives identity or mapping context on rejection.</param>
    /// <returns>True only when both artifacts form the measured SpriteBatchShader pair.</returns>
    static bool ValidatePair(const Ps3CookedShaderProgram& textured,
        const Ps3CookedShaderProgram& rounded, std::string& error);
};

} // namespace helengine::ps3

#endif
