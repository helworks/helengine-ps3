using helengine;
using System.Diagnostics;

namespace helengine.ps3.builder;

/// <summary>Builds a shell-free Docker invocation for one uniquely owned locked-SDK shader job.</summary>
public sealed class Ps3DockerShaderCommand {
    /// <summary>Initializes a Docker command using explicit validated toolchain and job paths.</summary>
    /// <param name="toolchain">Validated image and Cg mount configuration.</param>
    /// <param name="stage">Vertex or pixel shader stage.</param>
    /// <param name="containerName">Unique Docker container identity owned by this job.</param>
    /// <param name="jobDirectory">Absolute host directory mounted at /work for this isolated job.</param>
    public Ps3DockerShaderCommand(Ps3ShaderToolchain toolchain, ShaderStage stage, string containerName, string jobDirectory) {
        ArgumentNullException.ThrowIfNull(toolchain);
        if (stage != ShaderStage.Vertex && stage != ShaderStage.Pixel) throw new NotSupportedException($"RSX compiler stage '{stage}' is unsupported.");
        if (string.IsNullOrWhiteSpace(containerName) || containerName.Any(character => !char.IsAsciiLetterOrDigit(character) && character != '-' && character != '_')) {
            throw new ArgumentException("Docker container name must contain only ASCII letters, digits, dash and underscore.", nameof(containerName));
        }
        if (string.IsNullOrWhiteSpace(jobDirectory) || !Path.IsPathRooted(jobDirectory)) {
            throw new ArgumentException("An explicit absolute job directory is required.", nameof(jobDirectory));
        }
        Toolchain = toolchain;
        Stage = stage;
        ContainerName = containerName;
        JobDirectory = Path.GetFullPath(jobDirectory);
    }

    /// <summary>Gets the exact unique container identity used for cancellation and test observability.</summary>
    public string ContainerName { get; }

    /// <summary>Gets the isolated absolute host job directory mounted at /work.</summary>
    public string JobDirectory { get; }

    /// <summary>Creates a Docker process start description with every argument kept as a distinct value.</summary>
    /// <returns>Configured shell-free Docker process start information.</returns>
    public ProcessStartInfo CreateStartInfo() {
        ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true,
            WorkingDirectory = JobDirectory
        };
        Add(startInfo, "run");
        Add(startInfo, "--pull=never");
        Add(startInfo, "--rm");
        Add(startInfo, "--name");
        Add(startInfo, ContainerName);
        Add(startInfo, "--network");
        Add(startInfo, "none");
        Add(startInfo, "--mount");
        Add(startInfo, $"type=bind,source={Toolchain.CgMountSource},target={Toolchain.CgMountTarget},readonly");
        Add(startInfo, "--mount");
        Add(startInfo, $"type=bind,source={JobDirectory},target=/work");
        Add(startInfo, "--env");
        Add(startInfo, $"LD_LIBRARY_PATH={Toolchain.CgLibraryDirectory}");
        Add(startInfo, Toolchain.ImageId);
        Add(startInfo, Toolchain.CompilerPath);
        Add(startInfo, Stage == ShaderStage.Vertex ? "-v" : "-f");
        Add(startInfo, "-d");
        Add(startInfo, "-Wcg,-oglsl");
        Add(startInfo, "/work/source.glsl");
        Add(startInfo, "/work/program.bin");
        return startInfo;
    }

    /// <summary>Adds one literal argument without joining or shell-escaping it.</summary>
    /// <param name="startInfo">Docker process start description.</param>
    /// <param name="argument">One complete Docker CLI argument.</param>
    static void Add(ProcessStartInfo startInfo, string argument) {
        startInfo.ArgumentList.Add(argument);
    }

    /// <summary>Stores the validated toolchain used to construct the immutable invocation arguments.</summary>
    Ps3ShaderToolchain Toolchain { get; }

    /// <summary>Stores the requested vertex or pixel stage.</summary>
    ShaderStage Stage { get; }
}
