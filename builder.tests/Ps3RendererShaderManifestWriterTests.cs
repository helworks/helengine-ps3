using System.Text;
using helengine.baseplatform.Manifest;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies the renderer shader manifest is converted into a deterministic native header.</summary>
public sealed class Ps3RendererShaderManifestWriterTests {
    /// <summary>Checks both renderer variants map to their exact identity-derived runtime paths.</summary>
    [Fact]
    public void Write_EmitsExactRuntimePathsForBothRendererVariants() {
        string root = CreateOutputRoot();
        PlatformBuildArtifact[] artifacts = RequiredArtifacts();
        PlatformBuildManifest manifest = CreateManifest(artifacts);

        string headerPath = new Ps3RendererShaderManifestWriter().Write(Path.Combine(root, "generated"), manifest);
        string header = File.ReadAllText(headerPath, new UTF8Encoding(false, true));

        Assert.Equal(Path.Combine(root, "generated", "ps3_renderer_shaders.hpp"), headerPath);
        Assert.Contains($"inline constexpr char BatchTexturedShaderPath[] = \"{Identity("Textured").RuntimeRelativePath}\";", header, StringComparison.Ordinal);
        Assert.Contains($"inline constexpr char BatchRoundedShapeShaderPath[] = \"{Identity("RoundedShape").RuntimeRelativePath}\";", header, StringComparison.Ordinal);
        Assert.Contains("/// Cooked shader pair used by textured sprite batches.", header, StringComparison.Ordinal);
        Assert.Contains("/// Cooked shader pair used by rounded-shape sprite batches.", header, StringComparison.Ordinal);
        Assert.False(File.ReadAllBytes(headerPath).AsSpan().StartsWith(Encoding.UTF8.Preamble));
    }

    /// <summary>Checks a required variant cannot be silently omitted.</summary>
    [Theory]
    [InlineData("Textured")]
    [InlineData("RoundedShape")]
    public void Write_RejectsMissingRequiredVariant(string missingVariant) {
        PlatformBuildArtifact[] artifacts = RequiredArtifacts().Where(artifact => artifact.VariantId != missingVariant).ToArray();

        Assert.Throws<InvalidOperationException>(() => new Ps3RendererShaderManifestWriter().Write(
            Path.Combine(CreateOutputRoot(), "generated"), CreateManifest(artifacts)));
    }

    /// <summary>Checks repeated identity candidates are rejected rather than selected by input order.</summary>
    [Fact]
    public void Write_RejectsDuplicateRequiredIdentity() {
        PlatformBuildArtifact[] required = RequiredArtifacts();
        PlatformBuildArtifact duplicate = required[0];

        Assert.Throws<InvalidOperationException>(() => new Ps3RendererShaderManifestWriter().Write(
            Path.Combine(CreateOutputRoot(), "generated"), CreateManifest([.. required, duplicate])));
    }

    /// <summary>Checks malformed runtime, kind, variant and logical mappings fail visibly before publishing.</summary>
    [Theory]
    [InlineData("runtime")]
    [InlineData("kind")]
    [InlineData("variant")]
    [InlineData("logical")]
    public void Write_RejectsInvalidIdentityMapping(string invalidField) {
        PlatformBuildArtifact[] artifacts = RequiredArtifacts();
        PlatformBuildArtifact original = artifacts[0];
        artifacts[0] = invalidField switch {
            "runtime" => new PlatformBuildArtifact("../outside/shader.ps3sp", original.LogicalArtifactId, original.ContentHash, original.ArtifactKind, original.VariantId),
            "kind" => new PlatformBuildArtifact(original.RelativePath, original.LogicalArtifactId, original.ContentHash, "texture", original.VariantId),
            "variant" => new PlatformBuildArtifact(original.RelativePath, original.LogicalArtifactId, original.ContentHash, original.ArtifactKind, "Other"),
            "logical" => new PlatformBuildArtifact(original.RelativePath, "shaders/wrong.ps3sp", original.ContentHash, original.ArtifactKind, original.VariantId),
            _ => throw new ArgumentOutOfRangeException(nameof(invalidField)),
        };
        string generatedRoot = Path.Combine(CreateOutputRoot(), "generated");

        Assert.Throws<InvalidOperationException>(() => new Ps3RendererShaderManifestWriter().Write(generatedRoot, CreateManifest(artifacts)));
        Assert.False(File.Exists(Path.Combine(generatedRoot, "ps3_renderer_shaders.hpp")));
    }

    /// <summary>Checks unrelated valid artifacts are ignored and artifact order cannot affect output bytes.</summary>
    [Fact]
    public void Write_IgnoresUnrelatedArtifactsAndIsDeterministicAcrossOrderAndRepeatedWrites() {
        PlatformBuildArtifact[] required = RequiredArtifacts();
        PlatformBuildArtifact unrelated = new("scenes/scene.bin", "scene-id", "hash", "scene", "default");
        string outputRoot = Path.Combine(CreateOutputRoot(), "generated");
        Ps3RendererShaderManifestWriter writer = new();

        string headerPath = writer.Write(outputRoot, CreateManifest([unrelated, .. required]));
        byte[] firstBytes = File.ReadAllBytes(headerPath);
        writer.Write(outputRoot, CreateManifest([.. required.Reverse(), unrelated]));
        byte[] reorderedBytes = File.ReadAllBytes(headerPath);
        writer.Write(outputRoot, CreateManifest([unrelated, .. required]));
        byte[] repeatedBytes = File.ReadAllBytes(headerPath);

        Assert.Equal(firstBytes, reorderedBytes);
        Assert.Equal(firstBytes, repeatedBytes);
    }

