using helengine;

namespace helengine.ps3.builder;

/// <summary>Contains validated SDK bytecode, its unmodified compiler dump and parameter metadata.</summary>
public sealed class Ps3RsxProgram {
    /// <summary>Stores copied program data after binary and compiler-dump validation.</summary>
    /// <param name="stage">SDK vertex or pixel stage.</param>
    /// <param name="bytecode">Generated SDK bytecode to snapshot.</param>
    /// <param name="assembly">Unmodified cgcomp dump text.</param>
    /// <param name="toolchainIdentity">Identity of the pinned Docker image, compiler and Cg library.</param>
    /// <param name="parameters">Validated parameter records to snapshot.</param>
    internal Ps3RsxProgram(ShaderStage stage, byte[] bytecode, string assembly, string toolchainIdentity, IEnumerable<Ps3RsxParameter> parameters) {
        ArgumentNullException.ThrowIfNull(bytecode);
        ArgumentNullException.ThrowIfNull(parameters);
        if (string.IsNullOrWhiteSpace(assembly)) throw new ArgumentException("Compiler dump must be provided.", nameof(assembly));
        if (string.IsNullOrWhiteSpace(toolchainIdentity)) throw new ArgumentException("Toolchain identity must be provided.", nameof(toolchainIdentity));
        Stage = stage;
        BytecodeData = (byte[])bytecode.Clone();
        Assembly = assembly;
        ToolchainIdentity = toolchainIdentity;
        Parameters = Array.AsReadOnly(parameters.ToArray());
    }

    /// <summary>Gets the stage encoded by the SDK program header.</summary>
    public ShaderStage Stage { get; }

    /// <summary>Gets a fresh byte-for-byte copy of the unmodified SDK program.</summary>
    public byte[] Bytecode => (byte[])BytecodeData.Clone();

    /// <summary>Gets the exact compiler-produced dump text without postprocessing.</summary>
    public string Assembly { get; }

    /// <summary>Gets the validated identity of the Docker image, cgcomp path and mounted Cg library.</summary>
    public string ToolchainIdentity { get; }

    /// <summary>Gets the copied validated SDK parameter records.</summary>
    public IReadOnlyList<Ps3RsxParameter> Parameters { get; }

    /// <summary>Stores the private immutable snapshot used by the defensive bytecode property.</summary>
    byte[] BytecodeData { get; }
}
