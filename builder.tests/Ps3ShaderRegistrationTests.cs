using System.Reflection;
using System.Runtime.ExceptionServices;
using helengine;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Serializes tests that inspect process environment used only at shader compile time.</summary>
[CollectionDefinition("PS3 shader process environment", DisableParallelization = true)]
public sealed class Ps3ShaderEnvironmentCollection {
}

/// <summary>Verifies builder capability discovery and backend construction remain lazy and explicit.</summary>
[Collection("PS3 shader process environment")]
public sealed class Ps3ShaderRegistrationTests {
    /// <summary>Assembly containing the builder and its internal lazy backend adapter.</summary>
    static readonly Assembly BuilderAssembly = typeof(Ps3PlatformAssetBuilder).Assembly;

    /// <summary>Checks both existing builder constructors register capabilities without reading SDK configuration.</summary>
    [Fact]
    public void Builder_DefaultAndExecutorConstructorsRegisterWithoutSdkConfiguration() {
        string oldRepositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        string oldShaderWorkRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT");
        try {
            Environment.SetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT", null);
            Environment.SetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT", null);

            Ps3PlatformAssetBuilder defaultBuilder = new();
            Ps3PlatformAssetBuilder injectedExecutorBuilder = new(new NativeBuildExecutorStub());
            Assert.IsAssignableFrom<IPlatformShaderArtifactBuilder>((object)defaultBuilder);
            Assert.IsAssignableFrom<IShaderBackendRegistryContributor>((object)defaultBuilder);
            Assert.IsAssignableFrom<IPlatformShaderArtifactBuilder>((object)injectedExecutorBuilder);
            Assert.IsAssignableFrom<IShaderBackendRegistryContributor>((object)injectedExecutorBuilder);

            ShaderBackendRegistry registry = new();
            Assert.IsAssignableFrom<IShaderBackendRegistryContributor>((object)defaultBuilder).RegisterShaderBackends(registry);

            Assert.True(registry.ContainsTarget(ShaderCompileTarget.Ps3));
            Assert.Null(Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT"));
            Assert.Null(Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT"));
        } finally {
            Environment.SetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT", oldRepositoryRoot);
            Environment.SetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT", oldShaderWorkRoot);
        }
    }

    /// <summary>Checks the injected cooker overload is required and receives the builder's cook request.</summary>
    [Fact]
    public void Builder_InjectedCookerIsRequiredAndHandlesArtifactRequests() {
        Assert.Throws<ArgumentNullException>(() => new Ps3PlatformAssetBuilder((IPs3NativeBuildExecutor)null));
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object cooker = CreateCooker(workingRoot => {
            factoryCalls++;
            return backend;
        });
        Ps3PlatformAssetBuilder builder = CreateBuilder(new NativeBuildExecutorStub(), cooker);
        string sourceDirectory = Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "source");
        Directory.CreateDirectory(sourceDirectory);
        string sourcePath = Path.Combine(sourceDirectory, "shader.hlsl");
        PlatformShaderDependency dependency = new("builder-asset", "Vertex", "Pixel", "default");
        PlatformShaderArtifactCookSource source = new("builder-asset", "builder-source-hash", "source snapshot", sourcePath);
        PlatformShaderArtifactCookRequest request = new(
            Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "cook"),
            "ps3",
            "release",
            "ps3-standard",
            [dependency],
            [source], helengine.NumericPrecisionPolicy.Default);

        PlatformShaderArtifactCookResult result = Assert.IsAssignableFrom<IPlatformShaderArtifactBuilder>((object)builder).CookShaderArtifacts(request);

        Assert.Single(result.CookedArtifactDeclarations);
        Assert.Equal(1, factoryCalls);
        Assert.Equal(2, backend.Requests.Count);
    }

    /// <summary>Checks lazy metadata access has no factory side effect and compile forwards the exact arguments once created.</summary>
    [Fact]
    public void LazyBackend_ExposesCapabilitiesWithoutFactoryAndForwardsCompile() {
        Ps3ShaderCookBackendStub backend = new();
        int factoryCalls = 0;
        object lazy = CreateLazyBackend(() => {
            factoryCalls++;
            return backend;
        });
        IShaderBackend sharedBackend = Assert.IsAssignableFrom<IShaderBackend>(lazy);
        IShaderBackendCachePolicy cachePolicy = Assert.IsAssignableFrom<IShaderBackendCachePolicy>(lazy);

        Assert.Equal(ShaderCompileTarget.Ps3, sharedBackend.Target);
        Assert.Equal(3, sharedBackend.Capabilities.MinimumShaderModel.Major);
        Assert.Equal(0, sharedBackend.Capabilities.MinimumShaderModel.Minor);
        Assert.Equal(5, sharedBackend.Capabilities.MaximumShaderModel.Major);
        Assert.Equal(0, sharedBackend.Capabilities.MaximumShaderModel.Minor);
        Assert.Equal(new[] { ShaderStage.Vertex, ShaderStage.Pixel }, sharedBackend.Capabilities.SupportedStages);
        Assert.False(sharedBackend.Capabilities.SupportsRayTracing);
        Assert.False(cachePolicy.UseSharedCache);
        Assert.Equal(0, factoryCalls);

        ShaderCompileRequest request = CreateCompileRequest();
        IShaderIncludeResolver resolver = new Ps3ShaderIncludeResolverStub();
        ShaderCompileResult first = CompileLazy(lazy, request, resolver);
        ShaderCompileResult second = CompileLazy(lazy, request, resolver);

        Assert.IsType<Ps3ShaderCompileResult>(first);
        Assert.IsType<Ps3ShaderCompileResult>(second);
        Assert.Equal(1, factoryCalls);
        Assert.Equal(2, backend.Requests.Count);
        Assert.Same(request, backend.Requests[0]);
        Assert.Same(request, backend.Requests[1]);
        Assert.Same(resolver, backend.Resolvers[0]);
        Assert.Same(resolver, backend.Resolvers[1]);
    }

