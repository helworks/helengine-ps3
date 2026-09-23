# Task 2 full uncommitted source snapshot

Base HEAD: ef27238ed85fe3218618a752c11d9eeede0a75a2. No commits. New files are untracked, so this package contains their complete contents. SourceCompiler includes accepted Task 1 code as context; Task 2 adds Translate, semantics/debug preservation and reflection emission.

### builder/shaders/Ps3ShaderSourceCompiler.cs
```csharp
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

    /// <summary>Reflects compiler metadata, assigns stable names, and emits legacy GLSL without source rewriting.</summary>
    /// <param name="spirv">Frontend output.</param>
    /// <param name="request">Entry point and execution stage.</param>
    /// <param name="dependencies">Active dependencies copied from the include session before disposal.</param>
    /// <returns>Generated source and immutable metadata translation.</returns>
    unsafe Ps3ShaderTranslation EmitGlsl(byte[] spirv, ShaderCompileRequest request, IReadOnlyList<Ps3ShaderSourceDependency> dependencies) {
        string library = Path.Combine(AppContext.BaseDirectory, "runtimes", "win-x64", "native", "spirv-cross.dll");
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
                reflection.UniformMembers, reflection.Samplers, dependencies);
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
```
### builder/shaders/Ps3ShaderTranslation.cs
```csharp
using helengine;

namespace helengine.ps3.builder;

/// <summary>Contains PS3 GLSL output together with immutable reflection and active include dependencies.</summary>
public sealed class Ps3ShaderTranslation {
    /// <summary>Initializes a translation and copies all supplied collections into read-only snapshots.</summary>
    /// <param name="stage">Shader stage used for this translation.</param>
    /// <param name="glslSource">Compiler-generated GLSL source.</param>
    /// <param name="inputs">Active authored stage inputs.</param>
    /// <param name="outputs">Active authored stage outputs.</param>
    /// <param name="uniformMembers">Active constant-buffer members.</param>
    /// <param name="samplers">Active combined texture and sampler bindings.</param>
    /// <param name="dependencies">Active include dependencies resolved for this translation.</param>
    public Ps3ShaderTranslation(
        ShaderStage stage,
        string glslSource,
        IEnumerable<string> inputBuiltIns,
        IEnumerable<string> outputBuiltIns,
        IEnumerable<Ps3ShaderInterfaceVariable> inputs,
        IEnumerable<Ps3ShaderInterfaceVariable> outputs,
        IEnumerable<Ps3ShaderUniformMember> uniformMembers,
        IEnumerable<Ps3ShaderSamplerBinding> samplers,
        IEnumerable<Ps3ShaderSourceDependency> dependencies) {
        if (string.IsNullOrWhiteSpace(glslSource)) throw new ArgumentException("Generated GLSL source must be provided.", nameof(glslSource));
        Stage = stage;
        GlslSource = glslSource;
        InputBuiltIns = Copy(inputBuiltIns, nameof(inputBuiltIns));
        OutputBuiltIns = Copy(outputBuiltIns, nameof(outputBuiltIns));
        Inputs = Copy(inputs, nameof(inputs));
        Outputs = Copy(outputs, nameof(outputs));
        UniformMembers = Copy(uniformMembers, nameof(uniformMembers));
        Samplers = Copy(samplers, nameof(samplers));
        Dependencies = Copy(dependencies, nameof(dependencies));
    }

    /// <summary>Gets the compiled vertex or pixel stage.</summary>
    public ShaderStage Stage { get; }

    /// <summary>Gets the SPIRV-Cross-generated GLSL source without post-emission rewriting.</summary>
    public string GlslSource { get; }

    /// <summary>Gets the copied official SPIR-V built-in names consumed by this stage.</summary>
    public IReadOnlyList<string> InputBuiltIns { get; }

    /// <summary>Gets the copied official SPIR-V built-in names produced by this stage.</summary>
    public IReadOnlyList<string> OutputBuiltIns { get; }

    /// <summary>Gets the copied active input interface records.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Inputs { get; }

    /// <summary>Gets the copied active output interface records.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Outputs { get; }

    /// <summary>Gets the copied active uniform-member records.</summary>
    public IReadOnlyList<Ps3ShaderUniformMember> UniformMembers { get; }

    /// <summary>Gets the copied active combined sampler records.</summary>
    public IReadOnlyList<Ps3ShaderSamplerBinding> Samplers { get; }

    /// <summary>Gets the copied active include dependency records.</summary>
    public IReadOnlyList<Ps3ShaderSourceDependency> Dependencies { get; }

    /// <summary>Creates a read-only snapshot of one required collection.</summary>
    /// <typeparam name="T">Collection element type.</typeparam>
    /// <param name="items">Collection to snapshot.</param>
    /// <param name="parameterName">Argument name used in null diagnostics.</param>
    /// <returns>A read-only copied collection.</returns>
    static IReadOnlyList<T> Copy<T>(IEnumerable<T> items, string parameterName) {
        ArgumentNullException.ThrowIfNull(items, parameterName);
        return Array.AsReadOnly(items.ToArray());
    }
}
```
### builder/shaders/Ps3ShaderInterfaceVariable.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Describes one active authored user interface variable reflected from SPIR-V.</summary>
public sealed class Ps3ShaderInterfaceVariable {
    /// <summary>Initializes an interface record with compiler-provided semantic and type metadata.</summary>
    /// <param name="semantic">Authored HLSL semantic from HlslSemanticGOOGLE.</param>
    /// <param name="location">SPIR-V location decoration.</param>
    /// <param name="scalarType">SPIR-V scalar base type and bit width.</param>
    /// <param name="vectorWidth">Number of scalar components per vector.</param>
    /// <param name="columns">Number of matrix columns.</param>
    /// <param name="arrayLength">Product of literal array dimensions, or one for a non-array variable.</param>
    /// <param name="interpolation">Compiler decorations describing interpolation behavior.</param>
    /// <param name="generatedName">Stable compiler-assigned GLSL identifier.</param>
    public Ps3ShaderInterfaceVariable(
        string semantic,
        uint location,
        string scalarType,
        uint vectorWidth,
        uint columns,
        uint arrayLength,
        string interpolation,
        string generatedName) {
        if (string.IsNullOrWhiteSpace(semantic)) throw new ArgumentException("Authored HLSL semantic must be provided.", nameof(semantic));
        if (string.IsNullOrWhiteSpace(scalarType)) throw new ArgumentException("Scalar type must be provided.", nameof(scalarType));
        if (string.IsNullOrWhiteSpace(interpolation)) throw new ArgumentException("Interpolation metadata must be provided.", nameof(interpolation));
        if (string.IsNullOrWhiteSpace(generatedName)) throw new ArgumentException("Generated GLSL name must be provided.", nameof(generatedName));
        Semantic = semantic;
        Location = location;
        ScalarType = scalarType;
        VectorWidth = vectorWidth;
        Columns = columns;
        ArrayLength = arrayLength;
        Interpolation = interpolation;
        GeneratedName = generatedName;
    }

