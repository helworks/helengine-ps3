using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Collections.Generic;
using helengine;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies the PS3 builder declares and cooks its renderer-owned sprite batch shaders.</summary>
[Collection("PS3 shader process environment")]
public sealed class Ps3RendererShaderDependencyTests {
    /// <summary>Checks PS3 advertises both exact variants through a non-mutable dependency collection.</summary>
    [Fact]
    public void Builder_exposes_exact_renderer_shader_dependencies_as_read_only_data() {
        Ps3PlatformAssetBuilder builder = new();

        IReadOnlyList<PlatformShaderDependency> dependencies = builder.RendererShaderDependencies;

        Assert.False(dependencies is PlatformShaderDependency[]);
        IList<PlatformShaderDependency> listView = Assert.IsAssignableFrom<IList<PlatformShaderDependency>>(dependencies);
        Assert.Throws<NotSupportedException>(() => listView[0] = new PlatformShaderDependency(
            "SpriteBatchShader", "Other.vs", "Other.ps", "Textured"));
        Assert.Equal(new[] { "Textured", "RoundedShape" }, dependencies.Select(dependency => dependency.VariantName));
        Assert.All(dependencies, dependency => {
            Assert.Equal("SpriteBatchShader", dependency.ShaderAssetId);
            Assert.Equal("SpriteBatchShader.vs", dependency.VertexProgramName);
            Assert.Equal("SpriteBatchShader.ps", dependency.PixelProgramName);
        });
    }

    /// <summary>Checks a changed renderer variant define misses the shared compile cache while unchanged inputs hit it.</summary>
    [Fact]
    public void CompileCache_recompiles_changed_renderer_define_and_reuses_unchanged_request() {
        string sourceDirectory = Path.Combine(RequiredArtifactRoot(), "renderer-cache-source", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(sourceDirectory);
        string sourcePath = Path.Combine(sourceDirectory, "SpriteBatchShader.hlsl");
        string sourceText = "float4 VS() : SV_Position { return 0; }";
        Ps3ShaderIncludeResolverStub includeResolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult>(StringComparer.Ordinal) {
                ["common.hlsli"] = new ShaderIncludeResult("common.hlsli", "float IncludedValue = 1;")
            }
        };
        ShaderCacheTestBackend backend = new(ShaderCompileTarget.Ps3, "task4-cache-test-toolchain");
        ShaderCompileService service = new(includeResolver, new ShaderMemoryCompileCache(), new ShaderSourceHasher());
        service.RegisterBackend(backend);
        ShaderCompileRequest texturedRequest = CreateCacheRequest(sourcePath, sourceText, "0");
        ShaderCompileRequest roundedRequest = CreateCacheRequest(sourcePath, sourceText, "1");

        ShaderCompileResult first = service.Compile(texturedRequest);
        ShaderCompileResult changedDefine = service.Compile(roundedRequest);
        ShaderCompileResult unchanged = service.Compile(texturedRequest);

        Assert.Equal(2, backend.Calls);
        Assert.NotSame(first, changedDefine);
        Assert.Same(first, unchanged);
    }

