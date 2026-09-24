using helengine.ps3.builder;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies SPIRV-Cross resolves beside the registered compiler assembly.</summary>
public sealed class Ps3ShaderCompilerLibraryPathTests {
    /// <summary>Preserves the plugin directory, including spaces, when selecting its packaged native library.</summary>
    [Fact]
    public void ResolvesLibraryBesideExternalCompilerAssembly() {
        string actual = Ps3ShaderSourceCompiler.ResolveSpirvCrossLibraryPath(
            @"C:\external plugins\PS3 builder\helengine.ps3.builder.dll");

        Assert.Equal(@"C:\external plugins\PS3 builder\runtimes\win-x64\native\spirv-cross.dll", actual);
    }

    /// <summary>Rejects missing, relative, drive-relative, and directory-only compiler assembly locations.</summary>
    /// <param name="compilerAssemblyLocation">Invalid location that cannot identify a fully qualified assembly file.</param>
    [Theory]
    [InlineData(null)]
    [InlineData("")]
    [InlineData("   ")]
    [InlineData("builder.dll")]
    [InlineData("C:builder.dll")]
    [InlineData(@"C:\external plugins\PS3 builder\")]
    public void RejectsInvalidCompilerAssemblyLocation(string compilerAssemblyLocation) {
        Assert.Throws<ArgumentException>(() =>
            Ps3ShaderSourceCompiler.ResolveSpirvCrossLibraryPath(compilerAssemblyLocation));
    }
}
