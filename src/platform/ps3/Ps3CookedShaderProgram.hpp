#pragma once

#include "Ps3CookedShaderAttribute.hpp"
#include "Ps3CookedShaderSampler.hpp"
#include "Ps3CookedShaderStage.hpp"

#include <string>
#include <vector>

namespace helengine::ps3 {

/// <summary>Owns a complete, CPU-only snapshot of one validated P3SP shader pair.</summary>
struct Ps3CookedShaderProgram {
    /// <summary>Gets the exact authored shader asset identifier.</summary>
    std::string ShaderAssetId;

    /// <summary>Gets the authored vertex program name.</summary>
    std::string VertexProgramName;

    /// <summary>Gets the authored pixel program name.</summary>
    std::string PixelProgramName;

    /// <summary>Gets the authored variant identity.</summary>
    std::string VariantName;

    /// <summary>Gets the source hash recorded by the producer for provenance.</summary>
    std::string SourceHash;

    /// <summary>Gets the pinned SDK toolchain identity recorded by the producer.</summary>
    std::string ToolchainIdentity;

    /// <summary>Gets the validated vertex program and its owned CPU metadata.</summary>
    Ps3CookedShaderStage Vertex;

    /// <summary>Gets the validated pixel program and its owned CPU metadata.</summary>
    Ps3CookedShaderStage Pixel;

    /// <summary>Gets the authored vertex attributes and their physical RSX slots.</summary>
    std::vector<Ps3CookedShaderAttribute> Attributes;

    /// <summary>Gets the pixel texture/sampler mappings and their physical RSX units.</summary>
    std::vector<Ps3CookedShaderSampler> PixelSamplers;
};

} // namespace helengine::ps3
