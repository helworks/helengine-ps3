# Task 4 fix round 1 scoped diff

Baseline: task-4-review-snapshot.md. Scope: attribute array rejection, separate logical stage program names, coverage-report correction. Final48/48 pure+reader and7/7 Docker. Report: task-4-report.md, Review fix round1.

```diff
--- a/builder/shaders/Ps3ShaderBindingMapBuilder.cs
+++ b/builder/shaders/Ps3ShaderBindingMapBuilder.cs
@@ -109,16 +109,19 @@
     /// <summary>Maps every active reflected vertex input to its exact SDK attribute record.</summary>
     /// <param name="source">Reflected vertex translation.</param>
     /// <param name="program">Validated vertex RSX program.</param>
     /// <param name="variables">Parsed vertex Cg declarations.</param>
     /// <returns>Authored input mappings in source-reflection order.</returns>
     static List<Ps3ShaderAttributeBinding> BuildAttributes(Ps3ShaderTranslation source, Ps3RsxProgram program, IReadOnlyList<Ps3CgDumpVariable> variables) {
         List<Ps3ShaderAttributeBinding> result = new();
         foreach (Ps3ShaderInterfaceVariable variable in source.Inputs) {

+            if (variable.ArrayLength != 1) {
+                throw new NotSupportedException($"Active vertex attribute '{variable.Semantic}' has unsupported array length {variable.ArrayLength}.");
+            }
             Ps3CgDumpVariable declaration = FindActiveVariable(variables, variable.GeneratedName, "vertex attribute");
             uint slot = ParseVertexAttributeSlot(declaration);
             Ps3RsxParameter parameter = FindParameter(program, declaration.Name, Ps3RsxParameterCategory.Attribute);
             if (parameter.HardwareIndex != slot) throw new InvalidOperationException($"Vertex input '{variable.Semantic}' Cg slot {slot} disagrees with SDK attribute index {parameter.HardwareIndex}.");
             ValidateParameterType(variable.ScalarType, variable.VectorWidth, variable.Columns, parameter, declaration);
             result.Add(new Ps3ShaderAttributeBinding(variable, parameter));
         }
         foreach (Ps3CgDumpVariable declaration in variables) {
```

```diff
--- a/builder/shaders/Ps3ShaderPairCompiler.cs
+++ b/builder/shaders/Ps3ShaderPairCompiler.cs
@@ -61,19 +61,18 @@
     /// <param name="pixelRequest">Pixel request candidate.</param>
     static void ValidateCompatibleRequests(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest) {
         if (vertexRequest.Target != ShaderCompileTarget.Ps3 || pixelRequest.Target != ShaderCompileTarget.Ps3) {
             throw new InvalidOperationException("Both shader pair requests must target PS3.");
         }
         if (vertexRequest.Stage != ShaderStage.Vertex || pixelRequest.Stage != ShaderStage.Pixel) {
             throw new InvalidOperationException("Shader pair requests must be ordered as vertex then pixel stages.");
         }
-        if (!string.Equals(vertexRequest.ProgramName, pixelRequest.ProgramName, StringComparison.Ordinal)
-            || !string.Equals(vertexRequest.Variant, pixelRequest.Variant, StringComparison.Ordinal)) {
-            throw new InvalidOperationException("Vertex and pixel requests must share program and variant identities.");
+        if (!string.Equals(vertexRequest.Variant, pixelRequest.Variant, StringComparison.Ordinal)) {
+            throw new InvalidOperationException("Vertex and pixel requests must share a variant identity.");
         }
         if (vertexRequest.ShaderModel.Major != pixelRequest.ShaderModel.Major || vertexRequest.ShaderModel.Minor != pixelRequest.ShaderModel.Minor) {
             throw new InvalidOperationException("Vertex and pixel requests must use the same shader model.");
         }
         if (!HaveSameDefines(vertexRequest.Defines, pixelRequest.Defines)) {
             throw new InvalidOperationException("Vertex and pixel requests must use the same ordered preprocessor definitions.");
         }
         if (!HaveSameOptions(vertexRequest.Options, pixelRequest.Options)) {
```

