using helengine;

namespace helengine.ps3.builder;

/// <summary>Combines shared shader metadata with the immutable PS3 source translation and RSX program.</summary>
public sealed class Ps3ShaderCompileResult : ShaderCompileResult {
    /// <summary>Compiler-produced GLSL and physical SPIR-V reflection retained for pair reconciliation.</summary>
    public Ps3ShaderTranslation Translation { get; }

    /// <summary>Validated immutable program returned by the injected PS3 RSX compiler.</summary>
    public Ps3RsxProgram Program { get; }

    /// <summary>Creates a shared result whose base binary is an exact copy of the validated SDK bytes.</summary>
    /// <param name="request">Original shared compilation request.</param>
    /// <param name="definition">Shared logical metadata derived from the translation.</param>
    /// <param name="translation">Unmodified frontend output and physical reflection.</param>
    /// <param name="program">Validated RSX program produced for the requested stage.</param>
    internal Ps3ShaderCompileResult(ShaderCompileRequest request, ShaderProgramDefinition definition,
        Ps3ShaderTranslation translation, Ps3RsxProgram program)
        : base(request, definition, CreateBinary(request, program), Array.Empty<ShaderCompileDiagnostic>(), true) {
        Translation = translation ?? throw new ArgumentNullException(nameof(translation));
        Program = program ?? throw new ArgumentNullException(nameof(program));
        if (Program.Stage != request.Stage) {
            throw new ArgumentException("The RSX program stage must match the original shader request.", nameof(program));
        }
    }

    /// <summary>Copies program identity and SDK bytes into the shared compile result binary.</summary>
    /// <param name="request">Original shared request.</param>
    /// <param name="program">Validated SDK program.</param>
    /// <returns>A shared binary with the exact stage payload.</returns>
    static ShaderCompiledBinary CreateBinary(ShaderCompileRequest request, Ps3RsxProgram program) {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(program);
        if (program.Stage != request.Stage) {
            throw new ArgumentException("The RSX program stage must match the original shader request.", nameof(program));
        }
        byte[] bytecode = program.Bytecode;
        if (bytecode.Length == 0) {
            throw new ArgumentException("The RSX program must contain nonempty SDK bytecode.", nameof(program));
        }
        return new ShaderCompiledBinary(
            ShaderCompileTarget.Ps3,
            request.Stage,
            request.EntryPoint,
            request.Variant,
            bytecode);
    }
}
