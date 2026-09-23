namespace helengine.ps3.builder;

/// <summary>Describes one active combined texture and sampler binding pair.</summary>
public sealed class Ps3ShaderSamplerBinding {
    /// <summary>Initializes a combined texture/sampler record with both source bindings and the GLSL name.</summary>
    /// <param name="textureName">Authored texture resource name.</param>
    /// <param name="textureBinding">SPIR-V texture binding.</param>
    /// <param name="samplerName">Authored sampler resource name.</param>
    /// <param name="samplerBinding">SPIR-V sampler binding.</param>
    /// <param name="generatedName">Stable generated combined-sampler identifier.</param>
    /// <param name="dimension">SPIR-V image dimension name.</param>
    public Ps3ShaderSamplerBinding(string textureName, uint textureBinding, string samplerName, uint samplerBinding, string generatedName, string dimension) {
        if (string.IsNullOrWhiteSpace(textureName)) throw new ArgumentException("Texture name must be provided.", nameof(textureName));
        if (string.IsNullOrWhiteSpace(samplerName)) throw new ArgumentException("Sampler name must be provided.", nameof(samplerName));
        if (string.IsNullOrWhiteSpace(generatedName)) throw new ArgumentException("Generated combined-sampler name must be provided.", nameof(generatedName));
        if (string.IsNullOrWhiteSpace(dimension)) throw new ArgumentException("Texture dimension must be provided.", nameof(dimension));
        TextureName = textureName;
        TextureBinding = textureBinding;
        SamplerName = samplerName;
        SamplerBinding = samplerBinding;
        GeneratedName = generatedName;
        Dimension = dimension;
    }

    /// <summary>Gets the authored texture resource name.</summary>
    public string TextureName { get; }

    /// <summary>Gets the SPIR-V texture resource binding.</summary>
    public uint TextureBinding { get; }

    /// <summary>Gets the authored sampler resource name.</summary>
    public string SamplerName { get; }

    /// <summary>Gets the SPIR-V sampler resource binding.</summary>
    public uint SamplerBinding { get; }

    /// <summary>Gets the stable name assigned to this particular texture/sampler pair.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the image dimensionality reported by SPIRV-Cross.</summary>
    public string Dimension { get; }
}
