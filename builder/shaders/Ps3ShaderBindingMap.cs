namespace helengine.ps3.builder;

/// <summary>Contains validated RSX mappings for authored resources and inter-stage interfaces.</summary>
public sealed class Ps3ShaderBindingMap {
    /// <summary>Creates immutable snapshots of all validated source-to-RSX mappings.</summary>
    /// <param name="uniforms">Active authored uniform members with actual SDK constant records.</param>
    /// <param name="attributes">Authored vertex attributes with actual SDK attribute records.</param>
    /// <param name="samplers">Combined authored texture/sampler identities with actual SDK sampler records.</param>
    /// <param name="stageLinks">Validated active vertex-output to pixel-input links.</param>
    internal Ps3ShaderBindingMap(IEnumerable<Ps3ShaderUniformBinding> uniforms, IEnumerable<Ps3ShaderAttributeBinding> attributes,
        IEnumerable<Ps3ShaderSamplerMapEntry> samplers, IEnumerable<Ps3ShaderStageLink> stageLinks) {
        ArgumentNullException.ThrowIfNull(uniforms);
        ArgumentNullException.ThrowIfNull(attributes);
        ArgumentNullException.ThrowIfNull(samplers);
        ArgumentNullException.ThrowIfNull(stageLinks);
        Uniforms = Array.AsReadOnly(uniforms.ToArray());
        Attributes = Array.AsReadOnly(attributes.ToArray());
        Samplers = Array.AsReadOnly(samplers.ToArray());
        StageLinks = Array.AsReadOnly(stageLinks.ToArray());
    }

    /// <summary>Gets active authored uniform members mapped to their final SDK constant parameters.</summary>
    public IReadOnlyList<Ps3ShaderUniformBinding> Uniforms { get; }

    /// <summary>Gets authored vertex inputs mapped to their final SDK attribute parameters.</summary>
    public IReadOnlyList<Ps3ShaderAttributeBinding> Attributes { get; }

    /// <summary>Gets combined texture/sampler identities mapped to their final SDK sampler parameters.</summary>
    public IReadOnlyList<Ps3ShaderSamplerMapEntry> Samplers { get; }

    /// <summary>Gets active authored pixel inputs and their matching vertex-output channels.</summary>
    public IReadOnlyList<Ps3ShaderStageLink> StageLinks { get; }
}
