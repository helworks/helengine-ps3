namespace helengine.ps3.builder.tests;

/// <summary>Captures the caller and authored path of one include resolver invocation.</summary>
public sealed class Ps3ShaderIncludeRequest {
    /// <summary>Initializes an immutable include request record.</summary>
    /// <param name="requestingFile">Resolved source path issuing the include.</param>
    /// <param name="includePath">Path written in the include directive.</param>
    public Ps3ShaderIncludeRequest(string requestingFile, string includePath) {
        RequestingFile = requestingFile;
        IncludePath = includePath;
    }

    /// <summary>Gets the resolved path of the source that requested the include.</summary>
    public string RequestingFile { get; }

    /// <summary>Gets the include path as written in the shader source.</summary>
    public string IncludePath { get; }
}
