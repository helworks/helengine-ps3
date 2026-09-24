using helengine;

namespace helengine.ps3.builder;

/// <summary>Adapts the PS3 HLSL translator and RSX SDK compiler to the shared shader backend contract.</summary>
public sealed class Ps3ShaderBackend : IShaderBackend, IShaderBackendCachePolicy {
    /// <summary>Translates shared HLSL requests into immutable GLSL and SPIR-V reflection records.</summary>
    readonly Ps3ShaderSourceCompiler SourceCompiler;

    /// <summary>Compiles translated GLSL through the explicitly injected RSX SDK boundary.</summary>
    readonly IPs3RsxCompiler RsxCompiler;

    /// <summary>Caller-selected absolute parent directory for uniquely scoped SDK jobs.</summary>
    readonly string WorkingRoot;

    /// <summary>Immutable source-language capabilities advertised to shared compile callers.</summary>
    readonly ShaderBackendCapabilities BackendCapabilities;

    /// <summary>Initializes the backend with an explicit translator, SDK compiler and work root.</summary>
    /// <param name="sourceCompiler">PS3 HLSL-to-GLSL translator.</param>
    /// <param name="rsxCompiler">Injected RSX compiler, normally backed by the pinned Docker SDK.</param>
    /// <param name="workingRoot">Fully qualified parent directory reserved for compiler jobs.</param>
    public Ps3ShaderBackend(Ps3ShaderSourceCompiler sourceCompiler, IPs3RsxCompiler rsxCompiler, string workingRoot) {
        SourceCompiler = sourceCompiler ?? throw new ArgumentNullException(nameof(sourceCompiler));
        RsxCompiler = rsxCompiler ?? throw new ArgumentNullException(nameof(rsxCompiler));
        if (string.IsNullOrWhiteSpace(workingRoot)) {
            throw new ArgumentException("A fully qualified PS3 shader working root must be provided.", nameof(workingRoot));
        }
        if (!Path.IsPathFullyQualified(workingRoot)) {
            throw new ArgumentException("PS3 shader working root must be fully qualified.", nameof(workingRoot));
        }
        WorkingRoot = Path.GetFullPath(workingRoot);
        BackendCapabilities = new ShaderBackendCapabilities(
            new ShaderModel(3, 0),
            new ShaderModel(5, 0),
            Array.AsReadOnly(new[] { ShaderStage.Vertex, ShaderStage.Pixel }),
            false);
    }

    /// <summary>Gets the shared PS3 compilation target.</summary>
    public ShaderCompileTarget Target => ShaderCompileTarget.Ps3;

    /// <summary>Gets the vertex/pixel source-language range accepted by this backend.</summary>
    public ShaderBackendCapabilities Capabilities => BackendCapabilities;

    /// <summary>Gets whether the shared compile cache may be used; PS3 compilation bypasses it.</summary>
    public bool UseSharedCache => false;

    /// <summary>Compiles a shared request using no caller cancellation.</summary>
    /// <param name="request">PS3 shader compilation request.</param>
    /// <param name="includeResolver">Shared resolver passed to shaderc for active HLSL includes.</param>
    /// <returns>A shared result carrying the typed PS3 translation and validated RSX program.</returns>
    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        return Compile(request, includeResolver, CancellationToken.None);
    }

    /// <summary>Translates, validates, and compiles one PS3 vertex or pixel request.</summary>
    /// <param name="request">Shared shader request whose source, entry point and options remain authoritative.</param>
    /// <param name="includeResolver">Shared resolver used by shaderc's HLSL preprocessor.</param>
    /// <param name="cancellationToken">Token checked before translation, before SDK work and before returning.</param>
    /// <returns>A typed shared compile result with unchanged source reflection and SDK bytes.</returns>
    public Ps3ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver,
        CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(includeResolver);
        ValidateRequest(request);
        cancellationToken.ThrowIfCancellationRequested();

        Ps3ShaderTranslation translation = SourceCompiler.Translate(request, includeResolver);
        ShaderProgramDefinition definition = Ps3ShaderProgramMetadata.Create(request, translation);
        cancellationToken.ThrowIfCancellationRequested();

        Ps3RsxProgram program = RsxCompiler.Compile(translation, WorkingRoot, cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();
        if (program == null) {
            throw new InvalidOperationException("The PS3 RSX compiler returned no program.");
        }
        if (program.Stage != request.Stage) {
            throw new InvalidOperationException(
                $"The PS3 RSX compiler returned stage '{program.Stage}' for requested stage '{request.Stage}'.");
        }
        if (program.Bytecode.Length == 0) {
            throw new InvalidOperationException("The PS3 RSX compiler returned empty program bytecode.");
        }

        cancellationToken.ThrowIfCancellationRequested();
        return new Ps3ShaderCompileResult(request, definition, translation, program);
    }

    /// <summary>Rejects requests outside the backend target, stage, model or shared binding-space contract.</summary>
    /// <param name="request">Non-null request to validate before invoking the frontend.</param>
    static void ValidateRequest(ShaderCompileRequest request) {
        if (request.Target != ShaderCompileTarget.Ps3) {
            throw new NotSupportedException("The PS3 shader backend accepts PS3 compilation requests only.");
        }
        if (request.Stage != ShaderStage.Vertex && request.Stage != ShaderStage.Pixel) {
            throw new NotSupportedException("The PS3 shader backend supports vertex and pixel stages only.");
        }

        ShaderModel model = request.ShaderModel;
        if (model.Major < 3 || model.Major > 5 || (model.Major == 5 && model.Minor > 0)) {
            throw new NotSupportedException(
                $"The PS3 shader backend accepts HLSL source models from 3.0 through 5.0; received {model}.");
        }
        if (request.Options.BindingPolicy.DefaultSpace != 0) {
            throw new NotSupportedException("The PS3 shader backend supports descriptor space 0 only.");
        }
    }
}
