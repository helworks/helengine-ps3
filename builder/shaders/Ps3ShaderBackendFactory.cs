using helengine;

namespace helengine.ps3.builder;

/// <summary>Creates the production PS3 backend from the explicitly configured repository toolchain.</summary>
public static class Ps3ShaderBackendFactory {
    /// <summary>Name of the environment variable that identifies the PS3 repository checkout.</summary>
    public const string RepositoryRootEnvironmentVariable = "HELENGINE_PS3_REPOSITORY_ROOT";

    /// <summary>Loads the locked SDK configuration and creates a backend rooted in the caller's build-owned directory.</summary>
    /// <param name="workingRoot">Fully qualified directory parent used for compiler jobs.</param>
    /// <returns>A production HLSL-to-RSX shared shader backend.</returns>
    /// <exception cref="ArgumentException">Thrown when the working root is not fully qualified.</exception>
    /// <exception cref="InvalidOperationException">Thrown when the repository root is not configured explicitly.</exception>
    public static IShaderBackend Create(string workingRoot) {
        if (string.IsNullOrWhiteSpace(workingRoot)) {
            throw new ArgumentException("A fully qualified PS3 shader working root must be provided.", nameof(workingRoot));
        }
        if (!Path.IsPathFullyQualified(workingRoot)) {
            throw new ArgumentException("PS3 shader working root must be fully qualified.", nameof(workingRoot));
        }

        string repositoryRoot = Environment.GetEnvironmentVariable(RepositoryRootEnvironmentVariable);
        if (string.IsNullOrWhiteSpace(repositoryRoot)) {
            throw new InvalidOperationException($"Environment variable {RepositoryRootEnvironmentVariable} must name the PS3 repository root.");
        }
        if (!Path.IsPathFullyQualified(repositoryRoot)) {
            throw new InvalidOperationException($"Environment variable {RepositoryRootEnvironmentVariable} must contain a fully qualified path.");
        }

        Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(Path.GetFullPath(repositoryRoot));
        return new Ps3ShaderBackend(
            new Ps3ShaderSourceCompiler(),
            new Ps3DockerShaderCompiler(toolchain),
            Path.GetFullPath(workingRoot));
    }
}
