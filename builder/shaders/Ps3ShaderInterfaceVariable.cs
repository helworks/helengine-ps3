namespace helengine.ps3.builder;

/// <summary>Describes one active authored user interface variable reflected from SPIR-V.</summary>
public sealed class Ps3ShaderInterfaceVariable {
    /// <summary>Initializes an interface record with compiler-provided semantic and type metadata.</summary>
    /// <param name="semantic">Authored HLSL semantic from HlslSemanticGOOGLE.</param>
    /// <param name="location">SPIR-V location decoration.</param>
    /// <param name="scalarType">SPIR-V scalar base type and bit width.</param>
    /// <param name="vectorWidth">Number of scalar components per vector.</param>
    /// <param name="columns">Number of matrix columns.</param>
    /// <param name="arrayLength">Product of literal array dimensions, or one for a non-array variable.</param>
    /// <param name="interpolation">Compiler decorations describing interpolation behavior.</param>
    /// <param name="generatedName">Stable compiler-assigned GLSL identifier.</param>
    public Ps3ShaderInterfaceVariable(
        string semantic,
        uint location,
        string scalarType,
        uint vectorWidth,
        uint columns,
        uint arrayLength,
        string interpolation,
        string generatedName) {
        if (string.IsNullOrWhiteSpace(semantic)) throw new ArgumentException("Authored HLSL semantic must be provided.", nameof(semantic));
        if (string.IsNullOrWhiteSpace(scalarType)) throw new ArgumentException("Scalar type must be provided.", nameof(scalarType));
        if (string.IsNullOrWhiteSpace(interpolation)) throw new ArgumentException("Interpolation metadata must be provided.", nameof(interpolation));
        if (string.IsNullOrWhiteSpace(generatedName)) throw new ArgumentException("Generated GLSL name must be provided.", nameof(generatedName));
        Semantic = semantic;
        Location = location;
        ScalarType = scalarType;
        VectorWidth = vectorWidth;
        Columns = columns;
        ArrayLength = arrayLength;
        Interpolation = interpolation;
        GeneratedName = generatedName;
    }

    /// <summary>Gets the authored HLSL semantic decoration without ordinal-based inference.</summary>
    public string Semantic { get; }

    /// <summary>Gets the numeric SPIR-V location assigned by the compiler.</summary>
    public uint Location { get; }

    /// <summary>Gets the scalar base type and bit width reported by SPIRV-Cross.</summary>
    public string ScalarType { get; }

    /// <summary>Gets the number of scalar components in each vector.</summary>
    public uint VectorWidth { get; }

    /// <summary>Gets the number of matrix columns, or one for non-matrix types.</summary>
    public uint Columns { get; }

    /// <summary>Gets the checked product of literal array dimensions, or one for non-array types.</summary>
    public uint ArrayLength { get; }

    /// <summary>Gets the interpolation mode and applicable interpolation modifiers.</summary>
    public string Interpolation { get; }

    /// <summary>Gets the stable identifier assigned through the SPIRV-Cross compiler API.</summary>
    public string GeneratedName { get; }
}
