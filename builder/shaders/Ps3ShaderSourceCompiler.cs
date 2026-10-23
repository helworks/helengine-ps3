using Silk.NET.Shaderc;
using Silk.NET.SPIRV.Cross;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

[assembly: InternalsVisibleTo("helengine.ps3.builder.tests")]

namespace helengine.ps3.builder;

/// <summary>Translates authored HLSL through SPIR-V into legacy GLSL accepted by the Cg GLSL frontend.</summary>
public sealed class Ps3ShaderSourceCompiler {
    /// <summary>Allocator used only for shaderc include callback structures and UTF-8 buffers.</summary>
    readonly Func<int, IntPtr> IncludeMemoryAllocator;

    /// <summary>Initializes the compiler with the platform unmanaged allocator.</summary>
    public Ps3ShaderSourceCompiler() : this(Marshal.AllocHGlobal) { }

    /// <summary>Initializes the compiler with an injectable include-memory allocator for deterministic boundary tests.</summary>
    /// <param name="includeMemoryAllocator">Allocator used by one compile's shaderc include session.</param>
    internal Ps3ShaderSourceCompiler(Func<int, IntPtr> includeMemoryAllocator) {
        IncludeMemoryAllocator = includeMemoryAllocator ?? throw new ArgumentNullException(nameof(includeMemoryAllocator));
    }