    /// <summary>Checks real PS3 output changes for a copied source mutation and remains deterministic for repeated inputs.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CookRendererDependencies_withPinnedDockerConverter_invalidatesChangedSourceAndRemainsDeterministic() {
        string artifactRoot = RequiredArtifactRoot();
        string repositoryRoot = RequiredRepositoryRoot();
        string engineRoot = Path.Combine(Directory.GetParent(repositoryRoot).FullName, "helengine");
        string sourcePath = Path.Combine(engineRoot, "engine", "helengine.editor", "shaders", "builtin", "SpriteBatchShader.hlsl");
        string fixtureDirectory = Path.Combine(artifactRoot, "renderer-shader-source", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(fixtureDirectory);
        string copiedSourcePath = Path.Combine(fixtureDirectory, "SpriteBatchShader.hlsl");
        byte[] sourceBytes = File.ReadAllBytes(sourcePath);
        File.WriteAllBytes(copiedSourcePath, sourceBytes);
        string sourceText = new UTF8Encoding(false, true).GetString(sourceBytes);
        string sourceHash = Convert.ToHexString(SHA256.HashData(sourceBytes));
        Ps3PlatformAssetBuilder builder = new();
        string[] sourceHashes = [sourceHash, sourceHash, sourceHash, sourceHash];
        string[] sourceTexts = [sourceText, sourceText, sourceText, sourceText];
        byte[][][] artifactBytesByCook = new byte[4][][];
        string[][][] identitiesByCook = new string[4][][];
        byte[][][][] stagePayloadsByCook = new byte[4][][][];
        for (int cookIndex = 0; cookIndex < sourceHashes.Length; cookIndex++) {
            if (cookIndex == 2) {
                const string projectionAssignment = "output.position = mul(input.position, projection);";
                Assert.Equal(1, sourceText.Split(projectionAssignment, StringSplitOptions.None).Length - 1);
                string changedSourceText = sourceText.Replace(
                    projectionAssignment,
                    projectionAssignment + "\n    output.position.x += 0.125f * output.position.w;",
                    StringComparison.Ordinal);
                File.WriteAllText(copiedSourcePath, changedSourceText, new UTF8Encoding(false));
                byte[] changedSourceBytes = File.ReadAllBytes(copiedSourcePath);
                sourceTexts[2] = new UTF8Encoding(false, true).GetString(changedSourceBytes);
                sourceTexts[3] = sourceTexts[2];
                sourceHashes[2] = Convert.ToHexString(SHA256.HashData(changedSourceBytes));
                sourceHashes[3] = sourceHashes[2];
                Assert.NotEqual(sourceHash, sourceHashes[2]);
            }

            PlatformShaderArtifactCookRequest request = new(
                Path.Combine(artifactRoot, "renderer-shader-cook", Guid.NewGuid().ToString("N")),
                "ps3",
                "release",
                "ps3-standard",
                builder.RendererShaderDependencies,
                [new PlatformShaderArtifactCookSource("SpriteBatchShader", sourceHashes[cookIndex], sourceTexts[cookIndex], copiedSourcePath)]);
            PlatformShaderArtifactCookResult result = builder.CookShaderArtifacts(request);

            Assert.Equal(new[] { "Textured", "RoundedShape" }, result.CookedArtifactDeclarations.Select(declaration => declaration.VariantId));
            Assert.All(result.CookedArtifactDeclarations, declaration =>
                Assert.StartsWith("cooked/shaders/", declaration.RelativePath, StringComparison.Ordinal));
            artifactBytesByCook[cookIndex] = new byte[result.CookedArtifactDeclarations.Count][];
            identitiesByCook[cookIndex] = new string[result.CookedArtifactDeclarations.Count][];
            stagePayloadsByCook[cookIndex] = new byte[result.CookedArtifactDeclarations.Count][][];
            for (int artifactIndex = 0; artifactIndex < result.CookedArtifactDeclarations.Count; artifactIndex++) {
                PlatformCookedArtifactDeclaration declaration = result.CookedArtifactDeclarations[artifactIndex];
                string artifactPath = Path.Combine(request.CookRootPath, declaration.LogicalArtifactId.Replace('/', Path.DirectorySeparatorChar));
                byte[] artifactBytes = File.ReadAllBytes(artifactPath);
                Assert.Equal(new byte[] { 0x50, 0x33, 0x53, 0x50 }, artifactBytes[..4]);
                identitiesByCook[cookIndex][artifactIndex] = ReadIdentity(artifactBytes);
                Assert.Equal(new[] { "SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", declaration.VariantId, sourceHashes[cookIndex] }, identitiesByCook[cookIndex][artifactIndex]);
                string serializedMetadata = Encoding.UTF8.GetString(artifactBytes);
                Assert.Contains("BatchCameraBuffer", serializedMetadata, StringComparison.Ordinal);
                Assert.Contains("projection", serializedMetadata, StringComparison.Ordinal);
                artifactBytesByCook[cookIndex][artifactIndex] = artifactBytes;
                stagePayloadsByCook[cookIndex][artifactIndex] = ReadStagePayloads(artifactBytes).ToArray();
            }
        }

        Assert.Equal(2, artifactBytesByCook[0].Length);
        Assert.Equal(2, artifactBytesByCook[1].Length);
        Assert.Equal(2, artifactBytesByCook[2].Length);
        Assert.Equal(2, artifactBytesByCook[3].Length);
        for (int artifactIndex = 0; artifactIndex < 2; artifactIndex++) {
            Assert.Equal(identitiesByCook[0][artifactIndex], identitiesByCook[1][artifactIndex]);
            Assert.True(artifactBytesByCook[0][artifactIndex].SequenceEqual(artifactBytesByCook[1][artifactIndex]));
            Assert.Equal(identitiesByCook[2][artifactIndex], identitiesByCook[3][artifactIndex]);
            Assert.True(artifactBytesByCook[2][artifactIndex].SequenceEqual(artifactBytesByCook[3][artifactIndex]));
            Assert.NotEqual(identitiesByCook[0][artifactIndex][4], identitiesByCook[2][artifactIndex][4]);
            Assert.False(stagePayloadsByCook[0][artifactIndex][0].SequenceEqual(stagePayloadsByCook[2][artifactIndex][0]));
        }

        Assert.False(stagePayloadsByCook[0][0][0].SequenceEqual(stagePayloadsByCook[0][1][0]));
        Assert.False(stagePayloadsByCook[0][0][1].SequenceEqual(stagePayloadsByCook[0][1][1]));
    }

