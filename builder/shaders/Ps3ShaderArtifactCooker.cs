using helengine;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps3.builder;

/// <summary>Compiles requested PS3 shader dependencies through the shared service and publishes deterministic P3SP pairs.</summary>
public sealed class Ps3ShaderArtifactCooker {
    /// <summary>Exact shader asset identifier whose variants are defined by the shared Standard Shader table.</summary>
    const string StandardShaderAssetId = "ForwardStandardShader";

    /// <summary>Exact built-in sprite batch asset identifier whose variants come from the shared catalog.</summary>
    const string SpriteBatchShaderAssetId = "SpriteBatchShader";

    /// <summary>Graphics profile accepted by the PS3 pair cooker.</summary>
    const string StandardGraphicsProfileId = "ps3-standard";

    /// <summary>Absolute build-owned parent directory for this invocation's SDK compiler jobs.</summary>
    const string CompilerWorkDirectoryName = ".ps3-shader-work";

    /// <summary>Factory deferred until a nonempty, completely validated cook request is ready to compile.</summary>
    readonly Func<string, IShaderBackend> BackendFactory;

    /// <summary>Creates a cooker whose PS3 backend is initialized only after a valid nonempty request arrives.</summary>
    public Ps3ShaderArtifactCooker()
        : this(Ps3ShaderBackendFactory.Create) {
    }

    /// <summary>Creates a cooker with an injectable backend factory for isolated CPU tests.</summary>
    /// <param name="backendFactory">Factory receiving the explicit build-owned compiler work root.</param>
    /// <exception cref="ArgumentNullException">Thrown when the factory is missing.</exception>
    public Ps3ShaderArtifactCooker(Func<string, IShaderBackend> backendFactory) {
        BackendFactory = backendFactory ?? throw new ArgumentNullException(nameof(backendFactory));
    }