    /// <summary>Gets the authored HLSL semantic decoration without ordinal-based inference.</summary>
    public string Semantic { get; }

    /// <summary>Gets the numeric SPIR-V location assigned by the compiler.</summary>
    public uint Location { get; }

    /// <summary>Gets the scalar base type and bit width reported by SPIRV-Cross.</summary>
    public string ScalarType { get; }

    /// <summary>Gets the number of scalar components in each vector.</summary>
    public uint VectorWidth { get; }

    /// <summary>Gets the number of matrix columns, or one for non-matrix types.</summary>
    public uint Columns { get; }

    /// <summary>Gets the checked product of literal array dimensions, or one for non-array types.</summary>
    public uint ArrayLength { get; }

    /// <summary>Gets the interpolation mode and applicable interpolation modifiers.</summary>
    public string Interpolation { get; }

    /// <summary>Gets the stable identifier assigned through the SPIRV-Cross compiler API.</summary>
    public string GeneratedName { get; }
}
```
### builder/shaders/Ps3ShaderUniformMember.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Describes one active uniform-buffer member and its declared byte layout.</summary>
public sealed class Ps3ShaderUniformMember {
    /// <summary>Initializes a uniform-member record with compiler-reported identity, shape and layout.</summary>
    /// <param name="bufferName">Authored constant-buffer name.</param>
    /// <param name="bufferBinding">SPIR-V binding decoration for the constant buffer.</param>
    /// <param name="memberPath">Complete member path within the buffer, including nested fields.</param>
    /// <param name="byteOffset">Member byte offset relative to the containing buffer.</param>
    /// <param name="byteSize">Declared member size in bytes.</param>
    /// <param name="arrayStride">Declared array stride in bytes, or zero when not an array.</param>
    /// <param name="matrixStride">Physical SPIR-V stride in bytes between major matrix vectors, or zero when not a matrix.</param>
    /// <param name="rowMajor">Whether SPIR-V declares row-major physical storage; this may be opposite the HLSL modifier.</param>
    /// <param name="generatedName">Stable compiler-assigned, fully qualified GLSL uniform-member path.</param>
    /// <param name="scalarType">SPIR-V scalar base type and bit width.</param>
    /// <param name="vectorWidth">Number of scalar components per vector.</param>
    /// <param name="columns">Number of matrix columns.</param>
    /// <param name="arrayLength">Product of literal array dimensions, or one for a non-array member.</param>
    public Ps3ShaderUniformMember(
        string bufferName,
        uint bufferBinding,
        string memberPath,
        uint byteOffset,
        uint byteSize,
        uint arrayStride,
        uint matrixStride,
        bool rowMajor,
        string generatedName,
        string scalarType,
        uint vectorWidth,
        uint columns,
        uint arrayLength) {
        if (string.IsNullOrWhiteSpace(bufferName)) throw new ArgumentException("Buffer name must be provided.", nameof(bufferName));
        if (string.IsNullOrWhiteSpace(memberPath)) throw new ArgumentException("Complete member path must be provided.", nameof(memberPath));
        if (string.IsNullOrWhiteSpace(generatedName)) throw new ArgumentException("Generated GLSL name must be provided.", nameof(generatedName));
        if (string.IsNullOrWhiteSpace(scalarType)) throw new ArgumentException("Scalar type must be provided.", nameof(scalarType));
        BufferName = bufferName;
        BufferBinding = bufferBinding;
        MemberPath = memberPath;
        ByteOffset = byteOffset;
        ByteSize = byteSize;
        ArrayStride = arrayStride;
        MatrixStride = matrixStride;
        RowMajor = rowMajor;
        GeneratedName = generatedName;
        ScalarType = scalarType;
        VectorWidth = vectorWidth;
        Columns = columns;
        ArrayLength = arrayLength;
    }

    /// <summary>Gets the authored constant-buffer identifier.</summary>
    public string BufferName { get; }

    /// <summary>Gets the SPIR-V resource binding assigned to the constant buffer.</summary>
    public uint BufferBinding { get; }

    /// <summary>Gets the complete nested field path within the constant buffer.</summary>
    public string MemberPath { get; }

    /// <summary>Gets the byte offset within the constant buffer.</summary>
    public uint ByteOffset { get; }

    /// <summary>Gets the declared size in bytes of this member.</summary>
    public uint ByteSize { get; }

    /// <summary>Gets the compiler-declared stride between array elements, or zero for non-arrays.</summary>
    public uint ArrayStride { get; }

    /// <summary>Gets the compiler-declared physical stride between major matrix vectors, or zero for non-matrices.</summary>
    public uint MatrixStride { get; }

    /// <summary>
    /// Gets whether SPIR-V declares row-major physical matrix storage. This is the SPIR-V decoration reported by SPIRV-Cross,
    /// not the authored HLSL modifier: glslang maps HLSL Mrc and SPIR-V Mcr matrix dimensions in opposite order and reverses
    /// the row/column-major sense when lowering HLSL layout.
    /// </summary>
    public bool RowMajor { get; }

    /// <summary>Gets the stable fully qualified GLSL member path assigned through the SPIRV-Cross compiler API.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the scalar base type and bit width reported by SPIRV-Cross.</summary>
    public string ScalarType { get; }

    /// <summary>Gets the physical SPIR-V vector width, which can differ from the authored HLSL matrix row count.</summary>
    public uint VectorWidth { get; }

    /// <summary>Gets the physical SPIR-V matrix column count, or one for non-matrix values.</summary>
    public uint Columns { get; }

    /// <summary>Gets the checked product of literal array dimensions, or one for non-array values.</summary>
    public uint ArrayLength { get; }
}
```
### builder/shaders/Ps3ShaderSamplerBinding.cs
```csharp
namespace helengine.ps3.builder;

/// <summary>Describes one active combined texture and sampler binding pair.</summary>
public sealed class Ps3ShaderSamplerBinding {
    /// <summary>Initializes a combined texture/sampler record with both source bindings and the GLSL name.</summary>
    /// <param name="textureName">Authored texture resource name.</param>
    /// <param name="textureBinding">SPIR-V texture binding.</param>
    /// <param name="samplerName">Authored sampler resource name.</param>
    /// <param name="samplerBinding">SPIR-V sampler binding.</param>
    /// <param name="generatedName">Stable generated combined-sampler identifier.</param>
    /// <param name="dimension">SPIR-V image dimension name.</param>
    public Ps3ShaderSamplerBinding(string textureName, uint textureBinding, string samplerName, uint samplerBinding, string generatedName, string dimension) {
        if (string.IsNullOrWhiteSpace(textureName)) throw new ArgumentException("Texture name must be provided.", nameof(textureName));
        if (string.IsNullOrWhiteSpace(samplerName)) throw new ArgumentException("Sampler name must be provided.", nameof(samplerName));
        if (string.IsNullOrWhiteSpace(generatedName)) throw new ArgumentException("Generated combined-sampler name must be provided.", nameof(generatedName));
        if (string.IsNullOrWhiteSpace(dimension)) throw new ArgumentException("Texture dimension must be provided.", nameof(dimension));
        TextureName = textureName;
        TextureBinding = textureBinding;
        SamplerName = samplerName;
        SamplerBinding = samplerBinding;
        GeneratedName = generatedName;
        Dimension = dimension;
    }

    /// <summary>Gets the authored texture resource name.</summary>
    public string TextureName { get; }

    /// <summary>Gets the SPIR-V texture resource binding.</summary>
    public uint TextureBinding { get; }

    /// <summary>Gets the authored sampler resource name.</summary>
    public string SamplerName { get; }

    /// <summary>Gets the SPIR-V sampler resource binding.</summary>
    public uint SamplerBinding { get; }

    /// <summary>Gets the stable name assigned to this particular texture/sampler pair.</summary>
    public string GeneratedName { get; }

    /// <summary>Gets the image dimensionality reported by SPIRV-Cross.</summary>
    public string Dimension { get; }
}
```
### builder/shaders/Ps3ShaderReflection.cs
```csharp
using helengine;
using Silk.NET.SPIRV.Cross;

namespace helengine.ps3.builder;

/// <summary>Reads user stage interfaces from SPIR-V and assigns stable GLSL names before emission.</summary>
internal sealed unsafe class Ps3ShaderReflection {
    /// <summary>SPIR-V registry value for the compiler-authored HlslSemanticGOOGLE string decoration.</summary>
    const Silk.NET.SPIRV.Decoration HlslSemanticGoogleDecoration = (Silk.NET.SPIRV.Decoration)5635;

    /// <summary>SPIRV-Cross entry points used to read metadata and assign generated identifiers.</summary>
    readonly Cross Api;

    /// <summary>Context that owns the parsed intermediate representation and reflection handles.</summary>
    readonly Context* Context;

    /// <summary>Compiler whose SPIR-V metadata and emitted names are being processed.</summary>
    readonly Compiler* Compiler;

    /// <summary>Initializes a reflection pass for one live SPIRV-Cross compiler.</summary>
    /// <param name="api">Loaded SPIRV-Cross API.</param>
    /// <param name="context">Owning native compilation context.</param>
    /// <param name="compiler">Compiler created from the parsed SPIR-V module.</param>
    public Ps3ShaderReflection(Cross api, Context* context, Compiler* compiler) {
        Api = api ?? throw new ArgumentNullException(nameof(api));
        if (context == null) throw new ArgumentNullException(nameof(context));
        if (compiler == null) throw new ArgumentNullException(nameof(compiler));
        Context = context;
        Compiler = compiler;
    }

    /// <summary>Gets active user-declared stage inputs after compiler optimization.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Inputs { get; private set; } = Array.Empty<Ps3ShaderInterfaceVariable>();

    /// <summary>Gets active user-declared stage outputs after compiler optimization.</summary>
    public IReadOnlyList<Ps3ShaderInterfaceVariable> Outputs { get; private set; } = Array.Empty<Ps3ShaderInterfaceVariable>();

    /// <summary>Gets official SPIR-V built-in names consumed by this stage.</summary>
    public IReadOnlyList<string> InputBuiltIns { get; private set; } = Array.Empty<string>();

    /// <summary>Gets official SPIR-V built-in names produced by this stage.</summary>
    public IReadOnlyList<string> OutputBuiltIns { get; private set; } = Array.Empty<string>();

    /// <summary>Gets active constant-buffer leaf members and their compiler-declared layout.</summary>
    public IReadOnlyList<Ps3ShaderUniformMember> UniformMembers { get; private set; } = Array.Empty<Ps3ShaderUniformMember>();

    /// <summary>Gets active texture and sampler pairs after compiler-driven combined-image construction.</summary>
    public IReadOnlyList<Ps3ShaderSamplerBinding> Samplers { get; private set; } = Array.Empty<Ps3ShaderSamplerBinding>();

    /// <summary>Reflects active user inputs and outputs and assigns their stable GLSL names.</summary>
    public void ReflectInterfaces() {
        Resources* resources = null;
        Check(Api.CompilerCreateShaderResources(Compiler, &resources));
        Inputs = ReadInterface(resources, ResourceType.StageInput, "input");
        Outputs = ReadInterface(resources, ResourceType.StageOutput, "output");
        Api.CompilerUpdateActiveBuiltins(Compiler);
        InputBuiltIns = ReadBuiltIns(Silk.NET.SPIRV.StorageClass.Input);
        OutputBuiltIns = ReadBuiltIns(Silk.NET.SPIRV.StorageClass.Output);
        ValidateUnsupportedResources(resources);
        UniformMembers = ReadUniformMembers(resources);
    }

    /// <summary>Assigns stable names to active combined image samplers and captures their source bindings.</summary>
    /// <remarks>Call only after CompilerBuildCombinedImageSamplers has completed on the same compiler.</remarks>
    public void ReflectSamplers() {
        Resources* resources = null;
        Check(Api.CompilerCreateShaderResources(Compiler, &resources));
        Dictionary<uint, ReflectedResource> images = ReadSamplerResources(resources, ResourceType.SeparateImage, "texture");
        Dictionary<uint, ReflectedResource> samplers = ReadSamplerResources(resources, ResourceType.SeparateSamplers, "sampler");
        CombinedImageSampler* combinedSamplers = null;
        nuint combinedSamplerCount = 0;
        Api.CompilerGetCombinedImageSamplers(Compiler, &combinedSamplers, &combinedSamplerCount);

        List<Ps3ShaderSamplerBinding> reflectedSamplers = new(checked((int)combinedSamplerCount));
        HashSet<string> generatedNames = new(StringComparer.Ordinal);
        for (nuint index = 0; index < combinedSamplerCount; index++) {
            CombinedImageSampler combined = combinedSamplers[index];
            if (!images.TryGetValue(combined.ImageId, out ReflectedResource image)) {
                throw new InvalidOperationException($"SPIRV-Cross combined sampler references unknown texture id {combined.ImageId}.");
            }
            if (!samplers.TryGetValue(combined.SamplerId, out ReflectedResource sampler)) {
                throw new InvalidOperationException($"SPIRV-Cross combined sampler references unknown sampler id {combined.SamplerId}.");
            }

            uint textureBinding = Api.CompilerGetDecoration(Compiler, image.Id, Silk.NET.SPIRV.Decoration.Binding);
            uint samplerBinding = Api.CompilerGetDecoration(Compiler, sampler.Id, Silk.NET.SPIRV.Decoration.Binding);
            string generatedName = $"hel_ps3_sampler_t{textureBinding}_s{samplerBinding}";
            if (!generatedNames.Add(generatedName)) {
                throw new NotSupportedException($"Active PS3 texture/sampler pairs map to duplicate binding identity '{generatedName}'.");
            }

            string textureName = Api.CompilerGetNameS(Compiler, image.Id);
            string samplerName = Api.CompilerGetNameS(Compiler, sampler.Id);
            if (string.IsNullOrWhiteSpace(textureName) || string.IsNullOrWhiteSpace(samplerName)) {
                throw new InvalidOperationException($"Active PS3 combined sampler at texture binding {textureBinding}, sampler binding {samplerBinding} has no compiler-preserved authored resource name.");
            }
            CrossType* imageType = Api.CompilerGetTypeHandle(Compiler, image.TypeId);
            string dimension = Api.TypeGetImageDimension(imageType).ToString();
            if (string.IsNullOrWhiteSpace(dimension)) {
                throw new InvalidOperationException($"Active PS3 texture '{textureName}' has no compiler-reported image dimension.");
            }

            Api.CompilerSetName(Compiler, combined.CombinedId, generatedName);
            reflectedSamplers.Add(new Ps3ShaderSamplerBinding(
                textureName,
                textureBinding,
                samplerName,
                samplerBinding,
                generatedName,
                dimension));
        }

        Samplers = reflectedSamplers.AsReadOnly();
    }

    /// <summary>Reads one SPIRV-Cross resource class and applies stable names through compiler APIs.</summary>
    /// <param name="resources">SPIRV-Cross reflection resource handle.</param>
    /// <param name="resourceType">Stage input or output resource class.</param>
    /// <param name="nameSpace">Generated-name category for this interface direction.</param>
    /// <returns>Immutable reflected interface records.</returns>
    IReadOnlyList<Ps3ShaderInterfaceVariable> ReadInterface(Resources* resources, ResourceType resourceType, string nameSpace) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, resourceType, ref resourceList, ref resourceCount));

        List<Ps3ShaderInterfaceVariable> variables = new(checked((int)resourceCount));
        HashSet<string> generatedNames = new(StringComparer.Ordinal);
        for (nuint index = 0; index < resourceCount; index++) {
            ReflectedResource resource = resourceList[index];
            if (HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.BuiltIn)) continue;
            if (!HasDecoration(resource.Id, HlslSemanticGoogleDecoration)) {
                string sourceName = Api.CompilerGetNameS(Compiler, resource.Id);
                throw new InvalidOperationException(
                    $"Active PS3 shader {nameSpace} '{sourceName}' has no compiler-provided HlslSemanticGOOGLE decoration; reflection cannot safely identify its authored HLSL semantic.");
            }
            if (!HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.Location)) {
                string sourceName = Api.CompilerGetNameS(Compiler, resource.Id);
                throw new InvalidOperationException($"Active PS3 shader {nameSpace} '{sourceName}' has no SPIR-V Location decoration.");
            }

            string semantic = Api.CompilerGetDecorationStringS(Compiler, resource.Id, HlslSemanticGoogleDecoration);
            if (string.IsNullOrWhiteSpace(semantic)) {
                string sourceName = Api.CompilerGetNameS(Compiler, resource.Id);
                throw new InvalidOperationException($"Active PS3 shader {nameSpace} '{sourceName}' has an empty HlslSemanticGOOGLE decoration.");
            }
            uint location = Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.Location);
            string generatedName = $"hel_ps3_{nameSpace}_location_{location}_{NormalizeSemantic(semantic)}";
            if (!generatedNames.Add(generatedName)) {
                throw new NotSupportedException($"Active PS3 shader {nameSpace} resources produce the same stable name '{generatedName}'.");
            }

            CrossType* type = Api.CompilerGetTypeHandle(Compiler, resource.TypeId);
            uint arrayLength = GetArrayLength(type, semantic);
            string scalarType = $"{Api.TypeGetBasetype(type)}{Api.TypeGetBitWidth(type)}";
            string interpolation = GetInterpolation(resource.Id);
            Api.CompilerSetName(Compiler, resource.Id, generatedName);
            variables.Add(new Ps3ShaderInterfaceVariable(
                semantic,
                location,
                scalarType,
                Api.TypeGetVectorSize(type),
                Api.TypeGetColumns(type),
                arrayLength,
                interpolation,
                generatedName));
        }

        return variables.AsReadOnly();
    }

    /// <summary>Computes the checked length of literal interface array dimensions.</summary>
    /// <param name="type">Reflected interface type handle.</param>
    /// <param name="semantic">Semantic included in unsupported-shape diagnostics.</param>
    /// <returns>Product of array dimensions, or one for a non-array type.</returns>
    uint GetArrayLength(CrossType* type, string semantic) {
        uint dimensionCount = Api.TypeGetNumArrayDimensions(type);
        uint arrayLength = 1;
        for (uint dimensionIndex = 0; dimensionIndex < dimensionCount; dimensionIndex++) {
            if (Api.TypeArrayDimensionIsLiteral(type, dimensionIndex) == 0) {
                throw new NotSupportedException($"PS3 shader interface '{semantic}' has a non-literal array dimension without a tested RSX mapping.");
            }
            uint dimensionLength = Api.TypeGetArrayDimension(type, dimensionIndex);
            if (dimensionLength == 0) {
                throw new NotSupportedException($"PS3 shader interface '{semantic}' has a runtime array without a tested RSX mapping.");
            }
            arrayLength = checked(arrayLength * dimensionLength);
        }
        return arrayLength;
    }

    /// <summary>Reads explicit interpolation decorations without deriving them from variable order.</summary>
    /// <param name="id">SPIR-V variable id.</param>
    /// <returns>Comma-separated interpolation mode and modifiers, including the SPIR-V default when undecorated.</returns>
    string GetInterpolation(uint id) {
        List<string> decorations = [];
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Flat)) decorations.Add("Flat");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.NoPerspective)) decorations.Add("NoPerspective");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Centroid)) decorations.Add("Centroid");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Sample)) decorations.Add("Sample");
        if (HasDecoration(id, Silk.NET.SPIRV.Decoration.Invariant)) decorations.Add("Invariant");
        return decorations.Count == 0 ? "Smooth" : string.Join(",", decorations);
    }

    /// <summary>Returns active compiler built-ins by SPIR-V storage class, using official enum names.</summary>
    /// <param name="storageClass">Input or output storage class to inspect.</param>
    /// <returns>Names of active SPIR-V built-ins in enum order.</returns>
    IReadOnlyList<string> ReadBuiltIns(Silk.NET.SPIRV.StorageClass storageClass) {
        return Enum.GetValues<Silk.NET.SPIRV.BuiltIn>()
            .Distinct()
            .Where(builtIn => builtIn != Silk.NET.SPIRV.BuiltIn.Max && Api.CompilerHasActiveBuiltin(Compiler, builtIn, storageClass) != 0)
            .Select(builtIn => builtIn.ToString())
            .ToArray();
    }

    /// <summary>Reads active uniform buffers, gives each buffer a unique root name and walks nested members.</summary>
    /// <param name="resources">SPIRV-Cross resources associated with the active entry point.</param>
    /// <returns>All active scalar, vector and matrix leaf members.</returns>
    IReadOnlyList<Ps3ShaderUniformMember> ReadUniformMembers(Resources* resources) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, ResourceType.UniformBuffer, ref resourceList, ref resourceCount));

        List<Ps3ShaderUniformMember> members = [];
        HashSet<uint> bindings = [];
        Dictionary<uint, Dictionary<uint, string>> memberNamesByType = [];
        Dictionary<uint, string[]> authoredMemberNamesByType = [];
        for (nuint resourceIndex = 0; resourceIndex < resourceCount; resourceIndex++) {
            ReflectedResource resource = resourceList[resourceIndex];
            uint descriptorSet = HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.DescriptorSet)
                ? Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.DescriptorSet)
                : 0;
            if (descriptorSet != 0) {
                throw new NotSupportedException($"PS3 uniform buffer '{Api.CompilerGetNameS(Compiler, resource.Id)}' uses unsupported resource space {descriptorSet}.");
            }
            if (!HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.Binding)) {
                throw new InvalidOperationException($"Active PS3 uniform buffer '{Api.CompilerGetNameS(Compiler, resource.Id)}' has no SPIR-V Binding decoration.");
            }
            uint binding = Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.Binding);
            if (!bindings.Add(binding)) {
                throw new NotSupportedException($"Active PS3 uniform buffers share binding {binding}, which has no tested RSX mapping.");
            }

            string resourceName = Api.CompilerGetNameS(Compiler, resource.Id);
            string bufferName = string.IsNullOrWhiteSpace(resourceName)
                ? Api.CompilerGetNameS(Compiler, resource.BaseTypeId)
                : resourceName;
            if (string.IsNullOrWhiteSpace(bufferName)) {
                throw new InvalidOperationException($"Active PS3 uniform buffer at binding {binding} has neither a compiler-preserved resource name nor a named reflected base type.");
            }
            string generatedBufferName = $"hel_ps3_uniform_buffer_binding_{binding}";
            Api.CompilerSetName(Compiler, resource.Id, generatedBufferName);
            ReflectUniformStruct(resource.BaseTypeId, bufferName, binding, generatedBufferName, string.Empty, 0, 1, 0,
                memberNamesByType, authoredMemberNamesByType, members);
        }
        foreach (KeyValuePair<uint, Dictionary<uint, string>> typeNames in memberNamesByType) {
            foreach (KeyValuePair<uint, string> memberName in typeNames.Value) {
                Api.CompilerSetMemberName(Compiler, typeNames.Key, memberName.Key, memberName.Value);
            }
        }
        return members.AsReadOnly();
    }

    /// <summary>Rejects active resource categories and descriptor spaces without a tested PS3 mapping.</summary>
    /// <param name="resources">SPIRV-Cross resources associated with the active entry point.</param>
    void ValidateUnsupportedResources(Resources* resources) {
        ReflectedResource* storageImages = null;
        nuint storageImageCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, ResourceType.StorageImage, ref storageImages, ref storageImageCount));
        if (storageImageCount > 0) {
            ReflectedResource resource = storageImages[0];
            string name = GetResourceName(resource, "storage image");
            throw new NotSupportedException($"Active PS3 storage image '{name}' has no tested RSX mapping.");
        }

        ReflectedResource* storageBuffers = null;
        nuint storageBufferCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, ResourceType.StorageBuffer, ref storageBuffers, ref storageBufferCount));
        if (storageBufferCount > 0) {
            ReflectedResource resource = storageBuffers[0];
            string name = GetResourceName(resource, "storage buffer");
            EnsureDefaultResourceSpace(resource.Id, "storage buffer", name);
            string reason = ContainsRuntimeArray(resource.BaseTypeId, [])
                ? "contains an unsupported runtime array"
                : "has no tested RSX storage-buffer mapping";
            throw new NotSupportedException($"Active PS3 storage buffer '{name}' {reason}.");
        }
    }

    /// <summary>Walks a compiler-provided type graph and detects literal-free or zero-length array dimensions.</summary>
    /// <param name="typeId">SPIR-V type identifier to inspect.</param>
    /// <param name="visitedTypeIds">Type identifiers already visited to avoid repeated graph traversal.</param>
    /// <returns>True when this type or a nested member contains a runtime array.</returns>
    bool ContainsRuntimeArray(uint typeId, HashSet<uint> visitedTypeIds) {
        if (!visitedTypeIds.Add(typeId)) return false;
        CrossType* type = Api.CompilerGetTypeHandle(Compiler, typeId);
        uint dimensionCount = Api.TypeGetNumArrayDimensions(type);
        for (uint dimensionIndex = 0; dimensionIndex < dimensionCount; dimensionIndex++) {
            if (Api.TypeArrayDimensionIsLiteral(type, dimensionIndex) == 0 || Api.TypeGetArrayDimension(type, dimensionIndex) == 0) return true;
        }
        if (Api.TypeGetBasetype(type).ToString() != "Struct") return false;

        uint memberCount = Api.TypeGetNumMemberTypes(type);
        for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
            uint memberTypeId = Api.TypeGetMemberType(type, memberIndex);
            if (ContainsRuntimeArray(memberTypeId, visitedTypeIds)) return true;
        }
        return false;
    }

    /// <summary>Creates a compiler-resource map keyed by SPIR-V identifier and validates names and bindings.</summary>
    /// <param name="resources">SPIRV-Cross resources associated with the active entry point.</param>
    /// <param name="resourceType">Separate image or sampler resource category.</param>
    /// <param name="category">Resource category used in diagnostics.</param>
    /// <returns>Resources indexed by their SPIR-V ids.</returns>
    Dictionary<uint, ReflectedResource> ReadSamplerResources(Resources* resources, ResourceType resourceType, string category) {
        ReflectedResource* resourceList = null;
        nuint resourceCount = 0;
        Check(Api.ResourcesGetResourceListForType(resources, resourceType, ref resourceList, ref resourceCount));

        Dictionary<uint, ReflectedResource> reflectedResources = [];
        HashSet<uint> bindings = [];
        for (nuint index = 0; index < resourceCount; index++) {
            ReflectedResource resource = resourceList[index];
            string name = Api.CompilerGetNameS(Compiler, resource.Id);
            if (string.IsNullOrWhiteSpace(name)) {
                throw new InvalidOperationException($"Active PS3 {category} at SPIR-V id {resource.Id} has no compiler-preserved authored resource name.");
            }
            EnsureDefaultResourceSpace(resource.Id, category, name);
            if (!HasDecoration(resource.Id, Silk.NET.SPIRV.Decoration.Binding)) {
                throw new InvalidOperationException($"Active PS3 {category} '{name}' has no SPIR-V Binding decoration.");
            }
            uint binding = Api.CompilerGetDecoration(Compiler, resource.Id, Silk.NET.SPIRV.Decoration.Binding);
            if (!bindings.Add(binding)) {
                throw new NotSupportedException($"Active PS3 {category} resources share binding {binding}, which has no tested RSX mapping.");
            }
            reflectedResources.Add(resource.Id, resource);
        }
        return reflectedResources;
    }

    /// <summary>Returns a compiler-preserved resource name, falling back only to its compiler-provided base type name.</summary>
    /// <param name="resource">Reflected resource record.</param>
    /// <param name="category">Resource category included in the error message.</param>
    /// <returns>Compiler-provided resource or base-type identifier.</returns>
    string GetResourceName(ReflectedResource resource, string category) {
        string name = Api.CompilerGetNameS(Compiler, resource.Id);
        if (string.IsNullOrWhiteSpace(name)) name = Api.CompilerGetNameS(Compiler, resource.BaseTypeId);
        if (string.IsNullOrWhiteSpace(name)) {
            throw new InvalidOperationException($"Active PS3 {category} at SPIR-V id {resource.Id} has no compiler-preserved name.");
        }
        return name;
    }

    /// <summary>Rejects descriptor sets other than the PS3-supported default set zero.</summary>
    /// <param name="resourceId">SPIR-V resource identifier.</param>
    /// <param name="category">Resource category used in diagnostics.</param>
    /// <param name="name">Compiler-provided resource name.</param>
    void EnsureDefaultResourceSpace(uint resourceId, string category, string name) {
        if (!HasDecoration(resourceId, Silk.NET.SPIRV.Decoration.DescriptorSet)) return;
        uint descriptorSet = Api.CompilerGetDecoration(Compiler, resourceId, Silk.NET.SPIRV.Decoration.DescriptorSet);
        if (descriptorSet != 0) {
            throw new NotSupportedException($"Active PS3 {category} '{name}' uses unsupported resource space {descriptorSet}.");
        }
    }

    /// <summary>Recursively reflects a structure and preserves each nested member's absolute buffer offset.</summary>
    /// <param name="structureTypeId">SPIR-V type id for the current structure.</param>
    /// <param name="bufferName">Authored constant-buffer name.</param>
    /// <param name="binding">Constant-buffer binding.</param>
    /// <param name="generatedPathPrefix">Complete generated parent path to this structure.</param>
    /// <param name="memberPrefix">Complete parent member path.</param>
    /// <param name="baseOffset">Absolute byte offset of the current nested structure.</param>
    /// <param name="parentArrayLength">Array length inherited from containing structure members.</param>
    /// <param name="parentArrayStride">Array stride inherited from containing structure members.</param>
    /// <param name="memberNamesByType">Stable names to assign once per SPIR-V structure member type.</param>
    /// <param name="authoredMemberNamesByType">Original compiler-provided member names cached before any renaming.</param>
    /// <param name="members">Destination collection for reflected leaf members.</param>
    void ReflectUniformStruct(
        uint structureTypeId,
        string bufferName,
        uint binding,
        string generatedPathPrefix,
        string memberPrefix,
        uint baseOffset,
        uint parentArrayLength,
        uint parentArrayStride,
        Dictionary<uint, Dictionary<uint, string>> memberNamesByType,
        Dictionary<uint, string[]> authoredMemberNamesByType,
        List<Ps3ShaderUniformMember> members) {
        CrossType* structureType = Api.CompilerGetTypeHandle(Compiler, structureTypeId);
        uint memberCount = Api.TypeGetNumMemberTypes(structureType);
        if (!authoredMemberNamesByType.TryGetValue(structureTypeId, out string[] authoredMemberNames)) {
            authoredMemberNames = new string[memberCount];
            for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
                authoredMemberNames[memberIndex] = Api.CompilerGetMemberNameS(Compiler, structureTypeId, memberIndex);
            }
            authoredMemberNamesByType.Add(structureTypeId, authoredMemberNames);
        }
        string structureName = Api.CompilerGetNameS(Compiler, structureTypeId);
        if (string.IsNullOrWhiteSpace(structureName)) {
            throw new InvalidOperationException($"Active PS3 uniform buffer '{bufferName}' contains an unnamed reflected structure.");
        }

        for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
            string memberName = authoredMemberNames[memberIndex];
            if (string.IsNullOrWhiteSpace(memberName)) {
                throw new InvalidOperationException($"Active PS3 uniform buffer '{bufferName}' contains an unnamed member at index {memberIndex}.");
            }
            string memberPath = string.IsNullOrEmpty(memberPrefix) ? memberName : $"{memberPrefix}.{memberName}";
            string generatedMemberName = $"hel_ps3_uniform_type_{NormalizeSemantic(structureName)}_member_{memberIndex}";
            string generatedMemberPath = $"{generatedPathPrefix}.{generatedMemberName}";
            RegisterMemberName(memberNamesByType, structureTypeId, memberIndex, generatedMemberName);
            uint relativeOffset = 0;
            Check(Api.CompilerTypeStructMemberOffset(Compiler, structureType, memberIndex, &relativeOffset));
            uint byteOffset = checked(baseOffset + relativeOffset);
            nuint nativeByteSize = 0;
            Check(Api.CompilerGetDeclaredStructMemberSize(Compiler, structureType, memberIndex, &nativeByteSize));
            uint byteSize = checked((uint)nativeByteSize);

            uint memberTypeId = Api.TypeGetMemberType(structureType, memberIndex);
            CrossType* memberType = Api.CompilerGetTypeHandle(Compiler, memberTypeId);
            uint arrayDimensionCount = Api.TypeGetNumArrayDimensions(memberType);
            if (arrayDimensionCount > 1 || (arrayDimensionCount > 0 && parentArrayStride != 0)) {
                throw new NotSupportedException(
                    $"Active PS3 uniform member '{bufferName}.{memberPath}' requires multiple array strides, which the reflection DTO cannot represent exactly.");
            }
            bool memberIsStruct = Api.TypeGetBasetype(memberType).ToString() == "Struct";
            if (memberIsStruct && (arrayDimensionCount > 0 || parentArrayStride != 0)
                && ContainsArrayInStructureMembers(memberTypeId, [])) {
                throw new NotSupportedException(
                    $"Active PS3 uniform member '{bufferName}.{memberPath}' requires multiple array strides, which the reflection DTO cannot represent exactly.");
            }
            uint arrayLength = GetArrayLength(memberType, memberPath);
            uint fullArrayLength = checked(parentArrayLength * arrayLength);
            uint arrayStride = parentArrayStride;
            if (arrayDimensionCount > 0) {
                Check(Api.CompilerTypeStructMemberArrayStride(Compiler, structureType, memberIndex, &arrayStride));
            }

            if (memberIsStruct) {
                ReflectUniformStruct(memberTypeId, bufferName, binding, generatedMemberPath, memberPath, byteOffset,
                    fullArrayLength, arrayStride, memberNamesByType, authoredMemberNamesByType, members);
                continue;
            }

            uint columns = Api.TypeGetColumns(memberType);
            uint matrixStride = 0;
            bool rowMajor = false;
            if (columns > 1) {
                Check(Api.CompilerTypeStructMemberMatrixStride(Compiler, structureType, memberIndex, &matrixStride));
                rowMajor = Api.CompilerHasMemberDecoration(Compiler, structureTypeId, memberIndex, Silk.NET.SPIRV.Decoration.RowMajor) != 0;
            }

            string scalarType = $"{Api.TypeGetBasetype(memberType)}{Api.TypeGetBitWidth(memberType)}";
            members.Add(new Ps3ShaderUniformMember(
                bufferName,
                binding,
                memberPath,
                byteOffset,
                byteSize,
                arrayDimensionCount > 0 || parentArrayStride != 0 ? arrayStride : 0,
                matrixStride,
                rowMajor,
                generatedMemberPath,
                scalarType,
                Api.TypeGetVectorSize(memberType),
                columns,
                fullArrayLength));
        }
    }

    /// <summary>Records one stable type-scoped member name and rejects conflicting aliases for the same SPIR-V member.</summary>
    /// <param name="namesByType">Names already collected by SPIR-V structure type id.</param>
    /// <param name="structureTypeId">SPIR-V structure type id.</param>
    /// <param name="memberIndex">Member index within that type.</param>
    /// <param name="generatedName">Stable name derived from compiler-preserved type identity and member index.</param>
    void RegisterMemberName(Dictionary<uint, Dictionary<uint, string>> namesByType, uint structureTypeId, uint memberIndex, string generatedName) {
        if (!namesByType.TryGetValue(structureTypeId, out Dictionary<uint, string> typeNames)) {
            typeNames = [];
            namesByType.Add(structureTypeId, typeNames);
        }
        if (typeNames.TryGetValue(memberIndex, out string existingName)) {
            if (!string.Equals(existingName, generatedName, StringComparison.Ordinal)) {
                throw new InvalidOperationException($"SPIR-V structure member {structureTypeId}:{memberIndex} maps to conflicting generated names.");
            }
            return;
        }
        typeNames.Add(memberIndex, generatedName);
    }

    /// <summary>Checks nested structure members for array dimensions without requiring any struct or member names.</summary>
    /// <param name="structureTypeId">SPIR-V type id for the structure or array-of-structure type.</param>
    /// <param name="visitedTypeIds">Structure ids already inspected to avoid repeated graph traversal.</param>
    /// <returns>True when the structure or any nested member declares an array dimension.</returns>
    bool ContainsArrayInStructureMembers(uint structureTypeId, HashSet<uint> visitedTypeIds) {
        if (!visitedTypeIds.Add(structureTypeId)) return false;
        CrossType* structureType = Api.CompilerGetTypeHandle(Compiler, structureTypeId);
        if (Api.TypeGetBasetype(structureType).ToString() != "Struct") return false;

        uint memberCount = Api.TypeGetNumMemberTypes(structureType);
        for (uint memberIndex = 0; memberIndex < memberCount; memberIndex++) {
            uint memberTypeId = Api.TypeGetMemberType(structureType, memberIndex);
            CrossType* memberType = Api.CompilerGetTypeHandle(Compiler, memberTypeId);
            if (Api.TypeGetNumArrayDimensions(memberType) > 0) return true;
            if (Api.TypeGetBasetype(memberType).ToString() == "Struct"
                && ContainsArrayInStructureMembers(memberTypeId, visitedTypeIds)) return true;
        }
        return false;
    }

    /// <summary>Reads a decoration flag from SPIRV-Cross's byte-valued binding.</summary>
    /// <param name="id">SPIR-V object id.</param>
    /// <param name="decoration">Decoration to inspect.</param>
    /// <returns>True when the compiler reports the decoration.</returns>
    bool HasDecoration(uint id, Silk.NET.SPIRV.Decoration decoration) {
        return Api.CompilerHasDecoration(Compiler, id, decoration) != 0;
    }

    /// <summary>Produces an identifier-safe semantic suffix while retaining its authored characters where legal.</summary>
    /// <param name="semantic">Compiler-provided HLSL semantic.</param>
    /// <returns>ASCII letters, digits and underscore only.</returns>
    static string NormalizeSemantic(string semantic) {
        char[] characters = semantic.Select(character => char.IsAsciiLetterOrDigit(character) ? character : '_').ToArray();
        return new string(characters);
    }

    /// <summary>Throws with the compiler-owned diagnostic when a SPIRV-Cross operation fails.</summary>
    /// <param name="result">Native SPIRV-Cross result code.</param>
    void Check(Result result) {
        if (result != Result.Success) {
            throw new InvalidOperationException("PS3 shader reflection failed: " + Api.ContextGetLastErrorStringS(Context));
        }
    }
}
```
### builder.tests/Ps3ShaderReflectionTests.cs
```csharp
using helengine;
using helengine.ps3.builder;
using System.Security.Cryptography;
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

    /// <summary>Emits both stages of the complex fixture unchanged when an explicit probe output root is configured.</summary>
    [Fact]
    public void EmitsComplexFixtureGlslForNativeSdkProbeWhenOutputRootIsSet() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_REFLECTION_OUTPUT");
        if (string.IsNullOrWhiteSpace(outputRoot)) return;
        if (!Path.IsPathFullyQualified(outputRoot)) {
            throw new InvalidOperationException("Reflection probe output root must be an absolute build-owned path.");
        }

        Ps3ShaderSourceCompiler compiler = new();
        Ps3ShaderTranslation vertex = compiler.Translate(
            CreateRequest(ReflectionSource, ShaderStage.Vertex, "VS", optimize: true), new Ps3ShaderIncludeResolverStub());
        Ps3ShaderTranslation pixel = compiler.Translate(
            CreateRequest(ReflectionSource, ShaderStage.Pixel, "PS", optimize: true), new Ps3ShaderIncludeResolverStub());
        Directory.CreateDirectory(outputRoot);
        File.WriteAllText(Path.Combine(outputRoot, "VS.glsl"), vertex.GlslSource, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(outputRoot, "PS.glsl"), pixel.GlslSource, new UTF8Encoding(false));
        string metadata = JsonSerializer.Serialize(new {
            Vertex = CreateProbeMetadata(vertex),
            Pixel = CreateProbeMetadata(pixel)
        }, new JsonSerializerOptions { WriteIndented = true });
        File.WriteAllText(Path.Combine(outputRoot, "reflection.json"), metadata, new UTF8Encoding(false));
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
```
