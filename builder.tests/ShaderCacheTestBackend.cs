using System.Text;
using helengine;

namespace helengine.ps3.builder.tests;

/// <summary>
/// Provides deterministic shader results while recording every backend input relevant to cache safety.
/// </summary>
public class ShaderCacheTestBackend : IShaderBackend {
    /// <summary>
    /// Stores the target selected when this test backend is constructed.
    /// </summary>
    readonly ShaderCompileTarget TargetValue;

    /// <summary>
    /// Stores the supported capabilities advertised to the compile service.
    /// </summary>
    readonly ShaderBackendCapabilities CapabilitiesValue;

    /// <summary>
    /// Initializes a test backend with an explicit target and toolchain identity.
    /// </summary>
    /// <param name="target">Target registered by the backend.</param>
    /// <param name="toolchainIdentity">Identity embedded in each generated test result.</param>
    public ShaderCacheTestBackend(ShaderCompileTarget target, string toolchainIdentity) {
        if (string.IsNullOrWhiteSpace(toolchainIdentity)) {
            throw new ArgumentException("Toolchain identity must be provided.", nameof(toolchainIdentity));
        }

        TargetValue = target;
        CapabilitiesValue = new ShaderBackendCapabilities(
            new ShaderModel(4, 0),
            new ShaderModel(4, 0),
            [ShaderStage.Vertex, ShaderStage.Pixel],
            false);
        ToolchainIdentity = toolchainIdentity;
    }

    /// <summary>
    /// Gets the target handled by this backend.
    /// </summary>
    public ShaderCompileTarget Target => TargetValue;

    /// <summary>
    /// Gets the capabilities advertised by this backend.
    /// </summary>
    public ShaderBackendCapabilities Capabilities => CapabilitiesValue;

    /// <summary>
    /// Gets the number of requests compiled by this backend.
    /// </summary>
    public int Calls { get; private set; }

    /// <summary>
    /// Gets or sets the toolchain identity encoded into subsequent bytecode results.
    /// </summary>
    public string ToolchainIdentity { get; set; }

    /// <summary>
    /// Gets or sets the exception raised by the next compile request, when configured.
    /// </summary>
    public Exception Failure { get; set; }

    /// <summary>
    /// Compiles a deterministic test result that includes resolved source and all compile options.
    /// </summary>
    /// <param name="request">Request received from the shared compile service.</param>
    /// <param name="includeResolver">Resolver used to obtain the controlled include contents.</param>
    /// <returns>A successful result whose bytecode records the supplied inputs.</returns>
    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        Calls++;
        if (Failure != null) throw Failure;

        ShaderIncludeResult include = includeResolver.Resolve(request.Source.Path, "common.hlsli");
        string text = string.Join("|", include.Source, ToolchainIdentity,
            request.Options.GenerateDebugInfo, request.Options.Optimize,
            request.Options.TreatWarningsAsErrors);
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        ShaderProgramDefinition definition = new(request.ProgramName, request.Stage,
            request.EntryPoint, [], [], [], [new ShaderVariant(request.Variant, [])]);
        return new ShaderCompileResult(request, definition,
            new ShaderCompiledBinary(request.Target, request.Stage, request.EntryPoint,
                request.Variant, bytes), [], true);
    }
}