    /// <summary>Validates, compiles, reconciles and atomically writes each distinct requested program pair.</summary>
    /// <param name="request">Cook root, shader dependencies and their source snapshots.</param>
    /// <returns>Declarations for successfully published P3SP pair files in first-request order.</returns>
    /// <exception cref="ArgumentNullException">Thrown when the request is missing.</exception>
    /// <exception cref="InvalidOperationException">Thrown when a dependency, source, compiler result or selected variant is invalid.</exception>
    public PlatformShaderArtifactCookResult Cook(PlatformShaderArtifactCookRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        if (request.ShaderDependencies.Count == 0) {
            return new PlatformShaderArtifactCookResult(Array.Empty<PlatformCookedArtifactDeclaration>());
        }

        ValidateRequestContext(request);
        string cookRootPath = ValidateCookRootPath(request.CookRootPath);
        Dictionary<string, PlatformShaderArtifactCookSource> sourcesByAsset = IndexSources(request.ShaderSources);
        List<Ps3ShaderArtifactIdentity> identities = new(request.ShaderDependencies.Count);
        Dictionary<string, PlatformShaderDependency> dependenciesByPath = new(StringComparer.Ordinal);
        Dictionary<string, PlatformShaderArtifactCookSource> sourcesByPath = new(StringComparer.Ordinal);
        HashSet<string> uniquePaths = new(StringComparer.Ordinal);

        foreach (PlatformShaderDependency dependency in request.ShaderDependencies) {
            if (!dependency.HasProgramPair) {
                throw new InvalidOperationException($"PS3 shader dependency '{dependency.ShaderAssetId}' does not select a complete program pair and variant.");
            }
            if (!sourcesByAsset.TryGetValue(dependency.ShaderAssetId, out PlatformShaderArtifactCookSource source)) {
                throw new InvalidOperationException($"PS3 shader dependency '{dependency.ShaderAssetId}' has no matching source snapshot.");
            }
            ValidateSource(source);
            StandardShaderVariant variant = ResolveVariant(dependency.ShaderAssetId, dependency.VariantName);
            _ = ParseVariantDefines(variant.Defines);
            Ps3ShaderArtifactIdentity identity = new(
                dependency.ShaderAssetId,
                dependency.VertexProgramName,
                dependency.PixelProgramName,
                dependency.VariantName);
            if (uniquePaths.Add(identity.RelativePath)) {
                identities.Add(identity);
                dependenciesByPath.Add(identity.RelativePath, dependency);
                sourcesByPath.Add(identity.RelativePath, source);
            }
        }

        string workingRoot = Path.Combine(cookRootPath, CompilerWorkDirectoryName);
        IShaderBackend backend = BackendFactory(workingRoot);
        if (backend == null) {
            throw new InvalidOperationException("The PS3 shader artifact backend factory returned no backend.");
        }
        if (backend.Target != ShaderCompileTarget.Ps3) {
            throw new InvalidOperationException($"The PS3 shader artifact backend factory returned target '{backend.Target}' instead of 'Ps3'.");
        }

        Dictionary<string, byte[]> preparedArtifacts = new(StringComparer.Ordinal);
        foreach (Ps3ShaderArtifactIdentity identity in identities) {
            PlatformShaderDependency dependency = dependenciesByPath[identity.RelativePath];
            PlatformShaderArtifactCookSource source = sourcesByPath[identity.RelativePath];
            StandardShaderVariant variant = ResolveVariant(dependency.ShaderAssetId, dependency.VariantName);
            IReadOnlyList<ShaderDefine> variantDefines = ParseVariantDefines(variant.Defines);
            ShaderDefine[] defines = ShaderPlatformDefines.BuildDefines(
                ShaderCompileTarget.Ps3,
                new ShaderModel(4, 0),
                variantDefines);
            string sourceDirectory = Path.GetDirectoryName(Path.GetFullPath(source.SourcePath));
            ShaderFilesystemIncludeResolver resolver = new(sourceDirectory);
            ShaderCompileService service = new(resolver, new ShaderMemoryCompileCache(), new ShaderSourceHasher());
            service.RegisterBackend(backend);

            ShaderCompileRequest vertexRequest = CreateCompileRequest(
                source, dependency.VertexProgramName, variant.VertexEntryPoint, ShaderStage.Vertex,
                dependency.VariantName, defines);
            ShaderCompileRequest pixelRequest = CreateCompileRequest(
                source, dependency.PixelProgramName, variant.PixelEntryPoint, ShaderStage.Pixel,
                dependency.VariantName, defines);
            Ps3ShaderCompileResult vertex = CompileTyped(service, vertexRequest);
            Ps3ShaderCompileResult pixel = CompileTyped(service, pixelRequest);
            Ps3ShaderBindingMap bindings = new Ps3ShaderBindingMapBuilder().Build(
                vertex.Translation,
                vertex.Program,
                pixel.Translation,
                pixel.Program);
            Ps3ShaderProgramPair pair = new(
                vertex.Program,
                pixel.Program,
                vertex.Translation,
                pixel.Translation,
                bindings);
            byte[] bytes = Ps3ShaderArtifactWriter.Write(
                identity,
                source.SourceHash,
                variant.VertexEntryPoint,
                variant.PixelEntryPoint,
                pair);
            preparedArtifacts.Add(identity.RelativePath, bytes);
        }

        List<PlatformCookedArtifactDeclaration> declarations = new(identities.Count);
        foreach (Ps3ShaderArtifactIdentity identity in identities) {
            string finalPath = Path.Combine(cookRootPath, identity.RelativePath.Replace('/', Path.DirectorySeparatorChar));
            WriteArtifactAtomically(finalPath, preparedArtifacts[identity.RelativePath]);
            declarations.Add(new PlatformCookedArtifactDeclaration(
                relativePath: identity.RuntimeRelativePath,
                logicalArtifactId: identity.RelativePath,
                "shader",
                identity.VariantName));
        }

        return new PlatformShaderArtifactCookResult(declarations);
    }

