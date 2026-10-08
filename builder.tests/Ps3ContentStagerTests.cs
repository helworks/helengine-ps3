using System.Security.Cryptography;
using helengine.baseplatform.Manifest;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Exercises manifest preflight and preservation of prior staged content on rejected builds.</summary>
public sealed class Ps3ContentStagerTests {
    /// <summary>Checks that a later invalid declaration cannot erase or partially replace an earlier build.</summary>
    [Theory]
    [InlineData("missing")]
    [InlineData("duplicate")]
    [InlineData("null")]
    [InlineData("hash")]
    [InlineData("malformed-hash")]
    [InlineData("traversal")]
    [InlineData("rooted")]
    [InlineData("stream")]
    [InlineData("trailing-dot")]
    [InlineData("trailing-space")]
    public void Stage_InvalidManifestPreservesPreviousContent(string failure) {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        string staging = Path.Combine(root, "staging");
        Directory.CreateDirectory(source);
        Directory.CreateDirectory(staging);
        File.WriteAllText(Path.Combine(source, "first.bin"), "fresh content");
        File.WriteAllText(Path.Combine(staging, "previous.bin"), "previous content");
        PlatformBuildArtifact invalid = failure switch {
            "missing" => Artifact("missing.bin"),
            "duplicate" => Artifact("first.bin"),
            "null" => null,
            "hash" => Artifact("first.bin", "sha256:" + new string('0', 64)),
            "malformed-hash" => Artifact("first.bin", "sha256:invalid"),
            "traversal" => Artifact("folder/../first.bin"),
            "rooted" => Artifact("/first.bin"),
            "stream" => Artifact("first.bin:stream"),
            "trailing-dot" => Artifact("first.bin."),
            "trailing-space" => Artifact("first.bin "),
            _ => throw new ArgumentOutOfRangeException(nameof(failure))
        };
        // Different logical ids cannot exempt a repeated runtime path from collision checks.
        PlatformBuildArtifact first = failure is "hash" or "malformed-hash" ? Artifact("unused.bin") : Artifact("first.bin");
        if (failure is "hash" or "malformed-hash") File.WriteAllText(Path.Combine(source, "unused.bin"), "fresh");

        Assert.ThrowsAny<Exception>(() => new Ps3ContentStager().Stage(
            [first, invalid], source, staging, CancellationToken.None));

        Assert.Equal("previous content", File.ReadAllText(Path.Combine(staging, "previous.bin")));
        Assert.Equal(new[] { "previous.bin" }, Directory.GetFiles(staging).Select(Path.GetFileName));
        Assert.Equal("fresh content", File.ReadAllText(Path.Combine(source, "first.bin")));
    }

    /// <summary>Checks both directions of directory overlap and equality before a destructive staging refresh.</summary>
    [Theory]
    [InlineData("equal")]
    [InlineData("parent")]
    [InlineData("child")]
    public void Stage_OverlappingRootsPreserveBothDirectories(string relationship) {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        string staging = relationship switch {
            "equal" => source,
            "parent" => root,
            "child" => Path.Combine(source, "staging"),
            _ => throw new ArgumentOutOfRangeException(nameof(relationship))
        };
        Directory.CreateDirectory(source);
        Directory.CreateDirectory(staging);
        File.WriteAllText(Path.Combine(source, "source.bin"), "source");
        File.WriteAllText(Path.Combine(staging, "previous.bin"), "previous");

        Assert.Throws<InvalidOperationException>(() => new Ps3ContentStager().Stage(
            [], source + Path.DirectorySeparatorChar, staging + Path.DirectorySeparatorChar, CancellationToken.None));

        Assert.Equal("source", File.ReadAllText(Path.Combine(source, "source.bin")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(staging, "previous.bin")));
    }

    /// <summary>Checks that cancellation is observed even for an empty manifest before the previous stage is cleared.</summary>
    [Fact]
    public void Stage_CanceledEmptyManifestPreservesPreviousContent() {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        string staging = Path.Combine(root, "staging");
        Directory.CreateDirectory(source);
        Directory.CreateDirectory(staging);
        File.WriteAllText(Path.Combine(staging, "previous.bin"), "previous");
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();

        Assert.Throws<OperationCanceledException>(() => new Ps3ContentStager().Stage(
            [], source, staging, cancellation.Token));

        Assert.Equal("previous", File.ReadAllText(Path.Combine(staging, "previous.bin")));
    }

    /// <summary>Checks slash normalization, sorted declared content, real SHA-256 receipts and removal of stale files.</summary>
    [Fact]
    public void Stage_ValidManifestCopiesDeclaredBytesAndClearsStaleContent() {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        // A common textual prefix is valid when the roots are sibling directories.
        string staging = Path.Combine(root, "source-staged");
        Directory.CreateDirectory(Path.Combine(source, "folder"));
        Directory.CreateDirectory(staging);
        byte[] bytes = [0, 1, 2, 255];
        File.WriteAllBytes(Path.Combine(source, "folder", "Mixed.bin"), bytes);
        File.WriteAllText(Path.Combine(source, "a.bin"), "a");
        File.WriteAllText(Path.Combine(source, "undeclared.bin"), "exclude");
        File.WriteAllText(Path.Combine(staging, "stale.bin"), "remove");
        string receipt = "sha256:" + Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

        var staged = new Ps3ContentStager().Stage(
            [Artifact(@"folder\Mixed.bin", receipt), Artifact("a.bin")], source, staging, CancellationToken.None);

        Assert.Equal(new[] { "a.bin", "folder/Mixed.bin" }, staged);
        Assert.Equal(bytes, File.ReadAllBytes(Path.Combine(staging, "folder", "Mixed.bin")));
        Assert.Equal("a", File.ReadAllText(Path.Combine(staging, "a.bin")));
        Assert.False(File.Exists(Path.Combine(staging, "stale.bin")));
        Assert.False(File.Exists(Path.Combine(staging, "undeclared.bin")));
    }

