namespace helengine.ps3.builder.tests;

/// <summary>Resolves an explicitly configured fixture directory as a strict child of its task-owned root.</summary>
internal static class Ps3NativeShaderFixtureRoot {
    /// <summary>Requires an absolute configured path before returning its normalized full path.</summary>
    /// <param name="configuredPath">Path supplied by an explicit configuration value.</param>
    /// <param name="parameterName">Configuration parameter name to attach to validation errors.</param>
    /// <returns>The normalized fully qualified path.</returns>
    /// <exception cref="ArgumentException">Thrown when the configured path is not fully qualified.</exception>
    internal static string NormalizeFullyQualifiedPath(string configuredPath, string parameterName) {
        if (!Path.IsPathFullyQualified(configuredPath)) {
            throw new ArgumentException("The configured path must be fully qualified.", parameterName);
        }
        return Path.GetFullPath(configuredPath);
    }

    /// <summary>Requires absolute inputs and returns the normalized candidate only when it is below the parent root.</summary>
    /// <param name="nativeReaderRoot">Fully qualified task-owned parent directory.</param>
    /// <param name="configuredRoot">Fully qualified configured fixture directory.</param>
    /// <returns>The canonical fixture directory path, without creating it.</returns>
    /// <exception cref="ArgumentException">Thrown when either path is relative or the candidate is not a strict child.</exception>
    internal static string ResolveStrictDescendant(string nativeReaderRoot, string configuredRoot) {
        string parent = Path.TrimEndingDirectorySeparator(
            NormalizeFullyQualifiedPath(nativeReaderRoot, nameof(nativeReaderRoot)));
        string candidate = Path.TrimEndingDirectorySeparator(
            NormalizeFullyQualifiedPath(configuredRoot, nameof(configuredRoot)));
        string parentPrefix = Path.EndsInDirectorySeparator(parent)
            ? parent
            : parent + Path.DirectorySeparatorChar;
        StringComparison comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        if (string.Equals(candidate, parent, comparison) || !candidate.StartsWith(parentPrefix, comparison)) {
            throw new ArgumentException(
                $"The fixture root must be a strict child of {parent}; found {candidate}.",
                nameof(configuredRoot));
        }
        return candidate;
    }
}
