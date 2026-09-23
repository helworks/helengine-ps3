namespace helengine.ps3.builder.tests;

/// <summary>Validates that Task3 test artifacts stay in the designated sibling build tree.</summary>
internal static class Ps3Task3TestPaths {
    /// <summary>Resolves a Task3 artifact root only when it is absolute, build-owned and outside system temporary paths.</summary>
    /// <param name="candidate">Explicit artifact-root path supplied to the test process.</param>
    /// <param name="repositoryRoot">Repository checkout root used to locate its sibling build directory.</param>
    /// <returns>The normalized artifact-root path.</returns>
    internal static string ValidateArtifactsRoot(string candidate, string repositoryRoot) {
        if (string.IsNullOrWhiteSpace(candidate)) throw new ArgumentException("An explicit Task3 artifact root is required.", nameof(candidate));
        if (!Path.IsPathRooted(candidate)) throw new ArgumentException("Task3 artifact root must be an absolute path.", nameof(candidate));
        if (string.IsNullOrWhiteSpace(repositoryRoot) || !Path.IsPathRooted(repositoryRoot)) throw new ArgumentException("Repository root must be absolute.", nameof(repositoryRoot));

        string fullRoot = Path.GetFullPath(candidate);
        string fullRepositoryRoot = Path.GetFullPath(repositoryRoot);
        DirectoryInfo repositoryParent = Directory.GetParent(fullRepositoryRoot)
            ?? throw new ArgumentException("Repository root must have a parent directory.", nameof(repositoryRoot));
        string expectedBuildRoot = Path.GetFullPath(Path.Combine(repositoryParent.FullName, "builds", "helengine-ps3"));
        if (!IsStrictDescendant(fullRoot, expectedBuildRoot)) {
            throw new ArgumentException("Task3 artifacts must be below the checkout's sibling builds/helengine-ps3 directory.", nameof(candidate));
        }

        string systemTemporaryRoot = Path.GetFullPath(Path.GetTempPath());
        if (IsSameOrDescendant(fullRoot, systemTemporaryRoot)) {
            throw new ArgumentException("Task3 artifacts cannot be written under a system temporary directory.", nameof(candidate));
        }
        return fullRoot;
    }

    /// <summary>Determines whether a normalized path is strictly below a normalized parent path.</summary>
    /// <param name="path">Candidate child path.</param>
    /// <param name="parent">Expected parent path.</param>
    /// <returns>True only when the candidate is a strict descendant.</returns>
    static bool IsStrictDescendant(string path, string parent) {
        if (string.Equals(path, parent, StringComparison.OrdinalIgnoreCase)) return false;
        return IsSameOrDescendant(path, parent);
    }

    /// <summary>Determines whether a normalized path is equal to or below another normalized path.</summary>
    /// <param name="path">Candidate path.</param>
    /// <param name="parent">Potential parent path.</param>
    /// <returns>True when the candidate resolves within the parent directory.</returns>
    static bool IsSameOrDescendant(string path, string parent) {
        string relative = Path.GetRelativePath(parent, path);
        return !Path.IsPathRooted(relative)
            && relative != ".."
            && !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
            && !relative.StartsWith(".." + Path.AltDirectorySeparatorChar, StringComparison.Ordinal);
    }
}