    /// <summary>Checks lazy construction rejects a null backend and a backend for a different target on first compile.</summary>
    [Fact]
    public void LazyBackend_RejectsMissingOrWrongTargetBackend() {
        object nullLazy = CreateLazyBackend(() => null);
        InvalidOperationException nullException = Assert.Throws<InvalidOperationException>(
            () => CompileLazy(nullLazy, CreateCompileRequest(), new Ps3ShaderIncludeResolverStub()));
        Assert.Contains("backend", nullException.Message, StringComparison.OrdinalIgnoreCase);

        Ps3ShaderCookBackendStub wrongTarget = new() { Target = ShaderCompileTarget.Vulkan };
        object wrongTargetLazy = CreateLazyBackend(() => wrongTarget);
        InvalidOperationException targetException = Assert.Throws<InvalidOperationException>(
            () => CompileLazy(wrongTargetLazy, CreateCompileRequest(), new Ps3ShaderIncludeResolverStub()));
        Assert.Contains("Ps3", targetException.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Checks the public lazy backend requires its explicit work-root variable only when Compile is invoked.</summary>
    [Fact]
    public void LazyBackend_DefaultConfigurationFailsVisiblyOnlyOnCompile() {
        string oldShaderWorkRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT");
        try {
            Environment.SetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT", null);
            object lazy = CreateDefaultLazyBackend();
            IShaderBackend sharedBackend = Assert.IsAssignableFrom<IShaderBackend>(lazy);

            Assert.Equal(ShaderCompileTarget.Ps3, sharedBackend.Target);
            Assert.Equal(2, sharedBackend.Capabilities.SupportedStages.Count);
            InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
                () => CompileLazy(lazy, CreateCompileRequest(), new Ps3ShaderIncludeResolverStub()));
            Assert.Contains("HELENGINE_PS3_SHADER_WORK_ROOT", exception.Message, StringComparison.Ordinal);
        } finally {
            Environment.SetEnvironmentVariable("HELENGINE_PS3_SHADER_WORK_ROOT", oldShaderWorkRoot);
        }
    }

    /// <summary>Checks the production factory names its missing repository-root configuration without using cwd.</summary>
    [Fact]
    public void BackendFactory_RequiresExplicitRepositoryRoot() {
        string workRoot = Path.Combine(GetFixtureRoot(), Guid.NewGuid().ToString("N"), "work");
        string oldRepositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        try {
            Environment.SetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT", null);
            Type factoryType = BuilderAssembly.GetType("helengine.ps3.builder.Ps3ShaderBackendFactory");
            Assert.NotNull(factoryType);
            MethodInfo create = factoryType.GetMethod("Create", BindingFlags.Public | BindingFlags.Static,
                binder: null, types: [typeof(string)], modifiers: null);
            Assert.NotNull(create);

            InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Invoke(create, null, [workRoot]));

            Assert.Contains("HELENGINE_PS3_REPOSITORY_ROOT", exception.Message, StringComparison.Ordinal);
        } finally {
            Environment.SetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT", oldRepositoryRoot);
        }
    }

    /// <summary>Creates a cooker with its public factory-injection constructor for builder composition tests.</summary>
    /// <param name="backendFactory">Test backend creator.</param>
    /// <returns>The injected cooker instance.</returns>
    static object CreateCooker(Func<string, IShaderBackend> backendFactory) {
        Type cookerType = BuilderAssembly.GetType("helengine.ps3.builder.Ps3ShaderArtifactCooker");
        Assert.NotNull(cookerType);
        object cooker = Activator.CreateInstance(cookerType, [backendFactory]);
        Assert.NotNull(cooker);
        return cooker;
    }

    /// <summary>Creates a builder through the planned executor-plus-cooker constructor.</summary>
    /// <param name="executor">Injected native build executor.</param>
    /// <param name="cooker">Injected shader artifact cooker.</param>
    /// <returns>The composed builder.</returns>
    static Ps3PlatformAssetBuilder CreateBuilder(IPs3NativeBuildExecutor executor, object cooker) {
        ConstructorInfo constructor = typeof(Ps3PlatformAssetBuilder).GetConstructor([typeof(IPs3NativeBuildExecutor), cooker.GetType()]);
        Assert.NotNull(constructor);
        try {
            return Assert.IsType<Ps3PlatformAssetBuilder>(constructor.Invoke([executor, cooker]));
        } catch (TargetInvocationException exception) when (exception.InnerException != null) {
            ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
            throw;
        }
    }

