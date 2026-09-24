using helengine.baseplatform.Builders;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine;

namespace helengine.ps3.builder;

/// <summary>Publishes the PlayStation 3 platform contract and delegates native execution behind an injectable seam.</summary>
public sealed class Ps3PlatformAssetBuilder : IPlatformAssetBuilder, IPlatformShaderArtifactBuilder, IPlatformRendererShaderDependencyProvider, IShaderBackendRegistryContributor {
    /// <summary>Stores the immutable renderer-owned sprite batch shader variants declared by this platform.</summary>
    static readonly IReadOnlyList<PlatformShaderDependency> RendererShaderDependenciesValue = Array.AsReadOnly(new PlatformShaderDependency[] {
        new PlatformShaderDependency("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", "Textured"),
        new PlatformShaderDependency("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", "RoundedShape")
    });

    /// <summary>Executor that performs the native build phase.</summary>
    readonly IPs3NativeBuildExecutor NativeBuildExecutor;

    /// <summary>Cooks requested PS3 shader dependencies into deterministic pair artifacts.</summary>
    readonly Ps3ShaderArtifactCooker ShaderCooker;

    /// <summary>Translates the authored material schema into the cooked platform payload.</summary>
    readonly Ps3MaterialCooker MaterialCooker = new();

    /// <summary>Creates a builder using the production native executor.</summary>
    public Ps3PlatformAssetBuilder()
        : this(new Ps3NativeBuildExecutor(), new Ps3ShaderArtifactCooker()) {
    }

    /// <summary>Creates a builder using a caller-provided native executor.</summary>
    /// <param name="nativeBuildExecutor">Executor used for the native build phase.</param>
    /// <exception cref="ArgumentNullException">Thrown when the executor is missing.</exception>
    public Ps3PlatformAssetBuilder(IPs3NativeBuildExecutor nativeBuildExecutor)
        : this(nativeBuildExecutor, new Ps3ShaderArtifactCooker()) {
    }

    /// <summary>Creates a builder with injected native execution and shader cooking capabilities.</summary>
    /// <param name="nativeBuildExecutor">Executor used for the native build phase.</param>
    /// <param name="shaderCooker">Cooker used for requested shader pair artifacts.</param>
    /// <exception cref="ArgumentNullException">Thrown when the executor or cooker is missing.</exception>
    public Ps3PlatformAssetBuilder(IPs3NativeBuildExecutor nativeBuildExecutor, Ps3ShaderArtifactCooker shaderCooker) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
        ShaderCooker = shaderCooker ?? throw new ArgumentNullException(nameof(shaderCooker));
        Descriptor = new PlatformBuilderDescriptor(
            "helengine.ps3.builder",
            "0.2.0",
            "ps3",
            // Deliberately wide, matching the Dreamcast builder: an isolated engine build and
            // the shared engine report different version strings, and discovery enforces this
            // range against whichever engine is actually running.
            new EngineCompatibilityRange("0.4.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["ps3"],
            ["ps3"]);
        Definition = Ps3PlatformDefinitionFactory.Create();
    }

    /// <summary>Gets the discovery descriptor for this builder.</summary>
    public PlatformBuilderDescriptor Descriptor { get; }

    /// <summary>Gets the typed editor-facing platform definition.</summary>
    public helengine.baseplatform.Definitions.PlatformDefinition Definition { get; }

    /// <summary>Gets the immutable shader program pairs required by the PS3 renderer for sprite batching.</summary>
    public IReadOnlyList<PlatformShaderDependency> RendererShaderDependencies => RendererShaderDependenciesValue;

