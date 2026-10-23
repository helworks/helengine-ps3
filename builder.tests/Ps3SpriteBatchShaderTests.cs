using helengine;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps3.builder;
using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using Silk.NET.SPIRV;
using Silk.NET.SPIRV.Cross;

namespace helengine.ps3.builder.tests;

/// <summary>Checks the PS3 cooker dispatches only the declared built-in sprite batch variants.</summary>
[Collection("PS3 shader process environment")]
public sealed class Ps3SpriteBatchShaderTests {
    /// <summary>Provides each shared sprite batch variant as an independent cooker case.</summary>
    public static IEnumerable<object[]> SpriteBatchVariantCases => SpriteBatchShaderVariants.All
        .Select(variant => new object[] { variant });

    /// <summary>Checks each exact shared sprite batch variant preserves its entry points and define in PS3 requests.</summary>
    /// <param name="variant">Shared built-in variant to pass through the cooker.</param>
    [Theory]
    [MemberData(nameof(SpriteBatchVariantCases))]
    public void Cook_UsesExactSharedSpriteBatchVariant(StandardShaderVariant variant) {
        string sourcePath = CreateSourcePath();
        PlatformShaderDependency dependency = new("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant.Name);
        PlatformShaderArtifactCookSource source = new("SpriteBatchShader", "sprite-batch-source", "snapshot source", sourcePath);
        PlatformShaderArtifactCookRequest request = new(
            Path.Combine(RequiredArtifactsRoot(), "sprite-batch", Guid.NewGuid().ToString("N")),
            "ps3",
            "release",
            "ps3-standard",
            [dependency],
            [source], helengine.NumericPrecisionPolicy.Default);
        Ps3ShaderCookBackendStub backend = new();
        Ps3ShaderArtifactCooker cooker = new(_ => backend);

        PlatformShaderArtifactCookResult result = cooker.Cook(request);

        Assert.Single(result.CookedArtifactDeclarations);
        Assert.Equal(2, backend.Requests.Count);
        Assert.Equal("SpriteBatchShader.vs", backend.Requests[0].ProgramName);
        Assert.Equal("SpriteBatchShader.ps", backend.Requests[1].ProgramName);
        Assert.Equal(variant.Name, backend.Requests[0].Variant);
        Assert.Equal(variant.Name, backend.Requests[1].Variant);
        Assert.Equal(variant.VertexEntryPoint, backend.Requests[0].EntryPoint);
        Assert.Equal(variant.PixelEntryPoint, backend.Requests[1].EntryPoint);
        Assert.Equal(variant.Defines, backend.Requests[0].Defines
            .Where(define => define.Name == "HELENGINE_BATCH_ROUNDED")
            .Select(define => define.Name + "=" + define.Value));
        Assert.Equal(variant.Defines, backend.Requests[1].Defines
            .Where(define => define.Name == "HELENGINE_BATCH_ROUNDED")
            .Select(define => define.Name + "=" + define.Value));
    }

    /// <summary>Rejects default and unknown names for the built-in identity before constructing a compiler backend.</summary>
    /// <param name="variantName">Variant name absent from the exact shared sprite batch catalog.</param>
    [Theory]
    [InlineData("default")]
    [InlineData("Unknown")]
    public void Cook_RejectsUnlistedSpriteBatchVariantsBeforeFactory(string variantName) {
        string sourcePath = CreateSourcePath();
        PlatformShaderArtifactCookRequest request = new(
            Path.Combine(RequiredArtifactsRoot(), "sprite-batch", Guid.NewGuid().ToString("N")),
            "ps3",
            "release",
            "ps3-standard",
            [new PlatformShaderDependency("SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variantName)],
            [new PlatformShaderArtifactCookSource("SpriteBatchShader", "sprite-batch-source", "snapshot source", sourcePath)], helengine.NumericPrecisionPolicy.Default);
        int factoryCalls = 0;
        Ps3ShaderArtifactCooker cooker = new(_ => {
            factoryCalls++;
            return new Ps3ShaderCookBackendStub();
        });

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => cooker.Cook(request));

        Assert.Contains("Sprite Batch Shader variant", exception.Message, StringComparison.Ordinal);
        Assert.Equal(0, factoryCalls);
    }

    /// <summary>Reflects the four already-exported Vulkan modules with SPIRV-Cross, independent of PS3 translation.</summary>
    [Fact]
    public unsafe void VulkanBytecode_ReflectsActiveSpriteBatchInterfacesAndBindings() {
        string bytecodeRoot = RequiredBytecodeExportDirectory();
        AssertVulkanBytecode(bytecodeRoot, "Textured", "Vertex", ShaderStage.Vertex, ExecutionModel.Vertex);
        AssertVulkanBytecode(bytecodeRoot, "RoundedShape", "Vertex", ShaderStage.Vertex, ExecutionModel.Vertex);
        AssertVulkanBytecode(bytecodeRoot, "Textured", "Pixel", ShaderStage.Pixel, ExecutionModel.Fragment);
        AssertVulkanBytecode(bytecodeRoot, "RoundedShape", "Pixel", ShaderStage.Pixel, ExecutionModel.Fragment);
    }

    /// <summary>Runs both exact sprite batch variants through the pinned Docker SDK and publishes immutable P3SP pairs.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void Cook_WithPinnedDockerBackend_ConvertsBothSpriteBatchVariantsToValidatedP3sp() {
        string repositoryRoot = RequiredRepositoryRoot();
        string helengineRoot = Path.Combine(Directory.GetParent(repositoryRoot).FullName, "helengine");
        string sourcePath = Path.Combine(helengineRoot, "engine", "helengine.editor", "shaders", "builtin", "SpriteBatchShader.hlsl");
        string sourceText = File.ReadAllText(sourcePath);
        string sourceHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(sourceText))).ToLowerInvariant();
        string cookRoot = Path.Combine(RequiredArtifactsRoot(), "real-docker-cooker", Guid.NewGuid().ToString("N"));
        PlatformShaderArtifactCookRequest request = new(
            cookRoot,
            "ps3",
            "release",
            "ps3-standard",
            SpriteBatchShaderVariants.All.Select(variant => new PlatformShaderDependency(
                "SpriteBatchShader", "SpriteBatchShader.vs", "SpriteBatchShader.ps", variant.Name)).ToArray(),
            [new PlatformShaderArtifactCookSource("SpriteBatchShader", sourceHash, sourceText, sourcePath)], helengine.NumericPrecisionPolicy.Default);