    /// <summary>Creates a lazy backend through its internal test factory constructor.</summary>
    /// <param name="backendFactory">Backend factory recorded by this test.</param>
    /// <returns>The lazy backend instance.</returns>
    static object CreateLazyBackend(Func<IShaderBackend> backendFactory) {
        Type backendType = BuilderAssembly.GetType("helengine.ps3.builder.Ps3LazyShaderBackend");
        Assert.NotNull(backendType);
        ConstructorInfo constructor = backendType.GetConstructor(
            BindingFlags.Instance | BindingFlags.NonPublic,
            binder: null,
            types: [typeof(Func<IShaderBackend>)],
            modifiers: null);
        Assert.NotNull(constructor);
        return constructor.Invoke([backendFactory]);
    }

    /// <summary>Creates the production-configured lazy backend through its public parameterless constructor.</summary>
    /// <returns>The lazy backend without a resolved SDK toolchain.</returns>
    static object CreateDefaultLazyBackend() {
        Type backendType = BuilderAssembly.GetType("helengine.ps3.builder.Ps3LazyShaderBackend");
        Assert.NotNull(backendType);
        object backend = Activator.CreateInstance(backendType);
        Assert.NotNull(backend);
        return backend;
    }

    /// <summary>Invokes lazy Compile and unwraps its production exception.</summary>
    /// <param name="backend">Lazy PS3 backend adapter.</param>
    /// <param name="request">Exact shared request to forward.</param>
    /// <param name="resolver">Exact include resolver to forward.</param>
    /// <returns>Shared PS3 compilation result.</returns>
    static ShaderCompileResult CompileLazy(object backend, ShaderCompileRequest request, IShaderIncludeResolver resolver) {
        MethodInfo compile = backend.GetType().GetMethod("Compile", BindingFlags.Instance | BindingFlags.Public,
            binder: null, types: [typeof(ShaderCompileRequest), typeof(IShaderIncludeResolver)], modifiers: null);
        Assert.NotNull(compile);
        return Assert.IsAssignableFrom<ShaderCompileResult>(Invoke(compile, backend, [request, resolver]));
    }

    /// <summary>Invokes one reflected method while preserving its original exception type and message.</summary>
    /// <param name="method">Method to invoke.</param>
    /// <param name="instance">Instance target, or null for a static method.</param>
    /// <param name="arguments">Ordered method arguments.</param>
    /// <returns>Invocation result.</returns>
    static object Invoke(MethodInfo method, object instance, object[] arguments) {
        try {
            return method.Invoke(instance, arguments);
        } catch (TargetInvocationException exception) when (exception.InnerException != null) {
            ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
            throw;
        }
    }

    /// <summary>Creates a valid vertex shared request for direct lazy-backend contract checks.</summary>
    /// <returns>A PS3 vertex request with explicit source and compile options.</returns>
    static ShaderCompileRequest CreateCompileRequest() => new(
        new ShaderSourceInfo(Path.Combine(GetFixtureRoot(), "lazy-probe.hlsl"), "float4 VS() : SV_Position { return 0.0; }"),
        "LazyProbe",
        "VS",
        ShaderStage.Vertex,
        ShaderCompileTarget.Ps3,
        new ShaderModel(4, 0),
        "default",
        ShaderPlatformDefines.BuildDefines(ShaderCompileTarget.Ps3, new ShaderModel(4, 0), Array.Empty<ShaderDefine>()),
        new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));

    /// <summary>Resolves explicit fixture storage without using the host temporary directory.</summary>
    /// <returns>Validated task-owned fixture storage.</returns>
    static string GetFixtureRoot() {
        string outputRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        string repositoryRoot = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(outputRoot), "HELENGINE_PS3_SHADER_TEST_OUTPUT must name the cooker output root.");
        Assert.False(string.IsNullOrWhiteSpace(repositoryRoot), "HELENGINE_PS3_REPOSITORY_ROOT must name the PS3 checkout.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(outputRoot, repositoryRoot);
        string fixtureRoot = Path.Combine(validatedRoot, "fixture-storage");
        Directory.CreateDirectory(fixtureRoot);
        return fixtureRoot;
    }

    /// <summary>Suppresses native execution if an unrelated builder test explicitly invokes BuildAsync.</summary>
    sealed class NativeBuildExecutorStub : IPs3NativeBuildExecutor {
        /// <summary>Rejects unexpected native execution because registration must remain side-effect free.</summary>
        /// <param name="request">Platform build request.</param>
        /// <param name="generatedCoreRoot">Generated Core source root.</param>
        /// <param name="contentRoot">Staged content root.</param>
        /// <param name="cancellationToken">Cancellation token.</param>
        public void Build(PlatformBuildRequest request, string generatedCoreRoot, string contentRoot, CancellationToken cancellationToken) {
            throw new InvalidOperationException("Native execution is outside this registration test.");
        }
    }
}
