using helengine;
using helengine.ps3.builder;
using System.Security.Cryptography;
using System.Text;

namespace helengine.ps3.builder.tests;

/// <summary>Produces the real locked-SDK shader pair used by the authored GPU pixel fixture.</summary>
public sealed class Ps3ShaderGpuFixtureTests {
    /// <summary>Source snapshot for the transformed RGBA texture and two-stage tint fixture.</summary>
    const string FixtureSource = """
        cbuffer Frame : register(b0) { float4x4 Transform; float4 Tint; };
        cbuffer Surface : register(b1) { float4 PixelTint; };
        Texture2D Albedo : register(t0); SamplerState LinearSampler : register(s0);
        struct VIn { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
        struct VOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; float4 Color : COLOR0; };
        VOut VS(VIn v) { VOut o; o.Position=mul(float4(v.Position,1),Transform); o.Uv=v.Uv; o.Color=Tint; return o; }
        float4 PS(VOut v) : SV_Target { return Albedo.Sample(LinearSampler,v.Uv)*v.Color*PixelTint; }
        """;

    /// <summary>Compiles the fixture with the pinned SDK and writes its P3SP plus exact raw stage blobs.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void ProducesAuthoredGpuFixture() {
        string repositoryRoot = RequiredRepositoryRoot();
        string fixtureRoot = RequiredFixtureRoot(repositoryRoot);
        Directory.CreateDirectory(fixtureRoot);

        ShaderCompileOptions options = new(ShaderBindingPolicies.Default, true, true, false);
        ShaderModel model = new(4, 0);
        ShaderCompileRequest vertexRequest = CreateRequest("gpu.vs", "VS", ShaderStage.Vertex, model, options);
        ShaderCompileRequest pixelRequest = CreateRequest("gpu.ps", "PS", ShaderStage.Pixel, model, options);
        ShaderFilesystemIncludeResolver includeResolver = new(fixtureRoot);
        Ps3ShaderPairCompiler compiler = new(
            new Ps3ShaderSourceCompiler(),
            new Ps3DockerShaderCompiler(Ps3ShaderToolchain.Load(repositoryRoot)));
        string compilerRoot = Path.Combine(fixtureRoot, "compiler-work");
        Ps3ShaderProgramPair pair = compiler.Compile(
            vertexRequest,
            pixelRequest,
            includeResolver,
            compilerRoot,
            CancellationToken.None);

        Ps3ShaderUniformBuffer frame = Assert.Single(
            pair.VertexTranslation.UniformBuffers,
            buffer => buffer.BufferName == "Frame");
        Assert.Equal(80u, frame.ByteSize);
        Ps3ShaderUniformMember transform = Assert.Single(
            pair.VertexTranslation.UniformMembers,
            member => member.MemberPath == "Transform");
        Ps3ShaderUniformMember tint = Assert.Single(
            pair.VertexTranslation.UniformMembers,
            member => member.MemberPath == "Tint");
        Assert.Equal(0u, transform.ByteOffset);
        Assert.Equal(64u, tint.ByteOffset);
        Assert.Equal(16u, transform.MatrixStride);
        Assert.Single(pair.Bindings.Uniforms, uniform =>
            uniform.Stage == ShaderStage.Vertex && uniform.SourceMember.BufferName == "Frame"
            && uniform.SourceMember.MemberPath == "Transform");
        Assert.Single(pair.Bindings.Uniforms, uniform =>
            uniform.Stage == ShaderStage.Vertex && uniform.SourceMember.BufferName == "Frame"
            && uniform.SourceMember.MemberPath == "Tint");

        Ps3ShaderUniformBuffer surface = Assert.Single(
            pair.PixelTranslation.UniformBuffers,
            buffer => buffer.BufferName == "Surface");
        Assert.Equal(16u, surface.ByteSize);
        Ps3ShaderUniformMember pixelTint = Assert.Single(
            pair.PixelTranslation.UniformMembers,
            member => member.MemberPath == "PixelTint");
        Assert.Single(pair.Bindings.Uniforms, uniform =>
            uniform.Stage == ShaderStage.Pixel && uniform.SourceMember.BufferName == "Surface"
            && uniform.SourceMember.MemberPath == "PixelTint");
        Assert.DoesNotContain(pair.Bindings.Uniforms, uniform =>
            uniform.Stage == ShaderStage.Vertex && uniform.SourceMember.MemberPath == "PixelTint");
        Assert.DoesNotContain(pair.Bindings.Uniforms, uniform =>
            uniform.Stage == ShaderStage.Pixel && uniform.SourceMember.MemberPath is "Transform" or "Tint");