    /// <summary>Checks the selected platform and optional graphics profile before backend construction.</summary>
    /// <param name="request">Nonempty shader cook request.</param>
    static void ValidateRequestContext(PlatformShaderArtifactCookRequest request) {
        if (!string.Equals(request.PlatformId, "ps3", StringComparison.Ordinal)) {
            throw new InvalidOperationException($"PS3 shader cooking requires platform 'ps3', received '{request.PlatformId}'.");
        }
        if (!string.IsNullOrWhiteSpace(request.GraphicsProfileId)
            && !string.Equals(request.GraphicsProfileId, StandardGraphicsProfileId, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"PS3 shader cooking requires graphics profile '{StandardGraphicsProfileId}', received '{request.GraphicsProfileId}'.");
        }
    }

    /// <summary>Requires and normalizes the explicit absolute artifact root before any backend work can begin.</summary>
    /// <param name="cookRootPath">Root directory supplied by the platform cook request.</param>
    /// <returns>Normalized fully-qualified artifact root used for compiler jobs and final artifacts.</returns>
    static string ValidateCookRootPath(string cookRootPath) {
        if (string.IsNullOrWhiteSpace(cookRootPath) || !Path.IsPathFullyQualified(cookRootPath)) {
            throw new InvalidOperationException($"PS3 shader cooking requires a fully qualified CookRootPath, received '{cookRootPath}'.");
        }

        return Path.GetFullPath(cookRootPath);
    }

    /// <summary>Indexes the exact authored source snapshots included with the cook request.</summary>
    /// <param name="sources">Source snapshots keyed by shader asset identifier.</param>
    /// <returns>Ordinal asset-id lookup for requested shader sources.</returns>
    static Dictionary<string, PlatformShaderArtifactCookSource> IndexSources(
        IReadOnlyList<PlatformShaderArtifactCookSource> sources) {
        Dictionary<string, PlatformShaderArtifactCookSource> result = new(StringComparer.Ordinal);
        foreach (PlatformShaderArtifactCookSource source in sources) {
            if (!result.TryAdd(source.ShaderAssetId, source)) {
                throw new InvalidOperationException($"PS3 shader source '{source.ShaderAssetId}' was supplied more than once.");
            }
        }
        return result;
    }

    /// <summary>Requires source text, hash provenance, a source path, and its existing parent directory.</summary>
    /// <param name="source">Resolved authored source snapshot.</param>
    static void ValidateSource(PlatformShaderArtifactCookSource source) {
        if (!source.HasSourcePath || string.IsNullOrWhiteSpace(source.SourcePath)) {
            throw new InvalidOperationException($"PS3 shader source '{source.ShaderAssetId}' has no source path provenance.");
        }
        if (!Path.IsPathFullyQualified(source.SourcePath)) {
            throw new InvalidOperationException($"PS3 shader source '{source.ShaderAssetId}' requires a fully qualified source path, received '{source.SourcePath}'.");
        }
        if (string.IsNullOrWhiteSpace(source.SourceHash)) {
            throw new InvalidOperationException($"PS3 shader source '{source.ShaderAssetId}' has no source hash provenance.");
        }
        _ = Ps3ShaderArtifactIdentity.ValidateText(source.SourceHash, "sourceHash");
        if (string.IsNullOrWhiteSpace(source.SourceText)) {
            throw new InvalidOperationException($"PS3 shader source '{source.ShaderAssetId}' has no source text.");
        }

        string sourceDirectory = Path.GetDirectoryName(Path.GetFullPath(source.SourcePath));
        if (string.IsNullOrWhiteSpace(sourceDirectory) || !Directory.Exists(sourceDirectory)) {
            throw new InvalidOperationException($"PS3 shader source parent directory for '{source.ShaderAssetId}' does not exist: '{sourceDirectory}'.");
        }
    }

