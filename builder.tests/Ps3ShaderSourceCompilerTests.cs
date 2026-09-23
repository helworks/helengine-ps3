using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Exercises real HLSL translation with uniforms, texture sampling and compiler failures.</summary>
public sealed class Ps3ShaderSourceCompilerTests {
    /// <summary>HLSL with modern resource syntax that cannot be passed straight to legacy Cg.</summary>
    const string Source = """
        cbuffer Transform : register(b0) { float4x4 WorldViewProjection; float4 Tint; };
        Texture2D Diffuse : register(t0);
        SamplerState SurfaceSampler : register(s0);
        struct Input { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
        struct Varying { float4 Position : SV_Position; float2 Uv : TEXCOORD0; };
        Varying VS(Input input) { Varying o; o.Position = mul(WorldViewProjection, float4(input.Position, 1)); o.Uv = input.Uv; return o; }
        float4 PS(Varying input) : SV_Target { return Diffuse.Sample(SurfaceSampler, input.Uv) * Tint; }
        """;

    /// <summary>Both stages are translated by native shaderc and SPIRV-Cross.</summary>
    /// <param name="stage">Stage to translate.</param>
    /// <param name="entry">Authored entry point.</param>
    [Theory]
    [InlineData(ShaderStage.Vertex, "VS")]
    [InlineData(ShaderStage.Pixel, "PS")]
    public void ConvertsAuthoredHlsl(ShaderStage stage, string entry) {
        string glsl = new Ps3ShaderSourceCompiler().Compile(CreateRequest(Source, stage, entry), new Ps3ShaderIncludeResolverStub());
        Assert.Contains("#version 120", glsl);
        Assert.Contains("void main()", glsl);
        Assert.DoesNotContain("cbuffer", glsl);
    }

    /// <summary>Malformed source must produce a compiler diagnostic.</summary>
    [Fact]
    public void RejectsInvalidHlsl() {
        Assert.Throws<InvalidOperationException>(() => new Ps3ShaderSourceCompiler().Compile(CreateRequest("this is invalid", ShaderStage.Vertex, "VS"), new Ps3ShaderIncludeResolverStub()));
    }

    /// <summary>Rejects requests addressed to a backend other than PS3 before entering native compilation.</summary>
    [Fact]
    public void RejectsNonPs3Target() {
        ShaderCompileRequest request = CreateRequest("float4 VS() : SV_Position { return 0; }", ShaderStage.Vertex, "VS", target: ShaderCompileTarget.Vulkan);

        Assert.Throws<NotSupportedException>(() => new Ps3ShaderSourceCompiler().Compile(request, new Ps3ShaderIncludeResolverStub()));
    }

    /// <summary>Rejects shader stages that RSX's vertex and pixel compiler does not support.</summary>
    [Fact]
    public void RejectsUnsupportedStage() {
        ShaderCompileRequest request = CreateRequest("void CS() {}", ShaderStage.Compute, "CS");

        Assert.Throws<NotSupportedException>(() => new Ps3ShaderSourceCompiler().Compile(request, new Ps3ShaderIncludeResolverStub()));
    }

    /// <summary>Fails in managed setup if the session cannot preallocate its emergency native include result.</summary>
    [Fact]
    public void FailsBeforeCompilationWhenEmergencyIncludeResultCannotBeAllocated() {
        Ps3ShaderIncludeAllocator allocator = new(0);
        Ps3ShaderSourceCompiler compiler = new(allocator.Allocate);

        Assert.Throws<OutOfMemoryException>(() => compiler.Compile(
            CreateRequest("float4 PS() : SV_Target { return 0; }", ShaderStage.Pixel, "PS"), new Ps3ShaderIncludeResolverStub()));
        Assert.Equal(1, allocator.AllocationCount);
    }

