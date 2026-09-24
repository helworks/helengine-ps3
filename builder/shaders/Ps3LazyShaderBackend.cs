using helengine;

namespace helengine.ps3.builder;

/// <summary>Advertises PS3 compilation support and creates the production backend on the first compile request.</summary>
public sealed class Ps3LazyShaderBackend : IShaderBackend, IShaderBackendCachePolicy {
    /// <summary>Name of the environment variable that identifies shared-service compiler job storage.</summary>
    public const string WorkingRootEnvironmentVariable = "HELENGINE_PS3_SHADER_WORK_ROOT";

    /// <summary>Stable compile-time factory whose value is created at first use and then reused.</summary>
    readonly Lazy<IShaderBackend> BackendValue;

    /// <summary>Immutable HLSL model range and stage capabilities advertised during discovery.</summary>
    readonly ShaderBackendCapabilities BackendCapabilities = new(
        new ShaderModel(3, 0),
        new ShaderModel(5, 0),
        Array.AsReadOnly(new[] { ShaderStage.Vertex, ShaderStage.Pixel }),
        false);

    /// <summary>Creates the production adapter without reading environment variables or the SDK lock.</summary>
    public Ps3LazyShaderBackend()
        : this(CreateConfiguredBackend) {
    }

    /// <summary>Creates a lazy adapter around a caller-supplied backend factory for deterministic tests.</summary>
    /// <param name="backendFactory">Backend creator deferred until the first Compile call.</param>
    /// <exception cref="ArgumentNullException">Thrown when no backend factory is supplied.</exception>
    internal Ps3LazyShaderBackend(Func<IShaderBackend> backendFactory) {
        ArgumentNullException.ThrowIfNull(backendFactory);
        BackendValue = new Lazy<IShaderBackend>(() => ValidateBackend(backendFactory()));
    }

    /// <summary>Gets the target represented by this lazy backend without constructing the production compiler.</summary>
    public ShaderCompileTarget Target => ShaderCompileTarget.Ps3;

    /// <summary>Gets supported shader models and stages without consulting SDK configuration.</summary>
    public ShaderBackendCapabilities Capabilities => BackendCapabilities;

    /// <summary>Gets whether the shared shader cache is bypassed by PS3 backend compilation.</summary>
    public bool UseSharedCache => false;

    /// <summary>Compiles through the deferred production backend while forwarding both shared arguments unchanged.</summary>
    /// <param name="request">Exact shared shader compile request.</param>
    /// <param name="includeResolver">Exact shared include resolver.</param>
    /// <returns>The typed result returned by the configured PS3 backend.</returns>
    /// <exception cref="ArgumentNullException">Thrown when a request or include resolver is missing.</exception>
    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver) {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(includeResolver);
        return BackendValue.Value.Compile(request, includeResolver);
    }

    /// <summary>Reads explicit shared-service storage only at first compilation and asks the factory to load the SDK toolchain.</summary>
    /// <returns>A production PS3 backend configured under the requested work root.</returns>
    static IShaderBackend CreateConfiguredBackend() {
        string workingRoot = Environment.GetEnvironmentVariable(WorkingRootEnvironmentVariable);
        if (string.IsNullOrWhiteSpace(workingRoot)) {
            throw new InvalidOperationException($"Environment variable {WorkingRootEnvironmentVariable} must name the PS3 shader work root.");
        }
        if (!Path.IsPathFullyQualified(workingRoot)) {
            throw new InvalidOperationException($"Environment variable {WorkingRootEnvironmentVariable} must contain a fully qualified path.");
        }

        return Ps3ShaderBackendFactory.Create(Path.GetFullPath(workingRoot));
    }

    /// <summary>Rejects missing factories and backends that do not implement the advertised PS3 target.</summary>
    /// <param name="backend">Backend returned by the deferred factory.</param>
    /// <returns>The validated backend used for subsequent compile calls.</returns>
    static IShaderBackend ValidateBackend(IShaderBackend backend) {
        if (backend == null) {
            throw new InvalidOperationException("The PS3 shader backend factory returned no backend.");
        }
        if (backend.Target != ShaderCompileTarget.Ps3) {
            throw new InvalidOperationException($"The PS3 shader backend factory returned target '{backend.Target}' instead of 'Ps3'.");
        }
        return backend;
    }
}
