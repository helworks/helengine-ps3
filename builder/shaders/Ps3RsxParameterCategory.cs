namespace helengine.ps3.builder;

/// <summary>Identifies how an RSX parameter participates in a shader stage.</summary>
public enum Ps3RsxParameterCategory {
    /// <summary>Vertex input or fragment-stage input attribute record from the SDK attribute table.</summary>
    Attribute,

    /// <summary>Texture sampler whose SDK attribute index is a texture-unit number.</summary>
    Sampler,

    /// <summary>Constant-table parameter whose stage-specific element records carry its references.</summary>
    Constant
}
