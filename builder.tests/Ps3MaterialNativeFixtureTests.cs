using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Security.Cryptography;
using System.Text;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Produces the real PS3 standard material and shader pair consumed by the native reader gate.</summary>
public sealed class Ps3MaterialNativeFixtureTests {
    /// <summary>Directory containing the shared engine checkout and native PS3 build outputs.</summary>
    const string NativeMaterialBuildRoot = @"C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material";

    /// <summary>Expected canonical P3SP path for the fixed ForwardStandard identity.</summary>
    const string ExpectedShaderRelativePath = "shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp";

    /// <summary>Produces actual cooker output under explicit repository, engine and fixture roots.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void ProducesNativeMaterialFixture() {
        string repositoryRoot = RequiredDirectoryEnvironment("HELENGINE_PS3_REPOSITORY_ROOT");
        string helengineRoot = RequiredDirectoryEnvironment("HELENGINE_ROOT");
        string fixtureRoot = RequiredDirectoryEnvironment("HELENGINE_PS3_MATERIAL_NATIVE_FIXTURE_ROOT");
        AssertStrictChild(fixtureRoot, NativeMaterialBuildRoot, "HELENGINE_PS3_MATERIAL_NATIVE_FIXTURE_ROOT");
        Assert.True(File.Exists(Path.Combine(repositoryRoot, "toolchain-lock.json")),
            "HELENGINE_PS3_REPOSITORY_ROOT must name the PS3 checkout containing toolchain-lock.json.");
        Assert.Empty(Directory.EnumerateFileSystemEntries(fixtureRoot));

