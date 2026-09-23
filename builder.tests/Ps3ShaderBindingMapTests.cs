using helengine;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies authored shader metadata resolves to the actual stage-local RSX routing slots.</summary>
public sealed class Ps3ShaderBindingMapTests {
    /// <summary>Links matching TEX0 channels while retaining their different vertex and pixel slot indices.</summary>
    [Fact]
    public void LinksTexcoordZeroAcrossDifferentStageLocalIndices() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX0 : TEX0 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX0 : TEX0 : -1 : 1\n";
        Ps3ShaderTranslation vertexSource = new(
            ShaderStage.Vertex,
            "vertex source",
            Array.Empty<string>(),
            new[] { "Position" },
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            new[] { new Ps3ShaderInterfaceVariable("TEXCOORD0", 0, "FP3232", 2, 1, 1, "smooth", "vertexUv") },
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>(),
            Array.Empty<Ps3ShaderUniformBuffer>());
        Ps3ShaderTranslation pixelSource = new(
            ShaderStage.Pixel,
            "pixel source",
            Array.Empty<string>(),
            Array.Empty<string>(),
            new[] { new Ps3ShaderInterfaceVariable("TEXCOORD0", 0, "FP3232", 2, 1, 1, "smooth", "pixelUv") },
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>(),
            Array.Empty<Ps3ShaderUniformBuffer>());
        Ps3RsxProgram vertexProgram = new(ShaderStage.Vertex, Array.Empty<byte>(), vertexDump, "test-sdk", Array.Empty<Ps3RsxParameter>());
        Ps3RsxProgram pixelProgram = new(ShaderStage.Pixel, Array.Empty<byte>(), pixelDump, "test-sdk", Array.Empty<Ps3RsxParameter>());
        Ps3ShaderBindingMapBuilder builder = new();

        Ps3ShaderBindingMap map = builder.Build(vertexSource, vertexProgram, pixelSource, pixelProgram);