    /// <summary>Resolves exact shared built-in variants or the narrow custom VS/PS default convention.</summary>
    /// <param name="shaderAssetId">Authored shader asset identity.</param>
    /// <param name="variantName">Exact requested variant name.</param>
    /// <returns>Shared entry points and source defines for the selected variant.</returns>
    static StandardShaderVariant ResolveVariant(string shaderAssetId, string variantName) {
        if (string.Equals(shaderAssetId, StandardShaderAssetId, StringComparison.Ordinal)) {
            foreach (StandardShaderVariant variant in StandardShaderVariants.All) {
                if (string.Equals(variant.Name, variantName, StringComparison.Ordinal)) {
                    return variant;
                }
            }
            throw new InvalidOperationException($"PS3 Standard Shader variant '{variantName}' is not supported.");
        }
        if (string.Equals(shaderAssetId, SpriteBatchShaderAssetId, StringComparison.Ordinal)) {
            foreach (StandardShaderVariant variant in SpriteBatchShaderVariants.All) {
                if (string.Equals(variant.Name, variantName, StringComparison.Ordinal)) {
                    return variant;
                }
            }
            throw new InvalidOperationException($"PS3 Sprite Batch Shader variant '{variantName}' is not supported.");
        }
        if (!string.Equals(variantName, "default", StringComparison.Ordinal)) {
            throw new InvalidOperationException($"PS3 custom shader asset '{shaderAssetId}' supports the exact 'default' variant only, received '{variantName}'.");
        }

        return new StandardShaderVariant("default", "VS", "PS", new List<string>());
    }

    /// <summary>Parses shared variant definitions at the first equals sign and rejects missing names or values.</summary>
    /// <param name="definitions">Shared Standard Shader definition strings.</param>
    /// <returns>Typed additional preprocessor definitions in shared order.</returns>
    static IReadOnlyList<ShaderDefine> ParseVariantDefines(IReadOnlyList<string> definitions) {
        List<ShaderDefine> result = new(definitions.Count);
        foreach (string definition in definitions) {
            int separator = definition.IndexOf('=');
            string name = separator < 0 ? definition : definition[..separator];
            string value = separator < 0 ? "1" : definition[(separator + 1)..];
            if (string.IsNullOrWhiteSpace(name) || string.IsNullOrWhiteSpace(value)) {
                throw new InvalidOperationException($"PS3 shader variant contains an invalid define string '{definition}'.");
            }
            result.Add(new ShaderDefine(name, value));
        }
        return result;
    }