        PlatformShaderArtifactCookResult result = new Ps3ShaderArtifactCooker().Cook(request);

        Assert.Equal(new[] { "Textured", "RoundedShape" }, result.CookedArtifactDeclarations.Select(declaration => declaration.VariantId));
        List<string> artifactHashes = [];
        foreach (PlatformCookedArtifactDeclaration declaration in result.CookedArtifactDeclarations) {
            string artifactPath = Path.Combine(cookRoot, declaration.LogicalArtifactId.Replace('/', Path.DirectorySeparatorChar));
            byte[] artifactBytes = File.ReadAllBytes(artifactPath);
            Assert.True(artifactBytes.Length > 128);
            Assert.Equal(new byte[] { 0x50, 0x33, 0x53, 0x50 }, artifactBytes[..4]);
            Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(4, 4)));
            Assert.Equal((uint)artifactBytes.Length, BinaryPrimitives.ReadUInt32BigEndian(artifactBytes.AsSpan(8, 4)));
            artifactHashes.Add($"{declaration.VariantId} {Convert.ToHexString(SHA256.HashData(artifactBytes))} {artifactPath}");
        }
        File.WriteAllLines(Path.Combine(cookRoot, "P3SP-SHA256.txt"), artifactHashes, new UTF8Encoding(false));
    }

    /// <summary>Creates an absolute source identity whose parent is inside the configured build root.</summary>
    /// <returns>A nonexistent shader path with an existing parent directory for immutable source snapshots.</returns>
    static string CreateSourcePath() {
        string directory = Path.Combine(RequiredArtifactsRoot(), "sprite-batch-fixtures", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        return Path.Combine(directory, "SpriteBatchShader.hlsl");
    }

    /// <summary>Checks one exported Vulkan SPIR-V module through the existing SPIRV-Cross dependency.</summary>
    /// <param name="bytecodeRoot">Read-only directory containing the shared compiler's named SPIR-V exports.</param>
    /// <param name="variantName">Exact compiled variant identity.</param>
    /// <param name="stageName">Filename stage identity.</param>
    /// <param name="stage">Shared stage associated with the expected active inputs.</param>
    /// <param name="executionModel">SPIR-V entry-point execution model.</param>
    static unsafe void AssertVulkanBytecode(
        string bytecodeRoot,
        string variantName,
        string stageName,
        ShaderStage stage,
        ExecutionModel executionModel) {
        string bytecodePath = Path.Combine($"{bytecodeRoot}", $"SpriteBatchShader.Vulkan.{variantName}.{stageName}.bin");
        byte[] bytecode = File.ReadAllBytes(bytecodePath);
        Assert.True(bytecode.Length >= 20);
        Assert.Equal(0x07230203u, BitConverter.ToUInt32(bytecode, 0));

        string nativeLibrary = Path.Combine(
            Path.GetDirectoryName(typeof(Cross).Assembly.Location),
            "runtimes",
            "win-x64",
            "native",
            "spirv-cross.dll");
        Assert.True(File.Exists(nativeLibrary), $"Existing SPIRV-Cross native library was not found: {nativeLibrary}");
        using Cross api = new(Cross.CreateDefaultContext([nativeLibrary]));
        Context* context = null;
        Assert.Equal(Result.Success, api.ContextCreate(&context));
        try {
            ParsedIr* parsed = null;
            fixed (byte* data = bytecode) {
                Assert.Equal(Result.Success, api.ContextParseSpirv(context, (uint*)data, (nuint)(bytecode.Length / sizeof(uint)), &parsed));
            }

            Compiler* compiler = null;
            Assert.Equal(Result.Success, api.ContextCreateCompiler(context, Backend.Glsl, parsed, CaptureMode.TakeOwnership, &compiler));
            Assert.Equal(Result.Success, api.CompilerSetEntryPoint(compiler, stageName == "Vertex" ? "VS" : "PS", executionModel));
            Resources* resources = null;
            Assert.Equal(Result.Success, api.CompilerCreateShaderResources(compiler, &resources));

            ReflectedResource* inputs = null;
            nuint inputCount = 0;
            Assert.Equal(Result.Success, api.ResourcesGetResourceListForType(resources, ResourceType.StageInput, ref inputs, ref inputCount));
            int expectedInputCount = stage == ShaderStage.Vertex
                ? variantName == "Textured" ? 3 : 6
                : variantName == "Textured" ? 2 : 5;
            Assert.Equal(expectedInputCount, checked((int)inputCount));
            for (nuint inputIndex = 0; inputIndex < inputCount; inputIndex++) {
                CrossType* inputType = api.CompilerGetTypeHandle(compiler, inputs[inputIndex].TypeId);
                Assert.Equal(32u, api.TypeGetBitWidth(inputType));
                Assert.InRange(api.TypeGetVectorSize(inputType), 2u, 4u);
            }

            if (stage == ShaderStage.Vertex) {
                ReflectedResource* uniformBuffers = null;
                nuint uniformBufferCount = 0;
                Assert.Equal(Result.Success, api.ResourcesGetResourceListForType(resources, ResourceType.UniformBuffer, ref uniformBuffers, ref uniformBufferCount));
                Assert.Equal((nuint)1, uniformBufferCount);
                ReflectedResource buffer = uniformBuffers[0];
                Assert.Equal(0u, api.CompilerGetDecoration(compiler, buffer.Id, Decoration.Binding));
                CrossType* bufferType = api.CompilerGetTypeHandle(compiler, buffer.BaseTypeId);
                string bufferName = api.CompilerGetNameS(compiler, buffer.Id);
                if (string.IsNullOrWhiteSpace(bufferName)) bufferName = api.CompilerGetNameS(compiler, buffer.BaseTypeId);
                Assert.Equal("BatchCameraBuffer", bufferName);
                nuint bufferSize = 0;
                Assert.Equal(Result.Success, api.CompilerGetDeclaredStructSize(compiler, bufferType, ref bufferSize));
                Assert.Equal((nuint)64, bufferSize);
                Assert.Equal(1u, api.TypeGetNumMemberTypes(bufferType));
                Assert.Equal("projection", api.CompilerGetMemberNameS(compiler, buffer.BaseTypeId, 0));
                CrossType* projectionType = api.CompilerGetTypeHandle(compiler, api.TypeGetMemberType(bufferType, 0));
                Assert.Equal(4u, api.TypeGetVectorSize(projectionType));
                Assert.Equal(4u, api.TypeGetColumns(projectionType));
            }

            if (stage == ShaderStage.Pixel && variantName == "Textured") {
                ReflectedResource* textures = null;
                nuint textureCount = 0;
                Assert.Equal(Result.Success, api.ResourcesGetResourceListForType(resources, ResourceType.SeparateImage, ref textures, ref textureCount));
                Assert.Equal((nuint)1, textureCount);
                Assert.Equal("BatchTexture", api.CompilerGetNameS(compiler, textures[0].Id));
                Assert.Equal(100u, api.CompilerGetDecoration(compiler, textures[0].Id, Decoration.Binding));
                Assert.Equal(0u, api.CompilerGetDecoration(compiler, textures[0].Id, Decoration.DescriptorSet));
                CrossType* textureType = api.CompilerGetTypeHandle(compiler, textures[0].TypeId);
                Assert.Contains("2D", api.TypeGetImageDimension(textureType).ToString(), StringComparison.OrdinalIgnoreCase);

                ReflectedResource* samplers = null;
                nuint samplerCount = 0;
                Assert.Equal(Result.Success, api.ResourcesGetResourceListForType(resources, ResourceType.SeparateSamplers, ref samplers, ref samplerCount));
                Assert.Equal((nuint)1, samplerCount);
                Assert.Equal("BatchSampler", api.CompilerGetNameS(compiler, samplers[0].Id));
                Assert.Equal(200u, api.CompilerGetDecoration(compiler, samplers[0].Id, Decoration.Binding));
                Assert.Equal(0u, api.CompilerGetDecoration(compiler, samplers[0].Id, Decoration.DescriptorSet));
            }
            if (stage == ShaderStage.Pixel && variantName == "RoundedShape") {
                Assert.Equal(0, ReadResourceCount(api, resources, ResourceType.SeparateImage));
                Assert.Equal(0, ReadResourceCount(api, resources, ResourceType.SeparateSamplers));
            }
        } finally {
            api.ContextDestroy(context);
        }
    }

    /// <summary>Counts one reflected resource category for assertions against optimized rounded bytecode.</summary>
    /// <param name="api">SPIRV-Cross function table for the live context.</param>
    /// <param name="resources">Resources owned by the selected entry point.</param>
    /// <param name="resourceType">Resource category to inspect.</param>
    /// <returns>Number of resources in the selected category.</returns>
    static unsafe int ReadResourceCount(Cross api, Resources* resources, ResourceType resourceType) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Assert.Equal(Result.Success, api.ResourcesGetResourceListForType(resources, resourceType, ref resourceList, ref resourceCount));
        return checked((int)resourceCount);
    }

    /// <summary>Returns the validated build-owned root configured for PS3 shader tests.</summary>
    /// <returns>The explicit artifacts directory beneath the sibling builds root.</returns>
    static string RequiredArtifactsRoot() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(outputRoot), "HELENGINE_PS3_SHADER_TEST_OUTPUT must name the cooker output root.");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "HELENGINE_PS3_REPOSITORY_ROOT must name the PS3 checkout.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(outputRoot, repositoryRoot);
        Directory.CreateDirectory(validatedRoot);
        return validatedRoot;
    }

    /// <summary>Requires the named, read-only directory containing the shared compiler's Vulkan bytecode exports.</summary>
    /// <returns>The configured shared-test export directory.</returns>
    static string RequiredBytecodeExportDirectory() {
        string bytecodeRoot = Environment.GetEnvironmentVariable("HELENGINE_SPRITE_BATCH_BYTECODE_EXPORT_DIR");
        Assert.False(string.IsNullOrWhiteSpace(bytecodeRoot), "HELENGINE_SPRITE_BATCH_BYTECODE_EXPORT_DIR must name the existing shared compiler exports.");
        Assert.True(Directory.Exists(bytecodeRoot), $"The Vulkan bytecode export directory does not exist: {bytecodeRoot}");
        return bytecodeRoot;
    }

    /// <summary>Reads the PS3 checkout path used to resolve the locked Docker toolchain.</summary>
    /// <returns>The configured PS3 repository root.</returns>
    static string RequiredRepositoryRoot() {
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "HELENGINE_PS3_REPOSITORY_ROOT must name the PS3 checkout.");
        return repositoryRoot;
    }
}