    /// <summary>Requires the explicit PS3 task test artifact root and validates its approved build-root location.</summary>
    /// <returns>Validated build-owned test artifact root.</returns>
    static string RequiredArtifactRoot() {
        string candidate = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        string repositoryRoot = RequiredRepositoryRoot();
        if (string.IsNullOrWhiteSpace(candidate)) {
            throw new InvalidOperationException("HELENGINE_PS3_SHADER_TEST_OUTPUT must name the Task 4 artifact root.");
        }

        string validated = Ps3Task3TestPaths.ValidateArtifactsRoot(candidate, repositoryRoot);
        Directory.CreateDirectory(validated);
        return validated;
    }

    /// <summary>Requires the checked-out PS3 repository path used to locate the sibling shared engine.</summary>
    /// <returns>Absolute PS3 checkout path.</returns>
    static string RequiredRepositoryRoot() {
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        if (string.IsNullOrWhiteSpace(repositoryRoot) || !Path.IsPathFullyQualified(repositoryRoot)) {
            throw new InvalidOperationException("HELENGINE_PS3_REPOSITORY_ROOT must name the absolute PS3 checkout.");
        }

        return Path.GetFullPath(repositoryRoot);
    }

    /// <summary>Creates a fixed renderer compile request with one explicitly varied shape define.</summary>
    /// <param name="sourcePath">Build-owned source identity path.</param>
    /// <param name="sourceText">Unchanged source text shared by both requests.</param>
    /// <param name="defineValue">Value of the renderer rounded-shape preprocessor define.</param>
    /// <returns>Complete request for the shared PS3 compile cache.</returns>
    static ShaderCompileRequest CreateCacheRequest(string sourcePath, string sourceText, string defineValue) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo(sourcePath, sourceText),
            "SpriteBatchShader.vs",
            "VS",
            ShaderStage.Vertex,
            ShaderCompileTarget.Ps3,
            new ShaderModel(3, 0),
            "RendererVariant",
            [new ShaderDefine("HELENGINE_BATCH_ROUNDED", defineValue)],
            new ShaderCompileOptions(ShaderBindingPolicies.Default, true, true, false));
    }

    /// <summary>Reads the five stable source-identity strings at the start of a version 1 P3SP artifact.</summary>
    /// <param name="artifactBytes">Complete P3SP artifact bytes.</param>
    /// <returns>Asset, vertex program, pixel program, variant and source hash.</returns>
    static string[] ReadIdentity(byte[] artifactBytes) {
        Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(4, 4)));
        int offset = 12;
        string[] identity = new string[5];
        for (int index = 0; index < identity.Length; index++) {
            uint byteCount = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4;
            identity[index] = new UTF8Encoding(false, true).GetString(artifactBytes.AsSpan(offset, checked((int)byteCount)));
            offset += checked((int)byteCount);
        }

        return identity;
    }

    /// <summary>Reads both compiled stage blobs after skipping the P3SP identity and reflection records.</summary>
    /// <param name="artifactBytes">Complete P3SP artifact bytes.</param>
    /// <returns>Vertex and pixel program payloads in artifact order.</returns>
    static IReadOnlyList<byte[]> ReadStagePayloads(byte[] artifactBytes) {
        int offset = 12;
        for (int index = 0; index < 6; index++) {
            uint textLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4 + checked((int)textLength);
        }

        List<byte[]> payloads = [];
        for (int stage = 0; stage < 2; stage++) {
            offset += 4;
            uint entryPointLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4 + checked((int)entryPointLength);
            uint payloadLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4;
            payloads.Add(artifactBytes.AsSpan(offset, checked((int)payloadLength)).ToArray());
            offset += checked((int)payloadLength);
            uint bufferCount = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4;
            for (int buffer = 0; buffer < bufferCount; buffer++) {
                offset = SkipText(artifactBytes, offset);
                offset += 8;
            }

            uint uniformCount = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4;
            for (int uniform = 0; uniform < uniformCount; uniform++) {
                offset = SkipText(artifactBytes, offset);
                offset += 4;
                offset = SkipText(artifactBytes, offset);
                offset += 20;
                offset = SkipText(artifactBytes, offset);
                offset += 12;
                offset = SkipText(artifactBytes, offset);
                offset += 8;
            }
        }

        return payloads;
    }

    /// <summary>Advances over one length-prefixed UTF-8 string in the artifact stream.</summary>
    /// <param name="artifactBytes">P3SP bytes being parsed.</param>
    /// <param name="offset">Offset at the encoded string length.</param>
    /// <returns>Offset immediately after the encoded string.</returns>
    static int SkipText(byte[] artifactBytes, int offset) {
        uint textLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
        return offset + 4 + checked((int)textLength);
    }
}
