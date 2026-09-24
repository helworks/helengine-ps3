using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Records backend SDK calls and supplies immutable bytes or a caller-selected compiler failure.</summary>
internal sealed class Ps3ShaderBackendCompilerStub : IPs3RsxCompiler, IShaderIncludeResolver {
    /// <summary>Copies of the source translations received at the SDK boundary.</summary>
    readonly List<Ps3ShaderTranslation> TranslationValues = new();

    /// <summary>Explicit roots received for SDK jobs.</summary>
    readonly List<string> WorkingRootValues = new();

    /// <summary>Caller cancellation tokens received for SDK jobs.</summary>
    readonly List<CancellationToken> CancellationTokenValues = new();

    /// <summary>Private immutable bytes returned for each accepted SDK call.</summary>
    readonly byte[] Bytecode;

    /// <summary>Optional stage reported by the test SDK instead of the translation stage.</summary>
    readonly ShaderStage? StageOverride;

    /// <summary>Optional exception raised after recording the SDK call.</summary>
    readonly Exception Failure;

    /// <summary>Initializes the compiler double with optional bytes, output stage and failure.</summary>
    /// <param name="bytecode">Nonempty payload returned by default, or empty to test rejection.</param>
    /// <param name="stageOverride">Optional deliberately incorrect output stage.</param>
    /// <param name="failure">Optional exact exception instance to propagate.</param>
    public Ps3ShaderBackendCompilerStub(byte[] bytecode = null, ShaderStage? stageOverride = null,
        Exception failure = null) {
        Bytecode = bytecode == null ? new byte[] { 0x50, 0x53, 0x33, 0x01, 0x02, 0x03 } : (byte[])bytecode.Clone();
        StageOverride = stageOverride;
        Failure = failure;
    }

    /// <summary>Gets the number of SDK calls received.</summary>
    public int CallCount { get; private set; }

    /// <summary>Gets each immutable source translation passed into the SDK boundary.</summary>
    public IReadOnlyList<Ps3ShaderTranslation> Translations => TranslationValues;

    /// <summary>Gets each explicit working root passed into the SDK boundary.</summary>
    public IReadOnlyList<string> WorkingRoots => WorkingRootValues;

    /// <summary>Gets the cancellation tokens passed into the SDK boundary.</summary>
    public IReadOnlyList<CancellationToken> CancellationTokens => CancellationTokenValues;

    /// <summary>Gets or sets an action invoked after recording and before returning an SDK result.</summary>
    public Action OnCompile { get; set; }

    /// <summary>Gets or sets an action invoked when the frontend resolves an include through this test double.</summary>
    public Action OnResolve { get; set; }

    /// <summary>Gets the number of include resolutions received by this test double.</summary>
    public int ResolveCallCount { get; private set; }

    /// <summary>Resolves one include with valid HLSL and invokes the optional cancellation hook.</summary>
    /// <param name="requestingFile">Source path issuing the include request.</param>
    /// <param name="includePath">Include spelling from the HLSL source.</param>
    /// <returns>Stable include contents for the real shaderc preprocessor.</returns>
    public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
        ResolveCallCount++;
        OnResolve?.Invoke();
        return new ShaderIncludeResult(includePath, "float4 IncludedColor() { return float4(1.0, 0.0, 0.0, 1.0); }");
    }

    /// <summary>Records the SDK call and returns immutable program bytes or raises the configured exception.</summary>
    /// <param name="translation">Real source translation from shaderc and SPIRV-Cross.</param>
    /// <param name="workingRoot">Explicit caller-owned SDK work root.</param>
    /// <param name="cancellationToken">Caller token forwarded by the backend.</param>
    /// <returns>A program snapshot with a valid stage label and the configured immutable payload.</returns>
    public Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot,
        CancellationToken cancellationToken) {
        CallCount++;
        TranslationValues.Add(translation);
        WorkingRootValues.Add(workingRoot);
        CancellationTokenValues.Add(cancellationToken);
        OnCompile?.Invoke();
        if (Failure != null) throw Failure;
        ShaderStage stage = StageOverride ?? translation.Stage;
        string assembly = stage == ShaderStage.Vertex ? "#profile vp40\n" : "#profile fp40\n";
        return new Ps3RsxProgram(stage, Bytecode, assembly, "recording-ps3-sdk", Array.Empty<Ps3RsxParameter>());
    }
}
