# Task 1 review snapshot

Base HEAD ef27238ed85fe3218618a752c11d9eeede0a75a2; changes uncommitted. All shader files are untracked against HEAD, so snapshot includes the inherited prototype. Review task requirements and changes in that context. No commits.

```diff
warning: in the working copy of 'builder/shaders/Ps3ShaderSourceCompiler.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder/shaders/Ps3ShaderSourceCompiler.cs b/builder/shaders/Ps3ShaderSourceCompiler.cs
new file mode 100644
index 0000000..f9ca104
--- /dev/null
+++ b/builder/shaders/Ps3ShaderSourceCompiler.cs
@@ -0,0 +1,127 @@
+using Silk.NET.Shaderc;
+using Silk.NET.SPIRV.Cross;
+using System.Runtime.InteropServices;
+using System.Text;
+
+namespace helengine.ps3.builder;
+
+/// <summary>Translates authored HLSL through SPIR-V into legacy GLSL accepted by the Cg GLSL frontend.</summary>
+public sealed class Ps3ShaderSourceCompiler {
+    /// <summary>Compiles one PS3 vertex or pixel entry point with shaderc-managed source includes.</summary>
+    /// <param name="request">Source, entry point, stage and preprocessor definitions.</param>
+    /// <param name="includeResolver">Resolver used for active includes selected by shaderc preprocessing.</param>
+    /// <returns>GLSL 1.20 source for the RSX compiler's main entry point.</returns>
+    public string Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
+        ArgumentNullException.ThrowIfNull(request);
+        ArgumentNullException.ThrowIfNull(includeResolver);
+        if (request.Target != ShaderCompileTarget.Ps3) {
+            throw new NotSupportedException("The PS3 HLSL compiler accepts PS3 compilation requests only.");
+        }
+        if (request.Stage != ShaderStage.Vertex && request.Stage != ShaderStage.Pixel) {
+            throw new NotSupportedException("PS3 supports vertex and pixel shader stages only.");
+        }
+        return EmitGlsl(CompileSpirv(request, includeResolver), request);
+    }
+
+    /// <summary>Runs the same HLSL frontend used by the other engine backends, preserving reflection names.</summary>
+    /// <param name="request">Authored compilation request.</param>
+    /// <returns>Validated SPIR-V words.</returns>
+    unsafe byte[] CompileSpirv(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
+        using Ps3ShaderIncludeSession includeSession = new(includeResolver);
+        using Shaderc api = Shaderc.GetApi();
+        Silk.NET.Shaderc.Compiler* compiler = api.CompilerInitialize();
+        CompileOptions* options = api.CompileOptionsInitialize();
+        if (compiler == null || options == null) {
+            if (compiler != null) api.CompilerRelease(compiler);
+            if (options != null) api.CompileOptionsRelease(options);
+            throw new InvalidOperationException("Unable to initialize the PS3 HLSL frontend.");
+        }
+        try {
+            api.CompileOptionsSetSourceLanguage(options, SourceLanguage.Hlsl);
+            api.CompileOptionsSetTargetEnv(options, TargetEnv.Vulkan, (uint)EnvVersion.Vulkan10);
+            api.CompileOptionsSetHlslIoMapping(options, true);
+            api.CompileOptionsSetAutoMapLocations(options, true);
+            api.CompileOptionsSetAutoBindUniforms(options, true);
+            api.CompileOptionsSetPreserveBindings(options, true);
+            api.CompileOptionsSetOptimizationLevel(options, request.Options.Optimize ? OptimizationLevel.Performance : OptimizationLevel.Zero);
+            if (request.Options.GenerateDebugInfo) api.CompileOptionsSetGenerateDebugInfo(options);
+            if (request.Options.TreatWarningsAsErrors) api.CompileOptionsSetWarningsAsErrors(options);
+            api.CompileOptionsSetIncludeCallbacks(options, includeSession.ResolveCallback, includeSession.ReleaseCallback, null);
+            foreach (ShaderDefine define in request.Defines) {
+                string value = define.Value ?? string.Empty;
+                api.CompileOptionsAddMacroDefinition(options, define.Name, (nuint)Encoding.UTF8.GetByteCount(define.Name), value, (nuint)Encoding.UTF8.GetByteCount(value));
+            }
+            string source = request.Source.Source;
+            CompilationResult* result = api.CompileIntoSpv(compiler, source, (nuint)Encoding.UTF8.GetByteCount(source),
+                request.Stage == ShaderStage.Vertex ? ShaderKind.VertexShader : ShaderKind.FragmentShader,
+                request.Source.Path, request.EntryPoint, options);
+            if (result == null) {
+                if (includeSession.FatalFailure is Exception failure) {
+                    throw new InvalidOperationException("PS3 HLSL include processing failed: " + failure.Message, failure);
+                }
+                throw new InvalidOperationException("HLSL compiler returned no result.");
+            }
+            try {
+                if (includeSession.FatalFailure is Exception failure) {
+                    throw new InvalidOperationException("PS3 HLSL include processing failed: " + failure.Message, failure);
+                }
+                if (api.ResultGetCompilationStatus(result) != CompilationStatus.Success) {
+                    throw new InvalidOperationException("PS3 HLSL compilation failed: " + api.ResultGetErrorMessageS(result));
+                }
+                byte[] bytes = new byte[checked((int)api.ResultGetLength(result))];
+                if (bytes.Length == 0) throw new InvalidOperationException("HLSL compilation produced empty SPIR-V.");
+                Marshal.Copy((IntPtr)api.ResultGetBytes(result), bytes, 0, bytes.Length);
+                return bytes;
+            } finally {
+                api.ResultRelease(result);
+            }
+        } finally {
+            api.CompileOptionsRelease(options);
+            api.CompilerRelease(compiler);
+        }
+    }
+
+    /// <summary>Uses SPIRV-Cross's legacy GLSL backend; uniforms and combined samplers remain compiler-owned.</summary>
+    /// <param name="spirv">Frontend output.</param>
+    /// <param name="request">Entry point and execution stage.</param>
+    /// <returns>Unmodified generated source.</returns>
+    unsafe string EmitGlsl(byte[] spirv, ShaderCompileRequest request) {
+        string library = Path.Combine(AppContext.BaseDirectory, "runtimes", "win-x64", "native", "spirv-cross.dll");
+        if (!File.Exists(library)) throw new FileNotFoundException("Packaged SPIRV-Cross library is missing.", library);
+        using Cross api = new(Cross.CreateDefaultContext([library]));
+        Context* context = null;
+        Check(api, context, api.ContextCreate(&context));
+        try {
+            ParsedIr* parsed = null;
+            Silk.NET.SPIRV.Cross.Compiler* compiler = null;
+            fixed (byte* data = spirv) {
+                Check(api, context, api.ContextParseSpirv(context, (uint*)data, (nuint)(spirv.Length / 4), &parsed));
+            }
+            Check(api, context, api.ContextCreateCompiler(context, Backend.Glsl, parsed, CaptureMode.TakeOwnership, &compiler));
+            CompilerOptions* options = null;
+            Check(api, context, api.CompilerCreateCompilerOptions(compiler, &options));
+            Check(api, context, api.CompilerOptionsSetUint(options, CompilerOption.GlslVersion, 120));
+            Check(api, context, api.CompilerOptionsSetBool(options, CompilerOption.GlslES, 0));
+            Check(api, context, api.CompilerOptionsSetBool(options, CompilerOption.GlslEmitUniformBufferAsPlainUniforms, 1));
+            Check(api, context, api.CompilerInstallCompilerOptions(compiler, options));
+            Check(api, context, api.CompilerSetEntryPoint(compiler, request.EntryPoint,
+                request.Stage == ShaderStage.Vertex ? Silk.NET.SPIRV.ExecutionModel.Vertex : Silk.NET.SPIRV.ExecutionModel.Fragment));
+            Check(api, context, api.CompilerBuildCombinedImageSamplers(compiler));
+            byte* source = null;
+            Check(api, context, api.CompilerCompile(compiler, &source));
+            return Marshal.PtrToStringUTF8((IntPtr)source) ?? throw new InvalidOperationException("SPIRV-Cross returned no GLSL source.");
+        } finally {
+            if (context != null) api.ContextDestroy(context);
+        }
+    }
+
+    /// <summary>Propagates compiler diagnostics instead of continuing with incomplete intermediate output.</summary>
+    /// <param name="api">Cross compiler API.</param>
+    /// <param name="context">Compilation context when available.</param>
+    /// <param name="result">Native operation result.</param>
+    static unsafe void Check(Cross api, Context* context, Result result) {
+        if (result != Result.Success) {
+            throw new InvalidOperationException("PS3 shader conversion failed: " + (context == null ? result.ToString() : api.ContextGetLastErrorStringS(context)));
+        }
+    }
+}
warning: in the working copy of 'builder/shaders/Ps3ShaderIncludeSession.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder/shaders/Ps3ShaderIncludeSession.cs b/builder/shaders/Ps3ShaderIncludeSession.cs
new file mode 100644
index 0000000..aeb8daf
--- /dev/null
+++ b/builder/shaders/Ps3ShaderIncludeSession.cs
@@ -0,0 +1,199 @@
+using helengine;
+using Silk.NET.Shaderc;
+using System.Runtime.InteropServices;
+using System.Security.Cryptography;
+using System.Text;
+
+namespace helengine.ps3.builder;
+
+/// <summary>Owns shaderc include callbacks, resolved source records and native include memory for one compile.</summary>
+public sealed unsafe class Ps3ShaderIncludeSession : IDisposable {
+    /// <summary>Serializes dependency consistency checks and native allocation bookkeeping.</summary>
+    readonly object Gate = new();
+
+    /// <summary>Resolver invoked for active include directives.</summary>
+    readonly IShaderIncludeResolver IncludeResolver;
+
+    /// <summary>Strong references that keep native callback delegates alive until session disposal.</summary>
+    readonly IncludeResolveFn ResolveDelegate;
+
+    /// <summary>Strong reference that keeps the native release delegate alive until session disposal.</summary>
+    readonly IncludeResultReleaseFn ReleaseDelegate;
+
+    /// <summary>Silk callback handle retained until shaderc has released its options and results.</summary>
+    readonly PfnIncludeResolveFn ResolveCallbackHandle;
+
+    /// <summary>Silk release handle retained until shaderc has released its options and results.</summary>
+    readonly PfnIncludeResultReleaseFn ReleaseCallbackHandle;
+
+    /// <summary>Resolved source contents used to detect inconsistent repeated paths.</summary>
+    readonly Dictionary<string, string> ResolvedContents = new(StringComparer.Ordinal);
+
+    /// <summary>Every active include resolution, preserving repeats in callback order.</summary>
+    readonly List<Ps3ShaderSourceDependency> DependenciesList = [];
+
+    /// <summary>Native include result blocks still owned by shaderc or awaiting final session cleanup.</summary>
+    readonly Dictionary<IntPtr, Ps3ShaderIncludeAllocation> Allocations = [];
+
+    /// <summary>Whether this session has released all callback allocations.</summary>
+    bool IsDisposed;
+
+    /// <summary>The first fatal resolver or consistency error, retained across shaderc lookup retries.</summary>
+    Exception FatalFailureValue;
+
+    /// <summary>Initializes rooted callbacks and allocation ownership for one compiler invocation.</summary>
+    /// <param name="includeResolver">Resolver for active source include directives.</param>
+    public Ps3ShaderIncludeSession(IShaderIncludeResolver includeResolver) {
+        IncludeResolver = includeResolver ?? throw new ArgumentNullException(nameof(includeResolver));
+        ResolveDelegate = ResolveInclude;
+        ReleaseDelegate = ReleaseInclude;
+        ResolveCallbackHandle = PfnIncludeResolveFn.From(ResolveDelegate);
+        ReleaseCallbackHandle = PfnIncludeResultReleaseFn.From(ReleaseDelegate);
+    }
+
+    /// <summary>Gets all active include dependencies in the order shaderc requested them.</summary>
+    public IReadOnlyList<Ps3ShaderSourceDependency> Dependencies {
+        get {
+            lock (Gate) return DependenciesList.ToArray();
+        }
+    }
+
+    /// <summary>Gets the first fatal include failure; ordinary relative include misses remain eligible for fallback.</summary>
+    public Exception FatalFailure {
+        get {
+            lock (Gate) return FatalFailureValue;
+        }
+    }
+
+    /// <summary>Gets the rooted shaderc resolve callback handle.</summary>
+    public PfnIncludeResolveFn ResolveCallback => ResolveCallbackHandle;
+
+    /// <summary>Gets the rooted shaderc release callback handle.</summary>
+    public PfnIncludeResultReleaseFn ReleaseCallback => ReleaseCallbackHandle;
+
+    /// <summary>Releases any native include allocations shaderc did not release before compilation ended.</summary>
+    public void Dispose() {
+        lock (Gate) {
+            if (IsDisposed) return;
+            IsDisposed = true;
+            foreach (Ps3ShaderIncludeAllocation allocation in Allocations.Values) allocation.Free();
+            Allocations.Clear();
+        }
+        ResolveCallbackHandle.Dispose();
+        ReleaseCallbackHandle.Dispose();
+    }
+
+    /// <summary>Resolves one shaderc callback and converts all managed failures into error include results.</summary>
+    /// <param name="userData">Opaque shaderc callback data, unused because the delegate captures this session.</param>
+    /// <param name="requestedSource">Include path written by the shader author.</param>
+    /// <param name="type">Shaderc include mode value.</param>
+    /// <param name="requestingSource">Source path that contains the include directive.</param>
+    /// <param name="includeDepth">Depth of this include in the active include chain.</param>
+    /// <returns>A shaderc-owned result allocation that must be released through <see cref="ReleaseInclude"/>.</returns>
+    unsafe IncludeResult* ResolveInclude(void* userData, byte* requestedSource, int type, byte* requestingSource, nuint includeDepth) {
+        string includePath = string.Empty;
+        string requestingFile = string.Empty;
+        try {
+            includePath = DecodeUtf8(requestedSource);
+            requestingFile = DecodeUtf8(requestingSource);
+            if (IsDisposed) throw new ObjectDisposedException(nameof(Ps3ShaderIncludeSession));
+            ShaderIncludeResult resolved = IncludeResolver.Resolve(requestingFile, includePath);
+            RecordDependency(resolved);
+            return AllocateResult(resolved.Path, resolved.Source, false);
+        } catch (Exception error) {
+            string diagnostic = $"Unable to resolve include '{includePath}' requested by '{requestingFile}': {error.Message}";
+            if (!(type == (int)IncludeType.Relative && error is FileNotFoundException)) {
+                RecordFatalFailure(new InvalidOperationException(diagnostic, error));
+            }
+            try {
+                return AllocateResult(includePath, diagnostic, true);
+            } catch {
+                return null;
+            }
+        }
+    }
+
+    /// <summary>Releases one callback result exactly once; unknown or repeated pointers are ignored.</summary>
+    /// <param name="userData">Opaque shaderc callback data, unused because the delegate captures this session.</param>
+    /// <param name="includeResult">Native result pointer previously returned by this session.</param>
+    unsafe void ReleaseInclude(void* userData, IncludeResult* includeResult) {
+        if (includeResult == null) return;
+        Ps3ShaderIncludeAllocation allocation;
+        lock (Gate) {
+            if (!Allocations.Remove((IntPtr)includeResult, out allocation)) return;
+        }
+        allocation.Free();
+    }
+
+    /// <summary>Records the active include and rejects a resolver that changes content for one path mid-compile.</summary>
+    /// <param name="resolved">Resolved path and source returned by the compiler include resolver.</param>
+    void RecordDependency(ShaderIncludeResult resolved) {
+        string contentHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(resolved.Source)));
+        lock (Gate) {
+            if (ResolvedContents.TryGetValue(resolved.Path, out string priorContent) && !string.Equals(priorContent, resolved.Source, StringComparison.Ordinal)) {
+                throw new InvalidOperationException($"Include resolver returned different contents for '{resolved.Path}' in one compilation.");
+            }
+            ResolvedContents[resolved.Path] = resolved.Source;
+            DependenciesList.Add(new Ps3ShaderSourceDependency(resolved.Path, contentHash));
+        }
+    }
+
+    /// <summary>Retains the first fatal callback failure so later shaderc retries cannot replace its diagnostic.</summary>
+    /// <param name="failure">Fatal resolver or repeated-path consistency failure.</param>
+    void RecordFatalFailure(Exception failure) {
+        lock (Gate) FatalFailureValue ??= failure;
+    }
+
+    /// <summary>Creates a null-terminated UTF-8 result and tracks every native allocation until release.</summary>
+    /// <param name="sourceName">Resolved include path, or the failed authored path.</param>
+    /// <param name="content">Resolved include contents or error diagnostic.</param>
+    /// <param name="isError">Whether shaderc should treat the result as a failed include.</param>
+    /// <returns>The address of the allocated shaderc include result.</returns>
+    unsafe IncludeResult* AllocateResult(string sourceName, string content, bool isError) {
+        byte[] sourceNameBytes = Encoding.UTF8.GetBytes(sourceName);
+        byte[] contentBytes = Encoding.UTF8.GetBytes(content);
+        Ps3ShaderIncludeAllocation allocation = new();
+        try {
+            allocation.SourceName = AllocateUtf8(sourceNameBytes);
+            allocation.Content = AllocateUtf8(contentBytes);
+            allocation.Result = Marshal.AllocHGlobal(sizeof(IncludeResult));
+            IncludeResult result = new() {
+                SourceName = (byte*)allocation.SourceName,
+                SourceNameLength = isError ? 0u : (nuint)sourceNameBytes.Length,
+                Content = (byte*)allocation.Content,
+                ContentLength = (nuint)contentBytes.Length,
+                UserData = null
+            };
+            Marshal.StructureToPtr(result, allocation.Result, false);
+            lock (Gate) {
+                if (IsDisposed) throw new ObjectDisposedException(nameof(Ps3ShaderIncludeSession));
+                Allocations.Add(allocation.Result, allocation);
+            }
+            return (IncludeResult*)allocation.Result;
+        } catch {
+            allocation.Free();
+            throw;
+        }
+    }
+
+    /// <summary>Allocates UTF-8 bytes plus the trailing NUL required by shaderc native string consumers.</summary>
+    /// <param name="utf8Bytes">Encoded string bytes excluding the terminator.</param>
+    /// <returns>Unmanaged storage for the bytes and trailing NUL.</returns>
+    static IntPtr AllocateUtf8(byte[] utf8Bytes) {
+        IntPtr buffer = Marshal.AllocHGlobal(checked(utf8Bytes.Length + 1));
+        try {
+            Marshal.Copy(utf8Bytes, 0, buffer, utf8Bytes.Length);
+            Marshal.WriteByte(buffer, utf8Bytes.Length, 0);
+            return buffer;
+        } catch {
+            Marshal.FreeHGlobal(buffer);
+            throw;
+        }
+    }
+
+    /// <summary>Decodes a shaderc UTF-8, NUL-terminated callback string.</summary>
+    /// <param name="value">Native string pointer.</param>
+    /// <returns>The managed string, or an empty string for a null pointer.</returns>
+    static unsafe string DecodeUtf8(byte* value) => value == null ? string.Empty : Marshal.PtrToStringUTF8((IntPtr)value) ?? string.Empty;
+
+}
warning: in the working copy of 'builder/shaders/Ps3ShaderIncludeAllocation.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder/shaders/Ps3ShaderIncludeAllocation.cs b/builder/shaders/Ps3ShaderIncludeAllocation.cs
new file mode 100644
index 0000000..7d04003
--- /dev/null
+++ b/builder/shaders/Ps3ShaderIncludeAllocation.cs
@@ -0,0 +1,25 @@
+using System.Runtime.InteropServices;
+
+namespace helengine.ps3.builder;
+
+/// <summary>Owns the unmanaged result structure and UTF-8 buffers for one shaderc include response.</summary>
+internal sealed class Ps3ShaderIncludeAllocation {
+    /// <summary>Native include result structure address.</summary>
+    internal IntPtr Result;
+
+    /// <summary>Null-terminated UTF-8 resolved source name address.</summary>
+    internal IntPtr SourceName;
+
+    /// <summary>Null-terminated UTF-8 include body or diagnostic address.</summary>
+    internal IntPtr Content;
+
+    /// <summary>Frees each owned native block once and clears its address.</summary>
+    internal void Free() {
+        if (Content != IntPtr.Zero) Marshal.FreeHGlobal(Content);
+        if (SourceName != IntPtr.Zero) Marshal.FreeHGlobal(SourceName);
+        if (Result != IntPtr.Zero) Marshal.FreeHGlobal(Result);
+        Content = IntPtr.Zero;
+        SourceName = IntPtr.Zero;
+        Result = IntPtr.Zero;
+    }
+}
warning: in the working copy of 'builder/shaders/Ps3ShaderSourceDependency.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder/shaders/Ps3ShaderSourceDependency.cs b/builder/shaders/Ps3ShaderSourceDependency.cs
new file mode 100644
index 0000000..273f4df
--- /dev/null
+++ b/builder/shaders/Ps3ShaderSourceDependency.cs
@@ -0,0 +1,20 @@
+namespace helengine.ps3.builder;
+
+/// <summary>Identifies one active HLSL include by its resolved path and content digest.</summary>
+public sealed class Ps3ShaderSourceDependency {
+    /// <summary>Initializes a dependency record with its resolved path and stable content hash.</summary>
+    /// <param name="path">Resolver-provided path used as the include source name.</param>
+    /// <param name="contentHash">Uppercase hexadecimal SHA-256 digest of UTF-8 source bytes.</param>
+    public Ps3ShaderSourceDependency(string path, string contentHash) {
+        if (string.IsNullOrWhiteSpace(path)) throw new ArgumentException("Dependency path must be provided.", nameof(path));
+        if (string.IsNullOrWhiteSpace(contentHash)) throw new ArgumentException("Dependency content hash must be provided.", nameof(contentHash));
+        Path = path;
+        ContentHash = contentHash;
+    }
+
+    /// <summary>Gets the path returned by the include resolver.</summary>
+    public string Path { get; }
+
+    /// <summary>Gets the uppercase hexadecimal SHA-256 hash of the resolved UTF-8 content.</summary>
+    public string ContentHash { get; }
+}
warning: in the working copy of 'builder.tests/Ps3ShaderSourceCompilerTests.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder.tests/Ps3ShaderSourceCompilerTests.cs b/builder.tests/Ps3ShaderSourceCompilerTests.cs
new file mode 100644
index 0000000..ac56b49
--- /dev/null
+++ b/builder.tests/Ps3ShaderSourceCompilerTests.cs
@@ -0,0 +1,206 @@
+using helengine;
+using helengine.ps3.builder;
+
+namespace helengine.ps3.builder.tests;
+
+/// <summary>Exercises real HLSL translation with uniforms, texture sampling and compiler failures.</summary>
+public sealed class Ps3ShaderSourceCompilerTests {
+    /// <summary>HLSL with modern resource syntax that cannot be passed straight to legacy Cg.</summary>
+    const string Source = """
+        cbuffer Transform : register(b0) { float4x4 WorldViewProjection; float4 Tint; };
+        Texture2D Diffuse : register(t0);
+        SamplerState SurfaceSampler : register(s0);
+        struct Input { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
+        struct Varying { float4 Position : SV_Position; float2 Uv : TEXCOORD0; };
+        Varying VS(Input input) { Varying o; o.Position = mul(WorldViewProjection, float4(input.Position, 1)); o.Uv = input.Uv; return o; }
+        float4 PS(Varying input) : SV_Target { return Diffuse.Sample(SurfaceSampler, input.Uv) * Tint; }
+        """;
+
+    /// <summary>Both stages are translated by native shaderc and SPIRV-Cross.</summary>
+    /// <param name="stage">Stage to translate.</param>
+    /// <param name="entry">Authored entry point.</param>
+    [Theory]
+    [InlineData(ShaderStage.Vertex, "VS")]
+    [InlineData(ShaderStage.Pixel, "PS")]
+    public void ConvertsAuthoredHlsl(ShaderStage stage, string entry) {
+        string glsl = new Ps3ShaderSourceCompiler().Compile(CreateRequest(Source, stage, entry), new Ps3ShaderIncludeResolverStub());
+        Assert.Contains("#version 120", glsl);
+        Assert.Contains("void main()", glsl);
+        Assert.DoesNotContain("cbuffer", glsl);
+        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
+        if (!string.IsNullOrWhiteSpace(outputRoot)) {
+            Directory.CreateDirectory(outputRoot);
+            File.WriteAllText(Path.Combine(outputRoot, entry + ".glsl"), glsl);
+        }
+    }
+
+    /// <summary>Malformed source must produce a compiler diagnostic.</summary>
+    [Fact]
+    public void RejectsInvalidHlsl() {
+        Assert.Throws<InvalidOperationException>(() => new Ps3ShaderSourceCompiler().Compile(CreateRequest("this is invalid", ShaderStage.Vertex, "VS"), new Ps3ShaderIncludeResolverStub()));
+    }
+
+    /// <summary>Rejects requests addressed to a backend other than PS3 before entering native compilation.</summary>
+    [Fact]
+    public void RejectsNonPs3Target() {
+        ShaderCompileRequest request = CreateRequest("float4 VS() : SV_Position { return 0; }", ShaderStage.Vertex, "VS", target: ShaderCompileTarget.Vulkan);
+
+        Assert.Throws<NotSupportedException>(() => new Ps3ShaderSourceCompiler().Compile(request, new Ps3ShaderIncludeResolverStub()));
+    }
+
+    /// <summary>Rejects shader stages that RSX's vertex and pixel compiler does not support.</summary>
+    [Fact]
+    public void RejectsUnsupportedStage() {
+        ShaderCompileRequest request = CreateRequest("void CS() {}", ShaderStage.Compute, "CS");
+
+        Assert.Throws<NotSupportedException>(() => new Ps3ShaderSourceCompiler().Compile(request, new Ps3ShaderIncludeResolverStub()));
+    }
+
+    /// <summary>An active include resolves through the supplied resolver and reaches generated GLSL.</summary>
+    [Fact]
+    public void ResolvesActiveInclude() {
+        Ps3ShaderIncludeResolverStub resolver = new() {
+            Files = new Dictionary<string, ShaderIncludeResult> {
+                ["color.hlsl"] = new("color.hlsl", "static const float4 IncludedColor = float4(1, 0, 0, 1);")
+            }
+        };
+        string source = "#include \"color.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";
+
+        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);
+
+        Assert.Contains("void main()", output);
+        Assert.Contains(resolver.Requests, request => request.IncludePath == "color.hlsl");
+    }
+
+    /// <summary>Nested includes pass the parent include's resolved path back to the resolver.</summary>
+    [Fact]
+    public void ResolvesNestedRelativeInclude() {
+        Ps3ShaderIncludeResolverStub resolver = new() {
+            Files = new Dictionary<string, ShaderIncludeResult> {
+                ["folder/outer.hlsl"] = new("folder/outer.hlsl", "#include \"inner.hlsl\""),
+                ["inner.hlsl"] = new("folder/inner.hlsl", "static const float4 IncludedColor = float4(1, 0, 0, 1);")
+            }
+        };
+        string source = "#include \"folder/outer.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";
+
+        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);
+
+        Assert.Contains("void main()", output);
+        Assert.Contains(resolver.Requests, request => request.RequestingFile == "folder/outer.hlsl" && request.IncludePath == "inner.hlsl");
+    }
+
+    /// <summary>Conditional preprocessing prevents inactive includes from invoking the resolver.</summary>
+    [Fact]
+    public void DoesNotResolveInactiveInclude() {
+        Ps3ShaderIncludeResolverStub resolver = new();
+        string source = "#if 0\n#include \"inactive.hlsl\"\n#endif\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";
+
+        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);
+
+        Assert.Contains("void main()", output);
+        Assert.DoesNotContain(resolver.Requests, request => request.IncludePath == "inactive.hlsl");
+    }
+
+    /// <summary>A missing active include returns a compiler diagnostic containing its original path.</summary>
+    [Fact]
+    public void ReportsMissingActiveInclude() {
+        Ps3ShaderIncludeResolverStub resolver = new();
+        string source = "#include \"missing.hlsl\"\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";
+
+        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
+            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));
+
+        Assert.Contains("missing.hlsl", error.Message);
+    }
+
+    /// <summary>A resolver exception is converted into a compiler diagnostic without crossing native code.</summary>
+    [Fact]
+    public void ReportsResolverFailure() {
+        Ps3ShaderIncludeResolverStub resolver = new() { Failure = new IOException("resolver failed") };
+        string source = "#include \"broken.hlsl\"\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";
+
+        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
+            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));
+
+        Assert.Contains("broken.hlsl", error.Message);
+        Assert.Contains("resolver failed", error.Message);
+    }
+
+    /// <summary>UTF-8 include contents are passed with their encoded byte count.</summary>
+    [Fact]
+    public void ResolvesUtf8IncludeContent() {
+        Ps3ShaderIncludeResolverStub resolver = new() {
+            Files = new Dictionary<string, ShaderIncludeResult> {
+                ["color.hlsl"] = new("color.hlsl", "// café \U0001F3A8\nstatic const float4 IncludedColor = float4(1, 0, 0, 1);")
+            }
+        };
+        string source = "#include \"color.hlsl\"\nfloat4 PS() : SV_Target { return IncludedColor; }";
+
+        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);
+
+        Assert.Contains("void main()", output);
+        Assert.Contains(resolver.Requests, request => request.IncludePath == "color.hlsl");
+    }
+
+    /// <summary>Request defines select the active include branch before shaderc resolves includes.</summary>
+    [Fact]
+    public void ResolvesDefineSelectedInclude() {
+        Ps3ShaderIncludeResolverStub resolver = new() {
+            Files = new Dictionary<string, ShaderIncludeResult> {
+                ["selected.hlsl"] = new("selected.hlsl", "static const float4 IncludedColor = float4(1, 0, 0, 1);")
+            }
+        };
+        string source = "#if USE_COLOR\n#include \"selected.hlsl\"\n#else\n#include \"inactive.hlsl\"\n#endif\nfloat4 PS() : SV_Target { return IncludedColor; }";
+
+        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS", defines: [new ShaderDefine("USE_COLOR", "1")]), resolver);
+
+        Assert.Contains("void main()", output);
+        Assert.Contains(resolver.Requests, request => request.IncludePath == "selected.hlsl");
+        Assert.DoesNotContain(resolver.Requests, request => request.IncludePath == "inactive.hlsl");
+    }
+
+    /// <summary>Rejects a resolver that returns changed content for a resolved path during one compilation.</summary>
+    [Fact]
+    public void RejectsChangedContentForRepeatedResolvedPath() {
+        Ps3ShaderIncludeResolverStub resolver = new();
+        resolver.Results.Enqueue(new ShaderIncludeResult("colors.hlsl", "// first content version"));
+        resolver.Results.Enqueue(new ShaderIncludeResult("colors.hlsl", "// changed content version"));
+        string source = "#include \"colors.hlsl\"\n#include \"colors.hlsl\"\nfloat4 PS() : SV_Target { return float4(1, 0, 0, 1); }";
+
+        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() =>
+            new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver));
+
+        Assert.True(resolver.Requests.Count >= 2);
+        Assert.Contains("colors.hlsl", error.Message);
+        Assert.True(error.Message.Contains("different contents", StringComparison.Ordinal), error.Message);
+    }
+
+    /// <summary>Resolves repeated active includes when their resolved path content is unchanged.</summary>
+    [Fact]
+    public void AllowsRepeatedIdenticalIncludeContent() {
+        Ps3ShaderIncludeResolverStub resolver = new() {
+            Files = new Dictionary<string, ShaderIncludeResult> {
+                ["colors.hlsl"] = new("colors.hlsl", "#define INCLUDED_COLOR float4(1, 0, 0, 1)")
+            }
+        };
+        string source = "#include \"colors.hlsl\"\n#include \"colors.hlsl\"\nfloat4 PS() : SV_Target { return INCLUDED_COLOR; }";
+
+        string output = new Ps3ShaderSourceCompiler().Compile(CreateRequest(source, ShaderStage.Pixel, "PS"), resolver);
+
+        Assert.Contains("void main()", output);
+        Assert.Equal(2, resolver.Requests.Count(request => request.IncludePath == "colors.hlsl"));
+    }
+
+    /// <summary>Creates a fully specified PS3 request for the test source.</summary>
+    /// <param name="source">Authored HLSL text.</param>
+    /// <param name="stage">Pipeline stage.</param>
+    /// <param name="entry">Entry point name.</param>
+    /// <param name="sourcePath">Optional source path used for include callback context.</param>
+    /// <param name="defines">Optional request preprocessor definitions.</param>
+    /// <param name="target">Compilation target to place in the request.</param>
+    /// <returns>Shared engine compilation request.</returns>
+    static ShaderCompileRequest CreateRequest(string source, ShaderStage stage, string entry, string sourcePath = "probe.hlsl", IReadOnlyList<ShaderDefine> defines = null, ShaderCompileTarget target = ShaderCompileTarget.Ps3) {
+        return new(new ShaderSourceInfo(sourcePath, source), "Probe", entry, stage, target,
+            new ShaderModel(3, 0), "default", defines ?? [], new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));
+    }
+}
warning: in the working copy of 'builder.tests/Ps3ShaderIncludeResolverStub.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder.tests/Ps3ShaderIncludeResolverStub.cs b/builder.tests/Ps3ShaderIncludeResolverStub.cs
new file mode 100644
index 0000000..3ba6ed7
--- /dev/null
+++ b/builder.tests/Ps3ShaderIncludeResolverStub.cs
@@ -0,0 +1,32 @@
+using helengine;
+
+namespace helengine.ps3.builder.tests;
+
+/// <summary>Records shader include requests and returns test controlled include source.</summary>
+public sealed class Ps3ShaderIncludeResolverStub : IShaderIncludeResolver {
+    /// <summary>Include results indexed by the path written in each include directive.</summary>
+    public Dictionary<string, ShaderIncludeResult> Files { get; init; } = new(StringComparer.Ordinal);
+
+    /// <summary>Requests received from shaderc in their original callback order.</summary>
+    public List<Ps3ShaderIncludeRequest> Requests { get; } = [];
+
+    /// <summary>Optional ordered results used to simulate content changing between repeated paths.</summary>
+    public Queue<ShaderIncludeResult> Results { get; } = [];
+
+    /// <summary>Optional failure raised for every include request.</summary>
+    public Exception Failure { get; init; }
+
+    /// <summary>Records a request and returns its configured include content.</summary>
+    /// <param name="requestingFile">Resolved source path issuing the include.</param>
+    /// <param name="includePath">Path written in the include directive.</param>
+    /// <returns>The configured include result.</returns>
+    public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
+        Requests.Add(new Ps3ShaderIncludeRequest(requestingFile, includePath));
+        if (Failure != null) throw Failure;
+        if (Results.Count > 0) return Results.Dequeue();
+        if (!Files.TryGetValue(includePath, out ShaderIncludeResult result)) {
+            throw new FileNotFoundException($"Include '{includePath}' was not found.", includePath);
+        }
+        return result;
+    }
+}
warning: in the working copy of 'builder.tests/Ps3ShaderIncludeRequest.cs', LF will be replaced by CRLF the next time Git touches it
diff --git a/builder.tests/Ps3ShaderIncludeRequest.cs b/builder.tests/Ps3ShaderIncludeRequest.cs
new file mode 100644
index 0000000..ffc92b4
--- /dev/null
+++ b/builder.tests/Ps3ShaderIncludeRequest.cs
@@ -0,0 +1,18 @@
+namespace helengine.ps3.builder.tests;
+
+/// <summary>Captures the caller and authored path of one include resolver invocation.</summary>
+public sealed class Ps3ShaderIncludeRequest {
+    /// <summary>Initializes an immutable include request record.</summary>
+    /// <param name="requestingFile">Resolved source path issuing the include.</param>
+    /// <param name="includePath">Path written in the include directive.</param>
+    public Ps3ShaderIncludeRequest(string requestingFile, string includePath) {
+        RequestingFile = requestingFile;
+        IncludePath = includePath;
+    }
+
+    /// <summary>Gets the resolved path of the source that requested the include.</summary>
+    public string RequestingFile { get; }
+
+    /// <summary>Gets the include path as written in the shader source.</summary>
+    public string IncludePath { get; }
+}

```
