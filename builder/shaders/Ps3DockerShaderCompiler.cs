using helengine;
using System.Diagnostics;
using System.Text;

namespace helengine.ps3.builder;

/// <summary>Runs cgcomp in an isolated, network-disabled container from the repository's locked SDK image.</summary>
public sealed class Ps3DockerShaderCompiler : IPs3RsxCompiler {
    /// <summary>Initializes the compiler with a previously validated locked toolchain and Cg mount.</summary>
    /// <param name="toolchain">Validated SDK image, compiler path and Cg shared library.</param>
    public Ps3DockerShaderCompiler(Ps3ShaderToolchain toolchain) {
        ArgumentNullException.ThrowIfNull(toolchain);
        Toolchain = toolchain;
    }

    /// <summary>Compiles one Task2 translation under a caller-supplied root and validates its SDK binary and dump.</summary>
    /// <param name="translation">Translated GLSL and stage metadata.</param>
    /// <param name="workingRoot">Explicit build-owned parent directory for a unique job.</param>
    /// <param name="cancellationToken">Token that terminates this job's owned container and Docker process.</param>
    /// <returns>Validated program data copied from the SDK output.</returns>
    public Ps3RsxProgram Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken) {
        ArgumentNullException.ThrowIfNull(translation);
        return CompileJobAsync(translation, workingRoot, cancellationToken).GetAwaiter().GetResult();
    }

    /// <summary>Creates a unique host job directory, invokes Docker, stores logs and validates the generated outputs.</summary>
    /// <param name="translation">Translated GLSL and stage metadata.</param>
    /// <param name="workingRoot">Explicit absolute build-owned parent directory.</param>
    /// <param name="cancellationToken">Caller cancellation token.</param>
    /// <returns>Validated SDK program.</returns>
    async Task<Ps3RsxProgram> CompileJobAsync(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken) {
        cancellationToken.ThrowIfCancellationRequested();
        if (string.IsNullOrWhiteSpace(workingRoot) || !Path.IsPathRooted(workingRoot)) {
            throw new ArgumentException("An explicit absolute shader compiler working root is required.", nameof(workingRoot));
        }
        if (translation.Stage != ShaderStage.Vertex && translation.Stage != ShaderStage.Pixel) {
            throw new NotSupportedException($"RSX compiler stage '{translation.Stage}' is unsupported.");
        }

        string fullWorkingRoot = Path.GetFullPath(workingRoot);
        Directory.CreateDirectory(fullWorkingRoot);
        string jobDirectory = CreateUniqueJobDirectory(fullWorkingRoot);
        string containerName = Path.GetFileName(jobDirectory);
        string standardOutput = string.Empty;
        string standardError = string.Empty;
        File.WriteAllText(Path.Combine(jobDirectory, "source.glsl"), translation.GlslSource, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(jobDirectory, "container-name.txt"), containerName, new UTF8Encoding(false));
        Ps3DockerShaderCommand command = new(Toolchain, translation.Stage, containerName, jobDirectory);

        try {
            using Process process = new() { StartInfo = command.CreateStartInfo() };
            if (!process.Start()) throw new InvalidOperationException("Failed to start the Docker CLI for the locked PS3 shader toolchain.");
            Task<string> standardOutputTask = process.StandardOutput.ReadToEndAsync();
            Task<string> standardErrorTask = process.StandardError.ReadToEndAsync();
            try {
                await process.WaitForExitAsync(cancellationToken).ConfigureAwait(false);
            } catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) {
                Exception cleanupFailure = await StopOwnedContainerAsync(containerName, process).ConfigureAwait(false);
                standardOutput = await standardOutputTask.ConfigureAwait(false);
                standardError = await standardErrorTask.ConfigureAwait(false);
                SaveProcessLogs(jobDirectory, standardOutput, standardError);
                if (cleanupFailure != null) {
                    throw new OperationCanceledException($"Cancellation cleanup failed for owned Docker container '{containerName}'.", cleanupFailure, cancellationToken);
                }
                throw new OperationCanceledException($"PS3 shader Docker job '{containerName}' was cancelled.", cancellationToken);
            }
            standardOutput = await standardOutputTask.ConfigureAwait(false);
            standardError = await standardErrorTask.ConfigureAwait(false);
            SaveProcessLogs(jobDirectory, standardOutput, standardError);

            if (process.ExitCode != 0) {
                throw new InvalidOperationException($"cgcomp in pinned Docker image exited with code {process.ExitCode}. See '{jobDirectory}'.{Environment.NewLine}{standardError}");
            }
            string bytecodePath = Path.Combine(jobDirectory, "program.bin");
            if (!File.Exists(bytecodePath)) throw new InvalidOperationException($"cgcomp succeeded without producing '{bytecodePath}'. See '{jobDirectory}'.");
            byte[] bytecode = File.ReadAllBytes(bytecodePath);
            if (bytecode.Length == 0) throw new InvalidOperationException($"cgcomp produced an empty RSX program at '{bytecodePath}'. See '{jobDirectory}'.");
            string dumpPath = bytecodePath + ".dump";
            if (!File.Exists(dumpPath)) throw new InvalidOperationException($"cgcomp succeeded without producing its required dump '{dumpPath}'. See '{jobDirectory}'.");
            string assembly = File.ReadAllText(dumpPath);
            if (string.IsNullOrWhiteSpace(assembly)) throw new InvalidOperationException($"cgcomp produced an empty compiler dump at '{dumpPath}'. See '{jobDirectory}'.");
            return Ps3RsxProgramReader.Read(translation.Stage, bytecode, assembly, Toolchain.Identity);
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception error) {
            if (standardError.Length == 0) standardError = error.ToString();
            SaveProcessLogs(jobDirectory, standardOutput, standardError);
            throw;
        }
    }

    /// <summary>Creates one collision-resistant child directory beneath the exact caller-supplied root.</summary>
    /// <param name="workingRoot">Normalized caller-supplied root.</param>
    /// <returns>Newly created isolated job directory.</returns>
    static string CreateUniqueJobDirectory(string workingRoot) {
        while (true) {
            string jobDirectory = Path.Combine(workingRoot, "ps3-shader-" + Guid.NewGuid().ToString("N"));
            if (Directory.Exists(jobDirectory) || File.Exists(jobDirectory)) continue;
            Directory.CreateDirectory(jobDirectory);
            return jobDirectory;
        }
    }

    /// <summary>Stores the Docker CLI's unmodified output streams inside its isolated job for inspection.</summary>
    /// <param name="jobDirectory">Owned compiler job directory.</param>
    /// <param name="standardOutput">Captured Docker stdout.</param>
    /// <param name="standardError">Captured Docker stderr.</param>
    static void SaveProcessLogs(string jobDirectory, string standardOutput, string standardError) {
        File.WriteAllText(Path.Combine(jobDirectory, "compiler.stdout.log"), standardOutput, new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(jobDirectory, "compiler.stderr.log"), standardError, new UTF8Encoding(false));
    }

    /// <summary>Stops only the unique container and Docker CLI process owned by the cancelled shader job.</summary>
    /// <param name="containerName">Unique job container name.</param>
    /// <param name="dockerRunProcess">Docker CLI process created for this job.</param>
    /// <returns>A cleanup failure to preserve alongside cancellation, or null when shutdown completed.</returns>
    static Task<Exception> StopOwnedContainerAsync(string containerName, Process dockerRunProcess) {
        return CompleteOwnedContainerCleanupAsync(
            containerName,
            () => RemoveOwnedContainerAsync(containerName),
            () => WaitForDockerRunExitAsync(dockerRunProcess),
            () => ContainerExistsAsync(containerName));
    }

    /// <summary>Removes before and after Docker CLI termination, then verifies the owned container is absent.</summary>
    /// <param name="containerName">Unique container identity owned by the cancelled compiler job.</param>
    /// <param name="removeOwnedContainerAsync">Idempotently removes only the owned container and returns any removal failure.</param>
    /// <param name="waitForDockerRunExitAsync">Waits for, and if needed terminates, this job's Docker CLI process.</param>
    /// <param name="containerExistsAsync">Checks whether the exact owned container still exists.</param>
    /// <returns>A cleanup failure, or null when the CLI ended and the container is confirmed absent.</returns>
    internal static async Task<Exception> CompleteOwnedContainerCleanupAsync(
        string containerName,
        Func<Task<Exception>> removeOwnedContainerAsync,
        Func<Task> waitForDockerRunExitAsync,
        Func<Task<bool>> containerExistsAsync) {
        ArgumentException.ThrowIfNullOrWhiteSpace(containerName);
        ArgumentNullException.ThrowIfNull(removeOwnedContainerAsync);
        ArgumentNullException.ThrowIfNull(waitForDockerRunExitAsync);
        ArgumentNullException.ThrowIfNull(containerExistsAsync);

        try {
            _ = await removeOwnedContainerAsync().ConfigureAwait(false);
        } catch {
            // Retry after Docker CLI termination because a create/start request may still be completing.
        }

        Exception waitFailure = null;
        try {
            await waitForDockerRunExitAsync().ConfigureAwait(false);
        } catch (Exception error) {
            waitFailure = error;
        }

        Exception finalRemovalFailure = null;
        try {
            finalRemovalFailure = await removeOwnedContainerAsync().ConfigureAwait(false);
        } catch (Exception error) {
            finalRemovalFailure = error;
        }

        bool containerExists = true;
        Exception inspectFailure = null;
        try {
            containerExists = await containerExistsAsync().ConfigureAwait(false);
        } catch (Exception error) {
            inspectFailure = error;
        }

        if (!containerExists && inspectFailure == null) return waitFailure;
        Exception cleanupFailure = finalRemovalFailure ?? inspectFailure ?? new InvalidOperationException($"Owned Docker container '{containerName}' still exists after cancellation cleanup.");
        if (waitFailure != null) return new AggregateException($"Docker CLI termination and container cleanup failed for '{containerName}'.", waitFailure, cleanupFailure);
        return cleanupFailure;
    }

    /// <summary>Waits for the job's Docker CLI and kills only that process if it exceeds the bounded shutdown interval.</summary>
    /// <param name="dockerRunProcess">Docker CLI process created for this shader job.</param>
    static async Task WaitForDockerRunExitAsync(Process dockerRunProcess) {
        try {
            using CancellationTokenSource waitTimeout = new(TimeSpan.FromSeconds(10));
            await dockerRunProcess.WaitForExitAsync(waitTimeout.Token).ConfigureAwait(false);
        } catch (OperationCanceledException) {
            if (!dockerRunProcess.HasExited) dockerRunProcess.Kill(true);
            await dockerRunProcess.WaitForExitAsync().ConfigureAwait(false);
        }
    }

    /// <summary>Idempotently force-removes only the named job container and accepts an already-absent container.</summary>
    /// <param name="containerName">Unique job container name.</param>
    /// <returns>A cleanup error when removal fails, or null when removal succeeds.</returns>
    static async Task<Exception> RemoveOwnedContainerAsync(string containerName) {
        try {
            using Process removeProcess = new() { StartInfo = CreateDockerControlStartInfo("rm", "-f", containerName) };
            if (!removeProcess.Start()) return new InvalidOperationException("Failed to start docker rm for the owned shader container.");
            Task<string> outputTask = removeProcess.StandardOutput.ReadToEndAsync();
            Task<string> errorTask = removeProcess.StandardError.ReadToEndAsync();
            using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(10));
            await removeProcess.WaitForExitAsync(timeout.Token).ConfigureAwait(false);
            _ = await outputTask.ConfigureAwait(false);
            string error = await errorTask.ConfigureAwait(false);
            if (removeProcess.ExitCode == 0) return null;
            if (!await ContainerExistsAsync(containerName).ConfigureAwait(false)) return null;
            return new InvalidOperationException($"docker rm failed for owned container '{containerName}': {error}");
        } catch (Exception error) {
            return error;
        }
    }

    /// <summary>Checks exact container existence and distinguishes a missing name from Docker daemon errors.</summary>
    /// <param name="containerName">Unique container identity owned by the cancelled compiler job.</param>
    /// <returns>True when Docker reports the exact container; false only when Docker reports no such object.</returns>
    static async Task<bool> ContainerExistsAsync(string containerName) {
        using Process inspectProcess = new() { StartInfo = CreateDockerControlStartInfo("inspect", "--format", "{{.Id}}", containerName) };
        if (!inspectProcess.Start()) throw new InvalidOperationException("Failed to start docker inspect for the owned shader container.");
        Task<string> outputTask = inspectProcess.StandardOutput.ReadToEndAsync();
        Task<string> errorTask = inspectProcess.StandardError.ReadToEndAsync();
        await inspectProcess.WaitForExitAsync().ConfigureAwait(false);
        string output = await outputTask.ConfigureAwait(false);
        string error = await errorTask.ConfigureAwait(false);
        if (inspectProcess.ExitCode == 0) return output.Trim().Length > 0;
        if (error.Contains("no such object", StringComparison.OrdinalIgnoreCase) || error.Contains("no such container", StringComparison.OrdinalIgnoreCase)) return false;
        throw new InvalidOperationException($"docker inspect failed for owned container '{containerName}': {error}");
    }

    /// <summary>Creates a direct Docker control command with separate arguments and redirected output.</summary>
    /// <param name="arguments">Docker subcommand and literal arguments.</param>
    /// <returns>Configured Docker CLI process start description.</returns>
    static ProcessStartInfo CreateDockerControlStartInfo(params string[] arguments) {
        ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        foreach (string argument in arguments) startInfo.ArgumentList.Add(argument);
        return startInfo;
    }

    /// <summary>Stores the validated toolchain used by all invocations from this compiler instance.</summary>
    Ps3ShaderToolchain Toolchain { get; }
}
