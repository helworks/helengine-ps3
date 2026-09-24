#pragma once

#include "Ps3CookedShaderProgram.hpp"

#include <rsx/rsx_program.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace helengine::ps3 {

class Ps3Video;

/// <summary>Owns validated authored RSX program blobs and the fragment microcode needed for reusable draws.</summary>
class Ps3AuthoredProgram {
private:
    /// <summary>Alignment required for CPU copies passed to the SDK program readers.</summary>
    static constexpr std::size_t CpuProgramAlignment = 128u;

    /// <summary>Alignment required for fragment microcode in RSX local memory.</summary>
    static constexpr std::size_t FragmentUcodeAlignment = 64u;

    /// <summary>Owned CPU metadata and byte-for-byte public snapshot of the validated artifact.</summary>
    Ps3CookedShaderProgram ProgramValue;

    /// <summary>Borrowed video owner; it must outlive every live-owner call and explicit post-drain shutdown.</summary>
    Ps3Video* VideoValue;

    /// <summary>Aligned CPU copy of the SDK vertex program blob.</summary>
    void* VertexProgramBytes;

    /// <summary>Aligned CPU copy of the SDK pixel program blob.</summary>
    void* PixelProgramBytes;

    /// <summary>SDK vertex descriptor pointing into the owned aligned vertex blob.</summary>
    const rsxVertexProgram* VertexProgramValue;

    /// <summary>SDK fragment descriptor pointing into the owned aligned pixel blob.</summary>
    const rsxFragmentProgram* PixelProgramValue;

    /// <summary>Vertex microcode pointer into the owned aligned vertex blob.</summary>
    const void* VertexUcodeValue;

    /// <summary>RSX-local copy of fragment microcode referenced by queued fragment-program commands.</summary>
    void* FragmentUcodeBuffer;

    /// <summary>RSX offset corresponding to FragmentUcodeBuffer.</summary>
    std::uint32_t FragmentUcodeOffset;

    /// <summary>Exact number of fragment microcode bytes owned in RSX memory.</summary>
    std::uint32_t FragmentUcodeBytes;

    /// <summary>RSX-local bytes owned by this program, excluding its CPU metadata copies.</summary>
    std::size_t OwnedGpuBytesValue;

    /// <summary>Tracks which active vertex uniforms were uploaded for the current draw.</summary>
    std::vector<std::uint8_t> VertexUniformUploaded;

    /// <summary>Tracks which active pixel uniforms were uploaded for the current draw.</summary>
    std::vector<std::uint8_t> PixelUniformUploaded;

    /// <summary>True after Initialize has published all validated and allocated program state.</summary>
    bool Initialized;

    /// <summary>True after BeginDraw and until all active uniforms have been completed.</summary>
    bool DrawActive;

    /// <summary>True after resources have been released following a successful video drain.</summary>
    bool ShutdownComplete;

public:
    /// <summary>Creates an uninitialized owner without allocating or publishing a default shader.</summary>
    Ps3AuthoredProgram();

    /// <summary>Never accesses the borrowed video; logs and retains RSX storage if explicit shutdown was omitted.</summary>
    ~Ps3AuthoredProgram();

    /// <summary>Program ownership cannot be copied because it contains unique CPU and RSX allocations.</summary>
    Ps3AuthoredProgram(const Ps3AuthoredProgram&) = delete;

    /// <summary>Program ownership cannot be copy-assigned because it contains unique CPU and RSX allocations.</summary>
    Ps3AuthoredProgram& operator=(const Ps3AuthoredProgram&) = delete;

    /// <summary>Validates and copies both stage blobs before publishing one reusable RSX program.</summary>
    /// <param name="video">Initialized video owner; it must outlive this live owner and its explicit shutdown.</param>
    /// <param name="program">Complete CPU artifact returned by the bounded native reader.</param>
    /// <param name="error">Receives the failing stage or allocation detail when initialization fails.</param>
    /// <returns>True only after validation, aligned copies, microcode allocation, and offset resolution succeed.</returns>
    bool Initialize(Ps3Video& video, const Ps3CookedShaderProgram& program, std::string& error);

    /// <summary>Loads the owned vertex and fragment programs and starts a fresh active-uniform set.</summary>
    /// <param name="error">Receives the lifecycle reason when a draw cannot begin.</param>
    /// <returns>True when program-load commands were queued for the current video frame.</returns>
    bool BeginDraw(std::string& error);

    /// <summary>Uploads one complete active uniform using SDK-packed finite float vectors.</summary>
    /// <param name="stage">Stage identity: zero for vertex, one for pixel.</param>
    /// <param name="uniformIndex">Index into that stage's owned Program().Uniforms vector.</param>
    /// <param name="values">Packed SDK register vectors, not authored constant-buffer bytes.</param>
    /// <param name="valueCount">Exact float count required by the SDK type and element count.</param>
    /// <param name="error">Receives a stage, index, type, count, or value error before any parameter command.</param>
    /// <returns>True after every hardware element for the active mapping has been queued.</returns>
    bool UploadUniform(std::uint32_t stage, std::size_t uniformIndex,
        const float* values, std::size_t valueCount, std::string& error);

    /// <summary>Rejects missing active uniforms and orders fragment relocation updates before the draw.</summary>
    /// <param name="error">Receives the first active mapping that has not been uploaded.</param>
    /// <returns>True when all stage values are complete and fragment program location is refreshed.</returns>
    bool FinishUniformUpdates(std::string& error);

    /// <summary>Releases CPU and RSX storage only after the associated video owner has drained successfully.</summary>
    /// <param name="error">Receives the lifecycle reason when safe shutdown is not yet possible.</param>
    /// <returns>True when the owner is retired and OwnedGpuBytes() is zero.</returns>
    bool ShutdownAfterDrain(std::string& error);

    /// <summary>Returns the validated immutable CPU artifact snapshot while this owner is initialized.</summary>
    /// <returns>The exact identity, metadata, and original unmodified stage blobs.</returns>
    /// <exception cref="std::logic_error">Thrown before successful initialization or after retirement.</exception>
    const Ps3CookedShaderProgram& Program() const;

    /// <summary>Returns fragment microcode bytes currently allocated in RSX local memory.</summary>
    /// <returns>Owned RSX bytes, or zero after successful drain shutdown.</returns>
    std::size_t OwnedGpuBytes() const;

private:
    /// <summary>Frees resources known not to be referenced by queued RSX work and resets published state.</summary>
    void ReleaseOwnedResources();
};

} // namespace helengine::ps3
