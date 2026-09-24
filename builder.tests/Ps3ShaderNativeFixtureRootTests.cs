namespace helengine.ps3.builder.tests;

/// <summary>Verifies native-reader fixture roots are absolute strict descendants without performing filesystem writes.</summary>
public sealed class Ps3ShaderNativeFixtureRootTests {
    /// <summary>Accepts a fully qualified fixture directory beneath the native-reader root.</summary>
    [Fact]
    public void ResolveStrictDescendant_AcceptsAbsoluteChild() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();
        string child = Path.Combine(nativeReaderRoot, "fixture-" + Guid.NewGuid().ToString("N"));

        string resolved = Ps3NativeShaderFixtureRoot.ResolveStrictDescendant(nativeReaderRoot, child);

        Assert.Equal(Path.GetFullPath(child), resolved);
        Assert.False(Directory.Exists(child));
    }

    /// <summary>Rejects the native-reader root itself before any fixture directory can be created.</summary>
    [Fact]
    public void ResolveStrictDescendant_RejectsEqualRootBeforeWrite() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();

        AssertRejectedWithoutCreating(nativeReaderRoot, nativeReaderRoot);
    }

    /// <summary>Rejects an equal root with a trailing backslash before any fixture directory can be created.</summary>
    [Fact]
    public void ResolveStrictDescendant_RejectsRootWithTrailingBackslashBeforeWrite() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();

        AssertRejectedWithoutCreating(nativeReaderRoot, nativeReaderRoot + '\\');
    }

    /// <summary>Rejects an equal root with a trailing forward slash before any fixture directory can be created.</summary>
    [Fact]
    public void ResolveStrictDescendant_RejectsRootWithTrailingForwardSlashBeforeWrite() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();

        AssertRejectedWithoutCreating(nativeReaderRoot, nativeReaderRoot + '/');
    }

    /// <summary>Rejects an ancestor of the native-reader root before any fixture directory can be created.</summary>
    [Fact]
    public void ResolveStrictDescendant_RejectsAncestorBeforeWrite() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();
        string ancestor = Directory.GetParent(nativeReaderRoot)!.FullName;

        AssertRejectedWithoutCreating(nativeReaderRoot, ancestor);
    }

    /// <summary>Rejects a sibling whose name merely shares the native-reader root's textual prefix.</summary>
    [Fact]
    public void ResolveStrictDescendant_RejectsSiblingPrefixBeforeWrite() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();
        string sibling = nativeReaderRoot + "-sibling-" + Guid.NewGuid().ToString("N");

        AssertRejectedWithoutCreating(nativeReaderRoot, sibling);
    }

    /// <summary>Rejects relative input before normalization can turn it into an absolute path.</summary>
    [Fact]
    public void ResolveStrictDescendant_RejectsRelativePathBeforeWrite() {
        string nativeReaderRoot = CreateUnusedNativeReaderRoot();
        string relative = Path.Combine("relative-fixture", Guid.NewGuid().ToString("N"));

        Assert.False(Path.IsPathFullyQualified(relative));
        Assert.Throws<ArgumentException>(() =>
            Ps3NativeShaderFixtureRoot.ResolveStrictDescendant(nativeReaderRoot, relative));
    }

    /// <summary>Rejects a relative configured repository root before normalization can make it absolute.</summary>
    [Fact]
    public void NormalizeFullyQualifiedPath_RejectsRelativeRepositoryRoot() {
        string relativeRepositoryRoot = Path.Combine("relative-repository", Guid.NewGuid().ToString("N"));

        Assert.False(Path.IsPathFullyQualified(relativeRepositoryRoot));
        Assert.Throws<ArgumentException>(() =>
            Ps3NativeShaderFixtureRoot.NormalizeFullyQualifiedPath(relativeRepositoryRoot, "repositoryRoot"));
    }

    /// <summary>Builds a unique lexical test root without creating it or any of its parents.</summary>
    /// <returns>A fully qualified, unused native-reader-root-shaped path.</returns>
    static string CreateUnusedNativeReaderRoot() => Path.GetFullPath(Path.Combine(
        Path.GetTempPath(), "ps3-native-reader-root-tests", Guid.NewGuid().ToString("N"), "native-reader"));

    /// <summary>Checks an invalid root is rejected and the candidate path remains unwritten.</summary>
    /// <param name="nativeReaderRoot">Expected parent root.</param>
    /// <param name="candidate">Invalid fixture path to reject.</param>
    static void AssertRejectedWithoutCreating(string nativeReaderRoot, string candidate) {
        Assert.Throws<ArgumentException>(() =>
            Ps3NativeShaderFixtureRoot.ResolveStrictDescendant(nativeReaderRoot, candidate));
        Assert.False(Directory.Exists(candidate));
    }
}
