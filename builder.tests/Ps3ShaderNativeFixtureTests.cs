using helengine;
using helengine.ps3.builder;
using System.Security.Cryptography;
using System.Text;

namespace helengine.ps3.builder.tests;

/// <summary>Produces the real locked-SDK pair consumed by the native shader reader tests.</summary>
public sealed class Ps3ShaderNativeFixtureTests {
    /// <summary>Shared HLSL source used for both stages of the reader interoperability fixture.</summary>
    const string NativeFixtureSource = """
        cbuffer Frame : register(b0) { float4x4 Transform; float4 Tint; };
        Texture2D Albedo : register(t0); SamplerState LinearSampler : register(s0);
        struct VIn { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
        struct VOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; float4 Color : COLOR0; };
        VOut VS(VIn v) { VOut o; o.Position=mul(float4(v.Position,1),Transform); o.Uv=v.Uv; o.Color=Tint; return o; }
        float4 PS(VOut v) : SV_Target { return Albedo.Sample(LinearSampler,v.Uv)*v.Color; }
        """;

    /// <summary>Compiles real VS/PS binaries, verifies reflected layout and writes the P3SP plus exact SDK blobs.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void ProducesNativeReaderFixture() {
        string repositoryRoot = RequiredRepositoryRoot();
        string fixtureRoot = RequiredFixtureRoot(repositoryRoot);
        Directory.CreateDirectory(fixtureRoot);

        ShaderCompileOptions options = new(ShaderBindingPolicies.Default, true, true, false);
        ShaderModel model = new(4, 0);
        ShaderCompileRequest vertexRequest = CreateRequest("native.vs", "VS", ShaderStage.Vertex, model, options);
        ShaderCompileRequest pixelRequest = CreateRequest("native.ps", "PS", ShaderStage.Pixel, model, options);
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

        Ps3ShaderArtifactIdentity identity = new("native-fixture-é", "native.vs", "native.ps", "default");
        string sourceHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(NativeFixtureSource))).ToLowerInvariant();
        byte[] artifact = Ps3ShaderArtifactWriter.Write(identity, sourceHash, "VS", "PS", pair);
        File.WriteAllBytes(Path.Combine(fixtureRoot, "fixture.ps3sp"), artifact);
        File.WriteAllBytes(Path.Combine(fixtureRoot, "vertex.bin"), pair.Vertex.Bytecode);
        File.WriteAllBytes(Path.Combine(fixtureRoot, "pixel.bin"), pair.Pixel.Bytecode);
    }

    /// <summary>Creates one explicit PS3 source request for the pinned SDK pair compiler.</summary>
    /// <param name="programName">Logical stage program name recorded in the artifact identity.</param>
    /// <param name="entryPoint">HLSL entry point selected for this stage.</param>
    /// <param name="stage">Vertex or pixel stage.</param>
    /// <param name="model">Required Shader Model 4.0 target.</param>
    /// <param name="options">Shared compilation options for both stages.</param>
    /// <returns>A request bound to the exact fixture source and selected stage entry point.</returns>
    static ShaderCompileRequest CreateRequest(
        string programName,
        string entryPoint,
        ShaderStage stage,
        ShaderModel model,
        ShaderCompileOptions options) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("native-fixture.hlsl", NativeFixtureSource),
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
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "Set HELENGINE_PS3_REPOSITORY_ROOT to this PS3 checkout.");
        string fullRepositoryRoot = Ps3NativeShaderFixtureRoot.NormalizeFullyQualifiedPath(
            repositoryRoot!, nameof(repositoryRoot));
        Assert.True(File.Exists(Path.Combine(fullRepositoryRoot, "toolchain-lock.json")),
            $"HELENGINE_PS3_REPOSITORY_ROOT must contain toolchain-lock.json: {fullRepositoryRoot}");
        return fullRepositoryRoot;
    }

    /// <summary>Requires fixture storage to be an explicit child of the plan's owned native-reader build root.</summary>
    /// <param name="repositoryRoot">Validated repository root.</param>
    /// <returns>The canonical fixture-storage path.</returns>
    static string RequiredFixtureRoot(string repositoryRoot) {
        string configuredRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_NATIVE_FIXTURE_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(configuredRoot),
            "Set HELENGINE_PS3_SHADER_NATIVE_FIXTURE_ROOT below the native-reader build root.");
        string nativeReaderRoot = Path.GetFullPath(Path.Combine(
            repositoryRoot,
            "..",
            "builds",
            "helengine-ps3",
            "shader-integration-20260923",
            "native-reader"));
        return Ps3NativeShaderFixtureRoot.ResolveStrictDescendant(nativeReaderRoot, configuredRoot!);
    }
}
