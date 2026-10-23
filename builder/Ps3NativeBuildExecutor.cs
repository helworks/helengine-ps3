using System.Diagnostics;
using System.Globalization;
using System.Text.RegularExpressions;
using helengine.baseplatform.Requests;

namespace helengine.ps3.builder;

/// <summary>Runs the pinned PowerShell and Docker native build in an explicit source checkout.</summary>
public sealed class Ps3NativeBuildExecutor : IPs3NativeBuildExecutor {
    /// <summary>Name of the environment variable naming the PlayStation 3 source checkout.</summary>
    public const string RepositoryRootVariable = "HELENGINE_PS3_REPOSITORY_ROOT";

    /// <summary>Name of the environment variable overriding the PowerShell executable.</summary>
    public const string PowerShellVariable = "HELENGINE_PS3_POWERSHELL";

    /// <summary>Name of the optional environment variable selecting the native run duration in seconds.</summary>
    public const string RunSecondsVariable = "HELENGINE_PS3_NATIVE_RUN_SECONDS";

    /// <summary>Name of the optional stable native build directory, created first and verified for later in-place rebuilds.</summary>
    public const string ActiveBuildIdVariable = "HELENGINE_PS3_ACTIVE_BUILD_ID";

    /// <summary>Names an optional external SDK/Cg state root forwarded to the native script.</summary>
    public const string StateRootVariable = "HELENGINE_PS3_STATE_ROOT";

    /// <summary>Builds a Core-mode executable from the supplied generated Core root.</summary>
    /// <param name="request">Complete platform request.</param>
    /// <param name="generatedCoreRoot">Root of the generated Core C++ tree.</param>
    /// <param name="contentRoot">Staged cooked content installed beside the executable.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    /// <exception cref="ArgumentNullException">Thrown when the request is missing.</exception>
    /// <exception cref="ArgumentException">Thrown when the generated Core root is missing.</exception>
    /// <exception cref="InvalidOperationException">Thrown when the checkout is unset, the build fails, or the outputs are absent.</exception>
    public void Build(PlatformBuildRequest request, string generatedCoreRoot, string contentRoot, CancellationToken cancellationToken) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (string.IsNullOrWhiteSpace(generatedCoreRoot)) {
            throw new ArgumentException("Generated Core root is required.", nameof(generatedCoreRoot));
        } else if (string.IsNullOrWhiteSpace(contentRoot)) {
            throw new ArgumentException("Staged content root is required.", nameof(contentRoot));
        }
        cancellationToken.ThrowIfCancellationRequested();

        string repositoryRoot = Environment.GetEnvironmentVariable(RepositoryRootVariable);
        if (string.IsNullOrWhiteSpace(repositoryRoot)) {
            throw new InvalidOperationException($"Set {RepositoryRootVariable} to the PlayStation 3 source checkout before invoking the native executor.");
        }
        repositoryRoot = Path.GetFullPath(repositoryRoot);
        string buildScript = Path.Combine(repositoryRoot, "scripts", "build.ps1");
        if (!File.Exists(buildScript)) {
            throw new FileNotFoundException($"PlayStation 3 native build script was not found in {RepositoryRootVariable}.", buildScript);
        }
        if (!Directory.Exists(generatedCoreRoot)) {
            throw new InvalidOperationException($"Generated Core root does not exist: {generatedCoreRoot}");
        }

        string powershell = Environment.GetEnvironmentVariable(PowerShellVariable) ?? "powershell";
        string activeBuildId = Environment.GetEnvironmentVariable(ActiveBuildIdVariable);
        if (activeBuildId != null && !Regex.IsMatch(activeBuildId, "^[A-Za-z0-9][A-Za-z0-9._-]*$")) {
            throw new InvalidOperationException($"{ActiveBuildIdVariable} must be one safe existing build directory name.");
        }
        string buildId = activeBuildId ?? $"core-{Guid.NewGuid():N}";
        ProcessStartInfo startInfo = new(powershell) {
            WorkingDirectory = repositoryRoot,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add("-NoProfile");
        startInfo.ArgumentList.Add("-File");
        startInfo.ArgumentList.Add(buildScript);
        startInfo.ArgumentList.Add("-BuildId");
        startInfo.ArgumentList.Add(buildId);
        if (activeBuildId != null && Directory.Exists(Path.Combine(request.OutputRoot, buildId))) {
            startInfo.ArgumentList.Add("-ReuseBuild");
        }
        startInfo.ArgumentList.Add("-Mode");
        startInfo.ArgumentList.Add("Core");
        startInfo.ArgumentList.Add("-OutputRoot");
        startInfo.ArgumentList.Add(request.OutputRoot);
        startInfo.ArgumentList.Add("-GeneratedCoreRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(generatedCoreRoot));
        startInfo.ArgumentList.Add("-ContentRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(contentRoot));
        AppendStateRootArgument(startInfo, Environment.GetEnvironmentVariable(StateRootVariable));
        AppendRunDurationArgument(startInfo, Environment.GetEnvironmentVariable(RunSecondsVariable));

        using Process process = new() { StartInfo = startInfo, EnableRaisingEvents = true };
        if (!process.Start()) {
            throw new InvalidOperationException("Failed to start the PlayStation 3 native build process.");
        }
        try {
            process.WaitForExitAsync(cancellationToken).GetAwaiter().GetResult();
        } catch (OperationCanceledException) {
            try {
                process.Kill(entireProcessTree: true);
            } catch (InvalidOperationException) {
            }
            throw;
        }
        if (process.ExitCode != 0) {
            throw new InvalidOperationException($"PlayStation 3 native build failed with exit code {process.ExitCode}.");
        }

        string buildRoot = Path.Combine(request.OutputRoot, buildId);
        if (!File.Exists(Path.Combine(buildRoot, "helengine_ps3.self")) ||
            !File.Exists(Path.Combine(buildRoot, "build-manifest.json"))) {
            throw new InvalidOperationException($"PlayStation 3 native build completed without a verified output manifest: {buildRoot}");
        }
    }

    /// <summary>Forwards an explicit SDK state root without changing repository-local defaults.</summary>
    internal static void AppendStateRootArgument(ProcessStartInfo startInfo, string configuredValue) {
        ArgumentNullException.ThrowIfNull(startInfo);
        if (string.IsNullOrEmpty(configuredValue)) return;
        if (!Path.IsPathFullyQualified(configuredValue)) {
            throw new InvalidOperationException($"{StateRootVariable} must be an absolute directory path.");
        }
        startInfo.ArgumentList.Add("-StateRoot");
        startInfo.ArgumentList.Add(Path.GetFullPath(configuredValue));
    }

    /// <summary>Appends the optional validated native run duration as two process argument tokens.</summary>
    internal static void AppendRunDurationArgument(ProcessStartInfo startInfo, string configuredValue) {
        ArgumentNullException.ThrowIfNull(startInfo);
        if (string.IsNullOrEmpty(configuredValue)) {
            return;
        }
        if (!uint.TryParse(configuredValue, NumberStyles.None, CultureInfo.InvariantCulture, out uint seconds)
            || seconds > 3600u) {
            throw new InvalidOperationException($"{RunSecondsVariable} must be an unsigned decimal integer from 0 through 3600.");
        }
        startInfo.ArgumentList.Add("-RunSeconds");
        startInfo.ArgumentList.Add(seconds.ToString(CultureInfo.InvariantCulture));
    }
}
