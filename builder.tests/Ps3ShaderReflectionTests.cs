using helengine;
using helengine.ps3.builder;
using System.Security.Cryptography;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies shader reflection preserves authored identities and declared resource layout.</summary>
public sealed class Ps3ShaderReflectionTests {
    /// <summary>HLSL fixture with reordered varyings, duplicate nested member names, padded constants and paired samplers.</summary>
    const string ReflectionSource = """
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
            return (linearSample + pointSample) * input.Color;
        }
        """;

    /// <summary>Compiles optimized and unoptimized requests and checks semantics and generated names survive optimization.</summary>
    [Fact]
    public void PreservesActiveAuthoredInterfacesWhenOptimized() {
        Ps3ShaderSourceCompiler compiler = new();
        Ps3ShaderTranslation unoptimized = compiler.Translate(CreateRequest(ReflectionSource, ShaderStage.Vertex, "VS", optimize: false), new Ps3ShaderIncludeResolverStub());
        Ps3ShaderTranslation optimized = compiler.Translate(CreateRequest(ReflectionSource, ShaderStage.Vertex, "VS", optimize: true), new Ps3ShaderIncludeResolverStub());
        Ps3ShaderTranslation optimizedWithoutDebugInfo = compiler.Translate(
            CreateRequest(ReflectionSource, ShaderStage.Vertex, "VS", optimize: true, generateDebugInfo: false), new Ps3ShaderIncludeResolverStub());

        Assert.Equal(InterfaceIdentities(unoptimized.Inputs), InterfaceIdentities(optimized.Inputs));
        Assert.Equal(InterfaceIdentities(unoptimized.Outputs), InterfaceIdentities(optimized.Outputs));
        Assert.Equal(InterfaceIdentities(optimized.Inputs), InterfaceIdentities(optimizedWithoutDebugInfo.Inputs));
        Assert.Equal(InterfaceIdentities(optimized.Outputs), InterfaceIdentities(optimizedWithoutDebugInfo.Outputs));
        Assert.Contains(optimized.Inputs, variable => variable.Semantic == "POSITION");
        Assert.Contains(optimized.Inputs, variable => variable.Semantic == "TEXCOORD0");
        Assert.Contains(optimized.Inputs, variable => variable.Semantic == "COLOR0");
        Assert.Contains(optimized.Outputs, variable => variable.Semantic == "TEXCOORD0");
        Assert.Contains(optimized.Outputs, variable => variable.Semantic == "COLOR0");
        Assert.All(optimized.Inputs.Concat(optimized.Outputs), variable => Assert.Contains(variable.GeneratedName, optimized.GlslSource, StringComparison.Ordinal));
        Assert.Contains("Position", optimized.OutputBuiltIns);
        Assert.DoesNotContain(optimized.Outputs, variable => variable.Semantic == "SV_Position");
    }

