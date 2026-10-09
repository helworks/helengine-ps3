using helengine.baseplatform.Manifest;
using System.Security.Cryptography;

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

        cancellationToken.ThrowIfCancellationRequested();
        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        ValidateNoReparsePoints(sourceRoot);
        ValidateNoReparsePoints(stagingRoot);
        if (IsSameOrDescendant(sourceRoot, stagingRoot) || IsSameOrDescendant(stagingRoot, sourceRoot)) {
            throw new InvalidOperationException("PlayStation 3 source and staging roots must not overlap.");
        }

        HashSet<string> seenPaths = new(StringComparer.OrdinalIgnoreCase);
        List<(string RelativePath, string SourcePath, string DestinationPath)> prepared = new(artifacts.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            if (artifact == null) {
                throw new InvalidOperationException("PlayStation 3 cooked artifact manifest contains a null entry.");
            }
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
            ValidateNoReparsePoints(sourcePath);
            ValidateContentHash(artifact, sourcePath);
            prepared.Add((relativePath, sourcePath, destinationPath));
        }

        cancellationToken.ThrowIfCancellationRequested();
        // Invalid declarations must not erase the preceding build's content. Once the whole
        // manifest passes preflight, remove stale undeclared files before copying this build.
        if (Directory.Exists(stagingRoot)) {
            Directory.Delete(stagingRoot, true);
        }
        Directory.CreateDirectory(stagingRoot);
        List<string> staged = new(prepared.Count);
        foreach ((string relativePath, string sourcePath, string destinationPath) in prepared) {
            cancellationToken.ThrowIfCancellationRequested();
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath) ?? stagingRoot);
            File.Copy(sourcePath, destinationPath, true);
            staged.Add(relativePath);
        }

        staged.Sort(StringComparer.Ordinal);
        return staged;
    }

    /// <summary>Normalizes slash direction while rejecting rooted paths and ambiguous runtime path segments.</summary>
    /// <param name="relativePath">Path as declared by the manifest.</param>
    /// <returns>Canonical relative path the runtime will request.</returns>
    public static string NormalizeRelativePath(string relativePath) {
        if (string.IsNullOrWhiteSpace(relativePath)) {
            throw new InvalidOperationException("PlayStation 3 cooked artifacts must declare a relative path.");
        }

        string slashNormalized = relativePath.Replace('\\', '/');
        if (slashNormalized.Split('/').Any(segment => segment.Contains(':') || segment.EndsWith('.') || segment.EndsWith(' '))) {
            throw new InvalidOperationException($"Cooked artifact path '{relativePath}' contains a Windows path alias.");
        }
        string normalized = slashNormalized.Trim();
        // Shared packaged-path validation rejects rooted and traversal paths. Preserve case
        // for older manifests while collapsing separators the same way as packaged paths.
        _ = helengine.CanonicalPackagedAssetPath.Normalize(normalized);
        return string.Join('/', normalized.Split('/', StringSplitOptions.RemoveEmptyEntries));
    }

    /// <summary>Checks a declared SHA-256 receipt against the source bytes before staging changes.</summary>
    /// <param name="artifact">Manifest entry carrying the optional SHA-256 receipt.</param>
    /// <param name="sourcePath">Existing cooked file to verify.</param>
    static void ValidateContentHash(PlatformBuildArtifact artifact, string sourcePath) {
        // Older manifests used opaque identities. The editor's current receipts use sha256:.
        if (!artifact.ContentHash.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase)) return;
        using FileStream source = File.OpenRead(sourcePath);
        string actual = "sha256:" + Convert.ToHexString(SHA256.HashData(source));
        if (!string.Equals(actual, artifact.ContentHash, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"PlayStation 3 cooked artifact '{artifact.RelativePath}' does not match its declared SHA-256 content hash.");
        }
    }

    /// <summary>Checks directory equality and ancestry with a separator boundary, including filesystem roots.</summary>
    /// <param name="path">Normalized candidate directory.</param>
    /// <param name="parent">Normalized containing directory.</param>
    /// <returns>True if deleting the parent can also delete the candidate.</returns>
    static bool IsSameOrDescendant(string path, string parent) {
        string relative = Path.GetRelativePath(parent, path);
        return !Path.IsPathRooted(relative) && relative != ".."
            && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal);
    }

    /// <summary>Refuses filesystem links that can disguise overlapping roots or redirect a declared source outside its root.</summary>
    /// <param name="path">Source file, root, or destination root whose existing ancestors must be ordinary directories.</param>
    internal static void ValidateNoReparsePoints(string path) {
        string current = path;
        while (!string.IsNullOrEmpty(current)) {
            if ((File.Exists(current) || Directory.Exists(current))
                && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) {
                throw new InvalidOperationException($"Content staging path '{path}' crosses filesystem link '{current}'.");
            }
            current = Path.GetDirectoryName(current);
        }
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
