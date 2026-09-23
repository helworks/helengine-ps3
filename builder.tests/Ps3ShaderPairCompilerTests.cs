using helengine;
using helengine.ps3.builder;
using System.Runtime.CompilerServices;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies paired PS3 compilation validates request identity and forwards cancellation to both stage jobs.</summary>
public sealed class Ps3ShaderPairCompilerTests {
    /// <summary>HLSL source with one active vertex attribute and built-in position output.</summary>
    const string VertexSource = "float4 VS(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }";

    /// <summary>HLSL source with one fixed-function fragment color output.</summary>
    const string PixelSource = "float4 PS() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }";

    /// <summary>HLSL source with only the mandatory built-in position output for fixed-function pair-stub tests.</summary>
    const string PositionOnlyVertexSource = "float4 VS() : SV_Position { return float4(0.0, 0.0, 0.0, 1.0); }";

    /// <summary>Proves variant mismatches are rejected before either source translation or RSX compilation begins.</summary>
    [Fact]
    public void RejectsIncompatibleVariantBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "wireframe", ShaderCompileTarget.Ps3);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects non-PS3 targets before invoking the source or RSX compiler.</summary>
    [Fact]
    public void RejectsNonPs3TargetBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.DirectX11);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects requests that are not ordered as vertex followed by pixel before starting jobs.</summary>
    [Fact]
    public void RejectsIncorrectStageOrderBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects differing preprocessor define sets as incompatible pair variants before starting jobs.</summary>
    [Fact]
    public void RejectsDifferentDefineSetsBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3,
            new[] { new ShaderDefine("QUALITY", "high") });
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, Array.Empty<ShaderDefine>());

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects requests that target different shader models before starting either stage job.</summary>
    [Fact]
    public void RejectsDifferentShaderModelsBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, model: new ShaderModel(4, 0));

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects requests with different optimization options before starting either stage job.</summary>
    [Fact]
    public void RejectsDifferentCompileOptionsBeforeStartingJobs() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileOptions unoptimized = new(ShaderBindingPolicies.Default, true, false, false);
        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, options: unoptimized);

        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Compiles per-stage logical program names while preserving their shared variant identity.</summary>
    [Fact]
    public void CompilesPairWithDistinctStageProgramNames() {
        Ps3ShaderPairCompilerStub rsxCompiler = new();
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request(PositionOnlyVertexSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3, programName: "asset.vs");
        ShaderCompileRequest pixel = Request(PixelSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, programName: "asset.ps");

        Ps3ShaderProgramPair pair = compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None);

        Assert.Equal(2, rsxCompiler.CallCount);
        Assert.Equal(ShaderStage.Vertex, pair.Vertex.Stage);
        Assert.Equal(ShaderStage.Pixel, pair.Pixel.Stage);
    }

    /// <summary>Proves a cancellation raised by the pixel job is propagated after the vertex job and carries the caller token.</summary>
    [Fact]
    public void ForwardsCancellationTokenThroughBothStageJobs() {
        using CancellationTokenSource cancellationSource = new();
        Ps3ShaderPairCompilerStub rsxCompiler = new(2);
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
        ShaderCompileRequest vertex = Request(VertexSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request(PixelSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);

        Assert.Throws<OperationCanceledException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), cancellationSource.Token)));

        Assert.Equal(2, rsxCompiler.CallCount);
        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[0]);
        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[1]);
        Assert.NotEqual(rsxCompiler.WorkingRoots[0], rsxCompiler.WorkingRoots[1]);
    }

    /// <summary>Compiles the modern cbuffer, paired-texture and two-varying fixture with the pinned Docker SDK.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesModernTextureCbufferPairAndLinksBothVaryingsWithPinnedSdk() {
        ShaderCompileRequest vertex = Request(ModernPairSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
        ShaderCompileRequest pixel = Request(ModernPairSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), new Ps3DockerShaderCompiler(Ps3ShaderToolchain.Load(RequiredRepositoryRoot())));

        Ps3ShaderProgramPair pair = compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(),
            Path.Combine(RequiredArtifactsRoot(), "modern-texture-cbuffer-pair"), CancellationToken.None);

        Assert.Equal(ShaderStage.Vertex, pair.Vertex.Stage);
        Assert.Equal(ShaderStage.Pixel, pair.Pixel.Stage);
        Assert.Equal(ShaderStage.Vertex, pair.VertexTranslation.Stage);
        Assert.Equal(ShaderStage.Pixel, pair.PixelTranslation.Stage);
        Assert.Equal(3, pair.Bindings.Attributes.Count);
        Assert.Equal(2, pair.Bindings.StageLinks.Count);
        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "COLOR0" && link.VertexChannel == "TEX0" && link.PixelChannel == "TEX0");
        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "TEXCOORD0" && link.VertexChannel == "TEX1" && link.PixelChannel == "TEX1");
        Assert.All(pair.Bindings.StageLinks, link => Assert.Equal(link.VertexChannel, link.PixelChannel));
        Assert.Equal(2, pair.Bindings.Samplers.Count);
        Assert.Equal(2, pair.Bindings.Samplers.Select(binding => binding.TextureUnit).Distinct().Count());
        Assert.Contains(pair.Bindings.Uniforms, binding => binding.AuthoredMemberPath == "Primary.Tint");
        Ps3ShaderUniformBinding pixelTint = Assert.Single(pair.Bindings.Uniforms,
            binding => binding.Stage == ShaderStage.Pixel && binding.AuthoredMemberPath == "Primary.Tint");
        Assert.Contains(pixelTint.RsxParameter.Elements, element => element.FragmentRelocationOffsets.Count > 0);
    }

    /// <summary>Builds a request with an explicit target, entry point and variant for validation tests.</summary>
    /// <param name="source">HLSL source text.</param>
    /// <param name="stage">Requested pipeline stage.</param>
    /// <param name="variant">Variant identity.</param>
    /// <param name="target">Compilation target.</param>
    /// <param name="programName">Logical name assigned to the individual stage request.</param>
    /// <returns>The requested immutable compile request.</returns>
    static ShaderCompileRequest Request(string source, ShaderStage stage, string variant, ShaderCompileTarget target,
        IReadOnlyList<ShaderDefine> defines = null, ShaderModel model = null, ShaderCompileOptions options = null,
        string programName = "PairTest") {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("pair-test.hlsl", source),
            programName,
            stage == ShaderStage.Vertex ? "VS" : "PS",
            stage,
            target,
            model ?? new ShaderModel(3, 0),
            variant,
            defines ?? Array.Empty<ShaderDefine>(),
            options ?? new ShaderCompileOptions(ShaderBindingPolicies.Default, true, true, false));
    }

    /// <summary>Returns a unique child root under the explicitly configured build-owned shader-test directory.</summary>
    /// <returns>A build-owned pair job root.</returns>
    static string WorkingRoot() {
        string configuredRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(configuredRoot), "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to a build-owned Task 4 test root.");
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "Set HELENGINE_PS3_REPOSITORY_ROOT to this checkout.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(configuredRoot!, repositoryRoot!);
        return Path.Combine(validatedRoot, "pair-unit-tests", Guid.NewGuid().ToString("N"));
    }

    /// <summary>Builds and validates the explicit build-owned artifact root for pinned SDK tests.</summary>
    /// <returns>Canonical test output root below sibling builds/helengine-ps3.</returns>
    static string RequiredArtifactsRoot() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(outputRoot), "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to a build-owned Task 4 test root.");
        return Ps3Task3TestPaths.ValidateArtifactsRoot(outputRoot!, RequiredRepositoryRoot());
    }

    /// <summary>Returns the explicit repository root required by pinned SDK integration tests.</summary>
    /// <returns>Repository root containing toolchain-lock.json.</returns>
    static string RequiredRepositoryRoot() {
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "Set HELENGINE_PS3_REPOSITORY_ROOT to this checkout.");
        return repositoryRoot!;
    }

    /// <summary>HLSL fixture that exercises reordered varyings, nested constants, and two samplers on the same texture.</summary>
    const string ModernPairSource = """
        cbuffer Frame : register(b0)
        {
            float3 Direction;
            float Intensity;
            column_major float4x4 Transform;
            row_major float3x4 RowMajorTransform;
        };
        struct SharedTint { float4 Tint; };
        cbuffer MaterialPrimary : register(b1) { SharedTint Primary; };
        cbuffer MaterialSecondary : register(b2) { SharedTint Secondary; };
        Texture2D Albedo : register(t0);
        SamplerState LinearSampler : register(s0);
        SamplerState PointSampler : register(s1);
        struct VertexInput
        {
            float3 Position : POSITION;
            float2 Uv : TEXCOORD0;
            float4 Color : COLOR0;
        };
        struct VertexOutput
        {
            float4 Color : COLOR0;
            float2 Uv : TEXCOORD0;
            float4 Position : SV_Position;
        };
        VertexOutput VS(VertexInput input)
        {
            VertexOutput output;
            output.Color = input.Color * Primary.Tint * Secondary.Tint;
            output.Uv = input.Uv;
            float3 position = mul(Transform, float4(input.Position, 1)).xyz
                + mul(RowMajorTransform, float4(input.Position, 1))
                + Direction * Intensity;
            output.Position = float4(position, 1);
            return output;
        }
        float4 PS(VertexOutput input) : SV_Target
        {
            float4 linearSample = Albedo.Sample(LinearSampler, input.Uv);
            float4 pointSample = Albedo.Sample(PointSampler, input.Uv);
            return (linearSample + pointSample) * input.Color * Primary.Tint;
        }
        """;
}
