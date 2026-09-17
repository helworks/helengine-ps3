using helengine.baseplatform.Requests;

namespace helengine.ps3.builder;

/// <summary>Seam that isolates the containerized native build from the builder under test.</summary>
public interface IPs3NativeBuildExecutor {
    /// <summary>Builds the PlayStation 3 executable from an external generated Core root.</summary>
    /// <param name="request">Complete platform request.</param>
    /// <param name="generatedCoreRoot">Root of the generated Core C++ tree.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    void Build(PlatformBuildRequest request, string generatedCoreRoot, CancellationToken cancellationToken);
}