```diff
--- a/builder.tests/Ps3ShaderPairCompilerTests.cs
+++ b/builder.tests/Ps3ShaderPairCompilerTests.cs
@@ -7,146 +7,166 @@
 /// <summary>Verifies paired PS3 compilation validates request identity and forwards cancellation to both stage jobs.</summary>
 public sealed class Ps3ShaderPairCompilerTests {
     /// <summary>HLSL source with one active vertex attribute and built-in position output.</summary>
     const string VertexSource = "float4 VS(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }";
 
     /// <summary>HLSL source with one fixed-function fragment color output.</summary>
     const string PixelSource = "float4 PS() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }";
 
-    /// <summary>Proves variant mismatches are rejected before either source translation or RSX compilation begins.</summary>
-    [Fact]
-    public void RejectsIncompatibleVariantBeforeStartingJobs() {
-        Ps3ShaderPairCompilerStub rsxCompiler = new();
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
-        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "wireframe", ShaderCompileTarget.Ps3);
-
-        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
-        Assert.Equal(0, rsxCompiler.CallCount);
-    }
-
-    /// <summary>Rejects non-PS3 targets before invoking the source or RSX compiler.</summary>
-    [Fact]
-    public void RejectsNonPs3TargetBeforeStartingJobs() {
-        Ps3ShaderPairCompilerStub rsxCompiler = new();
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.DirectX11);
-        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
-
-        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
-        Assert.Equal(0, rsxCompiler.CallCount);
-    }
-
-    /// <summary>Rejects requests that are not ordered as vertex followed by pixel before starting jobs.</summary>
-    [Fact]
-    public void RejectsIncorrectStageOrderBeforeStartingJobs() {
-        Ps3ShaderPairCompilerStub rsxCompiler = new();
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
-        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
-
-        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
-        Assert.Equal(0, rsxCompiler.CallCount);
-    }
-
-    /// <summary>Rejects differing preprocessor define sets as incompatible pair variants before starting jobs.</summary>
-    [Fact]
-    public void RejectsDifferentDefineSetsBeforeStartingJobs() {
-        Ps3ShaderPairCompilerStub rsxCompiler = new();
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3,
-            new[] { new ShaderDefine("QUALITY", "high") });
-        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, Array.Empty<ShaderDefine>());
-
-        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
-        Assert.Equal(0, rsxCompiler.CallCount);
-    }
-
-    /// <summary>Rejects requests that target different shader models before starting either stage job.</summary>
-    [Fact]
-    public void RejectsDifferentShaderModelsBeforeStartingJobs() {
-        Ps3ShaderPairCompilerStub rsxCompiler = new();
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
-        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, model: new ShaderModel(4, 0));
-
-        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
-        Assert.Equal(0, rsxCompiler.CallCount);
-    }
-
-    /// <summary>Rejects requests with different optimization options before starting either stage job.</summary>
-    [Fact]
-    public void RejectsDifferentCompileOptionsBeforeStartingJobs() {
-        Ps3ShaderPairCompilerStub rsxCompiler = new();
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileOptions unoptimized = new(ShaderBindingPolicies.Default, true, false, false);
-        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
-        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, options: unoptimized);
-
-        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
-        Assert.Equal(0, rsxCompiler.CallCount);
-    }
-
-    /// <summary>Proves a cancellation raised by the pixel job is propagated after the vertex job and carries the caller token.</summary>
-    [Fact]
-    public void ForwardsCancellationTokenThroughBothStageJobs() {
-        using CancellationTokenSource cancellationSource = new();
-        Ps3ShaderPairCompilerStub rsxCompiler = new(2);
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
-        ShaderCompileRequest vertex = Request(VertexSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
-        ShaderCompileRequest pixel = Request(PixelSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
-
-        Assert.Throws<OperationCanceledException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), cancellationSource.Token)));
-
-        Assert.Equal(2, rsxCompiler.CallCount);
-        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[0]);
-        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[1]);
-        Assert.NotEqual(rsxCompiler.WorkingRoots[0], rsxCompiler.WorkingRoots[1]);
-    }
-
-    /// <summary>Compiles the modern cbuffer, paired-texture and two-varying fixture with the pinned Docker SDK.</summary>
-    [Fact]
-    [Trait("Category", "Docker")]
-    public void CompilesModernTextureCbufferPairAndLinksBothVaryingsWithPinnedSdk() {
-        ShaderCompileRequest vertex = Request(ModernPairSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
-        ShaderCompileRequest pixel = Request(ModernPairSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
-        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), new Ps3DockerShaderCompiler(Ps3ShaderToolchain.Load(RequiredRepositoryRoot())));
-
-        Ps3ShaderProgramPair pair = compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(),
-            Path.Combine(RequiredArtifactsRoot(), "modern-texture-cbuffer-pair"), CancellationToken.None);
-
-        Assert.Equal(ShaderStage.Vertex, pair.Vertex.Stage);
-        Assert.Equal(ShaderStage.Pixel, pair.Pixel.Stage);
-        Assert.Equal(ShaderStage.Vertex, pair.VertexTranslation.Stage);
-        Assert.Equal(ShaderStage.Pixel, pair.PixelTranslation.Stage);
-        Assert.Equal(3, pair.Bindings.Attributes.Count);
-        Assert.Equal(2, pair.Bindings.StageLinks.Count);
-        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "COLOR0" && link.VertexChannel == "TEX0" && link.PixelChannel == "TEX0");
-        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "TEXCOORD0" && link.VertexChannel == "TEX1" && link.PixelChannel == "TEX1");
-        Assert.All(pair.Bindings.StageLinks, link => Assert.Equal(link.VertexChannel, link.PixelChannel));
-        Assert.Equal(2, pair.Bindings.Samplers.Count);
-        Assert.Equal(2, pair.Bindings.Samplers.Select(binding => binding.TextureUnit).Distinct().Count());
-        Assert.Contains(pair.Bindings.Uniforms, binding => binding.AuthoredMemberPath == "Primary.Tint");
-        Ps3ShaderUniformBinding pixelTint = Assert.Single(pair.Bindings.Uniforms,
-            binding => binding.Stage == ShaderStage.Pixel && binding.AuthoredMemberPath == "Primary.Tint");
-        Assert.Contains(pixelTint.RsxParameter.Elements, element => element.FragmentRelocationOffsets.Count > 0);
-    }
-
-    /// <summary>Builds a request with an explicit target, entry point and variant for validation tests.</summary>
-    /// <param name="source">HLSL source text.</param>
-    /// <param name="stage">Requested pipeline stage.</param>
-    /// <param name="variant">Variant identity.</param>
-    /// <param name="target">Compilation target.</param>
-    /// <returns>The requested immutable compile request.</returns>
-    static ShaderCompileRequest Request(string source, ShaderStage stage, string variant, ShaderCompileTarget target,
-        IReadOnlyList<ShaderDefine> defines = null, ShaderModel model = null, ShaderCompileOptions options = null) {
-        return new ShaderCompileRequest(
-            new ShaderSourceInfo("pair-test.hlsl", source),
-            "PairTest",
+    /// <summary>HLSL source with only the mandatory built-in position output for fixed-function pair-stub tests.</summary>
+    const string PositionOnlyVertexSource = "float4 VS() : SV_Position { return float4(0.0, 0.0, 0.0, 1.0); }";
+
+    /// <summary>Proves variant mismatches are rejected before either source translation or RSX compilation begins.</summary>
+    [Fact]
+    public void RejectsIncompatibleVariantBeforeStartingJobs() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
+        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "wireframe", ShaderCompileTarget.Ps3);
+
+        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
+        Assert.Equal(0, rsxCompiler.CallCount);
+    }
+
+    /// <summary>Rejects non-PS3 targets before invoking the source or RSX compiler.</summary>
+    [Fact]
+    public void RejectsNonPs3TargetBeforeStartingJobs() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.DirectX11);
+        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
+
+        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
+        Assert.Equal(0, rsxCompiler.CallCount);
+    }
+
+    /// <summary>Rejects requests that are not ordered as vertex followed by pixel before starting jobs.</summary>
+    [Fact]
+    public void RejectsIncorrectStageOrderBeforeStartingJobs() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
+        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
+
+        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
+        Assert.Equal(0, rsxCompiler.CallCount);
+    }
+
+    /// <summary>Rejects differing preprocessor define sets as incompatible pair variants before starting jobs.</summary>
+    [Fact]
+    public void RejectsDifferentDefineSetsBeforeStartingJobs() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3,
+            new[] { new ShaderDefine("QUALITY", "high") });
+        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, Array.Empty<ShaderDefine>());
+
+        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
+        Assert.Equal(0, rsxCompiler.CallCount);
+    }
+
+    /// <summary>Rejects requests that target different shader models before starting either stage job.</summary>
+    [Fact]
+    public void RejectsDifferentShaderModelsBeforeStartingJobs() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
+        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, model: new ShaderModel(4, 0));
+
+        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
+        Assert.Equal(0, rsxCompiler.CallCount);
+    }
+
+    /// <summary>Rejects requests with different optimization options before starting either stage job.</summary>
+    [Fact]
+    public void RejectsDifferentCompileOptionsBeforeStartingJobs() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileOptions unoptimized = new(ShaderBindingPolicies.Default, true, false, false);
+        ShaderCompileRequest vertex = Request("not valid HLSL", ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
+        ShaderCompileRequest pixel = Request("not valid HLSL", ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, options: unoptimized);
+
+        Assert.Throws<InvalidOperationException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None)));
+        Assert.Equal(0, rsxCompiler.CallCount);
+    }
+
+    /// <summary>Compiles per-stage logical program names while preserving their shared variant identity.</summary>
+    [Fact]
+    public void CompilesPairWithDistinctStageProgramNames() {
+        Ps3ShaderPairCompilerStub rsxCompiler = new();
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request(PositionOnlyVertexSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3, programName: "asset.vs");
+        ShaderCompileRequest pixel = Request(PixelSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3, programName: "asset.ps");
+
+        Ps3ShaderProgramPair pair = compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), CancellationToken.None);
+
+        Assert.Equal(2, rsxCompiler.CallCount);
+        Assert.Equal(ShaderStage.Vertex, pair.Vertex.Stage);
+        Assert.Equal(ShaderStage.Pixel, pair.Pixel.Stage);
+    }
+
+    /// <summary>Proves a cancellation raised by the pixel job is propagated after the vertex job and carries the caller token.</summary>
+    [Fact]
+    public void ForwardsCancellationTokenThroughBothStageJobs() {
+        using CancellationTokenSource cancellationSource = new();
+        Ps3ShaderPairCompilerStub rsxCompiler = new(2);
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), rsxCompiler);
+        ShaderCompileRequest vertex = Request(VertexSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
+        ShaderCompileRequest pixel = Request(PixelSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
+
+        Assert.Throws<OperationCanceledException>((Action)(() => compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(), WorkingRoot(), cancellationSource.Token)));
+
+        Assert.Equal(2, rsxCompiler.CallCount);
+        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[0]);
+        Assert.Equal(cancellationSource.Token, rsxCompiler.CancellationTokens[1]);
+        Assert.NotEqual(rsxCompiler.WorkingRoots[0], rsxCompiler.WorkingRoots[1]);
+    }
+
+    /// <summary>Compiles the modern cbuffer, paired-texture and two-varying fixture with the pinned Docker SDK.</summary>
+    [Fact]
+    [Trait("Category", "Docker")]
+    public void CompilesModernTextureCbufferPairAndLinksBothVaryingsWithPinnedSdk() {
+        ShaderCompileRequest vertex = Request(ModernPairSource, ShaderStage.Vertex, "default", ShaderCompileTarget.Ps3);
+        ShaderCompileRequest pixel = Request(ModernPairSource, ShaderStage.Pixel, "default", ShaderCompileTarget.Ps3);
+        Ps3ShaderPairCompiler compiler = new(new Ps3ShaderSourceCompiler(), new Ps3DockerShaderCompiler(Ps3ShaderToolchain.Load(RequiredRepositoryRoot())));
+
+        Ps3ShaderProgramPair pair = compiler.Compile(vertex, pixel, new Ps3ShaderIncludeResolverStub(),
+            Path.Combine(RequiredArtifactsRoot(), "modern-texture-cbuffer-pair"), CancellationToken.None);
+
+        Assert.Equal(ShaderStage.Vertex, pair.Vertex.Stage);
+        Assert.Equal(ShaderStage.Pixel, pair.Pixel.Stage);
+        Assert.Equal(ShaderStage.Vertex, pair.VertexTranslation.Stage);
+        Assert.Equal(ShaderStage.Pixel, pair.PixelTranslation.Stage);
+        Assert.Equal(3, pair.Bindings.Attributes.Count);
+        Assert.Equal(2, pair.Bindings.StageLinks.Count);
+        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "COLOR0" && link.VertexChannel == "TEX0" && link.PixelChannel == "TEX0");
+        Assert.Contains(pair.Bindings.StageLinks, link => link.AuthoredSemantic == "TEXCOORD0" && link.VertexChannel == "TEX1" && link.PixelChannel == "TEX1");
+        Assert.All(pair.Bindings.StageLinks, link => Assert.Equal(link.VertexChannel, link.PixelChannel));
+        Assert.Equal(2, pair.Bindings.Samplers.Count);
+        Assert.Equal(2, pair.Bindings.Samplers.Select(binding => binding.TextureUnit).Distinct().Count());
+        Assert.Contains(pair.Bindings.Uniforms, binding => binding.AuthoredMemberPath == "Primary.Tint");
+        Ps3ShaderUniformBinding pixelTint = Assert.Single(pair.Bindings.Uniforms,
+            binding => binding.Stage == ShaderStage.Pixel && binding.AuthoredMemberPath == "Primary.Tint");
+        Assert.Contains(pixelTint.RsxParameter.Elements, element => element.FragmentRelocationOffsets.Count > 0);
+    }
+
+    /// <summary>Builds a request with an explicit target, entry point and variant for validation tests.</summary>
+    /// <param name="source">HLSL source text.</param>
+    /// <param name="stage">Requested pipeline stage.</param>
+    /// <param name="variant">Variant identity.</param>
+    /// <param name="target">Compilation target.</param>
+    /// <param name="programName">Logical name assigned to the individual stage request.</param>
+    /// <returns>The requested immutable compile request.</returns>
+    static ShaderCompileRequest Request(string source, ShaderStage stage, string variant, ShaderCompileTarget target,
+        IReadOnlyList<ShaderDefine> defines = null, ShaderModel model = null, ShaderCompileOptions options = null,
+        string programName = "PairTest") {
+        return new ShaderCompileRequest(
+            new ShaderSourceInfo("pair-test.hlsl", source),
+            programName,
             stage == ShaderStage.Vertex ? "VS" : "PS",
             stage,
             target,
             model ?? new ShaderModel(3, 0),
             variant,
             defines ?? Array.Empty<ShaderDefine>(),
             options ?? new ShaderCompileOptions(ShaderBindingPolicies.Default, true, true, false));
     }
```