    /// <summary>Creates one shared request from the exact source snapshot and selected stage metadata.</summary>
    /// <param name="source">Request-owned shader source snapshot.</param>
    /// <param name="programName">Logical program name from the material dependency.</param>
    /// <param name="entryPoint">Shared variant entry point for the requested stage.</param>
    /// <param name="stage">Vertex or pixel stage.</param>
    /// <param name="variantName">Exact dependency variant name.</param>
    /// <param name="defines">PS3 target, shader model and variant defines.</param>
    /// <returns>A source-provenance-preserving PS3 compile request.</returns>
    static ShaderCompileRequest CreateCompileRequest(
        PlatformShaderArtifactCookSource source,
        string programName,
        string entryPoint,
        ShaderStage stage,
        string variantName,
        IReadOnlyList<ShaderDefine> defines) => new(
            new ShaderSourceInfo(source.SourcePath, source.SourceText),
            programName,
            entryPoint,
            stage,
            ShaderCompileTarget.Ps3,
            new ShaderModel(4, 0),
            variantName,
            defines,
            new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));

    /// <summary>Requires one successful typed result whose complete shared request matches the request sent to the backend.</summary>
    /// <param name="service">Per-source shared compile service.</param>
    /// <param name="request">Exact request submitted to that service.</param>
    /// <returns>Typed immutable PS3 translation and SDK program.</returns>
    static Ps3ShaderCompileResult CompileTyped(ShaderCompileService service, ShaderCompileRequest request) {
        ShaderCompileResult result = service.Compile(request);
        if (result is not Ps3ShaderCompileResult typed || !result.Success) {
            throw new InvalidOperationException("PS3 cooking requires a successful typed PS3 result.");
        }
        if (!RequestsMatch(request, typed.Request)) {
            throw new InvalidOperationException("The PS3 backend returned a compile result for a different shader request.");
        }
        if (typed.Translation == null || typed.Program == null) {
            throw new InvalidOperationException("The PS3 backend returned an incomplete typed compile result.");
        }
        return typed;
    }

    /// <summary>Compares all request identity, source, define, model and option values before accepting a backend result.</summary>
    /// <param name="expected">Submitted shared request.</param>
    /// <param name="actual">Request stored in the returned compiler result.</param>
    /// <returns>True only when both requests describe the same compile operation.</returns>
    static bool RequestsMatch(ShaderCompileRequest expected, ShaderCompileRequest actual) {
        if (actual == null
            || !string.Equals(expected.Source.Path, actual.Source.Path, StringComparison.Ordinal)
            || !string.Equals(expected.Source.Source, actual.Source.Source, StringComparison.Ordinal)
            || !string.Equals(expected.ProgramName, actual.ProgramName, StringComparison.Ordinal)
            || !string.Equals(expected.EntryPoint, actual.EntryPoint, StringComparison.Ordinal)
            || expected.Stage != actual.Stage
            || expected.Target != actual.Target
            || expected.ShaderModel.Major != actual.ShaderModel.Major
            || expected.ShaderModel.Minor != actual.ShaderModel.Minor
            || !string.Equals(expected.Variant, actual.Variant, StringComparison.Ordinal)
            || expected.Options.GenerateDebugInfo != actual.Options.GenerateDebugInfo
            || expected.Options.Optimize != actual.Options.Optimize
            || expected.Options.TreatWarningsAsErrors != actual.Options.TreatWarningsAsErrors
            || !BindingPoliciesMatch(expected.Options.BindingPolicy, actual.Options.BindingPolicy)
            || expected.Defines.Count != actual.Defines.Count) {
            return false;
        }

        for (int index = 0; index < expected.Defines.Count; index++) {
            if (!string.Equals(expected.Defines[index].Name, actual.Defines[index].Name, StringComparison.Ordinal)
                || !string.Equals(expected.Defines[index].Value, actual.Defines[index].Value, StringComparison.Ordinal)) {
                return false;
            }
        }
        return true;
    }

    /// <summary>Compares the five binding-policy fields carried by a compile request.</summary>
    /// <param name="expected">Submitted binding policy.</param>
    /// <param name="actual">Result request binding policy.</param>
    /// <returns>True when every resource space and shift agrees.</returns>
    static bool BindingPoliciesMatch(ShaderBindingPolicy expected, ShaderBindingPolicy actual) =>
        expected.DefaultSpace == actual.DefaultSpace
        && expected.ConstantBufferShift == actual.ConstantBufferShift
        && expected.TextureShift == actual.TextureShift
        && expected.SamplerShift == actual.SamplerShift
        && expected.StorageShift == actual.StorageShift;

    /// <summary>Writes one completed artifact through a unique sibling file and replaces only its final owned path.</summary>
    /// <param name="finalPath">Hash-derived final artifact path.</param>
    /// <param name="bytes">Complete serialized P3SP bytes.</param>
    static void WriteArtifactAtomically(string finalPath, byte[] bytes) {
        string directory = Path.GetDirectoryName(finalPath);
        Directory.CreateDirectory(directory);
        string temporaryPath = Path.Combine(
            directory,
            "." + Path.GetFileName(finalPath) + "." + Guid.NewGuid().ToString("N") + ".tmp");
        try {
            File.WriteAllBytes(temporaryPath, bytes);
            File.Move(temporaryPath, finalPath, true);
        } finally {
            if (File.Exists(temporaryPath)) {
                File.Delete(temporaryPath);
            }
        }
    }
}
