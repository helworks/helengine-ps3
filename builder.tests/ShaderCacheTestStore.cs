using helengine;

namespace helengine.ps3.builder.tests;

/// <summary>
/// In-memory compile cache that records lookup and storage interactions by stable key text.
/// </summary>
public sealed class ShaderCacheTestStore : IShaderCompileCache {
    /// <summary>
    /// Stores results under the stable string representation of each cache key.
    /// </summary>
    readonly Dictionary<string, ShaderCompileResult> Results;

    /// <summary>
    /// Initializes an empty cache store with ordinal key matching.
    /// </summary>
    public ShaderCacheTestStore() {
        Results = new Dictionary<string, ShaderCompileResult>(StringComparer.Ordinal);
    }

    /// <summary>
    /// Gets the number of cache lookup attempts.
    /// </summary>
    public int TryGetCalls { get; private set; }

    /// <summary>
    /// Gets the number of results stored through the cache interface.
    /// </summary>
    public int StoreCalls { get; private set; }

    /// <summary>
    /// Attempts to retrieve a result by the stable cache-key string.
    /// </summary>
    /// <param name="key">Key identifying the compile request.</param>
    /// <param name="result">Result found for the key, or null when absent.</param>
    /// <returns>True when the cache contains a result for the key.</returns>
    public bool TryGet(ShaderCompileCacheKey key, out ShaderCompileResult result) {
        TryGetCalls++;
        return Results.TryGetValue(key.ToString(), out result);
    }

    /// <summary>
    /// Stores a compile result and records the cache write.
    /// </summary>
    /// <param name="key">Key identifying the compile request.</param>
    /// <param name="result">Result to store for the key.</param>
    public void Store(ShaderCompileCacheKey key, ShaderCompileResult result) {
        StoreCalls++;
        Results[key.ToString()] = result;
    }

    /// <summary>
    /// Adds a pre-existing result without counting it as a cache write by the service under test.
    /// </summary>
    /// <param name="key">Key identifying the compile request.</param>
    /// <param name="result">Stale or seeded result returned for the key.</param>
    public void Seed(ShaderCompileCacheKey key, ShaderCompileResult result) {
        Results[key.ToString()] = result;
    }
}