```diff
--- a/builder.tests/Ps3ShaderBindingMapTests.cs
+++ b/builder.tests/Ps3ShaderBindingMapTests.cs
@@ -247,16 +247,32 @@
             Program(ShaderStage.Pixel, PixelOutputDump()));
 
         Ps3ShaderAttributeBinding binding = Assert.Single(map.Attributes);
         Assert.Equal("POSITION", binding.AuthoredSemantic);
         Assert.Equal(0u, binding.HardwareSlot);
         Assert.Same(parameter, binding.RsxParameter);
     }
 

+    /// <summary>Rejects an active array vertex input rather than treating it as one RSX attribute slot.</summary>
+    [Fact]
+    public void RejectsArrayVertexAttributeInsteadOfMappingSingleSlot() {
+        Ps3ShaderInterfaceVariable position = new("POSITION", 0, "FP3232", 3, 1, 2, "smooth", "positionInput");
+        const string vertexDump = "#profile vp40\n"
+            + "#var float4 gl_Position : $vout.POSITION : HPOS : -1 : 1\n"
+            + "#var float3 positionInput : $vin.ATTR0 : ATTR0 : -1 : 1\n";
+        Ps3RsxParameter parameter = new("positionInput", Ps3RsxParameterCategory.Attribute, 0, 1, Ps3RsxDataType.Float3, Array.Empty<Ps3RsxConstantElement>());
+
+        Assert.Throws<NotSupportedException>(() => Map(
+            Translation(ShaderStage.Vertex, new[] { position }, Array.Empty<Ps3ShaderInterfaceVariable>()),
+            Program(ShaderStage.Vertex, vertexDump, parameter),
+            Translation(ShaderStage.Pixel, Array.Empty<Ps3ShaderInterfaceVariable>(), Array.Empty<Ps3ShaderInterfaceVariable>()),
+            Program(ShaderStage.Pixel, PixelOutputDump())));
+    }
+
     /// <summary>Maps duplicate authored member paths by their separate buffer identities and exact FP3232 metadata.</summary>
     [Fact]
     public void MapsSameNamedUniformMembersFromDifferentBuffersSeparately() {
         Ps3ShaderUniformMember primary = Uniform("Primary", 1, "Tint", "primaryTint");
         Ps3ShaderUniformMember secondary = Uniform("Secondary", 2, "Tint", "secondaryTint");
         Ps3RsxParameter primaryParameter = VertexConstant("primaryTint", Ps3RsxDataType.Float4, 1, 2);
         Ps3RsxParameter secondaryParameter = VertexConstant("secondaryTint", Ps3RsxDataType.Float4, 1, 3);
         string vertexDump = VertexPositionLine + "#var float4 primaryTint :  : c[2] : -1 : 1\n"
```