        const string materialAssetId = "Materials/Native-\u00E9";
        const string textureRelativePath = "cooked/textures/fixture.hasset";
        string shaderSourcePath = Path.Combine(
            helengineRoot,
            "engine",
            "helengine.editor",
            "shaders",
            "builtin",
            "ForwardStandardShader.hlsl");
        Assert.True(File.Exists(shaderSourcePath), $"Shared ForwardStandardShader source does not exist: {shaderSourcePath}");
        string shaderSource = new UTF8Encoding(false, true).GetString(File.ReadAllBytes(shaderSourcePath));
        string shaderSourceHash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(shaderSource))).ToLowerInvariant();

        Dictionary<string, string> fields = new(StringComparer.OrdinalIgnoreCase) {
            [Ps3MaterialSchemaIds.BaseColorFieldId] = "#C0201040",
            [Ps3MaterialSchemaIds.LightingModeFieldId] = Ps3MaterialSchemaIds.LightingLitValue,
            [Ps3MaterialSchemaIds.DoubleSidedFieldId] = "true",
            [Ps3MaterialSchemaIds.VertexColorModeFieldId] = Ps3MaterialSchemaIds.VertexColorModulateValue,
            [Ps3MaterialSchemaIds.TextureRelativePathFieldId] = textureRelativePath
        };
        PlatformMaterialCookRequest materialRequest = new(
            materialAssetId,
            materialAssetId + ".hasset",
            "ps3",
            "release",
            Ps3MaterialSchemaIds.StandardGraphicsProfileId,
            Ps3MaterialSchemaIds.StandardSchemaId,
            fields);
        PlatformMaterialCookResult materialResult = new Ps3MaterialCooker().Cook(materialRequest);
        PlatformShaderDependency shaderDependency = Assert.Single(materialResult.ReferencedShaderDependencies);

        Ps3ShaderArtifactIdentity identity = new(
            shaderDependency.ShaderAssetId,
            shaderDependency.VertexProgramName,
            shaderDependency.PixelProgramName,
            shaderDependency.VariantName);
        Assert.Equal(ExpectedShaderRelativePath, identity.RelativePath);

        PlatformShaderArtifactCookSource shaderSourceSnapshot = new(
            shaderDependency.ShaderAssetId,
            shaderSourceHash,
            shaderSource,
            shaderSourcePath);
        PlatformShaderArtifactCookRequest shaderRequest = new(
            fixtureRoot,
            "ps3",
            "release",
            Ps3MaterialSchemaIds.StandardGraphicsProfileId,
            [shaderDependency],
            [shaderSourceSnapshot]);
        PlatformShaderArtifactCookResult shaderResult = CookShaderArtifacts(shaderRequest);
        PlatformCookedArtifactDeclaration shaderArtifact = Assert.Single(shaderResult.CookedArtifactDeclarations);
        Assert.Equal(identity.RelativePath, shaderArtifact.RelativePath);
        Assert.True(File.Exists(ResolveFixturePath(fixtureRoot, shaderArtifact.RelativePath)),
            "The production shader artifact cooker did not publish the declared P3SP file.");

        WriteNewFile(Path.Combine(fixtureRoot, "material.p3mt"), materialResult.CookedMaterialBytes);
    }

    /// <summary>Creates the production shader artifact cooker through its public parameterless constructor.</summary>
    /// <param name="request">Cook request containing the material's sole shader dependency and real source snapshot.</param>
    /// <returns>The production cooker's declarations after real SDK-backed compilation.</returns>
    static PlatformShaderArtifactCookResult CookShaderArtifacts(PlatformShaderArtifactCookRequest request) {
        Assembly builderAssembly = typeof(Ps3PlatformAssetBuilder).Assembly;
        Type cookerType = builderAssembly.GetType("helengine.ps3.builder.Ps3ShaderArtifactCooker", throwOnError: true);
        object cooker = Activator.CreateInstance(cookerType);
        Assert.NotNull(cooker);
        MethodInfo cookMethod = cookerType.GetMethod(
            "Cook",
            BindingFlags.Instance | BindingFlags.Public,
            binder: null,
            types: [typeof(PlatformShaderArtifactCookRequest)],
            modifiers: null);
        Assert.NotNull(cookMethod);
        try {
            return Assert.IsType<PlatformShaderArtifactCookResult>(cookMethod.Invoke(cooker, [request]));
        } catch (TargetInvocationException exception) when (exception.InnerException != null) {
            ExceptionDispatchInfo.Capture(exception.InnerException).Throw();
            throw;
        }
    }

    /// <summary>Requires an explicit, fully-qualified directory environment value.</summary>
    /// <param name="name">Environment variable name used by the Docker fixture run.</param>
    /// <returns>The normalized existing directory path.</returns>
    static string RequiredDirectoryEnvironment(string name) {
        string value = Environment.GetEnvironmentVariable(name);
        Assert.False(string.IsNullOrWhiteSpace(value), $"Set {name} explicitly for the native material fixture.");
        Assert.True(Path.IsPathFullyQualified(value), $"{name} must be fully qualified: {value}");
        string fullPath = Path.GetFullPath(value);
        Assert.True(Directory.Exists(fullPath), $"{name} directory does not exist: {fullPath}");
        return fullPath;
    }

    /// <summary>Verifies a directory is strictly below its required parent directory.</summary>
    /// <param name="childPath">Directory that will hold fixture output.</param>
    /// <param name="parentPath">Required native-material build root.</param>
    /// <param name="label">Path label included in assertion failures.</param>
    static void AssertStrictChild(string childPath, string parentPath, string label) {
        string parent = Path.GetFullPath(parentPath).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string child = Path.GetFullPath(childPath).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        string strictPrefix = parent + Path.DirectorySeparatorChar;
        Assert.True(child.StartsWith(strictPrefix, StringComparison.OrdinalIgnoreCase),
            $"{label} must be a strict child of {parent}; found {child}.");
    }

    /// <summary>Resolves a producer-declared slash-separated artifact path beneath the fixture root.</summary>
    /// <param name="fixtureRoot">Root passed to the production shader cooker.</param>
    /// <param name="relativePath">Validated relative artifact path from its declaration.</param>
    /// <returns>Absolute artifact path under the fixture root.</returns>
    static string ResolveFixturePath(string fixtureRoot, string relativePath) {
        string localPath = relativePath.Replace('/', Path.DirectorySeparatorChar);
        string resolvedPath = Path.GetFullPath(Path.Combine(fixtureRoot, localPath));
        AssertStrictChild(resolvedPath, fixtureRoot, nameof(relativePath));
        return resolvedPath;
    }

    /// <summary>Writes newly produced payload bytes without replacing any existing fixture file.</summary>
    /// <param name="path">New material payload path.</param>
    /// <param name="bytes">Bytes returned by the production material cooker.</param>
    static void WriteNewFile(string path, byte[] bytes) {
        using FileStream stream = new(path, FileMode.CreateNew, FileAccess.Write, FileShare.None);
        stream.Write(bytes);
    }
}
