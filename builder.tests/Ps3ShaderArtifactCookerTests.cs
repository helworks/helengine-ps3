using System.Buffers.Binary;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies deterministic PS3 pair cooking without launching the SDK except in the Docker acceptance test.</summary>
[Collection("PS3 shader process environment")]
public sealed class Ps3ShaderArtifactCookerTests {
    /// <summary>Assembly containing the production PS3 builder types under test.</summary>
    static readonly Assembly BuilderAssembly = typeof(Ps3PlatformAssetBuilder).Assembly;

    /// <summary>Checks that an empty request does not construct a backend or require SDK configuration.</summary>
    [Fact]
    public void Cook_EmptyDependencies_DoesNotCreateBackend() {
        int factoryCalls = 0;
        object cooker = CreateCooker(workingRoot => {
            factoryCalls++;
            return new Ps3ShaderCookBackendStub();
        });

        PlatformShaderArtifactCookResult result = Cook(cooker, CreateEmptyRequest());

        Assert.Empty(result.CookedArtifactDeclarations);
        Assert.Equal(0, factoryCalls);
    }

    /// <summary>Checks repeated identical dependencies compile once per stage and publish one deterministic pair.</summary>
    [Fact]
    public void Cook_DeduplicatesIdenticalDependenciesAndPublishesOnePair() {
        string sourcePath = CreateSourcePath();
        PlatformShaderDependency dependency = Dependency("asset-a", "default");
        PlatformShaderArtifactCookSource source = Source("asset-a", sourcePath);
        PlatformShaderArtifactCookRequest request = CreateRequest([dependency, dependency], [source]);
        Ps3ShaderCookBackendStub backend = new();
        List<string> workingRoots = [];
        object cooker = CreateCooker(workingRoot => {
            workingRoots.Add(workingRoot);
            return backend;
        });

        PlatformShaderArtifactCookResult result = Cook(cooker, request);

        PlatformCookedArtifactDeclaration declaration = Assert.Single(result.CookedArtifactDeclarations);
        Assert.False(File.Exists(sourcePath));
        Assert.Equal(2, backend.Requests.Count);
        Assert.All(backend.Requests, compileRequest => Assert.Equal(sourcePath, compileRequest.Source.Path));
        Assert.All(backend.Requests, compileRequest => Assert.Equal("snapshot source", compileRequest.Source.Source));
        Assert.Single(workingRoots);
        Assert.Equal(Path.Combine(request.CookRootPath, ".ps3-shader-work"), workingRoots[0]);
        Ps3ShaderArtifactIdentity identity = new("asset-a", "Vertex", "Pixel", "default");
        Assert.StartsWith("shaders/", identity.RelativePath);
        Assert.Equal("cooked/" + identity.RelativePath, identity.RuntimeRelativePath);
        Assert.Equal("cooked/" + identity.RelativePath, declaration.RelativePath);
        Assert.Equal(identity.RelativePath, declaration.LogicalArtifactId);
        Assert.True(File.Exists(Path.Combine(request.CookRootPath, identity.RelativePath.Replace('/', Path.DirectorySeparatorChar))));
        Assert.False(Directory.Exists(Path.Combine(request.CookRootPath, "cooked")));
    }

    /// <summary>Checks distinct asset identities and program pairs sharing a variant never overwrite one another.</summary>
    [Fact]
    public void Cook_DistinctAssetsAndProgramPairsUseDistinctPaths() {
        string firstPath = CreateSourcePath();
        string secondPath = CreateSourcePath();
        PlatformShaderDependency[] dependencies = [
            Dependency("asset-one", "default", "Vertex", "Pixel"),
            Dependency("asset-two", "default", "Vertex", "Pixel"),
            Dependency("asset-one", "default", "VertexOther", "PixelOther")
        ];
        PlatformShaderArtifactCookSource[] sources = [Source("asset-one", firstPath), Source("asset-two", secondPath)];
        PlatformShaderArtifactCookRequest request = CreateRequest(dependencies, sources);
        Ps3ShaderCookBackendStub backend = new();
        object cooker = CreateCooker(_ => backend);

        PlatformShaderArtifactCookResult result = Cook(cooker, request);

        Assert.Equal(3, result.CookedArtifactDeclarations.Count);
        Assert.Equal(6, backend.Requests.Count);
        Assert.Equal(3, result.CookedArtifactDeclarations.Select(item => item.RelativePath).Distinct(StringComparer.Ordinal).Count());
        Assert.All(result.CookedArtifactDeclarations, declaration => Assert.Equal("shader", declaration.ArtifactKind));
        Assert.All(result.CookedArtifactDeclarations, declaration => Assert.Equal("default", declaration.VariantId));
    }

