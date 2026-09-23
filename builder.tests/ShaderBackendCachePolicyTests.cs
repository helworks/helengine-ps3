using System.Text;
using helengine;

namespace helengine.ps3.builder.tests;

/// <summary>
/// Verifies shared cache behavior for legacy backends and explicit backend cache policies.
/// </summary>
public sealed class ShaderBackendCachePolicyTests {
    /// <summary>
    /// Confirms an opted-out backend compiles instead of returning a stale shared cache entry.
    /// </summary>
    [Fact]
    public void DisabledPolicyBypassesASeededCacheEntry() {
        ShaderCompileRequest request = Request(ShaderCompileTarget.Ps3, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCacheTestBackend staleBackend = new(ShaderCompileTarget.Ps3, "stale-toolchain");
        ShaderCompileResult staleResult = staleBackend.Compile(request, resolver);
        ShaderCachePolicyTestBackend backend = new(ShaderCompileTarget.Ps3, "current-toolchain", false);
        ShaderCacheTestStore store = new();
        store.Seed(ShaderCompileRequestIdentity.CreateCacheKey(request, new ShaderSourceHasher()), staleResult);
        ShaderCompileService service = Service(resolver, store, backend);

        ShaderCompileResult result = service.Compile(request);

        Assert.NotSame(staleResult, result);
        Assert.Equal(1, backend.Calls);
        Assert.Equal(0, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Confirms identical requests still reach an opted-out backend on every call.
    /// </summary>
    [Fact]
    public void DisabledPolicyCompilesRepeatedRequestsEachTime() {
        ShaderCompileRequest request = Request(ShaderCompileTarget.Ps3, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCachePolicyTestBackend backend = new(ShaderCompileTarget.Ps3, "toolchain", false);
        ShaderCacheTestStore store = new();
        ShaderCompileService service = Service(resolver, store, backend);

        ShaderCompileResult first = service.Compile(request);
        ShaderCompileResult second = service.Compile(request);

        Assert.NotSame(first, second);
        Assert.Equal(2, backend.Calls);
        Assert.Equal(0, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Confirms changed resolved include text reaches an opted-out backend despite an unchanged cache key.
    /// </summary>
    [Fact]
    public void DisabledPolicyPassesChangedIncludeContentToBackend() {
        ShaderCompileRequest request = Request(ShaderCompileTarget.Ps3, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("first include");
        ShaderCachePolicyTestBackend backend = new(ShaderCompileTarget.Ps3, "toolchain", false);
        ShaderCacheTestStore store = new();
        ShaderCompileService service = Service(resolver, store, backend);
        string firstKey = CacheKey(request);
        ShaderCompileResult first = service.Compile(request);
        resolver.Files["common.hlsli"] = new ShaderIncludeResult("common.hlsli", "second include");

        ShaderCompileResult second = service.Compile(request);

        Assert.Equal(firstKey, CacheKey(request));
        Assert.NotEqual(Encoding.UTF8.GetString(first.Binary.Bytecode), Encoding.UTF8.GetString(second.Binary.Bytecode));
        Assert.Contains("second include", Encoding.UTF8.GetString(second.Binary.Bytecode));
        Assert.Equal(2, backend.Calls);
    }

    /// <summary>
    /// Confirms each compile option reaches the backend while request cache identity remains unchanged.
    /// </summary>
    /// <param name="changedOption">Option switched between the two compile requests.</param>
    [Theory]
    [InlineData("debug")]
    [InlineData("optimize")]
    [InlineData("warnings")]
    public void DisabledPolicyPassesChangedOptionsToBackend(string changedOption) {
        ShaderCompileRequest firstRequest = Request(ShaderCompileTarget.Ps3, false, true, false);
        bool debug = changedOption == "debug";
        bool optimize = changedOption != "optimize";
        bool warnings = changedOption == "warnings";
        ShaderCompileRequest secondRequest = Request(ShaderCompileTarget.Ps3, debug, optimize, warnings);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCachePolicyTestBackend backend = new(ShaderCompileTarget.Ps3, "toolchain", false);
        ShaderCacheTestStore store = new();
        ShaderCompileService service = Service(resolver, store, backend);
        string firstKey = CacheKey(firstRequest);

        ShaderCompileResult first = service.Compile(firstRequest);
        ShaderCompileResult second = service.Compile(secondRequest);

        Assert.Equal(firstKey, CacheKey(secondRequest));
        Assert.NotEqual(Encoding.UTF8.GetString(first.Binary.Bytecode), Encoding.UTF8.GetString(second.Binary.Bytecode));
        Assert.Equal(2, backend.Calls);
        Assert.Equal(0, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Confirms replacing the backend toolchain identity changes produced output for the same request key.
    /// </summary>
    [Fact]
    public void DisabledPolicyPassesChangedToolchainIdentityToBackend() {
        ShaderCompileRequest request = Request(ShaderCompileTarget.Ps3, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCachePolicyTestBackend backend = new(ShaderCompileTarget.Ps3, "toolchain-one", false);
        ShaderCacheTestStore store = new();
        ShaderCompileService service = Service(resolver, store, backend);
        string firstKey = CacheKey(request);
        ShaderCompileResult first = service.Compile(request);
        backend.ToolchainIdentity = "toolchain-two";

        ShaderCompileResult second = service.Compile(request);

        Assert.Equal(firstKey, CacheKey(request));
        Assert.NotEqual(Encoding.UTF8.GetString(first.Binary.Bytecode), Encoding.UTF8.GetString(second.Binary.Bytecode));
        Assert.Contains("toolchain-two", Encoding.UTF8.GetString(second.Binary.Bytecode));
        Assert.Equal(2, backend.Calls);
    }

    /// <summary>
    /// Confirms backend failures propagate without any shared cache lookup or storage.
    /// </summary>
    [Fact]
    public void DisabledPolicyPropagatesBackendFailureWithoutCacheInteraction() {
        ShaderCompileRequest request = Request(ShaderCompileTarget.Ps3, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        InvalidOperationException failure = new("backend failure");
        ShaderCachePolicyTestBackend backend = new(ShaderCompileTarget.Ps3, "toolchain", false) {
            Failure = failure
        };
        ShaderCacheTestStore store = new();
        ShaderCompileService service = Service(resolver, store, backend);

        InvalidOperationException actual = Assert.Throws<InvalidOperationException>(() => service.Compile(request));

        Assert.Same(failure, actual);
        Assert.Equal(1, backend.Calls);
        Assert.Equal(0, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Confirms a null request retains the service's existing argument exception.
    /// </summary>
    [Fact]
    public void CompileRejectsNullRequestBeforeCacheInteraction() {
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCacheTestStore store = new();
        ShaderCompileService service = new(resolver, store, new ShaderSourceHasher());

        Assert.Throws<ArgumentNullException>(() => service.Compile(null));
        Assert.Equal(0, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Confirms an unregistered target retains the service's existing invalid-operation exception.
    /// </summary>
    [Fact]
    public void CompileRejectsUnregisteredTargetBeforeCacheInteraction() {
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCacheTestStore store = new();
        ShaderCompileService service = new(resolver, store, new ShaderSourceHasher());

        Assert.Throws<InvalidOperationException>(() => service.Compile(
            Request(ShaderCompileTarget.Ps3, false, true, false)));
        Assert.Equal(0, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Confirms legacy and explicitly enabled backends cache the first result and reuse it on the second call.
    /// </summary>
    /// <param name="target">Non-native backend target registered for the service.</param>
    /// <param name="hasEnabledPolicy">True to use an explicit cache-enabled policy backend.</param>
    [Theory]
    [InlineData(ShaderCompileTarget.DirectX11, false)]
    [InlineData(ShaderCompileTarget.Vulkan, false)]
    [InlineData(ShaderCompileTarget.DirectX11, true)]
    [InlineData(ShaderCompileTarget.Vulkan, true)]
    public void LegacyAndEnabledPolicyBackendsUseSharedCache(ShaderCompileTarget target, bool hasEnabledPolicy) {
        ShaderCompileRequest request = Request(target, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCacheTestBackend backend = CreateBackend(target, hasEnabledPolicy, true);
        ShaderCacheTestStore store = new();
        ShaderCompileService service = Service(resolver, store, backend);

        ShaderCompileResult first = service.Compile(request);
        ShaderCompileResult second = service.Compile(request);

        Assert.Same(first, second);
        Assert.Equal(1, backend.Calls);
        Assert.Equal(2, store.TryGetCalls);
        Assert.Equal(1, store.StoreCalls);
    }

    /// <summary>
    /// Confirms legacy and explicitly enabled backends return a seeded same-key result without compiling or storing.
    /// </summary>
    /// <param name="target">Non-native backend target registered for the service.</param>
    /// <param name="hasEnabledPolicy">True to use an explicit cache-enabled policy backend.</param>
    [Theory]
    [InlineData(ShaderCompileTarget.DirectX11, false)]
    [InlineData(ShaderCompileTarget.Vulkan, false)]
    [InlineData(ShaderCompileTarget.DirectX11, true)]
    [InlineData(ShaderCompileTarget.Vulkan, true)]
    public void LegacyAndEnabledPolicyBackendsReturnSeededCacheResult(ShaderCompileTarget target, bool hasEnabledPolicy) {
        ShaderCompileRequest request = Request(target, false, true, false);
        Ps3ShaderIncludeResolverStub resolver = Resolver("included source");
        ShaderCacheTestBackend seedBackend = new(target, "seed-toolchain");
        ShaderCompileResult seededResult = seedBackend.Compile(request, resolver);
        ShaderCacheTestBackend backend = CreateBackend(target, hasEnabledPolicy, true);
        ShaderCacheTestStore store = new();
        store.Seed(ShaderCompileRequestIdentity.CreateCacheKey(request, new ShaderSourceHasher()), seededResult);
        ShaderCompileService service = Service(resolver, store, backend);

        ShaderCompileResult result = service.Compile(request);

        Assert.Same(seededResult, result);
        Assert.Equal(0, backend.Calls);
        Assert.Equal(1, store.TryGetCalls);
        Assert.Equal(0, store.StoreCalls);
    }

    /// <summary>
    /// Creates the canonical request used to expose omissions from the shared request cache identity.
    /// </summary>
    /// <param name="target">Backend target for the request.</param>
    /// <param name="debug">Whether debug information is requested.</param>
    /// <param name="optimize">Whether optimization is requested.</param>
    /// <param name="warnings">Whether warnings are treated as errors.</param>
    /// <returns>A valid pixel-shader request with controlled source and binding policy.</returns>
    static ShaderCompileRequest Request(ShaderCompileTarget target, bool debug, bool optimize, bool warnings) {
        ShaderSourceInfo source = new(Path.GetFullPath("cache-policy/main.hlsl"), "#include \"common.hlsli\"");
        ShaderCompileOptions options = new(new ShaderBindingPolicy(0, 0, 0, 0, 0), debug, optimize, warnings);
        return new ShaderCompileRequest(source, "CacheProbe.ps", "PS", ShaderStage.Pixel,
            target, new ShaderModel(4, 0), "default", [], options);
    }

    /// <summary>
    /// Builds an include resolver with controlled source for the shared cache tests.
    /// </summary>
    /// <param name="includeSource">Content returned for the requested common include.</param>
    /// <returns>A resolver configured for the backend's include request.</returns>
    static Ps3ShaderIncludeResolverStub Resolver(string includeSource) {
        Ps3ShaderIncludeResolverStub resolver = new();
        resolver.Files["common.hlsli"] = new ShaderIncludeResult("common.hlsli", includeSource);
        return resolver;
    }

    /// <summary>
    /// Creates and registers the supplied backend with an actual shared compile service instance.
    /// </summary>
    /// <param name="resolver">Include resolver used during backend compilation.</param>
    /// <param name="store">Cache store observed by the test.</param>
    /// <param name="backend">Backend registered for the request target.</param>
    /// <returns>A service that uses the supplied test dependencies.</returns>
    static ShaderCompileService Service(
        Ps3ShaderIncludeResolverStub resolver,
        ShaderCacheTestStore store,
        IShaderBackend backend) {
        ShaderCompileService service = new(resolver, store, new ShaderSourceHasher());
        service.RegisterBackend(backend);
        return service;
    }

    /// <summary>
    /// Returns the stable text used by the shared service to locate a request in its cache.
    /// </summary>
    /// <param name="request">Request whose existing identity is inspected.</param>
    /// <returns>Stable cache-key text.</returns>
    static string CacheKey(ShaderCompileRequest request) {
        return ShaderCompileRequestIdentity.CreateCacheKey(request, new ShaderSourceHasher()).ToString();
    }

    /// <summary>
    /// Creates either a legacy backend or one that explicitly allows shared caching.
    /// </summary>
    /// <param name="target">Target registered by the backend.</param>
    /// <param name="hasEnabledPolicy">True to implement the cache policy interface.</param>
    /// <param name="useSharedCache">Cache selection passed to an explicit-policy backend.</param>
    /// <returns>A backend suitable for shared cache behavior tests.</returns>
    static ShaderCacheTestBackend CreateBackend(
        ShaderCompileTarget target,
        bool hasEnabledPolicy,
        bool useSharedCache) {
        if (hasEnabledPolicy) return new ShaderCachePolicyTestBackend(target, "test-toolchain", useSharedCache);
        return new ShaderCacheTestBackend(target, "test-toolchain");
    }
}
