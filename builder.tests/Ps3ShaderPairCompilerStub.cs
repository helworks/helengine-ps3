using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Records pair-compiler job calls and can raise cancellation at a selected stage boundary.</summary>
internal sealed class Ps3ShaderPairCompilerStub : IPs3RsxCompiler {
    /// <summary>Job call count at which the stub raises an operation-canceled exception, or zero to compile both stages.</summary>
    readonly int CancelOnCall;

    /// <summary>Records the working roots passed to each stage job.</summary>
    readonly List<string> Roots = new();

    /// <summary>Records the caller cancellation token passed to each stage job.</summary>
    readonly List<CancellationToken> Tokens = new();

    /// <summary>Creates an RSX compiler stub with optional cancellation at a particular call number.</summary>
    /// <param name="cancelOnCall">One-based call number to cancel, or zero for no cancellation.</param>
    public Ps3ShaderPairCompilerStub(int cancelOnCall = 0) {
        CancelOnCall = cancelOnCall;
    }

    /// <summary>Gets the number of stage jobs started.</summary>
    public int CallCount { get; private set; }

    /// <summary>Gets all explicit per-stage working roots.</summary>
    public IReadOnlyList<string> WorkingRoots => Roots;

    /// <summary>Gets the exact cancellation tokens received by each stage job.</summary>
    public IReadOnlyList<CancellationToken> CancellationTokens => Tokens;

    /// <summary>Returns a minimal fixed-function-only program or raises cancellation on the configured call.</summary>
    /// <param name="translation">Source translation for the stage.</param>
    /// <param name="workingRoot">Unique explicit stage job root.</param>
    /// <param name="cancellationToken">Caller cancellation token.</param>
    /// <returns>A minimal RSX program with a stage profile and fixed-function output.</returns>
    public Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken) {
        CallCount++;
        Roots.Add(workingRoot);
        Tokens.Add(cancellationToken);
        if (CallCount == CancelOnCall) throw new OperationCanceledException(cancellationToken);
        string dump = translation.Stage == ShaderStage.Vertex
            ? "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            : "#profile fp40\n#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n";
        return new Ps3RsxProgram(translation.Stage, Array.Empty<byte>(), dump, "test-sdk", Array.Empty<Ps3RsxParameter>());
    }
}