    /// <summary>Checks that request source snapshots stay authoritative while nested includes resolve beside SourcePath.</summary>
    [Fact]
    public void Cook_UsesSourceSnapshotAndResolvesIncludesFromSourcePath() {
        string sourcePath = CreateSourcePath();
        string authoredFileText = "disk contents are deliberately different";
        string sourceSnapshot = "request source snapshot";
        File.WriteAllText(sourcePath, authoredFileText);
        string includePath = Path.Combine(Path.GetDirectoryName(sourcePath), "nested", "common.hlsli");
        Directory.CreateDirectory(Path.GetDirectoryName(includePath));
        File.WriteAllText(includePath, "float4 NestedColor() { return 1.0; }");
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("asset-include", "default")],
            [new PlatformShaderArtifactCookSource("asset-include", "snapshot-hash", sourceSnapshot, sourcePath)]);
        Ps3ShaderCookBackendStub backend = new() { IncludePathToResolve = Path.Combine("nested", "common.hlsli") };
        object cooker = CreateCooker(_ => backend);

        Cook(cooker, request);

        Assert.Equal(2, backend.Requests.Count);
        Assert.All(backend.Requests, compileRequest => Assert.Equal(sourceSnapshot, compileRequest.Source.Source));
        Assert.All(backend.Requests, compileRequest => Assert.Equal(sourcePath, compileRequest.Source.Path));
        Assert.Equal(2, backend.ResolvedIncludes.Count);
        Assert.All(backend.ResolvedIncludes, include => Assert.Equal(Path.GetFullPath(includePath), Path.GetFullPath(include.Path)));
        Assert.All(backend.ResolvedIncludes, include => Assert.Equal("float4 NestedColor() { return 1.0; }", include.Source));
    }

    /// <summary>Exposes the current shared Standard Shader variant records as ordered theory data.</summary>
    public static IEnumerable<object[]> StandardVariantCases => StandardShaderVariants.All.Select(variant => new object[] { variant });

    /// <summary>Checks standard variants use their shared entry points and exact parsed define records.</summary>
    /// <param name="variant">Shared standard variant under test.</param>
    [Theory]
    [MemberData(nameof(StandardVariantCases))]
    public void Cook_UsesExactSharedStandardVariant(StandardShaderVariant variant) {
        string sourcePath = CreateSourcePath();
        PlatformShaderDependency dependency = Dependency("ForwardStandardShader", variant.Name, "LogicalVertex", "LogicalPixel");
        PlatformShaderArtifactCookRequest request = CreateRequest([dependency], [Source("ForwardStandardShader", sourcePath)]);
        Ps3ShaderCookBackendStub backend = new();
        object cooker = CreateCooker(_ => backend);
        IReadOnlyList<ShaderDefine> variantDefines = ParseDefines(variant.Defines);
        string[] expectedDefines = ShaderPlatformDefines.BuildDefines(
            ShaderCompileTarget.Ps3,
            new ShaderModel(4, 0),
            variantDefines).Select(DefineText).ToArray();

        Cook(cooker, request);

        Assert.Equal(2, backend.Requests.Count);
        Assert.Equal(ShaderStage.Vertex, backend.Requests[0].Stage);
        Assert.Equal(ShaderStage.Pixel, backend.Requests[1].Stage);
        Assert.Equal("LogicalVertex", backend.Requests[0].ProgramName);
        Assert.Equal("LogicalPixel", backend.Requests[1].ProgramName);
        Assert.Equal(variant.VertexEntryPoint, backend.Requests[0].EntryPoint);
        Assert.Equal(variant.PixelEntryPoint, backend.Requests[1].EntryPoint);
        Assert.All(backend.Requests, compileRequest => Assert.Equal(expectedDefines, compileRequest.Defines.Select(DefineText)));
    }

    /// <summary>Checks a custom shader accepts only the explicit lowercase default pair and uses no variant defines.</summary>
    [Fact]
    public void Cook_CustomShaderDefaultUsesRequestedProgramsAndNoVariantDefines() {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("custom-shader", "default", "CustomVertex", "CustomPixel")],
            [Source("custom-shader", sourcePath)], graphicsProfile: string.Empty);
        Ps3ShaderCookBackendStub backend = new();
        object cooker = CreateCooker(_ => backend);

        Cook(cooker, request);

        Assert.Equal("CustomVertex", backend.Requests[0].ProgramName);
        Assert.Equal("CustomPixel", backend.Requests[1].ProgramName);
        Assert.Equal("VS", backend.Requests[0].EntryPoint);
        Assert.Equal("PS", backend.Requests[1].EntryPoint);
        Assert.All(backend.Requests, compileRequest => Assert.DoesNotContain(compileRequest.Defines, define =>
            define.Name != "HEL_API_PS3" && define.Name != "HEL_SM_MAJOR" && define.Name != "HEL_SM_MINOR"));
    }

    /// <summary>Checks wrong targets, profiles and incomplete program selections fail before backend creation.</summary>
    [Fact]
    public void Cook_ValidatesPlatformProfileAndProgramSelectionBeforeFactory() {
        string sourcePath = CreateSourcePath();
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object cooker = CreateCooker(_ => {
            factoryCalls++;
            return backend;
        });
        PlatformShaderArtifactCookSource source = Source("asset-validation", sourcePath);

        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest(
            [Dependency("asset-validation", "default")], [source], platform: "switch")));
        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest(
            [Dependency("asset-validation", "default")], [source], graphicsProfile: "other-profile")));
        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest(
            [new PlatformShaderDependency("asset-validation", string.Empty, string.Empty, string.Empty)], [source])));

        Assert.Equal(0, factoryCalls);
        Assert.Empty(backend.Requests);
    }

    /// <summary>Rejects relative, drive-relative and root-relative cook roots before backend construction or artifact writes.</summary>
    /// <param name="pathKind">Non-qualified Windows path form under test.</param>
    [Theory]
    [InlineData("relative")]
    [InlineData("drive-relative")]
    [InlineData("root-relative")]
    public void Cook_RejectsNonFullyQualifiedCookRootBeforeFactoryAndWrites(string pathKind) {
        string absoluteCookRoot = Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "cook");
        string cookRootPath = AsNonFullyQualifiedPath(pathKind, absoluteCookRoot);
        Assert.False(Path.IsPathFullyQualified(cookRootPath));
        Assert.Equal(Path.GetFullPath(absoluteCookRoot), Path.GetFullPath(cookRootPath));
        string sourcePath = CreateSourcePath();
        PlatformShaderDependency dependency = Dependency("asset-relative-cook-root", "default");
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [dependency], [Source(dependency.ShaderAssetId, sourcePath)], cookRootPath: absoluteCookRoot);
        SetCookRootPath(request, cookRootPath);
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object cooker = CreateCooker(_ => {
            factoryCalls++;
            return backend;
        });

        try {
            InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

            Assert.Contains("fully qualified", exception.Message, StringComparison.OrdinalIgnoreCase);
            Assert.Equal(0, factoryCalls);
            Assert.Empty(backend.Requests);
            Assert.False(Directory.Exists(Path.Combine(absoluteCookRoot, "shaders")));
        } finally {
            if (Directory.Exists(absoluteCookRoot)) {
                Directory.Delete(absoluteCookRoot, recursive: true);
            }
        }
    }

    /// <summary>Rejects non-fully-qualified source paths even when the current directory resolves them to an existing source.</summary>
    /// <param name="pathKind">Non-qualified Windows path form under test.</param>
    [Theory]
    [InlineData("relative")]
    [InlineData("drive-relative")]
    [InlineData("root-relative")]
    public void Cook_RejectsNonFullyQualifiedSourcePathBeforeFactoryAndWrites(string pathKind) {
        string absoluteSourcePath = CreateSourcePath();
        string sourcePath = AsNonFullyQualifiedPath(pathKind, absoluteSourcePath);
        Assert.False(Path.IsPathFullyQualified(sourcePath));
        Assert.Equal(Path.GetFullPath(absoluteSourcePath), Path.GetFullPath(sourcePath));
        PlatformShaderDependency dependency = Dependency("asset-relative-source-path", "default");
        PlatformShaderArtifactCookSource source = Source(dependency.ShaderAssetId, absoluteSourcePath);
        PlatformShaderArtifactCookRequest request = CreateRequest([dependency], [source]);
        SetSourcePath(source, sourcePath);
        string absoluteCookRoot = Path.GetFullPath(request.CookRootPath);
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object cooker = CreateCooker(_ => {
            factoryCalls++;
            return backend;
        });

        try {
            InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

            Assert.Contains("fully qualified", exception.Message, StringComparison.OrdinalIgnoreCase);
            Assert.Equal(0, factoryCalls);
            Assert.Empty(backend.Requests);
            Assert.False(Directory.Exists(Path.Combine(absoluteCookRoot, "shaders")));
        } finally {
            if (Directory.Exists(absoluteCookRoot)) {
                Directory.Delete(absoluteCookRoot, recursive: true);
            }
        }
    }

    /// <summary>Checks source matching, provenance, source text and source parent-directory requirements fail early.</summary>
    [Fact]
    public void Cook_RejectsMissingOrInvalidSourceProvenanceBeforeFactory() {
        string sourcePath = CreateSourcePath();
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object cooker = CreateCooker(_ => {
            factoryCalls++;
            return backend;
        });
        PlatformShaderDependency dependency = Dependency("asset-source", "default");

        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest([dependency], [])));
        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest([dependency], [
            new PlatformShaderArtifactCookSource("asset-source", "hash", "source snapshot")
        ])));
        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest([dependency], [
            new PlatformShaderArtifactCookSource("asset-source", "hash", " ", sourcePath)
        ])));
        string missingParent = Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "absent", "shader.hlsl");
        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest([dependency], [Source("asset-source", missingParent)])));

        Assert.Equal(0, factoryCalls);
        Assert.Empty(backend.Requests);
    }

    /// <summary>Checks unknown standard and nonstandard variants are rejected without inventing entry points.</summary>
    [Fact]
    public void Cook_RejectsUnsupportedVariantsBeforeFactory() {
        string sourcePath = CreateSourcePath();
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object cooker = CreateCooker(_ => {
            factoryCalls++;
            return backend;
        });

        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest(
            [Dependency("ForwardStandardShader", "forwardstandard")],
            [Source("ForwardStandardShader", sourcePath)])));
        Assert.Throws<InvalidOperationException>(() => Cook(cooker, CreateRequest(
            [Dependency("custom-shader", "Default")],
            [Source("custom-shader", sourcePath)])));

        Assert.Equal(0, factoryCalls);
        Assert.Empty(backend.Requests);
    }

    /// <summary>Checks a later invalid dependency cannot replace an earlier preexisting artifact.</summary>
    [Fact]
    public void Cook_LaterInvalidDependencyLeavesEveryExistingArtifactUntouched() {
        string firstPath = CreateSourcePath();
        string laterPath = CreateSourcePath();
        PlatformShaderDependency first = Dependency("asset-before-invalid", "default");
        PlatformShaderDependency later = Dependency("custom-shader", "unsupported");
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [first, later], [Source(first.ShaderAssetId, firstPath), Source(later.ShaderAssetId, laterPath)]);
        byte[] sentinel = [0xC0, 0xFF, 0xEE];
        string sentinelPath = ArtifactPath(request, new Ps3ShaderArtifactIdentity(first.ShaderAssetId, first.VertexProgramName, first.PixelProgramName, first.VariantName));
        Directory.CreateDirectory(Path.GetDirectoryName(sentinelPath));
        File.WriteAllBytes(sentinelPath, sentinel);
        Ps3ShaderCookBackendStub backend = new();
        object cooker = CreateCooker(_ => backend);

        Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

        Assert.Equal(sentinel, File.ReadAllBytes(sentinelPath));
        Assert.Empty(backend.Requests);
    }

    /// <summary>Checks a later compiler failure occurs before any final artifact is replaced or declared.</summary>
    [Fact]
    public void Cook_LaterCompileFailureLeavesEveryExistingArtifactUntouched() {
        string firstPath = CreateSourcePath();
        string secondPath = CreateSourcePath();
        PlatformShaderDependency first = Dependency("asset-before-compile-failure", "default");
        PlatformShaderDependency second = Dependency("asset-compile-failure", "default");
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [first, second], [Source(first.ShaderAssetId, firstPath), Source(second.ShaderAssetId, secondPath)]);
        byte[] sentinel = [0x11, 0x22, 0x33];
        string sentinelPath = ArtifactPath(request, new Ps3ShaderArtifactIdentity(first.ShaderAssetId, first.VertexProgramName, first.PixelProgramName, first.VariantName));
        Directory.CreateDirectory(Path.GetDirectoryName(sentinelPath));
        File.WriteAllBytes(sentinelPath, sentinel);
        Ps3ShaderCookBackendStub backend = new() {
            FailureOnCall = 3,
            Failure = new InvalidOperationException("injected second-pair failure")
        };
        object cooker = CreateCooker(_ => backend);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

        Assert.Equal("injected second-pair failure", exception.Message);
        Assert.Equal(3, backend.Requests.Count);
        Assert.Equal(sentinel, File.ReadAllBytes(sentinelPath));
        Assert.Empty(Directory.GetFiles(Path.GetDirectoryName(sentinelPath), "*.tmp"));
    }

    /// <summary>Checks a backend factory exception happens before a preexisting final artifact can be replaced.</summary>
    [Fact]
    public void Cook_BackendFactoryFailureLeavesExistingArtifactUntouched() {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("asset-factory-failure", "default")], [Source("asset-factory-failure", sourcePath)]);
        byte[] sentinel = [0x44, 0x55, 0x66];
        string finalPath = ArtifactPath(request, new Ps3ShaderArtifactIdentity("asset-factory-failure", "Vertex", "Pixel", "default"));
        Directory.CreateDirectory(Path.GetDirectoryName(finalPath));
        File.WriteAllBytes(finalPath, sentinel);
        InvalidOperationException expected = new("injected backend factory failure");
        int factoryCalls = 0;
        object cooker = CreateCooker(_ => {
            factoryCalls++;
            Assert.Equal(sentinel, File.ReadAllBytes(finalPath));
            throw expected;
        });

        InvalidOperationException actual = Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

        Assert.Same(expected, actual);
        Assert.Equal(1, factoryCalls);
        Assert.Equal(sentinel, File.ReadAllBytes(finalPath));
        Assert.Single(Directory.GetFiles(Path.GetDirectoryName(finalPath)));
    }

    /// <summary>Checks a generic or unsuccessful shared result cannot become a declared PS3 artifact.</summary>
    [Theory]
    [InlineData(true, false)]
    [InlineData(true, true)]
    public void Cook_RejectsUnsuccessfulOrUntypedBackendResult(bool returnUntyped, bool success) {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("asset-bad-result", "default")], [Source("asset-bad-result", sourcePath)]);
        Ps3ShaderCookBackendStub backend = new() { ReturnUntypedResult = returnUntyped, UntypedResultSuccess = success };
        object cooker = CreateCooker(_ => backend);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

        Assert.Contains("typed PS3 result", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Empty(Directory.Exists(Path.Combine(request.CookRootPath, "shaders"))
            ? Directory.GetFiles(Path.Combine(request.CookRootPath, "shaders"))
            : Array.Empty<string>());
    }

    /// <summary>Checks a typed result for a different request identity is rejected before pair publication.</summary>
    [Fact]
    public void Cook_RejectsBackendResultForDifferentRequest() {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("asset-unrelated-result", "default")], [Source("asset-unrelated-result", sourcePath)]);
        Ps3ShaderCookBackendStub backend = new() {
            ReturnedRequestFactory = original => new ShaderCompileRequest(
                original.Source,
                "unrelated-program",
                original.EntryPoint,
                original.Stage,
                original.Target,
                original.ShaderModel,
                original.Variant,
                original.Defines,
                original.Options)
        };
        object cooker = CreateCooker(_ => backend);

        Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));
        Assert.Empty(Directory.Exists(Path.Combine(request.CookRootPath, "shaders"))
            ? Directory.GetFiles(Path.Combine(request.CookRootPath, "shaders"))
            : Array.Empty<string>());
    }

    /// <summary>Checks a typed result whose stored stage differs from the submitted stage is refused.</summary>
    [Fact]
    public void Cook_RejectsBackendResultForDifferentStage() {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("asset-stage-mismatch", "default")], [Source("asset-stage-mismatch", sourcePath)]);
        Ps3ShaderCookBackendStub backend = new() {
            ReturnedRequestFactory = original => new ShaderCompileRequest(
                original.Source,
                original.ProgramName,
                original.EntryPoint,
                ShaderStage.Pixel,
                original.Target,
                original.ShaderModel,
                original.Variant,
                original.Defines,
                original.Options)
        };
        object cooker = CreateCooker(_ => backend);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Cook(cooker, request));

        Assert.Contains("different shader request", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Single(backend.Requests);
    }

    /// <summary>Checks source provenance and complete identity appear in the deterministic P3SP artifact.</summary>
    [Fact]
    public void Cook_PreservesSourceHashAndReplacesOnlyItsDeterministicArtifact() {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = CreateRequest(
            [Dependency("asset-provenance", "default")],
            [new PlatformShaderArtifactCookSource("asset-provenance", "exact-source-hash", "source snapshot", sourcePath)]);
        Ps3ShaderCookBackendStub backend = new();
        object cooker = CreateCooker(_ => backend);
        Ps3ShaderArtifactIdentity identity = new("asset-provenance", "Vertex", "Pixel", "default");
        string finalPath = ArtifactPath(request, identity);

        PlatformShaderArtifactCookResult first = Cook(cooker, request);
        byte[] firstBytes = File.ReadAllBytes(finalPath);
        PlatformShaderArtifactCookResult second = Cook(cooker, request);
        byte[] secondBytes = File.ReadAllBytes(finalPath);

        Assert.Single(first.CookedArtifactDeclarations);
        Assert.Single(second.CookedArtifactDeclarations);
        Assert.Equal(firstBytes, secondBytes);
        Assert.Equal(new[] { "asset-provenance", "Vertex", "Pixel", "default", "exact-source-hash" }, ReadIdentityText(firstBytes));
        IReadOnlyList<byte[]> stageBlobs = ReadStageBlobs(firstBytes);
        Assert.Equal(2, stageBlobs.Count);
        Assert.Equal(new byte[] { 0x51, 0x53, 0x33 }, stageBlobs[0]);
        Assert.Equal(new byte[] { 0x51, 0x53, 0x33 }, stageBlobs[1]);
        Assert.Equal("cooked/" + identity.RelativePath, first.CookedArtifactDeclarations[0].RelativePath);
        Assert.Equal(identity.RelativePath, first.CookedArtifactDeclarations[0].LogicalArtifactId);
        Assert.Equal("shader", first.CookedArtifactDeclarations[0].ArtifactKind);
        Assert.Equal("default", first.CookedArtifactDeclarations[0].VariantId);
    }

    /// <summary>Compiles an include-using pair with the configured SDK and checks artifact identity and recook bytes.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void Cook_WithPinnedDockerBackend_CompilesIncludedPairsForDistinctAssetsDeterministically() {
        string sourcePath = CreateSourcePath();
        string includePath = Path.Combine(Path.GetDirectoryName(sourcePath), "common.hlsli");
        const string sourceText = """
            #include "common.hlsli"
            struct VertexInput { float3 Position : POSITION; };
            struct VertexOutput { float4 Position : SV_Position; float4 Color : COLOR0; };
            VertexOutput VS(VertexInput input) { VertexOutput output; output.Position = float4(input.Position, 1.0); output.Color = IncludedColor(); return output; }
            float4 PS(VertexOutput input) : SV_Target { return input.Color; }
            """;
        File.WriteAllText(includePath, "float4 IncludedColor() { return float4(0.25, 0.5, 0.75, 1.0); }");
        string sourceHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(sourceText))).ToLowerInvariant();
        PlatformShaderDependency[] dependencies = [
            Dependency("docker-asset-one", "default"),
            Dependency("docker-asset-two", "default")
        ];
        PlatformShaderArtifactCookSource[] sources = [
            new("docker-asset-one", sourceHash, sourceText, sourcePath),
            new("docker-asset-two", sourceHash, sourceText, sourcePath)
        ];
        PlatformShaderArtifactCookRequest request = CreateRequest(dependencies, sources);
        object cooker = CreateCooker();

        PlatformShaderArtifactCookResult first = Cook(cooker, request);
        Dictionary<string, byte[]> firstBytes = first.CookedArtifactDeclarations.ToDictionary(
            declaration => declaration.RelativePath,
            declaration => File.ReadAllBytes(ArtifactPath(request, declaration.LogicalArtifactId)));
        PlatformShaderArtifactCookResult second = Cook(cooker, request);

        Assert.Equal(2, first.CookedArtifactDeclarations.Count);
        Assert.Equal(first.CookedArtifactDeclarations.Select(item => item.RelativePath).OrderBy(path => path, StringComparer.Ordinal),
            second.CookedArtifactDeclarations.Select(item => item.RelativePath).OrderBy(path => path, StringComparer.Ordinal));
        foreach (PlatformCookedArtifactDeclaration declaration in second.CookedArtifactDeclarations) {
            Assert.Equal("cooked/" + declaration.LogicalArtifactId, declaration.RelativePath);
            byte[] bytes = File.ReadAllBytes(ArtifactPath(request, declaration.LogicalArtifactId));
            Assert.Equal(firstBytes[declaration.RelativePath], bytes);
            Assert.Equal(new[] { declaration.LogicalArtifactId == new Ps3ShaderArtifactIdentity("docker-asset-one", "Vertex", "Pixel", "default").RelativePath
                    ? "docker-asset-one" : "docker-asset-two", "Vertex", "Pixel", "default", sourceHash }, ReadIdentityText(bytes));
        }
    }

    /// <summary>Creates an injected cooker instance through its planned public constructor.</summary>
    /// <param name="backendFactory">Optional backend factory used to avoid SDK access.</param>
    /// <returns>The concrete cooker instance.</returns>
    static object CreateCooker(Func<string, IShaderBackend> backendFactory = null) {
        Type cookerType = BuilderAssembly.GetType("helengine.ps3.builder.Ps3ShaderArtifactCooker");
        Assert.NotNull(cookerType);
        object cooker = backendFactory == null
            ? Activator.CreateInstance(cookerType)
            : Activator.CreateInstance(cookerType, [backendFactory]);
        Assert.NotNull(cooker);
        return cooker;
    }

    /// <summary>Invokes the planned cooker method and preserves its original exception for useful assertions.</summary>
    /// <param name="cooker">Concrete cooker instance.</param>
    /// <param name="request">Platform shader cook request.</param>
    /// <returns>Published artifact declarations.</returns>
    static PlatformShaderArtifactCookResult Cook(object cooker, PlatformShaderArtifactCookRequest request) {
        MethodInfo method = cooker.GetType().GetMethod("Cook", BindingFlags.Instance | BindingFlags.Public,
            binder: null, types: [typeof(PlatformShaderArtifactCookRequest)], modifiers: null);
        Assert.NotNull(method);
        try {
            return Assert.IsType<PlatformShaderArtifactCookResult>(method.Invoke(cooker, [request]));
        } catch (TargetInvocationException exception) when (exception.InnerException != null) {
            ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
            throw;
        }
    }

    /// <summary>Creates one valid dependency with optional authored pair names.</summary>
    /// <param name="assetId">Shader asset identity.</param>
    /// <param name="variant">Requested variant.</param>
    /// <param name="vertexProgram">Vertex logical program name.</param>
    /// <param name="pixelProgram">Pixel logical program name.</param>
    /// <returns>A platform dependency value.</returns>
    static PlatformShaderDependency Dependency(string assetId, string variant, string vertexProgram = "Vertex", string pixelProgram = "Pixel") =>
        new(assetId, vertexProgram, pixelProgram, variant);

    /// <summary>Creates a source snapshot whose parent exists while the authored source file may remain absent.</summary>
    /// <param name="assetId">Owning shader asset.</param>
    /// <param name="sourcePath">Fully qualified source path for relative include resolution.</param>
    /// <returns>Resolved shader source snapshot.</returns>
    static PlatformShaderArtifactCookSource Source(string assetId, string sourcePath) =>
        new(assetId, "source-hash", "snapshot source", sourcePath);

    /// <summary>Creates a complete cook request under a unique build-owned fixture directory.</summary>
    /// <param name="dependencies">Ordered dependency list.</param>
    /// <param name="sources">Matching source snapshots.</param>
    /// <param name="platform">Selected target platform.</param>
    /// <param name="graphicsProfile">Selected graphics profile, blank when unspecified.</param>
    /// <returns>An immutable platform shader cook request.</returns>
    static PlatformShaderArtifactCookRequest CreateRequest(
        IReadOnlyList<PlatformShaderDependency> dependencies,
        IReadOnlyList<PlatformShaderArtifactCookSource> sources,
        string platform = "ps3",
        string graphicsProfile = "ps3-standard",
        string cookRootPath = null) =>
        new(cookRootPath ?? Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "cook"), platform, "release", graphicsProfile, dependencies, sources, helengine.NumericPrecisionPolicy.Default);

    /// <summary>Converts an absolute fixture path into a Windows path form that is not fully qualified but resolves identically.</summary>
    /// <param name="pathKind">Requested relative, drive-relative or root-relative form.</param>
    /// <param name="absolutePath">Existing fully-qualified fixture path.</param>
    /// <returns>A non-fully-qualified path resolving to the same fixture location.</returns>
    static string AsNonFullyQualifiedPath(string pathKind, string absolutePath) {
        string fullPath = Path.GetFullPath(absolutePath);
        string relativePath = Path.GetRelativePath(Environment.CurrentDirectory, fullPath);
        string pathRoot = Path.GetPathRoot(fullPath);
        string currentPathRoot = Path.GetPathRoot(Environment.CurrentDirectory);
        if (!string.Equals(pathRoot, currentPathRoot, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Path-form test fixtures must share the current drive.");
        }

        return pathKind switch {
            "relative" => relativePath,
            "drive-relative" => pathRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + relativePath,
            "root-relative" => fullPath[(pathRoot.Length - 1)..],
            _ => throw new ArgumentOutOfRangeException(nameof(pathKind), pathKind, "Unknown non-qualified path form."),
        };
    }

    /// <summary>Injects a malformed root into an otherwise-valid immutable request to exercise cooker boundary validation.</summary>
    /// <param name="request">Valid request created through its public constructor.</param>
    /// <param name="cookRootPath">Non-fully-qualified root supplied to the cooker.</param>
    static void SetCookRootPath(PlatformShaderArtifactCookRequest request, string cookRootPath) {
        FieldInfo field = typeof(PlatformShaderArtifactCookRequest).GetField(
            "<CookRootPath>k__BackingField", BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.NotNull(field);
        field.SetValue(request, cookRootPath);
        Assert.Equal(cookRootPath, request.CookRootPath);
    }

    /// <summary>Injects a malformed source path into an otherwise-valid immutable source for cooker boundary validation.</summary>
    /// <param name="source">Valid source created through its public constructor.</param>
    /// <param name="sourcePath">Non-fully-qualified path supplied to the cooker.</param>
    static void SetSourcePath(PlatformShaderArtifactCookSource source, string sourcePath) {
        FieldInfo field = typeof(PlatformShaderArtifactCookSource).GetField(
            "SourcePathValue", BindingFlags.Instance | BindingFlags.NonPublic);
        Assert.NotNull(field);
        field.SetValue(source, sourcePath);
        Assert.Equal(sourcePath, source.SourcePath);
    }

    /// <summary>Creates a valid empty request without constructing a source or contacting an SDK.</summary>
    /// <returns>An empty shader dependency request.</returns>
    static PlatformShaderArtifactCookRequest CreateEmptyRequest() =>
        CreateRequest(Array.Empty<PlatformShaderDependency>(), Array.Empty<PlatformShaderArtifactCookSource>());

    /// <summary>Creates an owned fixture source path and its parent directory.</summary>
    /// <returns>A source path under the explicit fixture-storage root.</returns>
    static string CreateSourcePath() {
        string sourceDirectory = Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "source");
        Directory.CreateDirectory(sourceDirectory);
        return Path.Combine(sourceDirectory, "shader.hlsl");
    }

    /// <summary>Resolves the required fixture-storage directory under the task-owned build root.</summary>
    /// <returns>A directory reserved for input fixtures and SDK compiler outputs.</returns>
    static string GetFixtureRoot() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(outputRoot), "HELENGINE_PS3_SHADER_TEST_OUTPUT must name the cooker output root.");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "HELENGINE_PS3_REPOSITORY_ROOT must name the PS3 checkout.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(outputRoot, repositoryRoot);
        string fixtureRoot = Path.Combine(validatedRoot, "fixture-storage");
        Directory.CreateDirectory(fixtureRoot);
        return fixtureRoot;
    }

    /// <summary>Builds the final artifact path from a safe relative identity path.</summary>
    /// <param name="request">Cook operation that owns the output root.</param>
    /// <param name="identity">Artifact identity whose relative path is hash-safe.</param>
    /// <returns>Absolute final artifact path.</returns>
    static string ArtifactPath(PlatformShaderArtifactCookRequest request, Ps3ShaderArtifactIdentity identity) =>
        ArtifactPath(request, identity.RelativePath);

    /// <summary>Builds the physical artifact path from its stable cook-root-relative identity.</summary>
    /// <param name="request">Cook operation that owns the output root.</param>
    /// <param name="relativePath">Cook-root-relative artifact identity path.</param>
    /// <returns>Absolute final artifact path.</returns>
    static string ArtifactPath(PlatformShaderArtifactCookRequest request, string relativePath) =>
        Path.Combine(request.CookRootPath, relativePath.Replace('/', Path.DirectorySeparatorChar));

    /// <summary>Reads the first five source identity strings from the version 1 P3SP header.</summary>
    /// <param name="artifactBytes">Serialized pair bytes.</param>
    /// <returns>Asset, vertex program, pixel program, variant and source hash.</returns>
    static string[] ReadIdentityText(byte[] artifactBytes) {
        Assert.Equal(new byte[] { 0x50, 0x33, 0x53, 0x50 }, artifactBytes[..4]);
        Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(4, 4)));
        int offset = 12;
        string[] values = new string[5];
        for (int index = 0; index < values.Length; index++) {
            uint byteLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4;
            values[index] = new UTF8Encoding(false, true).GetString(artifactBytes.AsSpan(offset, checked((int)byteLength)));
            offset += checked((int)byteLength);
        }
        return values;
    }

    /// <summary>Reads both SDK blobs from a minimal emitted pair and verifies no source metadata records are present.</summary>
    /// <param name="artifactBytes">Serialized P3SP pair bytes.</param>
    /// <returns>Vertex and pixel SDK byte arrays in artifact order.</returns>
    static IReadOnlyList<byte[]> ReadStageBlobs(byte[] artifactBytes) {
        int offset = 12;
        for (int index = 0; index < 6; index++) {
            uint length = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4 + checked((int)length);
        }

        List<byte[]> blobs = [];
        for (int stageIndex = 0; stageIndex < 2; stageIndex++) {
            offset += 4;
            uint entryPointLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4 + checked((int)entryPointLength);
            uint blobLength = BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4));
            offset += 4;
            blobs.Add(artifactBytes.AsSpan(offset, checked((int)blobLength)).ToArray());
            offset += checked((int)blobLength);
            Assert.Equal(0u, BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4)));
            offset += 4;
            Assert.Equal(0u, BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(offset, 4)));
            offset += 4;
        }
        return blobs;
    }

    /// <summary>Parses shared define strings using their documented first-equals convention.</summary>
    /// <param name="definitions">Shared variant definition strings.</param>
    /// <returns>Typed define records in source order.</returns>
    static IReadOnlyList<ShaderDefine> ParseDefines(IReadOnlyList<string> definitions) {
        List<ShaderDefine> result = [];
        foreach (string definition in definitions) {
            int separator = definition.IndexOf('=');
            string name = separator < 0 ? definition : definition[..separator];
            string value = separator < 0 ? "1" : definition[(separator + 1)..];
            result.Add(new ShaderDefine(name, value));
        }
        return result;
    }

    /// <summary>Formats one define as the exact name/value spelling seen by the backend.</summary>
    /// <param name="define">Typed shared define.</param>
    /// <returns>Name or name/value text.</returns>
    static string DefineText(ShaderDefine define) => define.Name + "=" + define.Value;
}
