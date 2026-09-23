using helengine;

namespace helengine.ps3.builder;

/// <summary>Translates, compiles, and reconciles a compatible PS3 vertex/pixel shader pair.</summary>
public sealed class Ps3ShaderPairCompiler {
    /// <summary>Translates authored HLSL into PS3 GLSL and reflection.</summary>
    readonly Ps3ShaderSourceCompiler SourceCompiler;

    /// <summary>Runs each translated stage through the pinned RSX compiler.</summary>
    readonly IPs3RsxCompiler RsxCompiler;

    /// <summary>Reconciles the stage source metadata with final Cg and SDK declarations.</summary>
    readonly Ps3ShaderBindingMapBuilder BindingMapBuilder;

    /// <summary>Creates a pair compiler with its source translator and pinned SDK executor.</summary>
    /// <param name="sourceCompiler">PS3 HLSL source translator.</param>
    /// <param name="rsxCompiler">Pinned Docker RSX compiler.</param>
    public Ps3ShaderPairCompiler(Ps3ShaderSourceCompiler sourceCompiler, IPs3RsxCompiler rsxCompiler) {
        SourceCompiler = sourceCompiler ?? throw new ArgumentNullException(nameof(sourceCompiler));
        RsxCompiler = rsxCompiler ?? throw new ArgumentNullException(nameof(rsxCompiler));
        BindingMapBuilder = new Ps3ShaderBindingMapBuilder();
    }