    /// <summary>Reflects exact cbuffer bindings, paths, member sizes, offsets and matrix layout from compiler queries.</summary>
    [Fact]
    public void ReflectsPaddedAndNestedUniformMembers() {
        Ps3ShaderTranslation translation = new Ps3ShaderSourceCompiler().Translate(
            CreateRequest(ReflectionSource, ShaderStage.Vertex, "VS", optimize: true), new Ps3ShaderIncludeResolverStub());
        Ps3ShaderUniformMember direction = Assert.Single(translation.UniformMembers, member => member.MemberPath == "Direction");
        Ps3ShaderUniformMember intensity = Assert.Single(translation.UniformMembers, member => member.MemberPath == "Intensity");
        Ps3ShaderUniformMember transform = Assert.Single(translation.UniformMembers, member => member.MemberPath == "Transform");
        Ps3ShaderUniformMember rowMajorTransform = Assert.Single(translation.UniformMembers, member => member.MemberPath == "RowMajorTransform");
        Assert.True(translation.UniformMembers.Any(member => member.MemberPath == "Primary.Tint"),
            $"Reflected member paths: {string.Join(", ", translation.UniformMembers.Select(member => $"{member.BufferName}:{member.MemberPath}"))}");
        Ps3ShaderUniformMember primaryTint = Assert.Single(translation.UniformMembers, member => member.MemberPath == "Primary.Tint");
        Assert.True(translation.UniformMembers.Any(member => member.MemberPath == "Secondary.Tint"),
            $"Reflected member paths: {string.Join(", ", translation.UniformMembers.Select(member => $"{member.BufferName}:{member.MemberPath}"))}");
        Ps3ShaderUniformMember secondaryTint = Assert.Single(translation.UniformMembers, member => member.MemberPath == "Secondary.Tint");

        Assert.All(translation.UniformMembers, member => Assert.False(string.IsNullOrWhiteSpace(member.MemberPath)));
        Assert.Equal(0u, direction.BufferBinding);
        Assert.Equal(0u, direction.ByteOffset);
        Assert.Equal(12u, direction.ByteSize);
        Assert.Equal(0u, intensity.BufferBinding);
        Assert.Equal(12u, intensity.ByteOffset);
        Assert.Equal(4u, intensity.ByteSize);
        Assert.Equal(0u, transform.BufferBinding);
        Assert.Equal(16u, transform.ByteOffset);
        Assert.Equal(64u, transform.ByteSize);
        Assert.Equal(16u, transform.MatrixStride);
        Assert.True(transform.RowMajor);
        Assert.Equal(4u, transform.Columns);
        Assert.Equal(4u, transform.VectorWidth);
        Assert.Equal(0u, rowMajorTransform.BufferBinding);
        Assert.Equal(80u, rowMajorTransform.ByteOffset);
        Assert.Equal(48u, rowMajorTransform.ByteSize);
        Assert.Equal(16u, rowMajorTransform.MatrixStride);
        // These fields preserve physical SPIR-V layout: HLSL Mrc and SPIR-V Mcr conventions reverse the major decoration.
        Assert.False(rowMajorTransform.RowMajor);
        Assert.Equal(3u, rowMajorTransform.Columns);
        Assert.Equal(4u, rowMajorTransform.VectorWidth);
        Assert.Equal(1u, primaryTint.BufferBinding);
        Assert.Equal(2u, secondaryTint.BufferBinding);
        Assert.NotEqual(primaryTint.GeneratedName, secondaryTint.GeneratedName);
        Assert.StartsWith("hel_ps3_uniform_buffer_binding_1.", primaryTint.GeneratedName, StringComparison.Ordinal);
        Assert.StartsWith("hel_ps3_uniform_buffer_binding_2.", secondaryTint.GeneratedName, StringComparison.Ordinal);
        Assert.True(primaryTint.GeneratedName.Count(character => character == '.') >= 2, primaryTint.GeneratedName);
        Assert.True(secondaryTint.GeneratedName.Count(character => character == '.') >= 2, secondaryTint.GeneratedName);
        Assert.Contains(primaryTint.GeneratedName, translation.GlslSource, StringComparison.Ordinal);
        Assert.Contains(secondaryTint.GeneratedName, translation.GlslSource, StringComparison.Ordinal);
        Assert.Equal(4u, transform.Columns);
        Assert.Equal(4u, transform.VectorWidth);
    }

    /// <summary>Accepts one-dimensional uniform arrays when compiler offset, size and stride fit the DTO exactly.</summary>
    [Fact]
    public void ReflectsUniformMembersWithOneArrayStride() {
        const string source = "cbuffer ArrayBuffer : register(b0) { float4 Values[2]; }; float4 PS() : SV_Target { return Values[1]; }";

        Ps3ShaderTranslation translation = new Ps3ShaderSourceCompiler().Translate(
            CreateRequest(source, ShaderStage.Pixel, "PS", optimize: true), new Ps3ShaderIncludeResolverStub());

        Ps3ShaderUniformMember values = Assert.Single(translation.UniformMembers);
        Assert.Equal("Values", values.MemberPath);
        Assert.Equal(0u, values.ByteOffset);
        Assert.Equal(32u, values.ByteSize);
        Assert.Equal(16u, values.ArrayStride);
        Assert.Equal(2u, values.ArrayLength);
        Assert.Contains(values.GeneratedName, translation.GlslSource, StringComparison.Ordinal);
    }

