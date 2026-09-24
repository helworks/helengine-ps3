using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies the PS3 backend contract, source reflection metadata and pinned SDK integration.</summary>
public sealed class Ps3ShaderBackendTests {
    /// <summary>HLSL fixture with padded and nested buffers, a matrix, a multi-digit semantic and paired samplers.</summary>
    const string ReflectedShaderSource = """
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
            float2 Uv : TEXCOORD12;
            float4 Color : COLOR0;
        };
        struct VertexOutput
        {
            float4 Color : COLOR0;
            float2 Uv : TEXCOORD12;
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

    /// <summary>Small HLSL fixture for the actual Docker SDK pair test.</summary>
    const string DockerPairSource = """
        cbuffer Frame : register(b0) { float4x4 Transform; float4 Tint; };
        Texture2D Albedo : register(t0);
        SamplerState LinearSampler : register(s0);
        struct VertexInput { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
        struct VertexOutput { float4 Color : COLOR0; float2 Uv : TEXCOORD0; float4 Position : SV_Position; };
        VertexOutput VS(VertexInput input)
        {
            VertexOutput output;
            output.Position = mul(float4(input.Position, 1.0), Transform);
            output.Color = Tint;
            output.Uv = input.Uv;
            return output;
        }
        float4 PS(VertexOutput input) : SV_Target
        {
            return Albedo.Sample(LinearSampler, input.Uv) * input.Color;
        }
        """;

    /// <summary>HLSL source whose active function body is supplied through the shared include resolver.</summary>
    const string IncludeShaderSource = """
        #include "common.hlsli"
        float4 PS() : SV_Target { return IncludedColor(); }
        """;

    /// <summary>Compiles a real frontend request through the shared service without populating its cache.</summary>
    [Fact]
    public void SharedServiceReturnsTypedResultAndBypassesCache() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        Ps3ShaderIncludeResolverStub resolver = new();
        ShaderCacheTestStore cache = new();
        ShaderCompileService service = CreateService(resolver, cache, backend);
        ShaderCompileRequest request = Request(
            "float4 PS() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }",
            ShaderStage.Pixel,
            "PS",
            "SharedServiceProbe",
            variant: "quality",
            defines: [new ShaderDefine("USE_TINT", ""), new ShaderDefine("QUALITY", "high")]);

        Assert.False(((IShaderBackendCachePolicy)backend).UseSharedCache);
        ShaderCompileResult result = service.Compile(request);

        Ps3ShaderCompileResult ps3 = Assert.IsType<Ps3ShaderCompileResult>(result);
        Assert.Equal(request.ProgramName, ps3.ProgramDefinition.Name);
        Assert.Equal(request.EntryPoint, ps3.ProgramDefinition.EntryPoint);
        Assert.Equal(request.Stage, ps3.ProgramDefinition.Stage);
        Assert.Equal(ShaderCompileTarget.Ps3, ps3.Binary.Target);
        Assert.Equal(request.Variant, ps3.Binary.Variant);
        Assert.Equal(ps3.Program.Bytecode, ps3.Binary.Bytecode);
        Assert.Equal(new[] { "USE_TINT", "QUALITY=high" }, Assert.Single(ps3.ProgramDefinition.Variants).Defines);
        Assert.Same(ps3.Translation, Assert.Single(rsxCompiler.Translations));
        Assert.Equal(CancellationToken.None, Assert.Single(rsxCompiler.CancellationTokens));
        Assert.Equal(0, cache.TryGetCalls);
        Assert.Equal(0, cache.StoreCalls);
    }

    /// <summary>Compiles both stages and maps the compiler's declared layout into shared logical metadata.</summary>
    [Fact]
    public void ReflectedMetadataPreservesPhysicalLayoutAndMapsSharedResources() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        Ps3ShaderIncludeResolverStub resolver = new();
        ShaderCacheTestStore cache = new();
        ShaderCompileService service = CreateService(resolver, cache, backend);
        ShaderCompileRequest vertexRequest = Request(ReflectedShaderSource, ShaderStage.Vertex, "VS", "Reflected.vs", "lit");
        ShaderCompileRequest pixelRequest = Request(ReflectedShaderSource, ShaderStage.Pixel, "PS", "Reflected.ps", "lit");

        Ps3ShaderCompileResult vertex = Assert.IsType<Ps3ShaderCompileResult>(service.Compile(vertexRequest));
        Ps3ShaderCompileResult pixel = Assert.IsType<Ps3ShaderCompileResult>(service.Compile(pixelRequest));

        ShaderBinding frameBinding = Assert.Single(vertex.ProgramDefinition.Bindings,
            binding => binding.Name == "Frame" && binding.Type == ShaderResourceType.ConstantBuffer);
        Assert.Equal(0, frameBinding.Set);
        Assert.Equal(0, frameBinding.Slot);
        Assert.Equal(128, frameBinding.Size);
        ShaderConstantMember direction = Assert.Single(frameBinding.Members, member => member.Name == "Direction");
        ShaderConstantMember rowMajor = Assert.Single(frameBinding.Members, member => member.Name == "RowMajorTransform");
        Assert.Equal(0, direction.Offset);
        Assert.Equal(12, direction.Size);
        Assert.Equal("float4x3", rowMajor.Type);
        Assert.Equal(80, rowMajor.Offset);
        Assert.Equal(48, rowMajor.Size);
        ShaderBinding materialPrimary = Assert.Single(vertex.ProgramDefinition.Bindings,
            binding => binding.Name == "MaterialPrimary" && binding.Type == ShaderResourceType.ConstantBuffer);
        Assert.Contains(materialPrimary.Members,
            member => member.Name == "Primary.Tint" && member.Offset == 0 && member.Size == 16);
        ShaderBinding materialSecondary = Assert.Single(vertex.ProgramDefinition.Bindings,
            binding => binding.Name == "MaterialSecondary" && binding.Type == ShaderResourceType.ConstantBuffer);
        Assert.Contains(materialSecondary.Members,
            member => member.Name == "Secondary.Tint" && member.Offset == 0 && member.Size == 16);
        Ps3ShaderUniformMember physicalRowMajor = Assert.Single(vertex.Translation.UniformMembers,
            member => member.MemberPath == "RowMajorTransform");
        Assert.Equal(3u, physicalRowMajor.Columns);
        Assert.Equal(4u, physicalRowMajor.VectorWidth);
        Assert.Equal(16u, physicalRowMajor.MatrixStride);
        Assert.False(physicalRowMajor.RowMajor);

        ShaderVertexElement vertexUv = Assert.Single(vertex.ProgramDefinition.Inputs,
            input => input.Semantic == "TEXCOORD" && input.Index == 12);
        Assert.Equal("float2", vertexUv.Format);
        Assert.Contains(vertex.ProgramDefinition.Inputs,
            input => input.Semantic == "POSITION" && input.Index == 0 && input.Format == "float3");
        Assert.Contains(vertex.ProgramDefinition.Outputs,
            output => output.Semantic == "TEXCOORD" && output.Index == 12 && output.Format == "float2");
        Assert.DoesNotContain(vertex.ProgramDefinition.Outputs, output => output.Semantic == "POSITION");
        Assert.Contains(pixel.ProgramDefinition.Inputs,
            input => input.Semantic == "TEXCOORD" && input.Index == 12);
        Assert.Equal(pixel.Translation.Outputs.Count, pixel.ProgramDefinition.Outputs.Length);
        Assert.Contains(pixel.ProgramDefinition.Outputs, output => output.Semantic == "SV_TARGET");

        ShaderBinding albedo = Assert.Single(pixel.ProgramDefinition.Bindings,
            binding => binding.Name == "Albedo" && binding.Type == ShaderResourceType.Texture2D);
        ShaderBinding linearSampler = Assert.Single(pixel.ProgramDefinition.Bindings,
            binding => binding.Name == "LinearSampler" && binding.Type == ShaderResourceType.Sampler);
        ShaderBinding pointSampler = Assert.Single(pixel.ProgramDefinition.Bindings,
            binding => binding.Name == "PointSampler" && binding.Type == ShaderResourceType.Sampler);
        Assert.Equal(100, albedo.Slot);
        Assert.Equal(200, linearSampler.Slot);
        Assert.Equal(201, pointSampler.Slot);
        Assert.Equal(2, pixel.Translation.Samplers.Count);
        Assert.All(pixel.Translation.Samplers, sampler => Assert.Equal("Albedo", sampler.TextureName));
        Assert.Same(vertex.Translation, rsxCompiler.Translations[0]);
    }

    /// <summary>Applies nonzero and zero resource-class shifts without changing physical source bindings.</summary>
    [Fact]
    public void MetadataAppliesCustomAndZeroBindingPolicies() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderIncludeResolverStub resolver = new();
        ShaderBindingPolicy shiftedPolicy = new(0, 10, 111, 211, 300);
        Ps3ShaderBackend shiftedBackend = CreateBackend(rsxCompiler);
        ShaderCompileRequest shiftedRequest = Request(ReflectedShaderSource, ShaderStage.Pixel, "PS", "Shifted.ps", "custom",
            options: new ShaderCompileOptions(shiftedPolicy, true, true, false));

        Ps3ShaderCompileResult shifted = shiftedBackend.Compile(shiftedRequest, resolver, CancellationToken.None);

        Assert.Equal(111, Assert.Single(shifted.ProgramDefinition.Bindings,
            binding => binding.Name == "Albedo").Slot);
        Assert.Equal(211, Assert.Single(shifted.ProgramDefinition.Bindings,
            binding => binding.Name == "LinearSampler").Slot);
        Assert.Equal(212, Assert.Single(shifted.ProgramDefinition.Bindings,
            binding => binding.Name == "PointSampler").Slot);
        Assert.Equal(11, Assert.Single(shifted.ProgramDefinition.Bindings,
            binding => binding.Name == "MaterialPrimary").Slot);
        Assert.Equal(0u, Assert.Single(shifted.Translation.Samplers,
            sampler => sampler.SamplerName == "LinearSampler").TextureBinding);
        Assert.Equal(0u, Assert.Single(shifted.Translation.Samplers,
            sampler => sampler.SamplerName == "LinearSampler").SamplerBinding);

        ShaderBindingPolicy zeroPolicy = new(0, 0, 0, 0, 0);
        Ps3ShaderCompileResult zero = CreateBackend(new Ps3ShaderBackendCompilerStub())
            .Compile(Request(ReflectedShaderSource, ShaderStage.Pixel, "PS", "Unshifted.ps", "custom",
                options: new ShaderCompileOptions(zeroPolicy, true, true, false)), resolver, CancellationToken.None);

        Assert.Equal(0, Assert.Single(zero.ProgramDefinition.Bindings, binding => binding.Name == "Albedo").Slot);
        Assert.Equal(0, Assert.Single(zero.ProgramDefinition.Bindings, binding => binding.Name == "LinearSampler").Slot);
        Assert.Equal(1, Assert.Single(zero.ProgramDefinition.Bindings, binding => binding.Name == "PointSampler").Slot);
    }

    /// <summary>Compiles again when only resolved include contents change despite using the real shared service.</summary>
    [Fact]
    public void ChangedIncludeContentsReachFrontendAndSdkAgain() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        Ps3ShaderIncludeResolverStub resolver = new();
        resolver.Files["common.hlsli"] = new ShaderIncludeResult("common.hlsli",
            "float4 IncludedColor() { return float4(1.0, 0.0, 0.0, 1.0); }");
        ShaderCacheTestStore cache = new();
        ShaderCompileService service = CreateService(resolver, cache, backend);
        ShaderCompileRequest request = Request(IncludeShaderSource, ShaderStage.Pixel, "PS", "IncludeProbe.ps");

        Ps3ShaderCompileResult first = Assert.IsType<Ps3ShaderCompileResult>(service.Compile(request));
        resolver.Files["common.hlsli"] = new ShaderIncludeResult("common.hlsli",
            "float4 IncludedColor() { return float4(0.0, 1.0, 0.0, 1.0); }");
        Ps3ShaderCompileResult second = Assert.IsType<Ps3ShaderCompileResult>(service.Compile(request));

        Assert.NotEqual(first.Translation.GlslSource, second.Translation.GlslSource);
        Assert.Equal(2, rsxCompiler.CallCount);
        Assert.Equal(0, cache.TryGetCalls);
        Assert.Equal(0, cache.StoreCalls);
    }

    /// <summary>Rejects null compile requests and resolvers before invoking either compiler.</summary>
    [Fact]
    public void RejectsNullCompileArguments() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        Ps3ShaderIncludeResolverStub resolver = new();
        ShaderCompileRequest request = Request("float4 PS() : SV_Target { return 1.0; }", ShaderStage.Pixel, "PS");

        Assert.Throws<ArgumentNullException>(() => backend.Compile(null, resolver));
        Assert.Throws<ArgumentNullException>(() => backend.Compile(request, null));
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects unsupported targets, stages, source models and nonzero binding spaces before SDK invocation.</summary>
    [Theory]
    [InlineData(ShaderCompileTarget.Vulkan, ShaderStage.Pixel, 3, 0, 0)]
    [InlineData(ShaderCompileTarget.Ps3, ShaderStage.Compute, 3, 0, 0)]
    [InlineData(ShaderCompileTarget.Ps3, ShaderStage.Pixel, 2, 1, 0)]
    [InlineData(ShaderCompileTarget.Ps3, ShaderStage.Pixel, 5, 1, 0)]
    [InlineData(ShaderCompileTarget.Ps3, ShaderStage.Pixel, 3, 0, 1)]
    public void RejectsUnsupportedRequestBeforeSdk(ShaderCompileTarget target, ShaderStage stage,
        int major, int minor, int defaultSpace) {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        ShaderCompileRequest request = Request("this source is deliberately invalid", stage, "PS", target: target,
            model: new ShaderModel(major, minor),
            options: new ShaderCompileOptions(new ShaderBindingPolicy(defaultSpace, 0, 100, 200, 300), true, true, false));

        Assert.Throws<NotSupportedException>(() => backend.Compile(request, new Ps3ShaderIncludeResolverStub()));

        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Accepts both inclusive source-language model boundaries with the real HLSL frontend.</summary>
    [Theory]
    [InlineData(3, 0)]
    [InlineData(5, 0)]
    public void AcceptsShaderModelBoundaries(int major, int minor) {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        ShaderCompileRequest request = Request(
            "float4 PS() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }",
            ShaderStage.Pixel,
            "PS",
            model: new ShaderModel(major, minor));

        Ps3ShaderCompileResult result = backend.Compile(request, new Ps3ShaderIncludeResolverStub(), CancellationToken.None);

        Assert.Equal(major, result.Request.ShaderModel.Major);
        Assert.Equal(minor, result.Request.ShaderModel.Minor);
        Assert.Equal(ShaderStage.Pixel, result.Translation.Stage);
        Assert.Equal(1, rsxCompiler.CallCount);
    }

    /// <summary>Rejects invalid injected dependencies and roots during backend construction.</summary>
    [Fact]
    public void RejectsInvalidConstructorDependenciesAndWorkingRoot() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Assert.Throws<ArgumentNullException>(() => new Ps3ShaderBackend(null, rsxCompiler, WorkingRoot()));
        Assert.Throws<ArgumentNullException>(() => new Ps3ShaderBackend(new Ps3ShaderSourceCompiler(), null, WorkingRoot()));
        Assert.Throws<ArgumentException>(() => new Ps3ShaderBackend(new Ps3ShaderSourceCompiler(), rsxCompiler, " "));
        Assert.Throws<ArgumentException>(() => new Ps3ShaderBackend(new Ps3ShaderSourceCompiler(), rsxCompiler, "relative-root"));
    }

    /// <summary>Advertises vertex/pixel source-language support from shader model 3.0 through 5.0.</summary>
    [Fact]
    public void AdvertisesOnlyVertexAndPixelSourceStages() {
        Ps3ShaderBackend backend = CreateBackend(new Ps3ShaderBackendCompilerStub());

        Assert.Equal(3, backend.Capabilities.MinimumShaderModel.Major);
        Assert.Equal(0, backend.Capabilities.MinimumShaderModel.Minor);
        Assert.Equal(5, backend.Capabilities.MaximumShaderModel.Major);
        Assert.Equal(0, backend.Capabilities.MaximumShaderModel.Minor);
        Assert.Equal(new[] { ShaderStage.Vertex, ShaderStage.Pixel }, backend.Capabilities.SupportedStages);
        Assert.False(backend.Capabilities.SupportsRayTracing);
    }

    /// <summary>Stops a pre-cancelled request before invoking shaderc or the SDK compiler.</summary>
    [Fact]
    public void CancellationBeforeFrontendSkipsAllCompilation() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        Ps3ShaderIncludeResolverStub resolver = new();
        using CancellationTokenSource cancellationSource = new();
        cancellationSource.Cancel();

        Assert.Throws<OperationCanceledException>(() => backend.Compile(
            Request(IncludeShaderSource, ShaderStage.Pixel, "PS"), resolver, cancellationSource.Token));

        Assert.Empty(resolver.Requests);
        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Stops after include-driven translation when cancellation arrives before the SDK call.</summary>
    [Fact]
    public void CancellationAfterFrontendSkipsSdkCompiler() {
        Ps3ShaderBackendCompilerStub compilerAndResolver = new();
        Ps3ShaderBackend backend = CreateBackend(compilerAndResolver);
        using CancellationTokenSource cancellationSource = new();
        compilerAndResolver.OnResolve = cancellationSource.Cancel;

        Assert.Throws<OperationCanceledException>(() => backend.Compile(
            Request(IncludeShaderSource, ShaderStage.Pixel, "PS"), compilerAndResolver, cancellationSource.Token));

        Assert.Equal(1, compilerAndResolver.ResolveCallCount);
        Assert.Equal(0, compilerAndResolver.CallCount);
    }

    /// <summary>Stops before returning when the token is cancelled during the SDK compiler call.</summary>
    [Fact]
    public void CancellationAfterSdkCompilerSkipsResultReturn() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);
        using CancellationTokenSource cancellationSource = new();
        rsxCompiler.OnCompile = cancellationSource.Cancel;

        Assert.Throws<OperationCanceledException>(() => backend.Compile(
            Request("float4 PS() : SV_Target { return 1.0; }", ShaderStage.Pixel, "PS"),
            new Ps3ShaderIncludeResolverStub(), cancellationSource.Token));

        Assert.Equal(1, rsxCompiler.CallCount);
    }

    /// <summary>Propagates the exact exception instance raised by the injected SDK compiler.</summary>
    [Fact]
    public void PropagatesSdkCompilerExceptionWithoutWrapping() {
        InvalidOperationException failure = new("recorded SDK failure");
        Ps3ShaderBackendCompilerStub rsxCompiler = new(failure: failure);
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);

        InvalidOperationException actual = Assert.Throws<InvalidOperationException>(() => backend.Compile(
            Request("float4 PS() : SV_Target { return 1.0; }", ShaderStage.Pixel, "PS"),
            new Ps3ShaderIncludeResolverStub(), CancellationToken.None));

        Assert.Same(failure, actual);
    }

    /// <summary>Rejects malformed HLSL before starting the SDK job.</summary>
    [Fact]
    public void RejectsMalformedHlslBeforeSdkCompilation() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new();
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);

        Assert.Throws<InvalidOperationException>(() => backend.Compile(
            Request("float4 PS( : SV_Target {", ShaderStage.Pixel, "PS"),
            new Ps3ShaderIncludeResolverStub(), CancellationToken.None));

        Assert.Equal(0, rsxCompiler.CallCount);
    }

    /// <summary>Rejects a validated SDK program whose stage differs from the authored request.</summary>
    [Fact]
    public void RejectsSdkProgramForWrongStage() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new(stageOverride: ShaderStage.Vertex);
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);

        Assert.Throws<InvalidOperationException>(() => backend.Compile(
            Request("float4 PS() : SV_Target { return 1.0; }", ShaderStage.Pixel, "PS"),
            new Ps3ShaderIncludeResolverStub(), CancellationToken.None));

        Assert.Equal(1, rsxCompiler.CallCount);
    }

    /// <summary>Rejects an SDK result with empty program bytes instead of creating a fake shared binary.</summary>
    [Fact]
    public void RejectsEmptySdkBytecode() {
        Ps3ShaderBackendCompilerStub rsxCompiler = new(Array.Empty<byte>());
        Ps3ShaderBackend backend = CreateBackend(rsxCompiler);

        Assert.Throws<InvalidOperationException>(() => backend.Compile(
            Request("float4 PS() : SV_Target { return 1.0; }", ShaderStage.Pixel, "PS"),
            new Ps3ShaderIncludeResolverStub(), CancellationToken.None));

        Assert.Equal(1, rsxCompiler.CallCount);
    }

    /// <summary>Rejects source metadata whose integer sizes cannot be represented by shared metadata fields.</summary>
    [Fact]
    public void MetadataRejectsBufferAndMemberIntegerOverflow() {
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Vertex, "VS");
        Ps3ShaderTranslation oversizedBuffer = SyntheticTranslation(
            ShaderStage.Vertex,
            [new Ps3ShaderUniformBuffer("TooLarge", 0, (uint)int.MaxValue + 1)],
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());
        Ps3ShaderTranslation oversizedMember = SyntheticTranslation(
            ShaderStage.Vertex,
            [new Ps3ShaderUniformBuffer("Frame", 0, 16)],
            [new Ps3ShaderUniformMember("Frame", 0, "Value", uint.MaxValue, 4, 0, 0, false,
                "frame_value", "FP3232", 1, 1, 1)],
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());

        Assert.Throws<OverflowException>(() => Ps3ShaderProgramMetadata.Create(request, oversizedBuffer));
        Assert.Throws<OverflowException>(() => Ps3ShaderProgramMetadata.Create(request, oversizedMember));
    }

    /// <summary>Rejects binding-shift overflow before calling the shared unchecked slot calculation.</summary>
    [Fact]
    public void MetadataRejectsBindingShiftOverflow() {
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Pixel, "PS");
        Ps3ShaderTranslation translation = SyntheticTranslation(
            ShaderStage.Pixel,
            Array.Empty<Ps3ShaderUniformBuffer>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            [new Ps3ShaderSamplerBinding("Albedo", int.MaxValue, "Linear", 0, "combined", "Dim2D")],
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());

        Assert.Throws<OverflowException>(() => Ps3ShaderProgramMetadata.Create(request, translation));
    }

    /// <summary>Rejects unsupported constant scalar types and reports the affected member.</summary>
    [Fact]
    public void MetadataRejectsUnsupportedScalarType() {
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Vertex, "VS");
        Ps3ShaderTranslation translation = SyntheticTranslation(
            ShaderStage.Vertex,
            [new Ps3ShaderUniformBuffer("Frame", 0, 16)],
            [new Ps3ShaderUniformMember("Frame", 0, "Value", 0, 4, 0, 0, false,
                "frame_value", "SINT32", 1, 1, 1)],
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());

        NotSupportedException error = Assert.Throws<NotSupportedException>(
            () => Ps3ShaderProgramMetadata.Create(request, translation));

        Assert.Contains("Value", error.Message, StringComparison.Ordinal);
        Assert.Contains("SINT32", error.Message, StringComparison.Ordinal);
    }

    /// <summary>Rejects interface matrices and arrays outside the shared scalar/vector signature representation.</summary>
    [Theory]
    [InlineData(1, 2, 1)]
    [InlineData(2, 1, 2)]
    [InlineData(5, 1, 1)]
    public void MetadataRejectsUnrepresentableInterfaces(uint width, uint columns, uint arrayLength) {
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Vertex, "VS");
        Ps3ShaderTranslation translation = SyntheticTranslation(
            ShaderStage.Vertex,
            Array.Empty<Ps3ShaderUniformBuffer>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            [new Ps3ShaderInterfaceVariable("TEXCOORD2", 0, "FP3232", width, columns,
                arrayLength, "Smooth", "input_uv")],
            Array.Empty<Ps3ShaderInterfaceVariable>());

        Assert.Throws<NotSupportedException>(() => Ps3ShaderProgramMetadata.Create(request, translation));
    }

    /// <summary>Rejects unsupported sampler dimensions and includes both authored name and reflected dimension.</summary>
    [Fact]
    public void MetadataRejectsUnsupportedTextureDimensionWithIdentity() {
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Pixel, "PS");
        Ps3ShaderTranslation translation = SyntheticTranslation(
            ShaderStage.Pixel,
            Array.Empty<Ps3ShaderUniformBuffer>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            [new Ps3ShaderSamplerBinding("VolumeTexture", 2, "VolumeSampler", 3, "combined", "Dim3D")],
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());

        NotSupportedException error = Assert.Throws<NotSupportedException>(
            () => Ps3ShaderProgramMetadata.Create(request, translation));

        Assert.Contains("VolumeTexture", error.Message, StringComparison.Ordinal);
        Assert.Contains("Dim3D", error.Message, StringComparison.Ordinal);
    }

    /// <summary>Maps synthetic cube-texture reflection and applies the shared texture binding shift.</summary>
    [Fact]
    public void MetadataMapsCubeTextureAndAppliesTextureShift() {
        ShaderBindingPolicy policy = new(0, 0, 37, 0, 0);
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Pixel, "PS",
            options: new ShaderCompileOptions(policy, true, true, false));
        Ps3ShaderSamplerBinding cubeSampler = new("Sky", 5, "SkySampler", 2, "combined_sky", "DimCube");
        Ps3ShaderTranslation translation = SyntheticTranslation(
            ShaderStage.Pixel,
            Array.Empty<Ps3ShaderUniformBuffer>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            [cubeSampler],
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());

        ShaderProgramDefinition metadata = Ps3ShaderProgramMetadata.Create(request, translation);

        ShaderBinding texture = Assert.Single(metadata.Bindings, binding => binding.Name == "Sky");
        Assert.Equal(ShaderResourceType.TextureCube, texture.Type);
        Assert.Equal(42, texture.Slot);
    }

    /// <summary>Rejects two authored texture names that claim the same logical register.</summary>
    [Fact]
    public void MetadataRejectsConflictingTextureRegisters() {
        ShaderCompileRequest request = Request("synthetic", ShaderStage.Pixel, "PS");
        Ps3ShaderTranslation translation = SyntheticTranslation(
            ShaderStage.Pixel,
            Array.Empty<Ps3ShaderUniformBuffer>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            [new Ps3ShaderSamplerBinding("Albedo", 0, "Linear", 0, "combined_linear", "Dim2D"),
             new Ps3ShaderSamplerBinding("NormalMap", 0, "Point", 1, "combined_point", "Dim2D")],
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>());

        Assert.Throws<InvalidOperationException>(() => Ps3ShaderProgramMetadata.Create(request, translation));
    }

    /// <summary>Compiles the matrix/tint/texture fixture through two backend calls and reconciles its RSX pair.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesAndReconcilesRealTexturePairThroughSharedBackend() {
        Ps3ShaderBackend backend = CreateDockerBackend("backend-small-pair");
        Ps3ShaderIncludeResolverStub resolver = new();
        Ps3ShaderCompileResult vertex = backend.Compile(
            Request(DockerPairSource, ShaderStage.Vertex, "VS", "BackendPair.vs"), resolver, CancellationToken.None);
        Ps3ShaderCompileResult pixel = backend.Compile(
            Request(DockerPairSource, ShaderStage.Pixel, "PS", "BackendPair.ps"), resolver, CancellationToken.None);

        Ps3ShaderBindingMap bindings = new Ps3ShaderBindingMapBuilder().Build(
            vertex.Translation, vertex.Program, pixel.Translation, pixel.Program);

        Assert.Equal(ShaderStage.Vertex, vertex.Program.Stage);
        Assert.Equal(ShaderStage.Pixel, pixel.Program.Stage);
        Assert.Equal(2, bindings.StageLinks.Count);
        Assert.Single(bindings.Samplers);
        AssertSdkBytesReachSharedResults(vertex);
        AssertSdkBytesReachSharedResults(pixel);
    }

    /// <summary>Compiles the sibling editor's authored ForwardStandard vertex/pixel entries without rewriting the source.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesAuthoredForwardStandardShaderThroughSharedBackend() {
        string helengineRoot = Directory.GetParent(RequiredRepositoryRoot()).FullName;
        string sourcePath = Path.Combine(helengineRoot, "helengine", "engine", "helengine.editor",
            "shaders", "builtin", "ForwardStandardShader.hlsl");
        string source = File.ReadAllText(sourcePath);
        Ps3ShaderBackend backend = CreateDockerBackend("backend-forward-standard");
        Ps3ShaderIncludeResolverStub resolver = new();
        Ps3ShaderCompileResult vertex = backend.Compile(
            Request(source, ShaderStage.Vertex, "VS", "ForwardStandard.vs"), resolver, CancellationToken.None);
        Ps3ShaderCompileResult pixel = backend.Compile(
            Request(source, ShaderStage.Pixel, "PS", "ForwardStandard.ps"), resolver, CancellationToken.None);

        Ps3ShaderBindingMap bindings = new Ps3ShaderBindingMapBuilder().Build(
            vertex.Translation, vertex.Program, pixel.Translation, pixel.Program);

        Assert.NotEmpty(bindings.Attributes);
        Assert.NotEmpty(bindings.StageLinks);
        AssertSdkBytesReachSharedResults(vertex);
        AssertSdkBytesReachSharedResults(pixel);
    }

    /// <summary>Builds a shared compile request with explicit stage identity and defaults used by the fixture.</summary>
    /// <param name="source">Authored HLSL source text.</param>
    /// <param name="stage">Vertex or pixel stage to compile.</param>
    /// <param name="entryPoint">Requested HLSL entry point.</param>
    /// <param name="programName">Logical shared program name.</param>
    /// <param name="variant">Named program variant.</param>
    /// <param name="target">Requested shared target.</param>
    /// <param name="model">Requested HLSL source-language model.</param>
    /// <param name="defines">Ordered request preprocessor definitions.</param>
    /// <param name="options">Optional full shared compilation options.</param>
    /// <returns>A fully specified shared shader compile request.</returns>
    static ShaderCompileRequest Request(string source, ShaderStage stage, string entryPoint,
        string programName = "BackendProbe", string variant = "default", ShaderCompileTarget target = ShaderCompileTarget.Ps3,
        ShaderModel model = null, IReadOnlyList<ShaderDefine> defines = null, ShaderCompileOptions options = null) {
        return new ShaderCompileRequest(
            new ShaderSourceInfo("ps3-shader-backend-test.hlsl", source),
            programName,
            entryPoint,
            stage,
            target,
            model ?? new ShaderModel(3, 0),
            variant,
            defines ?? Array.Empty<ShaderDefine>(),
            options ?? new ShaderCompileOptions(ShaderBindingPolicies.Default, true, true, false));
    }

    /// <summary>Creates the PS3 backend with a supplied recording RSX compiler and an absolute test root.</summary>
    /// <param name="rsxCompiler">Compiler double for the slow SDK boundary.</param>
    /// <returns>A configured shared PS3 backend.</returns>
    static Ps3ShaderBackend CreateBackend(IPs3RsxCompiler rsxCompiler) {
        return new Ps3ShaderBackend(new Ps3ShaderSourceCompiler(), rsxCompiler, WorkingRoot());
    }

    /// <summary>Creates the backend wired to the pinned Docker SDK under a build-owned artifact root.</summary>
    /// <param name="childName">Unique purpose-specific child directory name.</param>
    /// <returns>A backend that translates through shaderc and runs cgcomp inside Docker.</returns>
    static Ps3ShaderBackend CreateDockerBackend(string childName) {
        string repositoryRoot = RequiredRepositoryRoot();
        return new Ps3ShaderBackend(
            new Ps3ShaderSourceCompiler(),
            new Ps3DockerShaderCompiler(Ps3ShaderToolchain.Load(repositoryRoot)),
            Path.Combine(RequiredArtifactsRoot(), childName));
    }

    /// <summary>Builds a real shared compile service using the existing cache test store.</summary>
    /// <param name="resolver">Shared include resolver.</param>
    /// <param name="cache">Existing cache test double.</param>
    /// <param name="backend">PS3 backend registered with the service.</param>
    /// <returns>The real shared shader compile service.</returns>
    static ShaderCompileService CreateService(IShaderIncludeResolver resolver, ShaderCacheTestStore cache,
        IShaderBackend backend) {
        ShaderCompileService service = new(resolver, cache, new ShaderSourceHasher());
        service.RegisterBackend(backend);
        return service;
    }

    /// <summary>Builds an immutable translation used only for unreachable metadata boundary conditions.</summary>
    /// <param name="stage">Compiled shader stage.</param>
    /// <param name="buffers">Compiler-reflected constant buffers.</param>
    /// <param name="members">Compiler-reflected leaf uniform members.</param>
    /// <param name="samplers">Compiler-reflected combined texture/sampler pairs.</param>
    /// <param name="inputs">Compiler-reflected user inputs.</param>
    /// <param name="outputs">Compiler-reflected user outputs.</param>
    /// <returns>A synthetic immutable source translation.</returns>
    static Ps3ShaderTranslation SyntheticTranslation(ShaderStage stage, Ps3ShaderUniformBuffer[] buffers,
        Ps3ShaderUniformMember[] members, Ps3ShaderSamplerBinding[] samplers,
        Ps3ShaderInterfaceVariable[] inputs, Ps3ShaderInterfaceVariable[] outputs) {
        return new Ps3ShaderTranslation(stage, "void main() {}", Array.Empty<string>(), Array.Empty<string>(),
            inputs, outputs, members, samplers, Array.Empty<Ps3ShaderSourceDependency>(), buffers);
    }

    /// <summary>Uses the existing RSX reader to validate bytes and confirms the shared binary keeps the SDK payload.</summary>
    /// <param name="result">Shared PS3 compile result under inspection.</param>
    static void AssertSdkBytesReachSharedResults(Ps3ShaderCompileResult result) {
        Ps3RsxProgram validated = Ps3RsxProgramReader.Read(result.Program.Stage, result.Program.Bytecode,
            result.Program.Assembly, result.Program.ToolchainIdentity);
        Assert.Equal(result.Program.Stage, validated.Stage);
        Assert.Equal(result.Program.Bytecode, result.Binary.Bytecode);
        Assert.True(result.Binary.Bytecode.Length > 36);
    }

    /// <summary>Returns an absolute path without selecting or creating a temporary working directory.</summary>
    /// <returns>An absolute path rooted in the current repository.</returns>
    static string WorkingRoot() {
        return Path.GetFullPath(Path.Combine(Environment.CurrentDirectory, "ps3-backend-work"));
    }

    /// <summary>Returns the repository root required by the locked Docker toolchain tests.</summary>
    /// <returns>PS3 repository root containing the toolchain lock file.</returns>
    static string RequiredRepositoryRoot() {
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot),
            "Set HELENGINE_PS3_REPOSITORY_ROOT to this checkout for Docker-category tests.");
        return repositoryRoot;
    }

    /// <summary>Returns the validated build-owned SDK artifact root configured for this test run.</summary>
    /// <returns>A strict child of the sibling builds/helengine-ps3 directory.</returns>
    static string RequiredArtifactsRoot() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(outputRoot),
            "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to a build-owned backend artifact root.");
        return Ps3Task3TestPaths.ValidateArtifactsRoot(outputRoot, RequiredRepositoryRoot());
    }
}
