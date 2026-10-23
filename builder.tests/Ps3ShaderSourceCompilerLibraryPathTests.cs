using System.Runtime.InteropServices;

namespace helengine.ps3.builder.tests;

/// <summary>Checks SPIRV-Cross host RID selection without changing its package version or shader translation.</summary>
public sealed class Ps3ShaderSourceCompilerLibraryPathTests {
    /// <summary>Selects the native library filename and architecture used by each supported host.</summary>
    /// <param name="host">NuGet runtime operating-system prefix.</param>
    /// <param name="architecture">Architecture of the managed compiler process.</param>
    /// <param name="rid">Expected package runtime identifier.</param>
    /// <param name="fileName">Expected native library filename.</param>
    [Theory]
    [InlineData("win", Architecture.X64, "win-x64", "spirv-cross.dll")]
    [InlineData("win", Architecture.Arm64, "win-arm64", "spirv-cross.dll")]
    [InlineData("osx", Architecture.X64, "osx-x64", "libspirv-cross.dylib")]
    [InlineData("osx", Architecture.Arm64, "osx-arm64", "libspirv-cross.dylib")]
    [InlineData("linux", Architecture.X64, "linux-x64", "libspirv-cross.so")]
    [InlineData("linux", Architecture.Arm64, "linux-arm64", "libspirv-cross.so")]
    public void ResolvesLibraryForHost(string host, Architecture architecture, string rid, string fileName) {
        string root = OperatingSystem.IsWindows() ? @"C:\temp\helengine\ps3\external plugins" : "/tmp/helengine/ps3/external plugins";
        string actual = Ps3ShaderSourceCompiler.ResolveSpirvCrossLibraryPath(Path.Combine(root, "helengine.ps3.builder.dll"), host, architecture);
        Assert.Equal(Path.Combine(root, "runtimes", rid, "native", fileName), actual);
    }

    /// <summary>Confirms the unmodified native package actually ships the required Apple Silicon binary.</summary>
    [Fact]
    public void PackageContainsMacArm64MachOLibrary() {
        string path = Path.Combine(AppContext.BaseDirectory, "runtimes", "osx-arm64", "native", "libspirv-cross.dylib");
        Assert.True(File.Exists(path), "Silk.NET.SPIRV.Cross.Native must preserve its packaged osx-arm64 asset in the builder output.");
        using FileStream stream = File.OpenRead(path);
        byte[] header = new byte[8];
        stream.ReadExactly(header);
        Assert.Equal(0xfeedfacfU, System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(header));
        Assert.Equal(0x0100000cU, System.Buffers.Binary.BinaryPrimitives.ReadUInt32LittleEndian(header.AsSpan(4)));
    }
}