    /// <summary>
    /// Cooks one authored material into the platform-owned payload the PlayStation 3 renderer
    /// reads. The boot gate draws no authored geometry yet, so these materials are packaged
    /// rather than drawn, in the same way authored meshes and cameras already are.
    /// </summary>
    /// <param name="request">Material request from the build graph.</param>
    /// <returns>Serialized platform material bytes with no shader dependencies.</returns>
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) {
        return MaterialCooker.Cook(request);
    }

    /// <summary>Cooks the requested shader dependencies into explicitly declared PS3 pair files.</summary>
    /// <param name="request">Cook root, dependency identities and their resolved source snapshots.</param>
    /// <returns>Declarations for every successfully published shader pair.</returns>
    public PlatformShaderArtifactCookResult CookShaderArtifacts(PlatformShaderArtifactCookRequest request) {
        return ShaderCooker.Cook(request);
    }

    /// <summary>Registers the side-effect-free PS3 backend adapter for later shared-service compilation.</summary>
    /// <param name="shaderBackendRegistry">Registry that receives the lazy PS3 backend.</param>
    /// <exception cref="ArgumentNullException">Thrown when the registry is missing.</exception>
    public void RegisterShaderBackends(ShaderBackendRegistry shaderBackendRegistry) {
        ArgumentNullException.ThrowIfNull(shaderBackendRegistry);
        shaderBackendRegistry.Register(new Ps3LazyShaderBackend());
    }

    /// <summary>
    /// Stages the declared cooked content, writes the runtime scene catalog beside generated
    /// Core, then compiles both into the native executable.
    /// </summary>
    /// <param name="request">Resolved graph build request.</param>
    /// <param name="progressReporter">Progress callback.</param>
    /// <param name="diagnosticReporter">Diagnostic callback.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    /// <returns>Final scene and loose-asset outcomes.</returns>
    /// <exception cref="ArgumentNullException">Thrown when a required argument is missing.</exception>
    public async Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (progressReporter == null) {
            throw new ArgumentNullException(nameof(progressReporter));
        } else if (diagnosticReporter == null) {
            throw new ArgumentNullException(nameof(diagnosticReporter));
        }

        List<PlatformBuildDiagnostic> diagnostics = [];
        try {
            cancellationToken.ThrowIfCancellationRequested();
            if (!string.Equals(request.Manifest.PlatformName, "ps3", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"PlayStation 3 builder received manifest for platform '{request.Manifest.PlatformName}'.");
            }
            if (string.IsNullOrWhiteSpace(request.GeneratedCoreCppRootPath)) {
                throw new InvalidOperationException("PlayStation 3 Core build requires GeneratedCoreCppRootPath.");
            }
            Directory.CreateDirectory(request.OutputRoot);

            // Content is staged before the native build, because the build installs the staged
            // tree beside the executable and compiles the catalog that names its scenes.
            string packageSourceRoot = ResolvePackageSourceRoot(request.WorkingRoot);
            string stagingRoot = Path.Combine(request.WorkingRoot, "ps3-authored-content");
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Content", "manifest", 0, 1, "Staging declared PlayStation 3 cooked artifacts."));
            IReadOnlyList<string> staged = new Ps3ContentStager().Stage(
                request.Manifest.CookedArtifacts,
                packageSourceRoot,
                stagingRoot,
                cancellationToken);
            new Ps3StartupManifestWriter().Write(request.GeneratedCoreCppRootPath, request.Manifest);
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Content", "manifest", 1, 1, $"Staged {staged.Count} cooked artifacts."));

            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native SELF", "ps3", 0, 1, "Preparing the PlayStation 3 native executor."));
            NativeBuildExecutor.Build(request, request.GeneratedCoreCppRootPath, stagingRoot, cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native SELF", "ps3", 1, 1, "Built the PlayStation 3 native runtime."));
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception exception) {
            PlatformBuildDiagnostic diagnostic = new(
                PlatformBuildDiagnosticSeverity.Error,
                "PS3BUILD_FAILED",
                exception.Message,
                request.Manifest.StartupSceneId,
                string.Empty,
                "ps3-builder");
            diagnostics.Add(diagnostic);
            diagnosticReporter.Report(diagnostic);
        }

        PlatformBuildItemOutcomeKind outcomeKind = diagnostics.Count == 0
            ? PlatformBuildItemOutcomeKind.Succeeded
            : PlatformBuildItemOutcomeKind.Failed;
        return await Task.FromResult(new PlatformBuildReport(
            diagnostics.Count == 0,
            [.. diagnostics],
            request.Manifest.Scenes.Select(scene => new PlatformBuildItemOutcome(scene.SceneId, outcomeKind)).ToArray(),
            request.Manifest.LooseAssets.Select(asset => new PlatformBuildItemOutcome(asset.AssetId, outcomeKind)).ToArray()));
    }

    /// <summary>Locates the editor-staged package-source tree that owns the cooked files.</summary>
    /// <param name="workingRootPath">Builder working root supplied by the build graph.</param>
    /// <returns>Absolute package-source root.</returns>
    static string ResolvePackageSourceRoot(string workingRootPath) {
        if (string.IsNullOrWhiteSpace(workingRootPath)) {
            throw new ArgumentException("Builder working root is required.", nameof(workingRootPath));
        }

        string packageSourceRoot = Path.Combine(Path.GetFullPath(workingRootPath), "package-source");
        if (!Directory.Exists(packageSourceRoot)) {
            throw new DirectoryNotFoundException($"PlayStation 3 builder package source root '{packageSourceRoot}' was not found.");
        }

        return packageSourceRoot;
    }
}