    /// <summary>Compiles one PS3 vertex or pixel entry point and returns its generated GLSL source.</summary>
    /// <param name="request">Source, entry point, stage and preprocessor definitions.</param>
    /// <param name="includeResolver">Resolver used for active includes selected by shaderc preprocessing.</param>
    /// <returns>GLSL 1.20 source for the RSX compiler's main entry point.</returns>
    public string Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        return Translate(request, includeResolver).GlslSource;
    }

    /// <summary>Compiles a shader and captures active interface reflection before GLSL emission.</summary>
    /// <param name="request">Source, entry point, stage and preprocessor definitions.</param>
    /// <param name="includeResolver">Resolver used for active includes selected by shaderc preprocessing.</param>
    /// <returns>Generated GLSL with copied reflection and include dependency records.</returns>
    public Ps3ShaderTranslation Translate(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(includeResolver);
        if (request.Target != ShaderCompileTarget.Ps3) {
            throw new NotSupportedException("The PS3 HLSL compiler accepts PS3 compilation requests only.");
        }
        if (request.Stage != ShaderStage.Vertex && request.Stage != ShaderStage.Pixel) {
            throw new NotSupportedException("PS3 supports vertex and pixel shader stages only.");
        }
        byte[] spirv = CompileSpirv(request, includeResolver, out IReadOnlyList<Ps3ShaderSourceDependency> dependencies);
        return EmitGlsl(spirv, request, dependencies);
    }

    /// <summary>Runs the same HLSL frontend used by the other engine backends, preserving reflection names.</summary>
    /// <param name="request">Authored compilation request.</param>
    /// <returns>Validated SPIR-V words.</returns>
    unsafe byte[] CompileSpirv(ShaderCompileRequest request, IShaderIncludeResolver includeResolver, out IReadOnlyList<Ps3ShaderSourceDependency> dependencies) {
        using Ps3ShaderIncludeSession includeSession = new(includeResolver, IncludeMemoryAllocator);
        using Shaderc api = Shaderc.GetApi();
        Silk.NET.Shaderc.Compiler* compiler = api.CompilerInitialize();
        CompileOptions* options = api.CompileOptionsInitialize();
        if (compiler == null || options == null) {
            if (compiler != null) api.CompilerRelease(compiler);
            if (options != null) api.CompileOptionsRelease(options);
            throw new InvalidOperationException("Unable to initialize the PS3 HLSL frontend.");
        }
        byte[] spirvBytes = null;
        string nativeDiagnostic = null;
        bool resultWasNull = false;
        try {
            api.CompileOptionsSetSourceLanguage(options, SourceLanguage.Hlsl);
            api.CompileOptionsSetTargetEnv(options, TargetEnv.Vulkan, (uint)EnvVersion.Vulkan10);
            api.CompileOptionsSetHlslIoMapping(options, true);
            api.CompileOptionsSetHlslFunctionality1(options, true);
            api.CompileOptionsSetAutoMapLocations(options, true);
            api.CompileOptionsSetAutoBindUniforms(options, true);
            api.CompileOptionsSetPreserveBindings(options, true);
            api.CompileOptionsSetOptimizationLevel(options, request.Options.Optimize ? OptimizationLevel.Performance : OptimizationLevel.Zero);
            // Reflection identifiers must survive optimization even when the caller did not request debug output.
            api.CompileOptionsSetGenerateDebugInfo(options);
            if (request.Options.TreatWarningsAsErrors) api.CompileOptionsSetWarningsAsErrors(options);
            api.CompileOptionsSetIncludeCallbacks(options, includeSession.ResolveCallback, includeSession.ReleaseCallback, null);
            foreach (ShaderDefine define in request.Defines) {
                string value = define.Value ?? string.Empty;
                api.CompileOptionsAddMacroDefinition(options, define.Name, (nuint)Encoding.UTF8.GetByteCount(define.Name), value, (nuint)Encoding.UTF8.GetByteCount(value));
            }
            string source = request.Source.Source;
            CompilationResult* result = api.CompileIntoSpv(compiler, source, (nuint)Encoding.UTF8.GetByteCount(source),
                request.Stage == ShaderStage.Vertex ? ShaderKind.VertexShader : ShaderKind.FragmentShader,
                request.Source.Path, request.EntryPoint, options);
            if (result == null) {
                resultWasNull = true;
            } else {
                try {
                    if (api.ResultGetCompilationStatus(result) != CompilationStatus.Success) {
                        nativeDiagnostic = api.ResultGetErrorMessageS(result);
                    } else {
                        spirvBytes = new byte[checked((int)api.ResultGetLength(result))];
                        if (spirvBytes.Length > 0) Marshal.Copy((IntPtr)api.ResultGetBytes(result), spirvBytes, 0, spirvBytes.Length);
                    }
                } finally {
                    api.ResultRelease(result);
                }
            }
        } finally {
            api.CompileOptionsRelease(options);
            api.CompilerRelease(compiler);
        }

        string fatalIncludeFailure = includeSession.FatalFailureMessage;
        if (fatalIncludeFailure != null) {
            throw new InvalidOperationException("PS3 HLSL include processing failed: " + fatalIncludeFailure);
        }
        if (resultWasNull) throw new InvalidOperationException("HLSL compiler returned no result.");
        if (nativeDiagnostic != null) throw new InvalidOperationException("PS3 HLSL compilation failed: " + nativeDiagnostic);
        if (spirvBytes == null || spirvBytes.Length == 0) throw new InvalidOperationException("HLSL compilation produced empty SPIR-V.");
        dependencies = includeSession.Dependencies;
        return spirvBytes;
    }

    /// <summary>Finds the packaged SPIRV-Cross library beside the compiler assembly.</summary>
    /// <param name="compilerAssemblyLocation">Fully qualified file location reported by the compiler assembly.</param>
    /// <returns>The absolute path to that package's native library for the current operating system and process architecture.</returns>
    /// <exception cref="ArgumentException">The assembly location is missing, not fully qualified, or lacks a file component.</exception>
    internal static string ResolveSpirvCrossLibraryPath(string compilerAssemblyLocation) {
        string host = OperatingSystem.IsWindows() ? "win" : OperatingSystem.IsMacOS() ? "osx" : OperatingSystem.IsLinux() ? "linux"
            : throw new PlatformNotSupportedException("The PS3 shader compiler does not package SPIRV-Cross for this host OS.");
        return ResolveSpirvCrossLibraryPath(compilerAssemblyLocation, host, RuntimeInformation.ProcessArchitecture);
    }

    /// <summary>Maps one supported host and process architecture to the unchanged SPIRV-Cross package layout.</summary>
    /// <param name="compilerAssemblyLocation">Fully qualified compiler assembly file location.</param>
    /// <param name="host">NuGet operating-system prefix: win, osx or linux.</param>
    /// <param name="architecture">Architecture of the managed compiler process.</param>
    /// <returns>The native library path beside the compiler assembly, preserving its directory.</returns>
    internal static string ResolveSpirvCrossLibraryPath(string compilerAssemblyLocation, string host, Architecture architecture) {
        if (string.IsNullOrWhiteSpace(compilerAssemblyLocation) || !Path.IsPathFullyQualified(compilerAssemblyLocation)) {
            throw new ArgumentException("The PS3 compiler assembly must have a fully qualified file location.", nameof(compilerAssemblyLocation));
        }

        string assemblyPath = Path.GetFullPath(compilerAssemblyLocation);
        string assemblyFileName = Path.GetFileName(assemblyPath);
        string assemblyDirectory = Path.GetDirectoryName(assemblyPath);
        if (string.IsNullOrEmpty(assemblyFileName) || string.IsNullOrEmpty(assemblyDirectory)) {
            throw new ArgumentException("The PS3 compiler assembly location must include both a directory and a file name.", nameof(compilerAssemblyLocation));
        }

        string fileName = host switch {
            "win" => "spirv-cross.dll",
            "osx" => "libspirv-cross.dylib",
            "linux" => "libspirv-cross.so",
            _ => throw new PlatformNotSupportedException("The PS3 shader compiler does not package SPIRV-Cross for this host OS.")
        };
        string runtimeIdentifier = host + "-" + architecture.ToString().ToLowerInvariant();
        return Path.Combine(assemblyDirectory, "runtimes", runtimeIdentifier, "native", fileName);
    }

    /// <summary>Reflects compiler metadata, assigns stable names, and emits legacy GLSL without source rewriting.</summary>
    /// <param name="spirv">Frontend output.</param>
    /// <param name="request">Entry point and execution stage.</param>
    /// <param name="dependencies">Active dependencies copied from the include session before disposal.</param>
    /// <returns>Generated source and immutable metadata translation.</returns>
    unsafe Ps3ShaderTranslation EmitGlsl(byte[] spirv, ShaderCompileRequest request, IReadOnlyList<Ps3ShaderSourceDependency> dependencies) {
        string library = ResolveSpirvCrossLibraryPath(typeof(Ps3ShaderSourceCompiler).Assembly.Location);
        if (!File.Exists(library)) throw new FileNotFoundException("Packaged SPIRV-Cross library is missing.", library);
        using Cross api = new(Cross.CreateDefaultContext([library]));
        Context* context = null;
        Check(api, context, api.ContextCreate(&context));
        try {
            ParsedIr* parsed = null;
            Silk.NET.SPIRV.Cross.Compiler* compiler = null;
            fixed (byte* data = spirv) {
                Check(api, context, api.ContextParseSpirv(context, (uint*)data, (nuint)(spirv.Length / 4), &parsed));
            }
            Check(api, context, api.ContextCreateCompiler(context, Backend.Glsl, parsed, CaptureMode.TakeOwnership, &compiler));
            CompilerOptions* options = null;
            Check(api, context, api.CompilerCreateCompilerOptions(compiler, &options));
            Check(api, context, api.CompilerOptionsSetUint(options, CompilerOption.GlslVersion, 120));
            Check(api, context, api.CompilerOptionsSetBool(options, CompilerOption.GlslES, 0));
            Check(api, context, api.CompilerOptionsSetBool(options, CompilerOption.GlslEmitUniformBufferAsPlainUniforms, 1));
            Check(api, context, api.CompilerInstallCompilerOptions(compiler, options));
            Check(api, context, api.CompilerSetEntryPoint(compiler, request.EntryPoint,
                request.Stage == ShaderStage.Vertex ? Silk.NET.SPIRV.ExecutionModel.Vertex : Silk.NET.SPIRV.ExecutionModel.Fragment));
            Ps3ShaderReflection reflection = new(api, context, compiler);
            reflection.ReflectInterfaces();
            Check(api, context, api.CompilerBuildCombinedImageSamplers(compiler));
            reflection.ReflectSamplers();
            byte* source = null;
            Check(api, context, api.CompilerCompile(compiler, &source));
            string glslSource = Marshal.PtrToStringUTF8((IntPtr)source) ?? throw new InvalidOperationException("SPIRV-Cross returned no GLSL source.");
            return new Ps3ShaderTranslation(request.Stage, glslSource, reflection.InputBuiltIns, reflection.OutputBuiltIns,
                reflection.Inputs, reflection.Outputs,
                reflection.UniformMembers, reflection.Samplers, dependencies, reflection.UniformBuffers);
        } finally {
            if (context != null) api.ContextDestroy(context);
        }
    }

    /// <summary>Propagates compiler diagnostics instead of continuing with incomplete intermediate output.</summary>
    /// <param name="api">Cross compiler API.</param>
    /// <param name="context">Compilation context when available.</param>
    /// <param name="result">Native operation result.</param>
    static unsafe void Check(Cross api, Context* context, Result result) {
        if (result != Result.Success) {
            throw new InvalidOperationException("PS3 shader conversion failed: " + (context == null ? result.ToString() : api.ContextGetLastErrorStringS(context)));
        }
    }
}
