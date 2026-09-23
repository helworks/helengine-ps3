namespace helengine.ps3.builder;

/// <summary>Describes one active uniform-buffer member and its declared byte layout.</summary>
public sealed class Ps3ShaderUniformMember {
    /// <summary>Initializes a uniform-member record with compiler-reported identity, shape and layout.</summary>
    /// <param name="bufferName">Authored constant-buffer name.</param>
    /// <param name="bufferBinding">SPIR-V binding decoration for the constant buffer.</param>
    /// <param name="memberPath">Complete member path within the buffer, including nested fields.</param>
    /// <param name="byteOffset">Member byte offset relative to the containing buffer.</param>
    /// <param name="byteSize">Declared member size in bytes.</param>
    /// <param name="arrayStride">Declared array stride in bytes, or zero when not an array.</param>
    /// <param name="matrixStride">Physical SPIR-V stride in bytes between major matrix vectors, or zero when not a matrix.</param>
    /// <param name="rowMajor">Whether SPIR-V declares row-major physical storage; this may be opposite the HLSL modifier.</param>
    /// <param name="generatedName">Stable compiler-assigned, fully qualified GLSL uniform-member path.</param>
    /// <param name="scalarType">SPIR-V scalar base type and bit width.</param>
    /// <param name="vectorWidth">Number of scalar components per vector.</param>
    /// <param name="columns">Number of matrix columns.</param>
    /// <param name="arrayLength">Product of literal array dimensions, or one for a non-array member.</param>
    public Ps3ShaderUniformMember(
        string bufferName,
        uint bufferBinding,
        string memberPath,
        uint byteOffset,
        uint byteSize,
        uint arrayStride,
        uint matrixStride,
        bool rowMajor,
        string generatedName,
        string scalarType,
        uint vectorWidth,
        uint columns,
        uint arrayLength) {
        if (string.IsNullOrWhiteSpace(bufferName)) throw new ArgumentException("Buffer name must be provided.", nameof(bufferName));
        if (string.IsNullOrWhiteSpace(memberPath)) throw new ArgumentException("Complete member path must be provided.", nameof(memberPath));
        if (string.IsNullOrWhiteSpace(generatedName)) throw new ArgumentException("Generated GLSL name must be provided.", nameof(generatedName));
        if (string.IsNullOrWhiteSpace(scalarType)) throw new ArgumentException("Scalar type must be provided.", nameof(scalarType));
        BufferName = bufferName;
        BufferBinding = bufferBinding;
        MemberPath = memberPath;
        ByteOffset = byteOffset;
        ByteSize = byteSize;
        ArrayStride = arrayStride;
        MatrixStride = matrixStride;
        RowMajor = rowMajor;
        GeneratedName = generatedName;
        ScalarType = scalarType;
        VectorWidth = vectorWidth;
        Columns = columns;
        ArrayLength = arrayLength;
    }

    /// <summary>Gets the authored constant-buffer identifier.</summary>
    public string BufferName { get; }

    /// <summary>Gets the SPIR-V resource binding assigned to the constant buffer.</summary>
    public uint BufferBinding { get; }

    /// <summary>Gets the complete nested field path within the constant buffer.</summary>
    public string MemberPath { get; }

    /// <summary>Gets the byte offset within the constant buffer.</summary>
    public uint ByteOffset { get; }

    /// <summary>Gets the declared size in bytes of this member.</summary>
    public uint ByteSize { get; }

    /// <summary>Gets the compiler-declared stride between array elements, or zero for non-arrays.</summary>
    public uint ArrayStride { get; }

    /// <summary>Gets the compiler-declared physical stride between major matrix vectors, or zero for non-matrices.</summary>
    public uint MatrixStride { get; }

    /// <summary>
    /// Gets whether SPIR-V declares row-major physical matrix storage. This is the SPIR-V decoration reported by SPIRV-Cross,
    /// not the authored HLSL modifier: glslang maps HLSL Mrc and SPIR-V Mcr matrix dimensions in opposite order and reverses
    /// the row/column-major sense when lowering HLSL layout.
    /// </summary>
    public bool RowMajor { get; }

    /// <summary>Gets the stable fully qualified GLSL member path assigned through the SPIRV-Cross compiler API.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the scalar base type and bit width reported by SPIRV-Cross.</summary>
    public string ScalarType { get; }

    /// <summary>Gets the physical SPIR-V vector width, which can differ from the authored HLSL matrix row count.</summary>
    public uint VectorWidth { get; }

    /// <summary>Gets the physical SPIR-V matrix column count, or one for non-matrix values.</summary>
    public uint Columns { get; }

    /// <summary>Gets the checked product of literal array dimensions, or one for non-array values.</summary>
    public uint ArrayLength { get; }
}
