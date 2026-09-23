namespace helengine.ps3.builder;

/// <summary>Associates one authored vertex input with its final SDK attribute parameter and slot.</summary>
public sealed class Ps3ShaderAttributeBinding {
    /// <summary>Creates a mapping for one reflected vertex input and its actual SDK attribute record.</summary>
    /// <param name="sourceVariable">Original authored input reflection record.</param>
    /// <param name="rsxParameter">Validated SDK attribute parameter.</param>
    public Ps3ShaderAttributeBinding(Ps3ShaderInterfaceVariable sourceVariable, Ps3RsxParameter rsxParameter) {
        SourceVariable = sourceVariable ?? throw new ArgumentNullException(nameof(sourceVariable));
        RsxParameter = rsxParameter ?? throw new ArgumentNullException(nameof(rsxParameter));
        if (rsxParameter.Category != Ps3RsxParameterCategory.Attribute || !rsxParameter.HardwareIndex.HasValue) {
            throw new ArgumentException("Vertex attribute mapping requires an SDK attribute parameter with a hardware slot.", nameof(rsxParameter));
        }
        AuthoredSemantic = sourceVariable.Semantic;
        GeneratedName = sourceVariable.GeneratedName;
        HardwareSlot = rsxParameter.HardwareIndex.Value;
    }

    /// <summary>Gets the original authored input reflection record.</summary>
    public Ps3ShaderInterfaceVariable SourceVariable { get; }

    /// <summary>Gets the exact validated SDK attribute parameter.</summary>
    public Ps3RsxParameter RsxParameter { get; }

    /// <summary>Gets the authored HLSL input semantic.</summary>
    public string AuthoredSemantic { get; }

    /// <summary>Gets the exact compiler-generated Cg parameter name.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the raw SDK vertex attribute index.</summary>
    public uint HardwareSlot { get; }
}
