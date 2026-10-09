using helengine.ps3.builder;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;

namespace helengine.ps3.builder.tests;

/// <summary>Captures the native build handoff and diagnostics while leaving all actual texture cooking and staging active.</summary>
public sealed class Ps3TextureBuildProbe : IPs3NativeBuildExecutor, IPlatformBuildProgressReporter, IPlatformBuildDiagnosticReporter {
    /// <summary>Gets the content root handed to native compilation after successful staging.</summary>
    public string ContentRoot { get; set; } = string.Empty;
    /// <summary>Gets diagnostics emitted by rejected or failed cooking requests.</summary>
    public List<PlatformBuildDiagnostic> Diagnostics { get; } = [];
    /// <summary>Records native handoff without invoking a compiler or generating console media.</summary>
    public void Build(PlatformBuildRequest request, string generatedCoreRoot, string contentRoot, CancellationToken cancellationToken) {
        cancellationToken.ThrowIfCancellationRequested(); ContentRoot = contentRoot;
    }
    /// <summary>Accepts normal progress callbacks without imposing UI behavior on the build.</summary>
    public void Report(PlatformBuildProgressUpdate update) { ArgumentNullException.ThrowIfNull(update); }
    /// <summary>Retains exact build diagnostics for assertions after the asynchronous pipeline returns.</summary>
    public void Report(PlatformBuildDiagnostic diagnostic) { Diagnostics.Add(diagnostic); }
}