    /// <summary>Checks that slash aliases cannot stage two declarations for the same runtime file.</summary>
    [Fact]
    public void Stage_SeparatorAliasesAreRejectedBeforeClearingPreviousContent() {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        string staging = Path.Combine(root, "staging");
        Directory.CreateDirectory(Path.Combine(source, "folder"));
        Directory.CreateDirectory(staging);
        File.WriteAllText(Path.Combine(source, "folder", "file.bin"), "source");
        File.WriteAllText(Path.Combine(staging, "previous.bin"), "previous");

        Assert.Throws<InvalidOperationException>(() => new Ps3ContentStager().Stage(
            [Artifact("folder/file.bin"), Artifact(@"folder\\file.bin")], source, staging, CancellationToken.None));

        Assert.Equal("previous", File.ReadAllText(Path.Combine(staging, "previous.bin")));
    }

    /// <summary>Checks a junction at either staging or an ancestor cannot disguise overlap with package source.</summary>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Stage_JunctionOverlapPreservesSourceAndPreviousContent(bool ancestorLink) {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        string link = Path.Combine(root, "link");
        Directory.CreateDirectory(source);
        File.WriteAllText(Path.Combine(source, "source.bin"), "source");
        File.WriteAllText(Path.Combine(source, "previous.bin"), "previous");
        // Directory junctions are available to regular Windows users and do not need symlink privileges.
        var start = new System.Diagnostics.ProcessStartInfo("cmd.exe") {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        start.ArgumentList.Add("/c");
        start.ArgumentList.Add("mklink");
        start.ArgumentList.Add("/J");
        start.ArgumentList.Add(link);
        start.ArgumentList.Add(source);
        using var process = System.Diagnostics.Process.Start(start);
        process.WaitForExit();
        Assert.True(process.ExitCode == 0, process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd());
        string staging = ancestorLink ? Path.Combine(link, "staging") : link;

        Assert.Throws<InvalidOperationException>(() => new Ps3ContentStager().Stage(
            [], source, staging, CancellationToken.None));

        Assert.Equal("source", File.ReadAllText(Path.Combine(source, "source.bin")));
        Assert.Equal("previous", File.ReadAllText(Path.Combine(source, "previous.bin")));
        Assert.False(Directory.Exists(Path.Combine(source, "staging")));
    }

    /// <summary>Checks a declared file cannot redirect source reads through a directory junction.</summary>
    [Fact]
    public void Stage_SourceJunctionPreservesPreviousContent() {
        string root = CreateRoot();
        string source = Path.Combine(root, "source");
        string external = Path.Combine(root, "external");
        string staging = Path.Combine(root, "staging");
        Directory.CreateDirectory(source);
        Directory.CreateDirectory(external);
        Directory.CreateDirectory(staging);
        File.WriteAllText(Path.Combine(external, "file.bin"), "external");
        File.WriteAllText(Path.Combine(staging, "previous.bin"), "previous");
        var start = new System.Diagnostics.ProcessStartInfo("cmd.exe") {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        start.ArgumentList.Add("/c");
        start.ArgumentList.Add("mklink");
        start.ArgumentList.Add("/J");
        start.ArgumentList.Add(Path.Combine(source, "redirect"));
        start.ArgumentList.Add(external);
        using var process = System.Diagnostics.Process.Start(start);
        process.WaitForExit();
        Assert.True(process.ExitCode == 0, process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd());

        Assert.Throws<InvalidOperationException>(() => new Ps3ContentStager().Stage(
            [Artifact("redirect/file.bin")], source, staging, CancellationToken.None));

        Assert.Equal("previous", File.ReadAllText(Path.Combine(staging, "previous.bin")));
        Assert.Equal("external", File.ReadAllText(Path.Combine(external, "file.bin")));
    }

    /// <summary>Creates a manifest record with either a legacy opaque identity or an explicit SHA-256 receipt.</summary>
    static PlatformBuildArtifact Artifact(string path, string hash = "legacy-identity") =>
        new(path, "logical-id", hash, "scene", "default");

    /// <summary>Reserves a test-specific directory below the coordinator's visible build output tree.</summary>
    static string CreateRoot() {
        string candidate = Environment.GetEnvironmentVariable("HELENGINE_PS3_CONTENT_STAGER_TEST_OUTPUT");
        if (string.IsNullOrWhiteSpace(candidate) || !Path.IsPathFullyQualified(candidate))
            throw new InvalidOperationException("HELENGINE_PS3_CONTENT_STAGER_TEST_OUTPUT must name an absolute workspace-owned output directory.");
        string allowed = Path.GetFullPath(@"C:\dev\helworks\builds\helengine-ps3");
        string fullRoot = Path.GetFullPath(candidate);
        string relative = Path.GetRelativePath(allowed, fullRoot);
        if (Path.IsPathRooted(relative) || relative == "." || relative == ".."
            || relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal))
            throw new InvalidOperationException("Content-stager test output must remain below builds/helengine-ps3.");
        string root = Path.Combine(fullRoot, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        return root;
    }
}
