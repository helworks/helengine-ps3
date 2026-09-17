using helengine.baseplatform.Builders;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.ps3.builder;

/// <summary>Publishes the PlayStation 3 platform contract and delegates native execution behind an injectable seam.</summary>
public sealed class Ps3PlatformAssetBuilder : IPlatformAssetBuilder {
    /// <summary>Executor that performs the native build phase.</summary>
    readonly IPs3NativeBuildExecutor NativeBuildExecutor;

    /// <summary>Creates a builder using the production native executor.</summary>
    public Ps3PlatformAssetBuilder()
        : this(new Ps3NativeBuildExecutor()) {
    }

    /// <summary>Creates a builder using a caller-provided native executor.</summary>
    /// <param name="nativeBuildExecutor">Executor used for the native build phase.</param>
    /// <exception cref="ArgumentNullException">Thrown when the executor is missing.</exception>
    public Ps3PlatformAssetBuilder(IPs3NativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
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

    /// <summary>
    /// Refuses material cooking, which belongs to the authored scene milestone rather than the
    /// boot gate. The refusal is explicit so a caller learns why rather than receiving empty
    /// cooked bytes that would fail confusingly on the console.
    /// </summary>
    /// <param name="request">Material request from the build graph.</param>
    /// <returns>Never returns normally.</returns>
    /// <exception cref="NotSupportedException">Always thrown, naming the milestone that implements cooking.</exception>
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) {
        throw new NotSupportedException(
            "The PlayStation 3 v0.2 boot gate does not cook materials. Material cooking arrives with the authored scene milestone.");
    }

    /// <summary>
    /// Compiles the regenerated Core into the native executable. No content is staged in this
    /// milestone, because the boot gate runs Core against an empty scene.
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
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native SELF", "ps3", 0, 1, "Preparing the PlayStation 3 native executor."));
            NativeBuildExecutor.Build(request, request.GeneratedCoreCppRootPath, cancellationToken);
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
}
