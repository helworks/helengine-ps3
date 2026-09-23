using helengine;

namespace helengine.ps3.builder.tests;

/// <summary>Misses the first relative include lookup, then returns a valid include for shaderc's retry.</summary>
public sealed class Ps3ShaderIncludeRetryResolver : IShaderIncludeResolver {
    /// <summary>Records each callback path in the order the bundled shaderc invokes the resolver.</summary>
    public List<Ps3ShaderIncludeRequest> Requests { get; } = [];

    /// <summary>Returns a shader body after one deliberate relative-lookup miss.</summary>
    /// <param name="requestingFile">Source path that requested the include.</param>
    /// <param name="includePath">Path authored in the include directive.</param>
    /// <returns>The include source after the initial lookup miss.</returns>
    public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
        Requests.Add(new Ps3ShaderIncludeRequest(requestingFile, includePath));
        if (Requests.Count == 1) throw new FileNotFoundException("Initial relative lookup miss.", includePath);
        return new ShaderIncludeResult("retry.hlsl", "static const float4 RetriedColor = float4(1, 0, 0, 1);");
    }
}