    /// <summary>Checks invalid input neither creates a header nor overwrites an earlier valid header.</summary>
    [Fact]
    public void Write_InvalidInputDoesNotCreateOrReplaceHeader() {
        string outputRoot = Path.Combine(CreateOutputRoot(), "generated");
        string headerPath = new Ps3RendererShaderManifestWriter().Write(outputRoot, CreateManifest(RequiredArtifacts()));
        byte[] previousBytes = File.ReadAllBytes(headerPath);

        Assert.Throws<InvalidOperationException>(() => new Ps3RendererShaderManifestWriter().Write(
            outputRoot, CreateManifest(RequiredArtifacts()[..1])));

        Assert.Equal(previousBytes, File.ReadAllBytes(headerPath));
    }

    /// <summary>Checks a post-construction null array entry reports its index and does not publish invalid output.</summary>
    /// <param name="preserveExistingHeader">Whether a valid header already exists before manifest mutation.</param>
    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void Write_RejectsNullArtifactInsertedAfterManifestConstruction(bool preserveExistingHeader) {
        string outputRoot = Path.Combine(CreateOutputRoot(), "generated");
        Ps3RendererShaderManifestWriter writer = new();
        PlatformBuildManifest manifest = CreateManifest(RequiredArtifacts());
        byte[] previousBytes = null;
        if (preserveExistingHeader) {
            string existingHeaderPath = writer.Write(outputRoot, manifest);
            previousBytes = File.ReadAllBytes(existingHeaderPath);
        }
        manifest.CookedArtifacts[1] = null;

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => writer.Write(outputRoot, manifest));

        Assert.Contains("index 1", exception.Message, StringComparison.Ordinal);
        string headerPath = Path.Combine(outputRoot, "ps3_renderer_shaders.hpp");
        if (preserveExistingHeader) {
            Assert.Equal(previousBytes, File.ReadAllBytes(headerPath));
        } else {
            Assert.False(File.Exists(headerPath));
            Assert.False(Directory.Exists(outputRoot));
        }
    }

    /// <summary>Builds both exact renderer shader identities from the checked-in identity algorithm.</summary>
    /// <returns>Textured and rounded-shape manifest artifacts.</returns>
    static PlatformBuildArtifact[] RequiredArtifacts() => [Artifact("Textured"), Artifact("RoundedShape")];

    /// <summary>Creates one real manifest record for a named PS3 renderer shader variant.</summary>
    /// <param name="variant">Required renderer variant name.</param>
    /// <returns>An artifact with exact logical and runtime identity paths.</returns>
    static PlatformBuildArtifact Artifact(string variant) {
        Ps3ShaderArtifactIdentity identity = Identity(variant);
        return new PlatformBuildArtifact(identity.RuntimeRelativePath, identity.RelativePath, "content-hash", "shader", identity.VariantName);
    }

    /// <summary>Derives the stable identity used by the renderer dependency contract.</summary>
    /// <param name="variant">Required renderer variant name.</param>
    /// <returns>Identity computed from the actual asset and program names.</returns>
    static Ps3ShaderArtifactIdentity Identity(string variant) => new(
        "SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant);

    /// <summary>Creates a minimal complete platform manifest carrying the supplied cooked artifacts.</summary>
    /// <param name="artifacts">Cooked artifacts under test.</param>
    /// <returns>A real manifest object whose public artifact array remains mutable.</returns>
    static PlatformBuildManifest CreateManifest(PlatformBuildArtifact[] artifacts) => new(
        1, "project", "1.0", "0.4.0", "ps3", "0.2.0", string.Empty,
        [], [], artifacts, [], [], new PlatformContainerWritePlan("ps3", []), [], PlatformBuildRuntimeFeatureManifest.Empty);

    /// <summary>Creates a unique test output beneath the explicitly authorized artifact root.</summary>
    /// <returns>A fresh directory reserved for this test's generated header.</returns>
    static string CreateOutputRoot() {
        string candidate = Environment.GetEnvironmentVariable("HELENGINE_PS3_NATIVE_TASK1_MANIFEST_TEST_OUTPUT");
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        if (string.IsNullOrWhiteSpace(candidate)) throw new InvalidOperationException("HELENGINE_PS3_NATIVE_TASK1_MANIFEST_TEST_OUTPUT must name the manifest test artifact root.");
        if (string.IsNullOrWhiteSpace(repositoryRoot)) throw new InvalidOperationException("HELENGINE_PS3_REPOSITORY_ROOT must name the PS3 checkout.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(candidate, repositoryRoot);
        string outputRoot = Path.Combine(validatedRoot, Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(outputRoot);
        return outputRoot;
    }
}
