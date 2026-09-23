using helengine;

namespace helengine.ps3.builder.tests;

/// <summary>
/// Test backend that explicitly selects whether the shared compile cache is allowed.
/// </summary>
public sealed class ShaderCachePolicyTestBackend : ShaderCacheTestBackend, IShaderBackendCachePolicy {
    /// <summary>
    /// Stores the cache choice supplied when this backend is constructed.
    /// </summary>
    readonly bool UseSharedCacheValue;

    /// <summary>
    /// Initializes a cache-policy backend with explicit target, toolchain and cache choice.
    /// </summary>
    /// <param name="target">Target registered by the backend.</param>
    /// <param name="toolchainIdentity">Identity embedded in each generated test result.</param>
    /// <param name="useSharedCache">True when the shared cache should be used.</param>
    public ShaderCachePolicyTestBackend(
        ShaderCompileTarget target,
        string toolchainIdentity,
        bool useSharedCache)
        : base(target, toolchainIdentity) {
        UseSharedCacheValue = useSharedCache;
    }

    /// <summary>
    /// Gets whether the shared shader compile cache should be used for this backend.
    /// </summary>
    public bool UseSharedCache => UseSharedCacheValue;
}
