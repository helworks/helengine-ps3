using helengine;

namespace helengine.ps3.builder.tests;

/// <summary>Records shader include requests and returns test controlled include source.</summary>
public sealed class Ps3ShaderIncludeResolverStub : IShaderIncludeResolver {
    /// <summary>Include results indexed by the path written in each include directive.</summary>
    public Dictionary<string, ShaderIncludeResult> Files { get; init; } = new(StringComparer.Ordinal);

    /// <summary>Requests received from shaderc in their original callback order.</summary>
    public List<Ps3ShaderIncludeRequest> Requests { get; } = [];

    /// <summary>Optional ordered results used to simulate content changing between repeated paths.</summary>
    public Queue<ShaderIncludeResult> Results { get; } = [];

    /// <summary>Optional failure raised for every include request.</summary>
    public Exception Failure { get; init; }

    /// <summary>Records a request and returns its configured include content.</summary>
    /// <param name="requestingFile">Resolved source path issuing the include.</param>
    /// <param name="includePath">Path written in the include directive.</param>
    /// <returns>The configured include result.</returns>
    public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
        Requests.Add(new Ps3ShaderIncludeRequest(requestingFile, includePath));
        if (Failure != null) throw Failure;
        if (Results.Count > 0) return Results.Dequeue();
        if (!Files.TryGetValue(includePath, out ShaderIncludeResult result)) {
            throw new FileNotFoundException($"Include '{includePath}' was not found.", includePath);
        }
        return result;
    }
}
