using System.Security.Cryptography;
using System.Text.Json.Nodes;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies external SDK state relocates only the locked Cg mount while preserving its digest and cache identity.</summary>
public sealed class Ps3ShaderToolchainStateRootTests : IDisposable {
    /// <summary>Fixed per-platform fixture directory overwritten and removed by each test.</summary>
    readonly string FixtureRoot = Path.Combine(OperatingSystem.IsWindows() ? "C:\\temp" : "/tmp", "helengine", "ps3", "shader-state-tests");

    /// <summary>Independent toolkit bytes used to verify the external library digest.</summary>
    readonly byte[] LibraryBytes = [1, 7, 3, 9, 2, 8];

    /// <summary>Creates a canonical lock and identical repository-local and external Cg installations.</summary>
    public Ps3ShaderToolchainStateRootTests() {
        if (Directory.Exists(FixtureRoot)) Directory.Delete(FixtureRoot, true);
        WriteLibrary(Path.Combine(RepositoryRoot, ".ps3"));
        WriteLibrary(StateRoot);
        JsonObject lockFile = new() {
            ["ResolvedImage"] = new JsonObject {
                ["ImageId"] = "sha256:" + new string('a', 64),
                ["CgcompPath"] = "/opt/ps3dev/bin/cgcomp"
            },
            ["ShaderToolkit"] = new JsonObject {
                ["MountSource"] = ".ps3/dependencies/cg/usr", ["MountTarget"] = "/opt/cg",
                ["LibraryPath"] = "lib64/libCg.so",
                ["LibCgSha256"] = Convert.ToHexString(SHA256.HashData(LibraryBytes))
            }
        };
        File.WriteAllText(Path.Combine(RepositoryRoot, "toolchain-lock.json"), lockFile.ToJsonString());
    }

    /// <summary>Gets the isolated repository fixture containing the unchanged lock.</summary>
    string RepositoryRoot => Path.Combine(FixtureRoot, "repository");

    /// <summary>Gets the external toolkit state fixture, deliberately outside the repository.</summary>
    string StateRoot => Path.Combine(FixtureRoot, "external state");

    /// <summary>Gets the expected relocated mount root.</summary>
    string ExternalMount => Path.Combine(StateRoot, "dependencies", "cg", "usr");

    /// <summary>Uses external Cg bytes when the local installation is missing, retaining the locked image and shader identity.</summary>
    [Fact]
    public void LoadsExternalCgStateWithoutChangingCanonicalIdentity() {
        Ps3ShaderToolchain original = Ps3ShaderToolchain.Load(RepositoryRoot, null);
        string lockBefore = File.ReadAllText(Path.Combine(RepositoryRoot, "toolchain-lock.json"));
        Directory.Delete(Path.Combine(RepositoryRoot, ".ps3"), true);

        Ps3ShaderToolchain external = Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: StateRoot);

        Assert.Equal(ExternalMount, external.CgMountSource);
        Assert.Equal(original.ImageId, external.ImageId);
        Assert.Equal(original.LocalImageId, external.LocalImageId);
        Assert.Equal(original.Identity, external.Identity);
        Assert.Equal(lockBefore, File.ReadAllText(Path.Combine(RepositoryRoot, "toolchain-lock.json")));
        Ps3DockerShaderCommand command = new(external, helengine.ShaderStage.Pixel, "shader-state-test", Path.Combine(FixtureRoot, "job"));
        Assert.Contains("type=bind,source=" + ExternalMount + ",target=/opt/cg,readonly", command.CreateStartInfo().ArgumentList);
    }

    /// <summary>Preserves repository-relative mounts without the optional external state root.</summary>
    [Fact]
    public void WithoutExternalStatePreservesLegacyMountLayout() {
        ChangeLock("MountSource", "cg");
        string legacyMount = Path.Combine(RepositoryRoot, "cg");
        Directory.CreateDirectory(Path.Combine(legacyMount, "lib64"));
        File.WriteAllBytes(Path.Combine(legacyMount, "lib64", "libCg.so"), LibraryBytes);

        Assert.Equal(legacyMount, Ps3ShaderToolchain.Load(RepositoryRoot, null).CgMountSource);
    }

    /// <summary>Rejects relative state roots before selecting or hashing a local toolkit.</summary>
    [Theory]
    [InlineData("relative-state")]
    [InlineData(" ")]
    public void RejectsRelativeStateRoot(string stateRoot) {
        Assert.Throws<ArgumentException>(() => Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: stateRoot));
    }

    /// <summary>Prevents relocation of a lock source that is not canonically below the repository's .ps3 directory.</summary>
    [Theory]
    [InlineData("cg")]
    [InlineData(".ps3/../cg")]
    [InlineData(".ps3-other/dependencies/cg/usr")]
    [InlineData("../.ps3/dependencies/cg/usr")]
    public void RejectsLockedMountOutsideCanonicalState(string mountSource) {
        ChangeLock("MountSource", mountSource);
        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: StateRoot));
    }

    /// <summary>Rejects an absolute lock source even when the requested external state is valid.</summary>
    [Fact]
    public void RejectsAbsoluteLockedMountSource() {
        ChangeLock("MountSource", ExternalMount);
        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: StateRoot));
    }

    /// <summary>Continues checking the external library's bytes instead of accepting a valid repository-local fallback.</summary>
    [Fact]
    public void RejectsTamperedExternalLibrary() {
        File.AppendAllText(Path.Combine(ExternalMount, "lib64", "libCg.so"), "tampered");
        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: StateRoot));
    }

    /// <summary>Reports a missing external library rather than silently using the repository's installation.</summary>
    [Fact]
    public void RejectsMissingExternalLibrary() {
        File.Delete(Path.Combine(ExternalMount, "lib64", "libCg.so"));
        Assert.Throws<FileNotFoundException>(() => Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: StateRoot));
    }

    /// <summary>Maintains the locked library's containment below its relocated toolkit mount.</summary>
    [Fact]
    public void RejectsLibraryPathEscapingRelocatedMount() {
        ChangeLock("LibraryPath", "../libCg.so");
        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(RepositoryRoot, null, stateRoot: StateRoot));
    }

    /// <summary>Removes only this class's fixed temporary fixture directory.</summary>
    public void Dispose() {
        if (Directory.Exists(FixtureRoot)) Directory.Delete(FixtureRoot, true);
    }

    /// <summary>Writes a real fixture Cg library at the canonical location below a supplied state root.</summary>
    void WriteLibrary(string stateRoot) {
        string library = Path.Combine(stateRoot, "dependencies", "cg", "usr", "lib64", "libCg.so");
        Directory.CreateDirectory(Path.GetDirectoryName(library));
        File.WriteAllBytes(library, LibraryBytes);
    }

    /// <summary>Changes one isolated test-lock field without touching the checkout's tracked lock.</summary>
    void ChangeLock(string field, string value) {
        string lockPath = Path.Combine(RepositoryRoot, "toolchain-lock.json");
        JsonObject document = JsonNode.Parse(File.ReadAllText(lockPath)).AsObject();
        document["ShaderToolkit"][field] = value;
        File.WriteAllText(lockPath, document.ToJsonString());
    }
}