    /// <summary>Uses the emergency include result rather than returning null when callback allocations fail.</summary>
    [Fact]
    public void ReturnsEmergencyIncludeErrorWhenCallbackAllocationFails() {
        Ps3ShaderIncludeAllocator allocator = new(3);
        Ps3ShaderSourceCompiler compiler = new(allocator.Allocate);
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["memory.hlsl"] = new("memory.hlsl", "static const float4 MemoryColor = float4(1, 0, 0, 1);")
            }
        };
        string source = "#include \"memory.hlsl\"\nfloat4 PS() : SV_Target { return MemoryColor; }";

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            compiler.Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));

        Assert.Contains("memory.hlsl", error.Message);
        Assert.Contains("native allocation", error.Message);
        Assert.True(allocator.AllocationCount > 3);
    }

    /// <summary>Preserves a deterministic shaderc preprocessor warning as an error when requested.</summary>
    [Fact]
    public void TreatsPreprocessorWarningsAsErrorsWhenRequested() {
        string source = "#warning PS3_WARNING_AS_ERROR_PROBE\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS", treatWarningsAsErrors: true), new Ps3ShaderIncludeResolverStub()));

        Assert.Contains("warning", error.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>An active include resolves through the supplied resolver and reaches generated GLSL.</summary>
    [Fact]
    public void ResolvesActiveInclude() {
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["color.hlsl"] = new("color.hlsl", "static const float4 IncludedColor = float4(1, 0, 0, 1);")
            }
        };
        string source = "#include \"color.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);

        Assert.Contains("void main()", output);
        Assert.Contains(resolver.Requests, request => request.IncludePath == "color.hlsl");
    }

    /// <summary>Nested includes pass the parent include's resolved path back to the resolver.</summary>
    [Fact]
    public void ResolvesNestedRelativeInclude() {
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["folder/outer.hlsl"] = new("folder/outer.hlsl", "#include \"inner.hlsl\""),
                ["inner.hlsl"] = new("folder/inner.hlsl", "static const float4 IncludedColor = float4(1, 0, 0, 1);")
            }
        };
        string source = "#include \"folder/outer.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);

        Assert.Contains("void main()", output);
        Assert.Contains(resolver.Requests, request => request.RequestingFile == "folder/outer.hlsl" && request.IncludePath == "inner.hlsl");
    }

    /// <summary>Conditional preprocessing prevents inactive includes from invoking the resolver.</summary>
    [Fact]
    public void DoesNotResolveInactiveInclude() {
        Ps3ShaderIncludeResolverStub resolver = new();
        string source = "#if 0\n#include \"inactive.hlsl\"\n#endif\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);

        Assert.Contains("void main()", output);
        Assert.DoesNotContain(resolver.Requests, request => request.IncludePath == "inactive.hlsl");
    }

    /// <summary>A missing active include returns a compiler diagnostic containing its original path.</summary>
    [Fact]
    public void ReportsMissingActiveInclude() {
        Ps3ShaderIncludeResolverStub resolver = new();
        string source = "#include \"missing.hlsl\"\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));

        Assert.Contains("missing.hlsl", error.Message);
    }

    /// <summary>Confirms the bundled shaderc retries a relative include after the resolver's first lookup miss.</summary>
    [Fact]
    public void RetriesRelativeIncludeAfterInitialFileNotFound() {
        Ps3ShaderIncludeRetryResolver resolver = new();
        string source = "#include \"retry.hlsl\"\nfloat4 PS() : SV_Target { return RetriedColor; }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);

        Assert.Contains("void main()", output);
        Assert.Equal(2, resolver.Requests.Count);
        Assert.Equal("retry.hlsl", resolver.Requests[0].IncludePath);
        Assert.Equal("retry.hlsl", resolver.Requests[1].IncludePath);
    }

    /// <summary>A resolver exception is converted into a compiler diagnostic without crossing native code.</summary>
    [Fact]
    public void ReportsResolverFailure() {
        Ps3ShaderIncludeResolverStub resolver = new() { Failure = new IOException("resolver failed") };
        string source = "#include \"broken.hlsl\"\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));

        Assert.Contains("broken.hlsl", error.Message);
        Assert.Contains("resolver failed", error.Message);
    }

    /// <summary>UTF-8 include contents are passed with their encoded byte count.</summary>
    [Fact]
    public void ResolvesUtf8IncludeContent() {
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["caf\u00E9.hlsl"] = new("caf\u00E9.hlsl", "// caf\u00E9 \U0001F3A8\nstatic const float4 IncludedColor = float4(1, 0, 0, 1);")
            }
        };
        string source = "#include \"caf\u00E9.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);

        Assert.Contains("void main()", output);
        Assert.Contains(resolver.Requests, request => request.IncludePath == "caf\u00E9.hlsl");
    }

    /// <summary>Request defines select the active include branch before shaderc resolves includes.</summary>
    [Fact]
    public void ResolvesDefineSelectedInclude() {
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["selected.hlsl"] = new("selected.hlsl", "static const float4 IncludedColor = float4(1, 0, 0, 1);")
            }
        };
        string source = "#if USE_COLOR\n#include \"selected.hlsl\"\n#else\n#include \"inactive.hlsl\"\n#endif\nfloat4 PS() : SV_Target { return IncludedColor; }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS", defines: [new ShaderDefine("USE_COLOR", "1")]), resolver);

        Assert.Contains("void main()", output);
        Assert.Contains(resolver.Requests, request => request.IncludePath == "selected.hlsl");
        Assert.DoesNotContain(resolver.Requests, request => request.IncludePath == "inactive.hlsl");
    }

    /// <summary>Rejects a resolver that returns changed content for a resolved path during one compilation.</summary>
    [Fact]
    public void RejectsChangedContentForRepeatedResolvedPath() {
        Ps3ShaderIncludeResolverStub resolver = new();
        resolver.Results.Enqueue(new ShaderIncludeResult("colors.hlsl", "// first content version"));
        resolver.Results.Enqueue(new ShaderIncludeResult("colors.hlsl", "// changed content version"));
        resolver.Results.Enqueue(new ShaderIncludeResult("colors.hlsl", "// first content version"));
        string source = "#include \"colors.hlsl\"\n#include \"colors.hlsl\"\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));

        Assert.True(resolver.Requests.Count >= 3);
        Assert.Contains("colors.hlsl", error.Message);
        Assert.True(error.Message.Contains("different contents", StringComparison.Ordinal), error.Message);
    }

    /// <summary>Resolves repeated active includes when their resolved path content is unchanged.</summary>
    [Fact]
    public void AllowsRepeatedIdenticalIncludeContent() {
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["colors.hlsl"] = new("colors.hlsl", "#define INCLUDED_COLOR float4(1, 0, 0, 1)")
            }
        };
        string source = "#include \"colors.hlsl\"\n#include \"colors.hlsl\"\nfloat4 PS() : SV_Target { return INCLUDED_COLOR; }";

        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);

        Assert.Contains("void main()", output);
        Assert.Equal(2, resolver.Requests.Count(request => request.IncludePath == "colors.hlsl"));
    }

    /// <summary>Creates a fully specified PS3 request for the test source.</summary>
    /// <param name="source">Authored HLSL text.</param>
    /// <param name="stage">Pipeline stage.</param>
    /// <param name="entry">Entry point name.</param>
    /// <param name="sourcePath">Optional source path used for include callback context.</param>
    /// <param name="defines">Optional request preprocessor definitions.</param>
    /// <param name="target">Compilation target to place in the request.</param>
    /// <param name="treatWarningsAsErrors">Whether shaderc must promote warnings to errors.</param>
    /// <returns>Shared engine compilation request.</returns>
    static ShaderCompileRequest CreateRequest(string source, ShaderStage stage, string entry, string sourcePath = "probe.hlsl", IReadOnlyList<ShaderDefine> defines = null, ShaderCompileTarget target = ShaderCompileTarget.Ps3, bool treatWarningsAsErrors = false) {
        return new(new ShaderSourceInfo(sourcePath, source), "Probe", entry, stage, target,
            new ShaderModel(3, 0), "default", defines ?? [], new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, treatWarningsAsErrors));
    }
}
