using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using helengine.ps3.builder;
using ProofFixture = (string Root, string ManifestId, string ConfigId);

namespace helengine.ps3.builder.tests;

/// <summary>Checks the content-addressed proof connecting a locked SDK manifest to a local Docker image.</summary>
public sealed class Ps3SdkImageIdentityTests {
    /// <summary>Serializes fixtures because every test overwrites the same fixed temporary platform directory.</summary>
    static readonly object FixtureLock = new();

    /// <summary>Accepts the classic Docker config ID while retaining the locked manifest identity.</summary>
    [Fact]
    public void ResolvesClassicConfigIdentityFromRawProof() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            Ps3SdkImageIdentity result = Ps3SdkImageIdentity.Resolve(fixture.ManifestId, "locked-sdk:prepared", fixture.Root, reference => {
                Assert.Equal("locked-sdk:prepared", reference);
                return Inspect(fixture.ConfigId);
            });
            Assert.Equal(fixture.ManifestId, result.CanonicalImageId);
            Assert.Equal(fixture.ConfigId, result.LocalImageId);
            Assert.Equal(fixture.ManifestId[7..], result.ManifestSha256);
            Assert.Equal(fixture.ConfigId[7..], result.ConfigSha256);
        }
    }

    /// <summary>Accepts the containerd manifest ID with the same verified config and ordered filesystem layers.</summary>
    [Fact]
    public void ResolvesContainerdManifestIdentity() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            Ps3SdkImageIdentity result = Ps3SdkImageIdentity.Resolve(fixture.ManifestId, null, fixture.Root, reference => {
                Assert.Equal(fixture.ConfigId, reference);
                return Inspect(fixture.ManifestId);
            });
            Assert.Equal(fixture.ManifestId, result.LocalImageId);
        }
    }

    /// <summary>Hashes the original bytes rather than a parsed and normalized representation of the manifest.</summary>
    [Fact]
    public void RejectsManifestBytesChangedWithoutChangingJsonMeaning() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            File.AppendAllText(Path.Combine(fixture.Root, "manifest.json"), "\n");
            Assert.Throws<InvalidDataException>(() => Resolve(fixture));
        }
    }

    /// <summary>Rejects any changed config bytes even when the changed document remains valid JSON.</summary>
    [Fact]
    public void RejectsConfigBytesChangedWithoutChangingJsonMeaning() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            File.AppendAllText(Path.Combine(fixture.Root, "config.json"), " ");
            Assert.Throws<InvalidDataException>(() => Resolve(fixture));
        }
    }

    /// <summary>Rejects an absent raw proof file instead of trusting a local image override.</summary>
    [Theory]
    [InlineData("manifest.json")]
    [InlineData("config.json")]
    public void RejectsMissingProof(string name) {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            File.Delete(Path.Combine(fixture.Root, name));
            Assert.Throws<FileNotFoundException>(() => Resolve(fixture));
        }
    }

    /// <summary>Rejects structurally valid manifests with unsupported schema, media type, size or platform.</summary>
    [Theory]
    [InlineData("schema")]
    [InlineData("manifest-media")]
    [InlineData("config-media")]
    [InlineData("size")]
    [InlineData("config-digest")]
    [InlineData("os")]
    [InlineData("architecture")]
    [InlineData("rootfs-type")]
    [InlineData("layer-count")]
    public void RejectsInvalidProofContract(string defect) {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof(defect);
            Assert.Throws<InvalidDataException>(() => Resolve(fixture));
        }
    }

    /// <summary>Rejects reordered, absent or unexpected local layers and unrelated local IDs/platforms.</summary>
    [Theory]
    [InlineData("layer-order")]
    [InlineData("missing-layer")]
    [InlineData("arbitrary-id")]
    [InlineData("mutable-id")]
    [InlineData("local-os")]
    [InlineData("local-architecture")]
    public void RejectsDifferentLocalImage(string defect) {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            JsonObject image = Inspect(fixture.ConfigId);
            if (defect == "layer-order") image["RootFS"]!["Layers"] = new JsonArray(LayerId('2'), LayerId('1'));
            if (defect == "missing-layer") image["RootFS"]!["Layers"] = new JsonArray(LayerId('1'));
            if (defect == "arbitrary-id") image["Id"] = LayerId('f');
            if (defect == "mutable-id") image["Id"] = "sdk:latest";
            if (defect == "local-os") image["Os"] = "windows";
            if (defect == "local-architecture") image["Architecture"] = "arm64";
            Assert.Throws<InvalidDataException>(() => Ps3SdkImageIdentity.Resolve(fixture.ManifestId, null, fixture.Root, _ => image));
        }
    }

    /// <summary>Supports the Docker schema-2 media types without accepting a different SDK payload.</summary>
    [Fact]
    public void AcceptsDockerSchemaTwoMediaTypes() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof("docker-media");
            Assert.Equal(fixture.ConfigId, Resolve(fixture).LocalImageId);
        }
    }

    /// <summary>Keeps cache identity and the lock canonical while Docker arguments use the verified classic ID.</summary>
    [Fact]
    public void ToolchainUsesLocalIdOnlyForExecution() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            string repository = CreateToolchain(fixture);
            string lockPath = Path.Combine(repository, "toolchain-lock.json");
            byte[] before = File.ReadAllBytes(lockPath);
            Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(repository, fixture.Root, _ => Inspect(fixture.ConfigId));
            Assert.Equal(fixture.ManifestId, toolchain.ImageId);
            Assert.Equal(fixture.ConfigId, toolchain.LocalImageId);
            Assert.StartsWith(fixture.ManifestId + "|", toolchain.Identity);
            Assert.DoesNotContain(fixture.ConfigId, toolchain.Identity);
            Assert.Equal(before, File.ReadAllBytes(lockPath));
            var command = new Ps3DockerShaderCommand(toolchain, helengine.ShaderStage.Pixel, "proof-test", fixture.Root).CreateStartInfo();
            Assert.Contains(fixture.ConfigId, command.ArgumentList);
            Assert.DoesNotContain(fixture.ManifestId, command.ArgumentList);
            int platformIndex = command.ArgumentList.IndexOf("--platform");
            Assert.True(platformIndex >= 0);
            Assert.Equal("linux/amd64", command.ArgumentList[platformIndex + 1]);
        }
    }

    /// <summary>Leaves legacy loads offline and does not introduce an inspection dependency without an explicit proof root.</summary>
    [Fact]
    public void ToolchainWithoutProofDoesNotInspectDocker() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(CreateToolchain(fixture), null, _ => throw new InvalidOperationException("Unexpected inspection"));
            Assert.Equal(fixture.ManifestId, toolchain.ImageId);
            Assert.Equal(toolchain.ImageId, toolchain.LocalImageId);
        }
    }

    /// <summary>Does not bypass a poisoned proof when the local image happens to have the canonical ID.</summary>
    [Fact]
    public void RejectsPoisonedProofEvenForCanonicalLocalId() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            File.AppendAllText(Path.Combine(fixture.Root, "manifest.json"), " ");
            Assert.Throws<InvalidDataException>(() => Ps3SdkImageIdentity.Resolve(fixture.ManifestId, null, fixture.Root, _ => Inspect(fixture.ManifestId)));
        }
    }

    /// <summary>Looks up the proven config when an imported image no longer carries its original tag.</summary>
    [Fact]
    public void MissingLookupTagFallsBackToProvenContentAddress() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            List<string> references = [];
            Ps3SdkImageIdentity result = Ps3SdkImageIdentity.Resolve(fixture.ManifestId, "locked-sdk:prepared", fixture.Root, reference => {
                references.Add(reference);
                return reference == fixture.ConfigId ? Inspect(fixture.ConfigId) : null;
            });
            Assert.Equal(new[] { "locked-sdk:prepared", fixture.ConfigId }, references);
            Assert.Equal(fixture.ConfigId, result.LocalImageId);
        }
    }

    /// <summary>Does not hide daemon, permissions or transport failures behind a different image lookup.</summary>
    [Fact]
    public void PropagatesInspectionFailureInsteadOfTryingAnotherImage() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            int calls = 0;
            Assert.Throws<InvalidOperationException>(() => Ps3SdkImageIdentity.Resolve(fixture.ManifestId, null, fixture.Root, _ => {
                calls++;
                throw new InvalidOperationException("Docker daemon unavailable");
            }));
            Assert.Equal(1, calls);
        }
    }

    /// <summary>Fails explicitly if no verified content address or lookup reference exists locally.</summary>
    [Fact]
    public void RejectsMissingLocalImage() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            Assert.Throws<InvalidDataException>(() => Ps3SdkImageIdentity.Resolve(fixture.ManifestId, null, fixture.Root, _ => null));
        }
    }

    /// <summary>Preserves the exact Cg shared-library verification when local SDK identity resolution is enabled.</summary>
    [Fact]
    public void ToolchainStillRejectsChangedCgLibrary() {
        lock (FixtureLock) {
            ProofFixture fixture = CreateProof();
            string repository = CreateToolchain(fixture);
            File.AppendAllText(Path.Combine(repository, "cg", "lib64", "libCg.so"), "changed");
            Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(repository, fixture.Root, _ => Inspect(fixture.ConfigId)));
        }
    }

    /// <summary>Creates a lock and hashed Cg mount under the same fixed fixture root without changing any checkout.</summary>
    static string CreateToolchain(ProofFixture fixture) {
        string root = Path.Combine(fixture.Root, "repository");
        string library = Path.Combine(root, "cg", "lib64", "libCg.so");
        Directory.CreateDirectory(Path.GetDirectoryName(library)!);
        byte[] libraryBytes = [1, 2, 3, 4];
        File.WriteAllBytes(library, libraryBytes);
        JsonObject lockFile = new() {
            ["ResolvedImage"] = new JsonObject { ["ImageId"] = fixture.ManifestId, ["Tag"] = "locked-sdk:prepared", ["CgcompPath"] = "/opt/ps3dev/bin/cgcomp" },
            ["ShaderToolkit"] = new JsonObject {
                ["MountSource"] = "cg", ["MountTarget"] = "/opt/cg", ["LibraryPath"] = "lib64/libCg.so",
                ["LibCgSha256"] = Convert.ToHexString(SHA256.HashData(libraryBytes)).ToLowerInvariant()
            }
        };
        File.WriteAllText(Path.Combine(root, "toolchain-lock.json"), lockFile.ToJsonString());
        return root;
    }

    /// <summary>Resolves one fixture against a matching classic-store inspection.</summary>
    static Ps3SdkImageIdentity Resolve(ProofFixture fixture) =>
        Ps3SdkImageIdentity.Resolve(fixture.ManifestId, null, fixture.Root, _ => Inspect(fixture.ConfigId));

    /// <summary>Creates ordered local inspection data; its IDs are independent of the resolver under test.</summary>
    static JsonObject Inspect(string id) => new() {
        ["Id"] = id, ["Os"] = "linux", ["Architecture"] = "amd64",
        ["RootFS"] = new JsonObject { ["Type"] = "layers", ["Layers"] = new JsonArray(LayerId('1'), LayerId('2')) }
    };

    /// <summary>Builds raw manifest/config bytes whose expected digests are computed independently using SHA-256.</summary>
    static ProofFixture CreateProof(string defect = null) {
        string root = Path.Combine(OperatingSystem.IsWindows() ? "C:\\temp" : "/tmp", "helengine", "ps3", "sdk-identity-tests");
        Directory.CreateDirectory(root);
        JsonObject config = new() {
            ["os"] = defect == "os" ? "windows" : "linux",
            ["architecture"] = defect == "architecture" ? "arm64" : "amd64",
            ["rootfs"] = new JsonObject {
                ["type"] = defect == "rootfs-type" ? "other" : "layers",
                ["diff_ids"] = new JsonArray(LayerId('1'), LayerId('2'))
            }
        };
        byte[] configBytes = Encoding.UTF8.GetBytes(config.ToJsonString());
        string configId = "sha256:" + Convert.ToHexString(SHA256.HashData(configBytes)).ToLowerInvariant();
        JsonObject manifest = new() {
            ["schemaVersion"] = defect == "schema" ? 1 : 2,
            ["mediaType"] = defect == "manifest-media" ? "other" : defect == "docker-media" ? "application/vnd.docker.distribution.manifest.v2+json" : "application/vnd.oci.image.manifest.v1+json",
            ["config"] = new JsonObject {
                ["mediaType"] = defect == "config-media" ? "other" : defect == "docker-media" ? "application/vnd.docker.container.image.v1+json" : "application/vnd.oci.image.config.v1+json",
                ["digest"] = defect == "config-digest" ? LayerId('f') : configId, ["size"] = configBytes.Length + (defect == "size" ? 1 : 0)
            },
            ["layers"] = new JsonArray(new JsonObject { ["digest"] = LayerId('a') }, new JsonObject { ["digest"] = LayerId('b') })
        };
        if (defect == "layer-count") manifest["layers"]!.AsArray().RemoveAt(1);
        byte[] manifestBytes = Encoding.UTF8.GetBytes(manifest.ToJsonString());
        string manifestId = "sha256:" + Convert.ToHexString(SHA256.HashData(manifestBytes)).ToLowerInvariant();
        File.WriteAllBytes(Path.Combine(root, "manifest.json"), manifestBytes);
        File.WriteAllBytes(Path.Combine(root, "config.json"), configBytes);
        return new ProofFixture(root, manifestId, configId);
    }

    /// <summary>Returns a full independently chosen layer identifier for ordered-comparison tests.</summary>
    static string LayerId(char digit) => "sha256:" + new string(digit, 64);

}
