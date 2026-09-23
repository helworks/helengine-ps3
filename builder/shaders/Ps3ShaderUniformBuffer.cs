namespace helengine.ps3.builder;

/// <summary>Describes the compiler-declared byte size and binding of one active constant-buffer structure.</summary>
public sealed class Ps3ShaderUniformBuffer {
    /// <summary>Initializes metadata captured directly from one active compiled SPIR-V structure.</summary>
    /// <param name="bufferName">Compiler-preserved constant-buffer name.</param>
    /// <param name="bufferBinding">SPIR-V binding decoration.</param>
    /// <param name="byteSize">Declared compiled structure size in bytes.</param>
    public Ps3ShaderUniformBuffer(string bufferName, uint bufferBinding, uint byteSize) {
        if (string.IsNullOrWhiteSpace(bufferName)) throw new ArgumentException("A uniform buffer name must be provided.", nameof(bufferName));
        if (byteSize == 0) throw new ArgumentOutOfRangeException(nameof(byteSize), "A declared uniform buffer size must be positive.");
        BufferName = bufferName;
        BufferBinding = bufferBinding;
        ByteSize = byteSize;
    }

    /// <summary>Gets the compiler-preserved authored buffer name.</summary>
    public string BufferName { get; }

    /// <summary>Gets the SPIR-V binding decoration used by the active buffer.</summary>
    public uint BufferBinding { get; }

    /// <summary>Gets the byte size SPIRV-Cross reports for the compiled structure type.</summary>
    public uint ByteSize { get; }
}
