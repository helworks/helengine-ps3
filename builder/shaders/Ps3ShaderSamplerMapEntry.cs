namespace helengine.ps3.builder;

/// <summary>Associates one authored texture/sampler pair with its actual RSX sampler parameter and texture unit.</summary>
public sealed class Ps3ShaderSamplerMapEntry {
    /// <summary>Creates a mapping for one reflected combined sampler and its SDK sampler record.</summary>
    /// <param name="sourceSampler">Original authored texture and sampler identities.</param>
    /// <param name="rsxParameter">Validated SDK sampler parameter.</param>
    public Ps3ShaderSamplerMapEntry(Ps3ShaderSamplerBinding sourceSampler, Ps3RsxParameter rsxParameter) {
        SourceSampler = sourceSampler ?? throw new ArgumentNullException(nameof(sourceSampler));
        RsxParameter = rsxParameter ?? throw new ArgumentNullException(nameof(rsxParameter));
        if (rsxParameter.Category != Ps3RsxParameterCategory.Sampler || !rsxParameter.HardwareIndex.HasValue) {
            throw new ArgumentException("Combined sampler mapping requires an SDK sampler parameter with a texture unit.", nameof(rsxParameter));
        }
        TextureName = sourceSampler.TextureName;
        TextureBinding = sourceSampler.TextureBinding;
        SamplerName = sourceSampler.SamplerName;
        SamplerBinding = sourceSampler.SamplerBinding;
        GeneratedName = sourceSampler.GeneratedName;
        Dimension = sourceSampler.Dimension;
        TextureUnit = rsxParameter.HardwareIndex.Value;
    }

    /// <summary>Gets the original authored combined texture/sampler reflection record.</summary>
    public Ps3ShaderSamplerBinding SourceSampler { get; }

    /// <summary>Gets the exact validated SDK sampler parameter.</summary>
    public Ps3RsxParameter RsxParameter { get; }

    /// <summary>Gets the authored texture resource name.</summary>
    public string TextureName { get; }

    /// <summary>Gets the source binding assigned to the authored texture resource.</summary>
    public uint TextureBinding { get; }

    /// <summary>Gets the authored sampler resource name.</summary>
    public string SamplerName { get; }

    /// <summary>Gets the source binding assigned to the authored sampler resource.</summary>
    public uint SamplerBinding { get; }

    /// <summary>Gets the exact unique generated combined sampler identifier.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the source compiler's image dimensionality name.</summary>
    public string Dimension { get; }

    /// <summary>Gets the raw SDK texture unit occupied by this combined sampler.</summary>
    public uint TextureUnit { get; }
}
