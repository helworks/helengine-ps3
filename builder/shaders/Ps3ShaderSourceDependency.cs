namespace helengine.ps3.builder;

/// <summary>Identifies one active HLSL include by its resolved path and content digest.</summary>
public sealed class Ps3ShaderSourceDependency {
    /// <summary>Initializes a dependency record with its resolved path and stable content hash.</summary>
    /// <param name="path">Resolver-provided path used as the include source name.</param>
    /// <param name="contentHash">Uppercase hexadecimal SHA-256 digest of UTF-8 source bytes.</param>
    public Ps3ShaderSourceDependency(string path, string contentHash) {
        if (string.IsNullOrWhiteSpace(path)) throw new ArgumentException("Dependency path must be provided.", nameof(path));
        if (string.IsNullOrWhiteSpace(contentHash)) throw new ArgumentException("Dependency content hash must be provided.", nameof(contentHash));
        Path = path;
        ContentHash = contentHash;
    }

    /// <summary>Gets the path returned by the include resolver.</summary>
    public string Path { get; }

    /// <summary>Gets the uppercase hexadecimal SHA-256 hash of the resolved UTF-8 content.</summary>
    public string ContentHash { get; }
}