        Assert.Contains(pair.Bindings.Attributes, attribute => attribute.SourceVariable.Semantic == "POSITION");
        Assert.Contains(pair.Bindings.Attributes, attribute => attribute.SourceVariable.Semantic == "TEXCOORD0");
        Ps3ShaderSamplerMapEntry sampler = Assert.Single(pair.Bindings.Samplers,
            item => item.TextureName == "Albedo" && item.SamplerName == "LinearSampler");
        Assert.Equal("Dim2D", sampler.Dimension);
        Assert.InRange(sampler.TextureUnit, 0u, 15u);

        Ps3ShaderArtifactIdentity identity = new("authored-gpu-fixture", "gpu.vs", "gpu.ps", "default");
        string sourceHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(FixtureSource))).ToLowerInvariant();
        byte[] artifact = Ps3ShaderArtifactWriter.Write(identity, sourceHash, "VS", "PS", pair);
        string artifactPath = Path.Combine(fixtureRoot, "fixture.ps3sp");
        string vertexPath = Path.Combine(fixtureRoot, "vertex.bin");
        string pixelPath = Path.Combine(fixtureRoot, "pixel.bin");
        File.WriteAllBytes(artifactPath, artifact);
        File.WriteAllBytes(vertexPath, pair.Vertex.Bytecode);
        File.WriteAllBytes(pixelPath, pair.Pixel.Bytecode);

        Assert.Equal(artifact, File.ReadAllBytes(artifactPath));
        Assert.Equal(pair.Vertex.Bytecode, File.ReadAllBytes(vertexPath));
        Assert.Equal(pair.Pixel.Bytecode, File.ReadAllBytes(pixelPath));
        Assert.NotEmpty(pixelTint.GeneratedName);
    }

    /// <summary>Creates one explicit PS3 SM4 request with the fixture's shared options and default variant.</summary>
    /// <param name="programName">Logical stage program name recorded in the artifact identity.</param>
    /// <param name="entryPoint">HLSL entry point selected for this stage.</param>
    /// <param name="stage">Vertex or pixel stage.</param>
    /// <param name="model">Required Shader Model 4.0 target.</param>
    /// <param name="options">Shared compilation options for the pair.</param>
    /// <returns>A request bound to the exact authored fixture source.</returns>
    static ShaderCompileRequest CreateRequest(
        string programName,
        string entryPoint,
        ShaderStage stage,
        ShaderModel model,
        ShaderCompileOptions options) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("gpu-fixture.hlsl", FixtureSource),
            programName,
            entryPoint,
            stage,
            ShaderCompileTarget.Ps3,
            model,
            "default",
            Array.Empty<ShaderDefine>(),
            options);
    }

    /// <summary>Returns the explicitly configured PS3 checkout that owns the locked toolchain.</summary>
    /// <returns>The fully qualified repository root.</returns>
    static string RequiredRepositoryRoot() {
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "Set HELENGINE_PS3_REPOSITORY_ROOT to this checkout.");
        Assert.True(Path.IsPathFullyQualified(repositoryRoot!), "HELENGINE_PS3_REPOSITORY_ROOT must be fully qualified.");
        string fullRepositoryRoot = Path.GetFullPath(repositoryRoot!);
        Assert.True(File.Exists(Path.Combine(fullRepositoryRoot, "toolchain-lock.json")),
            $"HELENGINE_PS3_REPOSITORY_ROOT must contain toolchain-lock.json: {fullRepositoryRoot}");
        return fullRepositoryRoot;
    }

    /// <summary>Requires fixture files to remain strictly within the authored-GPU task's owned build root.</summary>
    /// <param name="repositoryRoot">Validated repository root.</param>
    /// <returns>The canonical fixture-storage path.</returns>
    static string RequiredFixtureRoot(string repositoryRoot) {
        string configuredRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_GPU_FIXTURE_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(configuredRoot),
            "Set HELENGINE_PS3_SHADER_GPU_FIXTURE_ROOT below the authored-GPU build root.");
        string taskRoot = Path.GetFullPath(Path.Combine(
            repositoryRoot,
            "..",
            "builds",
            "helengine-ps3",
            "shader-integration-20260923",
            "authored-gpu"));
        return Ps3NativeShaderFixtureRoot.ResolveStrictDescendant(taskRoot, configuredRoot!);
    }
}
