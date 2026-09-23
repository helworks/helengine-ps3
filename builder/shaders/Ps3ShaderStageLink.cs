namespace helengine.ps3.builder;

/// <summary>Associates one authored fragment input with its matching vertex output and stage-local RSX channels.</summary>
public sealed class Ps3ShaderStageLink {
    /// <summary>Creates a validated immutable link between matching authored interface variables.</summary>
    /// <param name="authoredSemantic">Shared HLSL semantic, such as TEXCOORD0.</param>
    /// <param name="scalarType">SPIR-V scalar base type and bit width.</param>
    /// <param name="vectorWidth">Number of scalar components in the vector.</param>
    /// <param name="columns">Number of physical matrix columns.</param>
    /// <param name="arrayLength">Number of literal array elements.</param>
    /// <param name="interpolation">Compiler-reported interpolation mode and modifiers.</param>
    /// <param name="vertexChannel">Canonical RSX vertex-output channel.</param>
    /// <param name="pixelChannel">Canonical RSX pixel-input channel.</param>
    /// <param name="vertexSlot">Pinned SDK's stage-local vertex-output slot index.</param>
    /// <param name="pixelSlot">Pinned SDK's stage-local pixel-input slot index.</param>
    public Ps3ShaderStageLink(string authoredSemantic, string scalarType, uint vectorWidth, uint columns, uint arrayLength, string interpolation,
        string vertexChannel, string pixelChannel, uint vertexSlot, uint pixelSlot) {
        if (string.IsNullOrWhiteSpace(authoredSemantic)) throw new ArgumentException("Authored semantic is required.", nameof(authoredSemantic));
        if (string.IsNullOrWhiteSpace(scalarType)) throw new ArgumentException("Scalar type is required.", nameof(scalarType));
        if (string.IsNullOrWhiteSpace(interpolation)) throw new ArgumentException("Interpolation metadata is required.", nameof(interpolation));
        if (string.IsNullOrWhiteSpace(vertexChannel)) throw new ArgumentException("Vertex channel is required.", nameof(vertexChannel));
        if (string.IsNullOrWhiteSpace(pixelChannel)) throw new ArgumentException("Pixel channel is required.", nameof(pixelChannel));
        AuthoredSemantic = authoredSemantic;
        ScalarType = scalarType;
        VectorWidth = vectorWidth;
        Columns = columns;
        ArrayLength = arrayLength;
        Interpolation = interpolation;
        VertexChannel = vertexChannel;
        PixelChannel = pixelChannel;
        VertexSlot = vertexSlot;
        PixelSlot = pixelSlot;
    }

    /// <summary>Gets the authored HLSL semantic shared by both stages.</summary>
    public string AuthoredSemantic { get; }

    /// <summary>Gets the scalar base type and bit width reflected from SPIR-V.</summary>
    public string ScalarType { get; }

    /// <summary>Gets the number of scalar components in each vector.</summary>
    public uint VectorWidth { get; }

    /// <summary>Gets the physical matrix column count, or one for non-matrix values.</summary>
    public uint Columns { get; }

    /// <summary>Gets the literal array length, or one for non-array values.</summary>
    public uint ArrayLength { get; }

    /// <summary>Gets the compiler-reported interpolation mode and modifiers.</summary>
    public string Interpolation { get; }

    /// <summary>Gets the canonical Cg/RSX vertex-output route name.</summary>
    public string VertexChannel { get; }

    /// <summary>Gets the canonical Cg/RSX pixel-input route name.</summary>
    public string PixelChannel { get; }

    /// <summary>Gets the raw stage-local RSX vertex-output slot index.</summary>
    public uint VertexSlot { get; }

    /// <summary>Gets the raw stage-local RSX pixel-input slot index.</summary>
    public uint PixelSlot { get; }
}