    /// <summary>Rejects uniform layouts with more than one array stride instead of flattening their dimensions.</summary>
    /// <param name="source">HLSL fixture containing multidimensional or nested independent array dimensions.</param>
    /// <param name="memberName">Active buffer path expected in the unsupported-layout diagnostic.</param>
    [Theory]
    [InlineData("cbuffer MultiArray : register(b0) { float4 Values[2][3]; }; float4 PS() : SV_Target { return Values[1][2]; }", "Values")]
    [InlineData("struct Inner { float4 Values[3]; }; cbuffer MultiArray : register(b0) { Inner Blocks[2]; }; float4 PS() : SV_Target { return Blocks[1].Values[2]; }", "Blocks")]
    public void RejectsUniformMembersWithMultipleArrayStrides(string source, string memberName) {
        NotSupportedException error = Assert.Throws<NotSupportedException>(() => new Ps3ShaderSourceCompiler().Translate(
            CreateRequest(source, ShaderStage.Pixel, "PS", optimize: true), new Ps3ShaderIncludeResolverStub()));

        Assert.Contains("multiple array strides", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(memberName, error.Message, StringComparison.Ordinal);
    }

    /// <summary>Creates unique stable generated uniforms for every texture and sampler binding pair.</summary>
    [Fact]
    public void NamesCombinedSamplersByBothResourceBindings() {
        Ps3ShaderTranslation translation = new Ps3ShaderSourceCompiler().Translate(
            CreateRequest(ReflectionSource, ShaderStage.Pixel, "PS", optimize: true), new Ps3ShaderIncludeResolverStub());

        Assert.Equal(2, translation.Samplers.Count);
        Assert.NotEqual(translation.Samplers[0].GeneratedName, translation.Samplers[1].GeneratedName);
        Assert.All(translation.Samplers, sampler => Assert.Contains(sampler.GeneratedName, translation.GlslSource, StringComparison.Ordinal));
        Assert.All(translation.Samplers, sampler => Assert.Equal(0u, sampler.TextureBinding));
        Assert.Equal(new uint[] { 0, 1 }, translation.Samplers.Select(sampler => sampler.SamplerBinding).Order().ToArray());
        Assert.All(translation.Samplers, sampler => Assert.Equal("Albedo", sampler.TextureName));
        Assert.Contains(translation.Samplers, sampler => sampler.SamplerName == "LinearSampler");
        Assert.Contains(translation.Samplers, sampler => sampler.SamplerName == "PointSampler");
        Assert.All(translation.Samplers, sampler => Assert.Equal("Dim2D", sampler.Dimension));
    }

    /// <summary>Copies resolved include dependency records into the translation before disposing the callback session.</summary>
    [Fact]
    public void PreservesIncludeDependenciesAfterTranslation() {
        const string includedSource = "static const float4 IncludedColor = float4(1, 0, 0, 1);";
        Ps3ShaderIncludeResolverStub resolver = new() {
            Files = new Dictionary<string, ShaderIncludeResult> {
                ["included-color.hlsl"] = new("included-color.hlsl", includedSource)
            }
        };
        string source = "#include \"included-color.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";

        Ps3ShaderTranslation translation = new Ps3ShaderSourceCompiler().Translate(
            CreateRequest(source, ShaderStage.Pixel, "PS", optimize: true), resolver);

        Ps3ShaderSourceDependency dependency = Assert.Single(translation.Dependencies);
        Assert.Equal("included-color.hlsl", dependency.Path);
        Assert.Equal(Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(includedSource))), dependency.ContentHash);
        Assert.True(Assert.IsAssignableFrom<IList<Ps3ShaderSourceDependency>>(translation.Dependencies).IsReadOnly);
    }

    /// <summary>Always validates reflection for both complex fixture stages and optionally emits them for the native SDK probe.</summary>
    [Fact]
    public void EmitsComplexFixtureGlslForNativeSdkProbeWhenOutputRootIsSet() {
        Ps3ShaderSourceCompiler compiler = new();
        Ps3ShaderTranslation vertex = compiler.Translate(
            CreateRequest(ReflectionSource, ShaderStage.Vertex, "VS", optimize: true), new Ps3ShaderIncludeResolverStub());
        Ps3ShaderTranslation pixel = compiler.Translate(
            CreateRequest(ReflectionSource, ShaderStage.Pixel, "PS", optimize: true), new Ps3ShaderIncludeResolverStub());

        Assert.False(string.IsNullOrWhiteSpace(vertex.GlslSource));
        Assert.False(string.IsNullOrWhiteSpace(pixel.GlslSource));
        Assert.Contains(vertex.Inputs, variable => variable.Semantic == "POSITION");
        Assert.Contains("Position", vertex.OutputBuiltIns);
        Assert.DoesNotContain(vertex.Outputs, variable => variable.Semantic == "SV_Position");
        Ps3ShaderUniformMember primaryTint = Assert.Single(vertex.UniformMembers, member => member.MemberPath == "Primary.Tint");
        Ps3ShaderUniformMember secondaryTint = Assert.Single(vertex.UniformMembers, member => member.MemberPath == "Secondary.Tint");
        Assert.Contains(primaryTint.GeneratedName, vertex.GlslSource, StringComparison.Ordinal);
        Assert.Contains(secondaryTint.GeneratedName, vertex.GlslSource, StringComparison.Ordinal);
        Assert.Equal(2, pixel.Samplers.Count);
        Assert.All(pixel.Samplers, sampler => Assert.Contains(sampler.GeneratedName, pixel.GlslSource, StringComparison.Ordinal));

        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_REFLECTION_OUTPUT");
        if (string.IsNullOrWhiteSpace(outputRoot)) return;
        string repositoryRoot = GetRepositoryRoot();
        if (!IsValidReflectionProbeOutputRoot(outputRoot, repositoryRoot)) {
            throw new InvalidOperationException(
                "Reflection probe output root must be below the sibling builds/helengine-ps3 directory.");
        }

        Directory.CreateDirectory(outputRoot);
        File.WriteAllText(Path.Combine(outputRoot, "VS.glsl"), vertex.GlslSource, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(outputRoot, "PS.glsl"), pixel.GlslSource, new UTF8Encoding(false));
        string metadata = JsonSerializer.Serialize(new {
            Vertex = CreateProbeMetadata(vertex),
            Pixel = CreateProbeMetadata(pixel)
        }, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(outputRoot, "reflection.json"), metadata, new UTF8Encoding(false));
    }

    /// <summary>Accepts canonical output roots under the standard sibling build directory without touching the filesystem.</summary>
    [Fact]
    public void AcceptsReflectionProbeOutputRootsInsideBuildDirectory() {
        string repositoryRoot = GetRepositoryRoot();
        string buildsRoot = GetReflectionProbeBuildsRoot(repositoryRoot);

        Assert.True(IsValidReflectionProbeOutputRoot(Path.Combine(buildsRoot, "shader-integration-test"), repositoryRoot));
        Assert.True(IsValidReflectionProbeOutputRoot(
            Path.Combine(buildsRoot, "shader-integration-test", "task-2-probe"), repositoryRoot));
        Assert.True(IsValidReflectionProbeOutputRoot(
            Path.Combine(buildsRoot, "shader-integration-test", "task-2-probe", "..", "other-probe"), repositoryRoot));
    }

    /// <summary>Rejects non-build-owned, relative, traversal, base-root and prefix-sibling paths without creating them.</summary>
    [Fact]
    public void RejectsReflectionProbeOutputRootsOutsideBuildDirectory() {
        string repositoryRoot = GetRepositoryRoot();
        string buildsRoot = GetReflectionProbeBuildsRoot(repositoryRoot);
        string[] rejectedRoots = [
            "",
            "relative/task-2-probe",
            buildsRoot,
            Path.Combine(buildsRoot, "..", "helengine-ps3-escape", "task-2-probe"),
            buildsRoot + "-escape" + Path.DirectorySeparatorChar + "task-2-probe",
            Path.GetTempPath()
        ];

        foreach (string rejectedRoot in rejectedRoots) {
            Assert.False(IsValidReflectionProbeOutputRoot(rejectedRoot, repositoryRoot), rejectedRoot);
        }
    }

    /// <summary>Rejects resources without a tested RSX mapping with a diagnostic naming the unsupported category.</summary>
    /// <param name="source">HLSL declaring one unsupported resource shape.</param>
    /// <param name="entryPoint">Entry point that actively uses the resource.</param>
    /// <param name="diagnostic">Required diagnostic category.</param>
    [Theory]
    [InlineData("RWTexture2D<float4> Target : register(u0); float4 PS() : SV_Target { Target[uint2(0, 0)] = 1; return 1; }", "PS", "storage image")]
    [InlineData("StructuredBuffer<float4> Values : register(t0); float4 PS() : SV_Target { return Values[0]; }", "PS", "runtime array")]
    [InlineData("Texture2D Image : register(t0, space1); SamplerState State : register(s0, space1); float4 PS() : SV_Target { return Image.Sample(State, float2(0, 0)); }", "PS", "resource space 1")]
    public void RejectsUnsupportedResourceShapes(string source, string entryPoint, string diagnostic) {
        NotSupportedException error = Assert.Throws<NotSupportedException>(() => new Ps3ShaderSourceCompiler().Translate(
            CreateRequest(source, ShaderStage.Pixel, entryPoint, optimize: true), new Ps3ShaderIncludeResolverStub()));

        Assert.Contains(diagnostic, error.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Builds a fully specified PS3 request with independently selected optimization.</summary>
    /// <param name="source">Authored HLSL source.</param>
    /// <param name="stage">Requested pipeline stage.</param>
    /// <param name="entryPoint">Authored entry point.</param>
    /// <param name="optimize">Whether shaderc optimization is enabled.</param>
    /// <returns>A PS3 request that enables debug metadata required for reflection.</returns>
    static ShaderCompileRequest CreateRequest(string source, ShaderStage stage, string entryPoint, bool optimize, bool generateDebugInfo = true) {
        return new(new ShaderSourceInfo("reflection.hlsl", source), "ReflectionProbe", entryPoint, stage, ShaderCompileTarget.Ps3,
            new ShaderModel(3, 0), "default", [], new ShaderCompileOptions(ShaderBindingPolicies.Default, generateDebugInfo, optimize, false));
    }

    /// <summary>Creates deterministic identity strings for comparing active interface variables between compiler variants.</summary>
    /// <param name="variables">Reflected variables to compare.</param>
    /// <returns>Sorted semantic and shape identities.</returns>
    static string[] InterfaceIdentities(IEnumerable<Ps3ShaderInterfaceVariable> variables) {
        return variables.Select(variable => $"{variable.Semantic}|{variable.ScalarType}|{variable.VectorWidth}|{variable.Columns}|{variable.ArrayLength}")
            .Order(StringComparer.Ordinal).ToArray();
    }

    /// <summary>Finds the repository root from this compiled test source path instead of embedding a machine-specific path.</summary>
    /// <param name="testSourcePath">Compile-time source path supplied by the caller-file-path attribute.</param>
    /// <returns>The repository root containing the test project.</returns>
    static string GetRepositoryRoot([CallerFilePath] string testSourcePath = "") {
        string testDirectory = Path.GetDirectoryName(testSourcePath);
        DirectoryInfo repositoryDirectory = testDirectory is null ? null : Directory.GetParent(testDirectory);
        return repositoryDirectory?.FullName
            ?? throw new InvalidOperationException("Could not derive the repository root from the reflection test source path.");
    }

    /// <summary>Builds the standard helengine-ps3 directory beside the repository's workspace parent.</summary>
    /// <param name="repositoryRoot">Canonical repository root derived from the test source file.</param>
    /// <returns>The expected sibling builds/helengine-ps3 directory.</returns>
    static string GetReflectionProbeBuildsRoot(string repositoryRoot) {
        return Path.GetFullPath(Path.Combine(repositoryRoot, "..", "builds", "helengine-ps3"));
    }

    /// <summary>Checks canonical path containment below the standard build root without creating or resolving filesystem entries.</summary>
    /// <param name="outputRoot">Configured reflection probe output path.</param>
    /// <param name="repositoryRoot">Repository root used to derive the sibling build root.</param>
    /// <returns>True only when the absolute canonical output is a descendant of the standard build root.</returns>
    static bool IsValidReflectionProbeOutputRoot(string outputRoot, string repositoryRoot) {
        if (string.IsNullOrWhiteSpace(outputRoot) || !Path.IsPathFullyQualified(outputRoot)) return false;

        string buildsRoot = GetReflectionProbeBuildsRoot(repositoryRoot);
        string buildsRootPrefix = Path.TrimEndingDirectorySeparator(buildsRoot) + Path.DirectorySeparatorChar;
        string canonicalOutputRoot;
        try {
            canonicalOutputRoot = Path.GetFullPath(outputRoot);
        }
        catch (ArgumentException) {
            return false;
        }
        catch (NotSupportedException) {
            return false;
        }

        StringComparison comparison = OperatingSystem.IsWindows()
            ? StringComparison.OrdinalIgnoreCase
            : StringComparison.Ordinal;
        return canonicalOutputRoot.StartsWith(buildsRootPrefix, comparison);
    }

    /// <summary>Projects compiler-owned reflection metadata without embedding generated GLSL in the probe JSON.</summary>
    /// <param name="translation">Completed stage translation.</param>
    /// <returns>Serializable stage, interface, uniform, sampler and dependency metadata.</returns>
    static object CreateProbeMetadata(Ps3ShaderTranslation translation) {
        return new {
            Stage = translation.Stage.ToString(),
            InputBuiltIns = translation.InputBuiltIns,
            OutputBuiltIns = translation.OutputBuiltIns,
            Inputs = translation.Inputs.Select(variable => new {
                variable.Semantic,
                variable.Location,
                variable.ScalarType,
                variable.VectorWidth,
                variable.Columns,
                variable.ArrayLength,
                variable.Interpolation,
                variable.GeneratedName
            }),
            Outputs = translation.Outputs.Select(variable => new {
                variable.Semantic,
                variable.Location,
                variable.ScalarType,
                variable.VectorWidth,
                variable.Columns,
                variable.ArrayLength,
                variable.Interpolation,
                variable.GeneratedName
            }),
            UniformMembers = translation.UniformMembers.Select(member => new {
                member.BufferName,
                member.BufferBinding,
                member.MemberPath,
                member.ByteOffset,
                member.ByteSize,
                member.ArrayStride,
                member.MatrixStride,
                member.RowMajor,
                member.GeneratedName,
                member.ScalarType,
                member.VectorWidth,
                member.Columns,
                member.ArrayLength
            }),
            Samplers = translation.Samplers.Select(sampler => new {
                sampler.TextureName,
                sampler.TextureBinding,
                sampler.SamplerName,
                sampler.SamplerBinding,
                sampler.GeneratedName,
                sampler.Dimension
            }),
            Dependencies = translation.Dependencies.Select(dependency => new {
                dependency.Path,
                dependency.ContentHash
            })
        };
    }
}