    /// <summary>Compiles both requests under unique stage roots and returns only a fully reconciled pair.</summary>
    /// <param name="vertexRequest">Authored vertex-stage request.</param>
    /// <param name="pixelRequest">Authored pixel-stage request.</param>
    /// <param name="includeResolver">Shared source include resolver and compiler preprocessor callback.</param>
    /// <param name="workingRoot">Explicit build-owned directory for this pair's compiler jobs.</param>
    /// <param name="cancellationToken">Token forwarded to each SDK stage job.</param>
    /// <returns>A complete pair with both SDK programs, translations and validated bindings.</returns>
    public Ps3ShaderProgramPair Compile(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest,
        IShaderIncludeResolver includeResolver, string workingRoot, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(vertexRequest);
        ArgumentNullException.ThrowIfNull(pixelRequest);
        ArgumentNullException.ThrowIfNull(includeResolver);
        if (string.IsNullOrWhiteSpace(workingRoot)) throw new ArgumentException("Explicit pair working root must be provided.", nameof(workingRoot));
        ValidateCompatibleRequests(vertexRequest, pixelRequest);
        cancellationToken.ThrowIfCancellationRequested();

        string pairRoot = CreateUniquePairDirectory(workingRoot);
        string vertexRoot = Path.Combine(pairRoot, "vertex");
        string pixelRoot = Path.Combine(pairRoot, "pixel");
        Directory.CreateDirectory(vertexRoot);
        Directory.CreateDirectory(pixelRoot);

        Ps3ShaderTranslation vertexTranslation = SourceCompiler.Translate(vertexRequest, includeResolver);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3RsxProgram vertexProgram = RsxCompiler.Compile(vertexTranslation, vertexRoot, cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3ShaderTranslation pixelTranslation = SourceCompiler.Translate(pixelRequest, includeResolver);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3RsxProgram pixelProgram = RsxCompiler.Compile(pixelTranslation, pixelRoot, cancellationToken);
        cancellationToken.ThrowIfCancellationRequested();
        Ps3ShaderBindingMap bindings = BindingMapBuilder.Build(vertexTranslation, vertexProgram, pixelTranslation, pixelProgram);
        return new Ps3ShaderProgramPair(vertexProgram, pixelProgram, vertexTranslation, pixelTranslation, bindings);
    }

    /// <summary>Rejects incompatible target, stage, program, variant, defines, shader model or shared options before work starts.</summary>
    /// <param name="vertexRequest">Vertex request candidate.</param>
    /// <param name="pixelRequest">Pixel request candidate.</param>
    static void ValidateCompatibleRequests(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest) {
        if (vertexRequest.Target != ShaderCompileTarget.Ps3 || pixelRequest.Target != ShaderCompileTarget.Ps3) {
            throw new InvalidOperationException("Both shader pair requests must target PS3.");
        }
        if (vertexRequest.Stage != ShaderStage.Vertex || pixelRequest.Stage != ShaderStage.Pixel) {
            throw new InvalidOperationException("Shader pair requests must be ordered as vertex then pixel stages.");
        }
        if (!string.Equals(vertexRequest.Variant, pixelRequest.Variant, StringComparison.Ordinal)) {
            throw new InvalidOperationException("Vertex and pixel requests must share a variant identity.");
        }
        if (vertexRequest.ShaderModel.Major != pixelRequest.ShaderModel.Major || vertexRequest.ShaderModel.Minor != pixelRequest.ShaderModel.Minor) {
            throw new InvalidOperationException("Vertex and pixel requests must use the same shader model.");
        }
        if (!HaveSameDefines(vertexRequest.Defines, pixelRequest.Defines)) {
            throw new InvalidOperationException("Vertex and pixel requests must use the same ordered preprocessor definitions.");
        }
        if (!HaveSameOptions(vertexRequest.Options, pixelRequest.Options)) {
            throw new InvalidOperationException("Vertex and pixel requests must use the same shader compilation options and binding policy.");
        }
    }

    /// <summary>Compares ordered define names and values without normalizing duplicate preprocessor definitions.</summary>
    /// <param name="vertexDefines">Vertex request definitions.</param>
    /// <param name="pixelDefines">Pixel request definitions.</param>
    /// <returns>True when both requests have identical ordered definitions.</returns>
    static bool HaveSameDefines(IReadOnlyList<ShaderDefine> vertexDefines, IReadOnlyList<ShaderDefine> pixelDefines) {
        if (vertexDefines.Count != pixelDefines.Count) return false;
        for (int index = 0; index < vertexDefines.Count; index++) {
            if (!string.Equals(vertexDefines[index].Name, pixelDefines[index].Name, StringComparison.Ordinal)
                || !string.Equals(vertexDefines[index].Value, pixelDefines[index].Value, StringComparison.Ordinal)) return false;
        }
        return true;
    }

    /// <summary>Compares every shared compilation option and binding-policy field.</summary>
    /// <param name="vertexOptions">Vertex request options.</param>
    /// <param name="pixelOptions">Pixel request options.</param>
    /// <returns>True when both options records are equivalent.</returns>
    static bool HaveSameOptions(ShaderCompileOptions vertexOptions, ShaderCompileOptions pixelOptions) {
        ShaderBindingPolicy vertexPolicy = vertexOptions.BindingPolicy;
        ShaderBindingPolicy pixelPolicy = pixelOptions.BindingPolicy;
        return vertexOptions.GenerateDebugInfo == pixelOptions.GenerateDebugInfo
            && vertexOptions.Optimize == pixelOptions.Optimize
            && vertexOptions.TreatWarningsAsErrors == pixelOptions.TreatWarningsAsErrors
            && vertexPolicy.DefaultSpace == pixelPolicy.DefaultSpace
            && vertexPolicy.ConstantBufferShift == pixelPolicy.ConstantBufferShift
            && vertexPolicy.TextureShift == pixelPolicy.TextureShift
            && vertexPolicy.SamplerShift == pixelPolicy.SamplerShift
            && vertexPolicy.StorageShift == pixelPolicy.StorageShift;
    }

    /// <summary>Creates a unique explicit pair directory below the caller's build-owned work root.</summary>
    /// <param name="workingRoot">Caller-supplied pair directory parent.</param>
    /// <returns>The newly created unique pair directory.</returns>
    static string CreateUniquePairDirectory(string workingRoot) {
        string fullWorkingRoot = Path.GetFullPath(workingRoot);
        Directory.CreateDirectory(fullWorkingRoot);
        while (true) {
            string pairRoot = Path.Combine(fullWorkingRoot, "ps3-shader-pair-" + Guid.NewGuid().ToString("N"));
            try {
                Directory.CreateDirectory(pairRoot);
                return pairRoot;
            } catch (IOException) when (Directory.Exists(pairRoot)) {
                continue;
            }
        }
    }
}