        Ps3ShaderStageLink link = Assert.Single(map.StageLinks);
        Assert.Equal("TEXCOORD0", link.AuthoredSemantic);
        Assert.Equal("TEX0", link.VertexChannel);
        Assert.Equal("TEX0", link.PixelChannel);
        Assert.Equal(7u, link.VertexSlot);
        Assert.Equal(4u, link.PixelSlot);
    }

    /// <summary>Confirms the binary reader still accepts a fixed-function position output after dump parsing is shared.</summary>
    [Fact]
    public void ReaderRetainsBinaryValidationForPositionOnlyVertexProgram() {
        byte[] bytecode = CreatePositionOnlyVertexProgram();
        const string dump = "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n";

        Ps3RsxProgram program = Ps3RsxProgramReader.Read(ShaderStage.Vertex, bytecode, dump, "test-sdk");

        Assert.Equal(ShaderStage.Vertex, program.Stage);
        Assert.Equal(bytecode, program.Bytecode);
        Assert.Empty(program.Parameters);
    }

    /// <summary>Preserves exact active and inactive Cg declaration fields for both reconciliation and SDK validation.</summary>
    [Fact]
    public void SharedDumpReaderPreservesExactFieldsAndInactiveDeclarations() {
        const string dump = "#profile vp40\n"
            + "#var float2 activeUv : $vout.TEX0 : TEX0 : -1 : 1\n"
            + "#var float4 optimizedColor :  : c[7] : 12 : 0\n";

        IReadOnlyList<Ps3CgDumpVariable> variables = Ps3CgDumpReader.Read(ShaderStage.Vertex, dump);

        Assert.Equal(2, variables.Count);
        Assert.Equal("float2", variables[0].TypeToken);
        Assert.Equal("activeUv", variables[0].Name);
        Assert.Equal("$vout.TEX0", variables[0].SemanticBinding);
        Assert.Equal("TEX0", variables[0].HardwareBinding);
        Assert.Equal(-1, variables[0].CompilerParameterIndex);
        Assert.True(variables[0].IsActive);
        Assert.Equal("optimizedColor", variables[1].Name);
        Assert.Equal("c[7]", variables[1].HardwareBinding);
        Assert.Equal(12, variables[1].CompilerParameterIndex);
        Assert.False(variables[1].IsActive);
    }

    /// <summary>Matches interfaces by authored semantic when struct fields are reordered and leaves unused outputs unlinked.</summary>
    [Fact]
    public void LinksReorderedColorAndUvFieldsAndAllowsUnusedVertexOutput() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float4 vertexColor : $vout.TEX0 : TEX0 : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX1 : TEX1 : -1 : 1\n"
            + "#var float3 unusedNormal : $vout.TEX2 : TEX2 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX1 : TEX1 : -1 : 1\n"
            + "#var float4 pixelColor : $vin.TEX0 : TEX0 : -1 : 1\n";
        Ps3ShaderInterfaceVariable vertexColor = Interface("COLOR0", "vertexColor", 4);
        Ps3ShaderInterfaceVariable vertexUv = Interface("TEXCOORD0", "vertexUv", 2);
        Ps3ShaderInterfaceVariable unusedNormal = Interface("NORMAL0", "unusedNormal", 3);
        Ps3ShaderInterfaceVariable pixelUv = Interface("TEXCOORD0", "pixelUv", 2);
        Ps3ShaderInterfaceVariable pixelColor = Interface("COLOR0", "pixelColor", 4);

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { vertexColor, vertexUv, unusedNormal }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, new[] { pixelUv, pixelColor }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump));

        Assert.Equal(new[] { "TEXCOORD0", "COLOR0" }, map.StageLinks.Select(link => link.AuthoredSemantic));
        Assert.Equal(new[] { "TEX1", "TEX0" }, map.StageLinks.Select(link => link.VertexChannel));
        Assert.DoesNotContain(map.StageLinks, link => link.AuthoredSemantic == "NORMAL0");
    }

    /// <summary>Rejects equal numeric stage-local indices when their canonical RSX channels differ.</summary>
    [Fact]
    public void RejectsEqualRawSlotsWhenCanonicalChannelsDiffer() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX0 : TEX0 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX3 : TEX3 : -1 : 1\n";

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, new[] { Interface("TEXCOORD0", "pixelUv", 2) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump)));
    }

    /// <summary>Rejects a fragment input with no vertex producer for its authored semantic.</summary>
    [Fact]
    public void RejectsPixelInputWithoutVertexProducer() {
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float2 vertexUv : $vout.TEX0 : TEX0 : -1 : 1\n";
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float4 pixelColor : $vin.TEX0 : TEX0 : -1 : 1\n";

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, new[] { Interface("COLOR0", "pixelColor", 4) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump)));
    }

    /// <summary>Rejects an inter-stage scalar type outside the supported FP3232 varying subset.</summary>
    [Fact]
    public void RejectsUnsupportedPixelVaryingScalarType() {
        Assert.Throws<NotSupportedException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { new Ps3ShaderInterfaceVariable("TEXCOORD0", 0, "SINT32", 2, 1, 1, "smooth", "pixelUv") }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float2 pixelUv"))));
    }

    /// <summary>Rejects source reflection whose scalar type cannot be reconciled with the actual float Cg declaration.</summary>
    [Fact]
    public void RejectsUnsupportedScalarTypeEvenWhenBothStagesAgree() {
        Ps3ShaderInterfaceVariable vertexOutput = new("TEXCOORD0", 0, "SINT32", 2, 1, 1, "smooth", "vertexUv");
        Ps3ShaderInterfaceVariable pixelInput = new("TEXCOORD0", 0, "SINT32", 2, 1, 1, "smooth", "pixelUv");

        Assert.Throws<NotSupportedException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { vertexOutput }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { pixelInput }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float2 pixelUv"))));
    }

    /// <summary>Rejects a varying whose vector width differs between vertex and pixel stages.</summary>
    [Fact]
    public void RejectsVaryingWidthMismatch() {
        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { Interface("TEXCOORD0", "pixelUv", 3) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float3 pixelUv"))));
    }

    /// <summary>Rejects a varying whose interpolation mode differs between vertex and pixel stages.</summary>
    [Fact]
    public void RejectsVaryingInterpolationMismatch() {
        Ps3ShaderInterfaceVariable flatPixelUv = new("TEXCOORD0", 0, "FP3232", 2, 1, 1, "flat", "pixelUv");

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { flatPixelUv }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelTex0Dump("float2 pixelUv"))));
    }

    /// <summary>Rejects matching authored semantics routed to different hardware channels with different raw indices.</summary>
    [Fact]
    public void RejectsMatchingSemanticWithMismatchedHardwareChannels() {
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float2 pixelUv : $vin.TEX1 : TEX1 : -1 : 1\n";

        Assert.Throws<InvalidOperationException>(() => Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { Interface("TEXCOORD0", "vertexUv", 2) }),
            Program(ShaderStage.Vertex, VertexTex0Dump("float2 vertexUv")),
            Translation(ShaderStage.Pixel, new[] { Interface("TEXCOORD0", "pixelUv", 2) }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, pixelDump)));
    }

    /// <summary>Does not demand an authored varying producer for the compiler's fixed-function WPOS input.</summary>
    [Fact]
    public void DoesNotLinkPixelWposBuiltinAsUserVarying() {
        const string pixelDump = "#profile fp40\n"
            + "#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n"
            + "#var float4 fragment.position : $vin.WPOS : WPOS : -1 : 1\n";
        Ps3ShaderTranslation pixelSource = new(
            ShaderStage.Pixel,
            "pixel source",
            new[] { "FragCoord" },
            Array.Empty<string>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>(),
            Array.Empty<Ps3ShaderUniformBuffer>());

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, VertexPositionDump()),
            pixelSource,
            Program(ShaderStage.Pixel, pixelDump));

        Assert.Empty(map.StageLinks);
    }

    /// <summary>Maps active vertex attributes through their generated names to the actual SDK attribute parameters.</summary>
    [Fact]
    public void MapsAuthoredVertexAttributeToItsSdkParameter() {
        Ps3ShaderInterfaceVariable position = new("POSITION", 0, "FP3232", 3, 1, 1, "smooth", "positionInput");
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float3 positionInput : $vin.ATTR0 : ATTR0 : -1 : 1\n";
        Ps3RsxParameter parameter = new("positionInput", Ps3RsxParameterCategory.Attribute, 0, 1, Ps3RsxDataType.Float3, Array.Empty<Ps3RsxConstantElement>());

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, new[] { position }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, vertexDump, parameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Ps3ShaderAttributeBinding binding = Assert.Single(map.Attributes);
        Assert.Equal("POSITION", binding.AuthoredSemantic);
        Assert.Equal(0u, binding.HardwareSlot);
        Assert.Same(parameter, binding.RsxParameter);
    }

    /// <summary>Rejects an active array vertex input rather than treating it as one RSX attribute slot.</summary>
    [Fact]
    public void RejectsArrayVertexAttributeInsteadOfMappingSingleSlot() {
        Ps3ShaderInterfaceVariable position = new("POSITION", 0, "FP3232", 3, 1, 2, "smooth", "positionInput");
        const string vertexDump = "#profile vp40\n"
            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
            + "#var float3 positionInput : $vin.ATTR0 : ATTR0 : -1 : 1\n";
        Ps3RsxParameter parameter = new("positionInput", Ps3RsxParameterCategory.Attribute, 0, 1, Ps3RsxDataType.Float3, Array.Empty<Ps3RsxConstantElement>());

        Assert.Throws<NotSupportedException>(() => Map(
            Translation(ShaderStage.Vertex, new[] { position }, Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, vertexDump, parameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump())));
    }

    /// <summary>Maps duplicate authored member paths by their separate buffer identities and exact FP3232 metadata.</summary>
    [Fact]
    public void MapsSameNamedUniformMembersFromDifferentBuffersSeparately() {
        Ps3ShaderUniformMember primary = Uniform("Primary", 1, "Tint", "primaryTint");
        Ps3ShaderUniformMember secondary = Uniform("Secondary", 2, "Tint", "secondaryTint");
        Ps3RsxParameter primaryParameter = VertexConstant("primaryTint", Ps3RsxDataType.Float4, 1, 2);
        Ps3RsxParameter secondaryParameter = VertexConstant("secondaryTint", Ps3RsxDataType.Float4, 1, 3);
        string vertexDump = VertexPositionLine + "#var float4 primaryTint :  : c[2] : -1 : 1\n"
            + "#var float4 secondaryTint :  : c[3] : -1 : 1\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { primary, secondary },
                uniformBuffers: [new("Primary", 1, 16), new("Secondary", 2, 16)]),
            Program(ShaderStage.Vertex, vertexDump, primaryParameter, secondaryParameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Assert.Equal(2, map.Uniforms.Count);
        Assert.Equal(new[] { "Primary", "Secondary" }, map.Uniforms.Select(binding => binding.AuthoredBufferName));
        Assert.All(map.Uniforms, binding => Assert.False(string.IsNullOrWhiteSpace(binding.AuthoredMemberPath)));
        Assert.Equal(new[] { primaryParameter, secondaryParameter }, map.Uniforms.Select(binding => binding.RsxParameter));
    }

    /// <summary>Uses physical SPIR-V matrix columns and vector width to reconcile the SDK constant-vector count.</summary>
    [Fact]
    public void MapsPhysicalFloat3x4MatrixToThreeVertexConstantRegisters() {
        Ps3ShaderUniformMember matrix = new("Frame", 0, "RowMajorTransform", 80, 48, 0, 16, false,
            "physicalMatrix", "FP3232", 4, 3, 1);
        Ps3RsxParameter parameter = VertexConstant("physicalMatrix", Ps3RsxDataType.Float3x4, 3, 4);
        string vertexDump = VertexPositionLine + "#var float3x4 physicalMatrix :  : c[4], 3 : -1 : 1\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { matrix },
                uniformBuffers: [new("Frame", 0, 128)]),
            Program(ShaderStage.Vertex, vertexDump, parameter),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Ps3ShaderUniformBinding binding = Assert.Single(map.Uniforms);
        Assert.Equal("FP3232", binding.SourceMember.ScalarType);
        Assert.Equal(3u, binding.SourceMember.Columns);
        Assert.Equal(4u, binding.SourceMember.VectorWidth);
        Assert.Equal(new uint?[] { 4, 5, 6 }, binding.RsxParameter.Elements.Select(element => element.VertexRegister));
    }

    /// <summary>Allows a translation member to disappear when Cg marks the corresponding declaration inactive.</summary>
    [Fact]
    public void OmitsOptimizedOutUniformMemberWithoutInventingAParameter() {
        Ps3ShaderUniformMember tint = Uniform("Material", 1, "Tint", "inactiveTint");
        string vertexDump = VertexPositionLine + "#var float4 inactiveTint :  : c[0] : -1 : 0\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), new[] { tint },
                uniformBuffers: [new("Material", 1, 16)]),
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump()));

        Assert.Empty(map.Uniforms);
    }

    /// <summary>Maps two combined samplers that share a texture but use distinct authored sampler identities and RSX units.</summary>
    [Fact]
    public void MapsTextureSamplerPairsToDistinctSdkTextureUnits() {
        Ps3ShaderSamplerBinding linear = new("Albedo", 0, "LinearSampler", 0, "sampleLinear", "Dim2D");
        Ps3ShaderSamplerBinding point = new("Albedo", 0, "PointSampler", 1, "samplePoint", "Dim2D");
        Ps3RsxParameter linearParameter = new("sampleLinear", Ps3RsxParameterCategory.Sampler, 0, 1, Ps3RsxDataType.Sampler2D, Array.Empty<Ps3RsxConstantElement>());
        Ps3RsxParameter pointParameter = new("samplePoint", Ps3RsxParameterCategory.Sampler, 1, 1, Ps3RsxDataType.Sampler2D, Array.Empty<Ps3RsxConstantElement>());
        string pixelDump = PixelOutputLine
            + "#var sampler2D sampleLinear :  : texunit 0 : -1 : 1\n"
            + "#var sampler2D samplePoint :  : texunit 1 : -1 : 1\n";

        Ps3ShaderBindingMap map = Map(
            Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Vertex, VertexPositionDump()),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>(), samplers: new[] { linear, point }),
            Program(ShaderStage.Pixel, pixelDump, linearParameter, pointParameter));

        Assert.Equal(2, map.Samplers.Count);
        Assert.Equal(new[] { "LinearSampler", "PointSampler" }, map.Samplers.Select(binding => binding.SamplerName));
        Assert.Equal(new uint[] { 0, 1 }, map.Samplers.Select(binding => binding.TextureUnit));
        Assert.Equal(new[] { linearParameter, pointParameter }, map.Samplers.Select(binding => binding.RsxParameter));
    }

    /// <summary>Rejects unsupported active output routes instead of silently omitting them from the validated map.</summary>
    [Fact]
    public void RejectsUnknownActiveOutputRoute() {
        string vertexDump = VertexPositionLine + "#var float2 outputUv : $vout.MYSTERY : MYSTERY : -1 : 1\n";
        Ps3ShaderTranslation vertex = Translation(ShaderStage.Vertex, Array.Empty<Ps3ShaderInterfaceVariable>(),
            new[] { Interface("TEXCOORD0", "outputUv", 2) });

        Assert.Throws<InvalidOperationException>(() => Map(
            vertex,
            Program(ShaderStage.Vertex, vertexDump),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump())));
    }

    /// <summary>Rejects a vertex source translation that omits its required built-in Position output.</summary>
    [Fact]
    public void RequiresReflectedPositionBuiltin() {
        Ps3ShaderTranslation vertexSource = new(
            ShaderStage.Vertex,
            "vertex source",
            Array.Empty<string>(),
            Array.Empty<string>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderInterfaceVariable>(),
            Array.Empty<Ps3ShaderUniformMember>(),
            Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>(),
            Array.Empty<Ps3ShaderUniformBuffer>());

        Assert.Throws<InvalidOperationException>(() => Map(
            vertexSource,
            Program(ShaderStage.Vertex, VertexPositionDump()),
            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
            Program(ShaderStage.Pixel, PixelOutputDump())));
    }

    /// <summary>Creates a standard authored interface record for a 32-bit floating-point vector.</summary>
    /// <param name="semantic">HLSL semantic.</param>
    /// <param name="generatedName">Compiler-generated identifier used in the Cg dump.</param>
    /// <param name="width">Number of reflected scalar components.</param>
    /// <returns>The requested interface metadata.</returns>
    static Ps3ShaderInterfaceVariable Interface(string semantic, string generatedName, uint width) {
        return new Ps3ShaderInterfaceVariable(semantic, 0, "FP3232", width, 1, 1, "smooth", generatedName);
    }

    /// <summary>Creates a translation with explicit interface, uniform and sampler metadata.</summary>
    /// <param name="stage">Stage represented by the translation.</param>
    /// <param name="inputs">Reflected active stage inputs.</param>
    /// <param name="outputs">Reflected active stage outputs.</param>
    /// <param name="uniforms">Optional reflected active uniform members.</param>
    /// <param name="samplers">Optional reflected active combined samplers.</param>
    /// <param name="uniformBuffers">Explicit compiled-size metadata for synthetic buffers used by this fixture.</param>
    /// <returns>An immutable source translation fixture.</returns>
    static Ps3ShaderTranslation Translation(ShaderStage stage, Ps3ShaderInterfaceVariable[] inputs, Ps3ShaderInterfaceVariable[] outputs,
        Ps3ShaderUniformMember[] uniforms = null, Ps3ShaderSamplerBinding[] samplers = null,
        Ps3ShaderUniformBuffer[] uniformBuffers = null) {
        return new Ps3ShaderTranslation(stage, "shader source", Array.Empty<string>(), stage == ShaderStage.Vertex ? new[] { "Position" } : Array.Empty<string>(),
            inputs, outputs, uniforms ?? Array.Empty<Ps3ShaderUniformMember>(), samplers ?? Array.Empty<Ps3ShaderSamplerBinding>(),
            Array.Empty<Ps3ShaderSourceDependency>(), uniformBuffers ?? Array.Empty<Ps3ShaderUniformBuffer>());
    }

    /// <summary>Creates a parsed-program fixture around explicit Cg dump and RSX parameter data.</summary>
    /// <param name="stage">Expected RSX program stage.</param>
    /// <param name="dump">Cg profile and variable declarations.</param>
    /// <param name="parameters">Explicit SDK parameter records.</param>
    /// <returns>The requested stage program fixture.</returns>
    static Ps3RsxProgram Program(ShaderStage stage, string dump, params Ps3RsxParameter[] parameters) {
        return new Ps3RsxProgram(stage, Array.Empty<byte>(), dump, "test-sdk", parameters);
    }

    /// <summary>Builds the authored-interface reconciliation result for explicit stage fixtures.</summary>
    /// <param name="vertexSource">Vertex source metadata.</param>
    /// <param name="vertexProgram">Vertex SDK and dump fixture.</param>
    /// <param name="pixelSource">Pixel source metadata.</param>
    /// <param name="pixelProgram">Pixel SDK and dump fixture.</param>
    /// <returns>The validated binding map.</returns>
    static Ps3ShaderBindingMap Map(Ps3ShaderTranslation vertexSource, Ps3RsxProgram vertexProgram,
        Ps3ShaderTranslation pixelSource, Ps3RsxProgram pixelProgram) {
        return new Ps3ShaderBindingMapBuilder().Build(vertexSource, vertexProgram, pixelSource, pixelProgram);
    }

    /// <summary>Creates one float4 uniform metadata record with an exact generated Cg identifier.</summary>
    /// <param name="bufferName">Authored constant-buffer name.</param>
    /// <param name="bufferBinding">Authored constant-buffer binding.</param>
    /// <param name="memberPath">Authored member path.</param>
    /// <param name="generatedName">Compiler-generated Cg parameter name.</param>
    /// <returns>The requested uniform member metadata.</returns>
    static Ps3ShaderUniformMember Uniform(string bufferName, uint bufferBinding, string memberPath, string generatedName) {
        return new Ps3ShaderUniformMember(bufferName, bufferBinding, memberPath, 0, 16, 0, 0, false, generatedName, "FP3232", 4, 1, 1);
    }

    /// <summary>Creates one named vertex constant parameter with its physical register list.</summary>
    /// <param name="name">Exact Cg parameter name.</param>
    /// <param name="dataType">Pinned SDK constant type.</param>
    /// <param name="elementCount">Physical constant vector count.</param>
    /// <param name="firstRegister">First vertex constant register.</param>
    /// <returns>The requested SDK constant parameter.</returns>
    static Ps3RsxParameter VertexConstant(string name, Ps3RsxDataType dataType, uint elementCount, uint firstRegister) {
        List<Ps3RsxConstantElement> elements = new();
        for (uint index = 0; index < elementCount; index++) {
            elements.Add(new Ps3RsxConstantElement(index, firstRegister + index, Array.Empty<uint>(), new uint[4]));
        }
        return new Ps3RsxParameter(name, Ps3RsxParameterCategory.Constant, null, elementCount, dataType, elements);
    }

    /// <summary>Creates a vertex dump with a required position route and one TEX0 output.</summary>
    /// <param name="outputDeclaration">Full float varying declaration prefix and name.</param>
    /// <returns>A complete minimal vp40 dump.</returns>
    static string VertexTex0Dump(string outputDeclaration) {
        return VertexPositionLine + $"#var {outputDeclaration} : $vout.TEX0 : TEX0 : -1 : 1\n";
    }

    /// <summary>Creates a pixel dump with its fixed-function color output and one TEX0 input.</summary>
    /// <param name="inputDeclaration">Full float varying declaration prefix and name.</param>
    /// <returns>A complete minimal fp40 dump.</returns>
    static string PixelTex0Dump(string inputDeclaration) {
        return PixelOutputLine + $"#var {inputDeclaration} : $vin.TEX0 : TEX0 : -1 : 1\n";
    }

    /// <summary>Creates a valid vertex dump containing only fixed-function position.</summary>
    /// <returns>A minimal vp40 dump.</returns>
    static string VertexPositionDump() {
        return VertexPositionLine;
    }

    /// <summary>Creates a valid pixel dump containing only the fixed-function color output.</summary>
    /// <returns>A minimal fp40 dump.</returns>
    static string PixelOutputDump() {
        return PixelOutputLine;
    }

    /// <summary>Gets the pinned SDK's active vertex position declaration line.</summary>
    static string VertexPositionLine => "#profile vp40\n#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n";

    /// <summary>Gets the pinned SDK's active fragment color output declaration line.</summary>
    static string PixelOutputLine => "#profile fp40\n#var float4 gl_FragData[0] : $vout.COL : COL : -1 : 1\n";

    /// <summary>Builds the smallest SDK vertex container with a valid aligned microcode slot.</summary>
    /// <returns>A structurally valid one-instruction program with no parameter tables.</returns>
    static byte[] CreatePositionOnlyVertexProgram() {
        byte[] bytecode = new byte[64];
        bytecode[0] = 0x56;
        bytecode[1] = 0x50;
        bytecode[10] = 0;
        bytecode[11] = 1;
        bytecode[15] = 36;
        bytecode[19] = 36;
        bytecode[23] = 48;
        return bytecode;
    }
}
