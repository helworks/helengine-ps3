using helengine.baseplatform.Manifest;

namespace helengine.ps3.builder;

/// <summary>
/// Copies manifest-declared cooked files into a staging root that the native build installs
/// beside the executable. Only declared artifacts are staged, so a stray file in the editor's
/// package-source tree can never reach the console.
/// </summary>
public sealed class Ps3ContentStager {
    /// <summary>Stages every declared artifact and returns the staged list sorted by path.</summary>
    /// <param name="artifacts">Cooked artifacts declared by the build manifest.</param>
    /// <param name="sourceRootPath">Editor package-source root that owns the cooked files.</param>
    /// <param name="stagingRootPath">Fresh staging root; any previous content is removed.</param>
    /// <param name="cancellationToken">Cancellation token for the copy loop.</param>
    /// <returns>Relative paths staged, in sorted order.</returns>
    public IReadOnlyList<string> Stage(
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        string sourceRootPath,
        string stagingRootPath,
        CancellationToken cancellationToken) {
        if (artifacts == null) {
            throw new ArgumentNullException(nameof(artifacts));
        } else if (string.IsNullOrWhiteSpace(sourceRootPath)) {
            throw new ArgumentException("Source root is required.", nameof(sourceRootPath));
        } else if (string.IsNullOrWhiteSpace(stagingRootPath)) {
            throw new ArgumentException("Staging root is required.", nameof(stagingRootPath));
        }

        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        if (string.Equals(TrimSeparators(sourceRoot), TrimSeparators(stagingRoot), StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("PlayStation 3 source and staging roots must be different directories.");
        }

        // A stale staging root would ship content this build never declared.
        if (Directory.Exists(stagingRoot)) {
            Directory.Delete(stagingRoot, true);
        }
        Directory.CreateDirectory(stagingRoot);

        HashSet<string> seenPaths = new(StringComparer.OrdinalIgnoreCase);
        List<string> staged = new(artifacts.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            string relativePath = NormalizeRelativePath(artifact.RelativePath);
            if (!seenPaths.Add(relativePath)) {
                throw new InvalidOperationException($"PlayStation 3 cooked artifact path collision: '{relativePath}'.");
            }

            string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException(
                    $"PlayStation 3 cooked artifact '{relativePath}' was not found under package source root '{sourceRoot}'.",
                    sourcePath);
            }

            string destinationPath = ResolveUnderRoot(stagingRoot, relativePath);
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath) ?? stagingRoot);
            File.Copy(sourcePath, destinationPath, true);
            staged.Add(relativePath);
        }

        staged.Sort(StringComparer.Ordinal);
        return staged;
    }

    /// <summary>Normalizes one manifest-relative path to forward slashes with no leading separator.</summary>
    /// <param name="relativePath">Path as declared by the manifest.</param>
    /// <returns>Canonical relative path the runtime will request.</returns>
    public static string NormalizeRelativePath(string relativePath) {
        if (string.IsNullOrWhiteSpace(relativePath)) {
            throw new InvalidOperationException("PlayStation 3 cooked artifacts must declare a relative path.");
        }

        return relativePath.Replace('\\', '/').TrimStart('/');
    }

    /// <summary>Resolves a relative path under a root and refuses anything that escapes it.</summary>
    /// <param name="rootPath">Absolute containing root.</param>
    /// <param name="relativePath">Canonical relative path.</param>
    /// <returns>Absolute path inside the root.</returns>
    static string ResolveUnderRoot(string rootPath, string relativePath) {
        string combined = Path.GetFullPath(Path.Combine(rootPath, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        string rootPrefix = TrimSeparators(rootPath) + Path.DirectorySeparatorChar;
        if (!combined.StartsWith(rootPrefix, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PlayStation 3 cooked artifact path '{relativePath}' escapes its root.");
        }

        return combined;
    }

    /// <summary>Removes trailing directory separators so two roots compare equal.</summary>
    /// <param name="path">Path to trim.</param>
    /// <returns>Path without trailing separators.</returns>
    static string TrimSeparators(string path) {
        return path.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
    }
}
