using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Records shared shader requests and supplies typed immutable PS3 results without invoking the SDK.</summary>
internal sealed class Ps3ShaderCookBackendStub : IShaderBackend, IShaderBackendCachePolicy {
    /// <summary>Shared requests received by this backend in compile order.</summary>
    readonly List<ShaderCompileRequest> RequestValues = [];

    /// <summary>Include resolvers received by this backend in compile order.</summary>
    readonly List<IShaderIncludeResolver> ResolverValues = [];

    /// <summary>Results returned by include lookups explicitly requested by this test double.</summary>
    readonly List<ShaderIncludeResult> ResolvedIncludeValues = [];

    /// <summary>Immutable vertex and pixel capabilities advertised by the test backend.</summary>
    readonly ShaderBackendCapabilities BackendCapabilities = new(
        new ShaderModel(3, 0),
        new ShaderModel(5, 0),
        Array.AsReadOnly(new[] { ShaderStage.Vertex, ShaderStage.Pixel }),
        false);

    /// <summary>Gets or sets the backend target returned for lazy-factory validation tests.</summary>
    public ShaderCompileTarget Target { get; set; } = ShaderCompileTarget.Ps3;

    /// <summary>Gets the immutable test capability declaration.</summary>
    public ShaderBackendCapabilities Capabilities => BackendCapabilities;

    /// <summary>Gets whether the shared compile cache must bypass this deterministic result provider.</summary>
    public bool UseSharedCache => false;

    /// <summary>Gets requests in the exact order received by the fake backend.</summary>
    public IReadOnlyList<ShaderCompileRequest> Requests => RequestValues;

    /// <summary>Gets the include resolver reference received with each request.</summary>
    public IReadOnlyList<IShaderIncludeResolver> Resolvers => ResolverValues;

    /// <summary>Gets include results requested by setting IncludePathToResolve.</summary>
    public IReadOnlyList<ShaderIncludeResult> ResolvedIncludes => ResolvedIncludeValues;

    /// <summary>Gets or sets an include path that Compile resolves through the supplied resolver.</summary>
    public string IncludePathToResolve { get; set; }

    /// <summary>Gets or sets the one-based backend call that should throw Failure.</summary>
    public int FailureOnCall { get; set; }

    /// <summary>Gets or sets the exception raised on FailureOnCall.</summary>
    public Exception Failure { get; set; }

    /// <summary>Gets or sets whether a generic shared result should replace the required PS3 result.</summary>
    public bool ReturnUntypedResult { get; set; }

    /// <summary>Gets or sets the Success flag for a deliberately generic shared result.</summary>
    public bool UntypedResultSuccess { get; set; } = true;

    /// <summary>Gets or sets a transform that creates a deliberately unrelated result request.</summary>
    public Func<ShaderCompileRequest, ShaderCompileRequest> ReturnedRequestFactory { get; set; }

    /// <summary>Compiles one request by recording its identity, exercising includes when requested, and returning a test DTO.</summary>
    /// <param name="request">Shared PS3 request.</param>
    /// <param name="includeResolver">Filesystem or test resolver supplied by the service.</param>
    /// <returns>A typed immutable PS3 compile result unless a negative case is configured.</returns>
    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        RequestValues.Add(request);
        ResolverValues.Add(includeResolver);
        if (!string.IsNullOrWhiteSpace(IncludePathToResolve)) {
            ResolvedIncludeValues.Add(includeResolver.Resolve(request.Source.Path, IncludePathToResolve));
        }
        if (FailureOnCall == RequestValues.Count) {
            throw Failure ?? new InvalidOperationException("Configured fake PS3 backend failure.");
        }

        ShaderCompileRequest resultRequest = ReturnedRequestFactory == null ? request : ReturnedRequestFactory(request);
        if (ReturnUntypedResult) {
            Ps3ShaderTranslation translation = CreateTranslation(resultRequest.Stage);
            ShaderProgramDefinition definition = Ps3ShaderProgramMetadata.Create(resultRequest, translation);
            ShaderCompiledBinary binary = new(
                resultRequest.Target,
                resultRequest.Stage,
                resultRequest.EntryPoint,
                resultRequest.Variant,
                new byte[] { 0x51, 0x53, 0x33 });
            return new ShaderCompileResult(
                resultRequest,
                definition,
                binary,
                Array.Empty<ShaderCompileDiagnostic>(),
                UntypedResultSuccess);
        }

        Ps3ShaderTranslation typedTranslation = CreateTranslation(resultRequest.Stage);
        Ps3RsxProgram program = CreateProgram(resultRequest.Stage);
        ShaderProgramDefinition typedDefinition = Ps3ShaderProgramMetadata.Create(resultRequest, typedTranslation);
        return new Ps3ShaderCompileResult(resultRequest, typedDefinition, typedTranslation, program);
    }

    /// <summary>Creates the minimum immutable source translation accepted by pair reconciliation.</summary>
    /// <param name="stage">Stage requested by the shared service.</param>
    /// <returns>A translation with the required vertex Position built-in and no optional resources.</returns>
    static Ps3ShaderTranslation CreateTranslation(ShaderStage stage) => new(
        stage,
        "void main() {}",
        Array.Empty<string>(),
        stage == ShaderStage.Vertex ? new[] { "Position" } : Array.Empty<string>(),
        Array.Empty<Ps3ShaderInterfaceVariable>(),
        Array.Empty<Ps3ShaderInterfaceVariable>(),
        Array.Empty<Ps3ShaderUniformMember>(),
        Array.Empty<Ps3ShaderSamplerBinding>(),
        Array.Empty<Ps3ShaderSourceDependency>(),
        Array.Empty<Ps3ShaderUniformBuffer>());

    /// <summary>Creates a stage-labelled PS3 program and the minimal active Cg routing declaration.</summary>
    /// <param name="stage">Stage requested by the shared service.</param>
    /// <returns>A nonempty immutable test program compatible with empty reflected resources.</returns>
    static Ps3RsxProgram CreateProgram(ShaderStage stage) {
        string assembly = stage == ShaderStage.Vertex
            ? "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            : "#profile fp40\n#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n";
        return new Ps3RsxProgram(stage, new byte[] { 0x51, 0x53, 0x33 }, assembly, "test-toolchain", Array.Empty<Ps3RsxParameter>());
    }
}
