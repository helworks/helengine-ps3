using helengine.ps3.builder;
using System.Security.Cryptography;
using System.Text.Json.Nodes;

namespace helengine.ps3.builder.tests;

/// <summary>Verifies locked Cg toolchain configuration and Docker compiler execution.</summary>
public sealed class Ps3DockerShaderCompilerTests {
    /// <summary>A one-instruction fragment shader used for real SDK smoke compilation.</summary>
    const string FragmentSource = "#version 120\nvoid main() { gl_FragColor = vec4(1.0, 0.25, 0.5, 1.0); }\n";

    /// <summary>A fragment shader with one active uniform whose binary reference must become a relocation.</summary>
    const string ConstantFragmentSource = "#version 120\nuniform vec4 tint;\nvoid main() { gl_FragColor = gl_Color * tint; }\n";

    /// <summary>A vertex shader whose mat4 and homogeneous position require four RSX constants and a literal value.</summary>
    const string VertexMatrixSource = "#version 120\nuniform mat4 MVP;\nattribute vec3 position;\nvoid main() { gl_Position = MVP * vec4(position, 1.0) + vec4(1.0, 2.0, 3.0, 4.0); }\n";

    /// <summary>Loads a fully hashed test toolchain and exposes the expected mounted compiler identity.</summary>
    [Fact]
    public void LoadsPinnedToolchainConfiguration() {
        string root = CreateValidConfigurationRoot(out string imageId, out string compilerPath, out string mountSource, out string mountTarget, out string digest);

        Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(root);

        Assert.Equal(imageId, toolchain.ImageId);
        Assert.Equal(compilerPath, toolchain.CompilerPath);
        Assert.Equal(mountSource, toolchain.CgMountSource);
        Assert.Equal(mountTarget, toolchain.CgMountTarget);
        Assert.Contains(digest, toolchain.Identity, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Rejects a missing immutable Docker image identity from the toolchain lock.</summary>
    [Fact]
    public void RejectsEmptyLockedImageIdentity() {
        string root = CreateValidConfigurationRoot(out _, out _, out _, out _, out _);
        JsonObject lockFile = ReadTestLock(root);
        lockFile["ResolvedImage"]!["ImageId"] = " ";
        WriteTestLock(root, lockFile);

        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(root));
    }

    /// <summary>Rejects an invalid or mismatched Cg shared-library SHA-256 from the toolchain lock.</summary>
    [Fact]
    public void RejectsInvalidCgLibraryDigest() {
        string root = CreateValidConfigurationRoot(out _, out _, out _, out _, out _);
        JsonObject lockFile = ReadTestLock(root);
        lockFile["ShaderToolkit"]!["LibCgSha256"] = new string('0', 64);
        WriteTestLock(root, lockFile);

        Assert.Throws<InvalidDataException>(() => Ps3ShaderToolchain.Load(root));
    }

    /// <summary>Rejects relative, temporary and out-of-tree artifact roots before creating directories.</summary>
    [Fact]
    public void RejectsUnsafeTask3ArtifactRoots() {
        string repositoryRoot = RequiredRepositoryRoot();
        string repositoryParent = Directory.GetParent(repositoryRoot)!.FullName;
        string validRoot = Path.Combine(repositoryParent, "builds", "helengine-ps3", "task-3-test");

        Assert.Equal(Path.GetFullPath(validRoot), Ps3Task3TestPaths.ValidateArtifactsRoot(validRoot, repositoryRoot));
        Assert.Throws<ArgumentException>(() => Ps3Task3TestPaths.ValidateArtifactsRoot("relative-artifacts", repositoryRoot));
        Assert.Throws<ArgumentException>(() => Ps3Task3TestPaths.ValidateArtifactsRoot(Path.Combine(repositoryParent, "other-output"), repositoryRoot));
        Assert.Throws<ArgumentException>(() => Ps3Task3TestPaths.ValidateArtifactsRoot(Path.Combine(Path.GetTempPath(), "task-3-test"), repositoryRoot));
    }

    /// <summary>Builds Docker arguments as isolated values and keeps a host path containing spaces intact.</summary>
    [Fact]
    public void BuildsDockerArgumentsWithoutShellOrPathSplitting() {
        string root = CreateValidConfigurationRoot(out string imageId, out string compilerPath, out string mountSource, out string mountTarget, out _);
        Ps3ShaderToolchain toolchain = Ps3ShaderToolchain.Load(root);
        string jobDirectory = Path.Combine(RequiredArtifactsRoot(), "Docker Command With Spaces", "job-1");
        Ps3DockerShaderCommand command = new(toolchain, helengine.ShaderStage.Pixel, "helengine-shader-test-1", jobDirectory);

        System.Diagnostics.ProcessStartInfo startInfo = command.CreateStartInfo();

        Assert.False(startInfo.UseShellExecute);
        Assert.Equal("docker", startInfo.FileName);
        Assert.Equal("run", startInfo.ArgumentList[0]);
        Assert.Equal("--pull=never", startInfo.ArgumentList[1]);
        Assert.Equal("type=bind,source=" + mountSource + ",target=" + mountTarget + ",readonly", startInfo.ArgumentList[8]);
        Assert.Equal("type=bind,source=" + jobDirectory + ",target=/work", startInfo.ArgumentList[10]);
        Assert.Equal(imageId, startInfo.ArgumentList[13]);
        Assert.Equal(compilerPath, startInfo.ArgumentList[14]);
        Assert.Equal("-f", startInfo.ArgumentList[15]);
        Assert.Equal("-Wcg,-oglsl", startInfo.ArgumentList[17]);
        Assert.Equal("/work/source.glsl", startInfo.ArgumentList[18]);
        Assert.Equal("/work/program.bin", startInfo.ArgumentList[19]);
    }

    /// <summary>Compiles a shader through the pinned SDK using a working path that contains spaces.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesRealFragmentShaderUnderPathWithSpaces() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Docker Jobs", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        Ps3ShaderTranslation translation = CreateTranslation(FragmentSource);

        Ps3RsxProgram program = compiler.Compile(translation, workingRoot, CancellationToken.None);

        Assert.Equal(helengine.ShaderStage.Pixel, program.Stage);
        Assert.True(program.Bytecode.Length > 36);
        Assert.Contains("!!ARBfp1.0", program.Assembly, StringComparison.Ordinal);
        Assert.NotEmpty(program.ToolchainIdentity);
    }

    /// <summary>A real SDK-produced fragment constant preserves its relocation offsets and has no fabricated register.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesConstantBearingFragmentProgram() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Fragment Constant", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));

        Ps3RsxProgram program = compiler.Compile(CreateTranslation(ConstantFragmentSource), workingRoot, CancellationToken.None);

        Ps3RsxParameter constant = Assert.Single(program.Parameters, parameter => parameter.Category == Ps3RsxParameterCategory.Constant);
        Assert.Null(constant.HardwareIndex);
        Assert.NotEmpty(Assert.Single(constant.Elements).FragmentRelocationOffsets);
    }

    /// <summary>A real SDK vertex conversion preserves matrix continuation registers and any unnamed internal constants.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void CompilesVertexMatrixAndInternalConstants() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Vertex Matrix", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));

        Ps3RsxProgram program = compiler.Compile(CreateTranslation(helengine.ShaderStage.Vertex, VertexMatrixSource), workingRoot, CancellationToken.None);

        Assert.Equal(helengine.ShaderStage.Vertex, program.Stage);
        Assert.Contains(program.Parameters, parameter => parameter.Name == "position" && parameter.Category == Ps3RsxParameterCategory.Attribute);
        Ps3RsxParameter matrix = Assert.Single(program.Parameters, parameter => parameter.Name == "MVP");
        Assert.Equal(Ps3RsxDataType.Float4x4, matrix.DataType);
        Assert.Equal(4u, matrix.ElementCount);
        Assert.Equal(new uint?[] { 0, 1, 2, 3 }, matrix.Elements.Select(element => element.VertexRegister));
        Ps3RsxParameter unnamed = Assert.Single(program.Parameters, parameter => parameter.Category == Ps3RsxParameterCategory.Constant && parameter.Name == null);
        Assert.Single(unnamed.Elements);
        Assert.Equal(Ps3RsxDataType.Float4, unnamed.DataType);
        Assert.Equal(new uint[] { 0x3F800000, 0x40000000, 0x40400000, 0x40800000 }, unnamed.Elements[0].InternalValues);
    }

    /// <summary>Rejects malformed generated GLSL and retains the failed isolated job's diagnostic files.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public void RejectsMalformedSourceAndRetainsJobEvidence() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Malformed Shader", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        Ps3ShaderTranslation translation = CreateTranslation("#version 120\nthis is not GLSL\n");

        InvalidOperationException error = Assert.Throws<InvalidOperationException>(() => compiler.Compile(translation, workingRoot, CancellationToken.None));

        Assert.Contains("cgcomp", error.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains(Directory.GetDirectories(workingRoot), job => File.Exists(Path.Combine(job, "compiler.stderr.log")));
    }

    /// <summary>Two concurrent Docker jobs return their own generated programs without sharing output files.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public async Task RunsConcurrentJobsWithoutOutputCollisions() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Concurrent Jobs", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(workingRoot);
        string unrelatedOutput = Path.Combine(workingRoot, "program.bin");
        byte[] sentinel = [9, 8, 7, 6];
        File.WriteAllBytes(unrelatedOutput, sentinel);
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        Ps3ShaderTranslation firstTranslation = CreateTranslation(FragmentSource);
        Ps3ShaderTranslation secondTranslation = CreateTranslation(FragmentSource.Replace("0.25", "0.75", StringComparison.Ordinal));

        Task<Ps3RsxProgram> firstTask = Task.Run(() => compiler.Compile(firstTranslation, workingRoot, CancellationToken.None));
        Task<Ps3RsxProgram> secondTask = Task.Run(() => compiler.Compile(secondTranslation, workingRoot, CancellationToken.None));
        Ps3RsxProgram[] programs = await Task.WhenAll(firstTask, secondTask);

        Assert.All(programs, program => Assert.True(program.Bytecode.Length > 36));
        Assert.False(programs[0].Bytecode.SequenceEqual(programs[1].Bytecode));
        Assert.NotEqual(programs[0].Assembly, programs[1].Assembly);
        byte[][] jobOutputs = Directory.GetDirectories(workingRoot)
            .Select(job => File.ReadAllBytes(Path.Combine(job, "program.bin")))
            .ToArray();
        Assert.Equal(2, jobOutputs.Length);
        Assert.All(programs, program => Assert.Contains(jobOutputs, output => output.SequenceEqual(program.Bytecode)));
        Assert.Equal(sentinel, File.ReadAllBytes(unrelatedOutput));
        Assert.Equal(2, Directory.GetDirectories(workingRoot).Length);
    }

    /// <summary>Cancellation interrupts an active Docker compiler process and reports cancellation to its caller.</summary>
    [Fact]
    [Trait("Category", "Docker")]
    public async Task CancelsActiveDockerJob() {
        string root = RequiredRepositoryRoot();
        string workingRoot = Path.Combine(RequiredArtifactsRoot(), "Cancelled Job", Guid.NewGuid().ToString("N"));
        Ps3DockerShaderCompiler compiler = new(Ps3ShaderToolchain.Load(root));
        string expensiveSource = CreateExpensiveFragmentSource();
        using CancellationTokenSource cancellation = new();
        Task<Ps3RsxProgram> compileTask = Task.Run(() => compiler.Compile(CreateTranslation(expensiveSource), workingRoot, cancellation.Token));
        string containerName = null;
        try {
            containerName = await WaitForRunningContainerName(compileTask, workingRoot);
            cancellation.Cancel();

            await Assert.ThrowsAnyAsync<OperationCanceledException>(() => compileTask);
            Assert.False(await ContainerExists(containerName));
        } finally {
            cancellation.Cancel();
            try {
                await compileTask.WaitAsync(TimeSpan.FromSeconds(15));
            } catch (OperationCanceledException) {
            } catch (TimeoutException) {
                if (!string.IsNullOrWhiteSpace(containerName)) await KillOwnedContainer(containerName);
                throw new InvalidOperationException("Cancelled compiler job did not exit and was force-stopped by its unique container name.");
            } catch (InvalidOperationException) {
            }
        }
    }

    /// <summary>A second idempotent removal catches a container created while the Docker CLI is terminating.</summary>
    [Fact]
    public async Task RemovesContainerCreatedDuringDockerCliTermination() {
        bool containerExists = false;
        int removeCalls = 0;
        List<string> lifecycle = [];
        Func<Task<Exception>> removeOwnedContainer = () => {
            lifecycle.Add("remove");
            removeCalls++;
            if (removeCalls == 1) Assert.False(containerExists);
            if (removeCalls == 2) containerExists = false;
            return Task.FromResult<Exception>(null);
        };
        Func<Task> waitForDockerCliExit = () => {
            lifecycle.Add("cli-exit");
            containerExists = true;
            return Task.CompletedTask;
        };
        Func<Task<bool>> inspectContainer = () => {
            lifecycle.Add("inspect");
            return Task.FromResult(containerExists);
        };

        Exception cleanupFailure = await Ps3DockerShaderCompiler.CompleteOwnedContainerCleanupAsync(
            "ps3-shader-controlled-test",
            removeOwnedContainer,
            waitForDockerCliExit,
            inspectContainer);

        Assert.Null(cleanupFailure);
        Assert.Equal(2, removeCalls);
        Assert.False(containerExists);
        Assert.Equal(new[] { "remove", "cli-exit", "remove", "inspect" }, lifecycle);
    }

    /// <summary>Creates a temporary valid lock and a matching Cg library under the explicit artifact root.</summary>
    /// <param name="imageId">Resolved test image identity.</param>
    /// <param name="compilerPath">Locked compiler path.</param>
    /// <param name="mountSource">Resolved test Cg source mount.</param>
    /// <param name="mountTarget">Locked container mount target.</param>
    /// <param name="digest">SHA-256 digest written into the test lock.</param>
    /// <returns>The isolated repository root containing the lock and Cg library.</returns>
    static string CreateValidConfigurationRoot(out string imageId, out string compilerPath, out string mountSource, out string mountTarget, out string digest) {
        string repositoryRoot = RequiredRepositoryRoot();
        string root = Path.Combine(RequiredArtifactsRoot(), "Toolchain Config", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        JsonObject lockFile = JsonNode.Parse(File.ReadAllText(Path.Combine(repositoryRoot, "toolchain-lock.json")))!.AsObject();
        imageId = "sha256:" + new string('a', 64);
        compilerPath = "/opt/ps3dev/bin/cgcomp";
        mountSource = Path.Combine(root, "cg");
        mountTarget = "/opt/cg";
        byte[] library = [1, 7, 3, 9, 2, 8];
        string libraryPath = Path.Combine(mountSource, "lib64", "libCg.so");
        Directory.CreateDirectory(Path.GetDirectoryName(libraryPath)!);
        File.WriteAllBytes(libraryPath, library);
        digest = Convert.ToHexString(SHA256.HashData(library)).ToLowerInvariant();
        lockFile["ShaderToolkit"]!["MountSource"] = "cg";
        lockFile["ShaderToolkit"]!["MountTarget"] = mountTarget;
        lockFile["ShaderToolkit"]!["LibraryPath"] = "lib64/libCg.so";
        lockFile["ShaderToolkit"]!["LibCgSha256"] = digest;
        lockFile["ResolvedImage"]!["ImageId"] = imageId;
        lockFile["ResolvedImage"]!["CgcompPath"] = compilerPath;
        WriteTestLock(root, lockFile);
        return root;
    }

    /// <summary>Reads a test lock without changing the repository's pinned lock.</summary>
    /// <param name="root">Isolated configuration root.</param>
    /// <returns>The editable test JSON object.</returns>
    static JsonObject ReadTestLock(string root) {
        return JsonNode.Parse(File.ReadAllText(Path.Combine(root, "toolchain-lock.json")))!.AsObject();
    }

    /// <summary>Writes an isolated test lock under the build-owned artifact root.</summary>
    /// <param name="root">Isolated configuration root.</param>
    /// <param name="lockFile">Lock JSON to write.</param>
    static void WriteTestLock(string root, JsonObject lockFile) {
        File.WriteAllText(Path.Combine(root, "toolchain-lock.json"), lockFile.ToJsonString());
    }

    /// <summary>Builds the existing Task2 translation DTO for a supplied generated GLSL string.</summary>
    /// <param name="source">GLSL text sent to the pinned SDK compiler.</param>
    /// <returns>A pixel translation with no fabricated reflection resources.</returns>
    static Ps3ShaderTranslation CreateTranslation(string source) {
        return CreateTranslation(helengine.ShaderStage.Pixel, source);
    }

    /// <summary>Builds a Task2 translation for an explicit vertex or pixel source string.</summary>
    /// <param name="stage">Translation stage.</param>
    /// <param name="source">Compiler-generated GLSL for the test.</param>
    /// <returns>A translation carrying the exact stage and no fabricated reflection records.</returns>
    static Ps3ShaderTranslation CreateTranslation(helengine.ShaderStage stage, string source) {
        return new Ps3ShaderTranslation(stage, source, [], [], [], [], [], [], [], []);
    }

    /// <summary>Creates a large unrolled fragment expression to keep a real Docker job active for cancellation.</summary>
    /// <returns>Valid generated GLSL with substantial compile work.</returns>
    static string CreateExpensiveFragmentSource() {
        System.Text.StringBuilder source = new("#version 120\nvoid main() {\nfloat x = 0.25;\n");
        for (int index = 0; index < 12000; index++) source.Append("x = x * 1.00001 + 0.00001;\n");
        source.Append("gl_FragColor = vec4(x);\n}\n");
        return source.ToString();
    }

    /// <summary>Waits until this compiler job's named container is running, proving cancellation targets active work.</summary>
    /// <param name="compileTask">Active compiler task.</param>
    /// <param name="workingRoot">Compiler working root containing its unique child job directory.</param>
    /// <returns>The exact running container name owned by the compiler job.</returns>
    static async Task<string> WaitForRunningContainerName(Task<Ps3RsxProgram> compileTask, string workingRoot) {
        DateTime deadline = DateTime.UtcNow.AddSeconds(45);
        while (DateTime.UtcNow < deadline && !compileTask.IsCompleted) {
            foreach (string job in Directory.Exists(workingRoot) ? Directory.GetDirectories(workingRoot) : []) {
                string namePath = Path.Combine(job, "container-name.txt");
                if (!File.Exists(namePath)) continue;
                string name = File.ReadAllText(namePath).Trim();
                if (name.Length > 0 && await ContainerIsRunning(name)) return name;
            }
            await Task.Delay(100);
        }
        throw new TimeoutException("The real Docker compiler job did not become active before its compile task ended or timed out.");
    }

    /// <summary>Checks whether a Docker container with the exact task-owned identity exists.</summary>
    /// <param name="containerName">Compiler-generated unique container name.</param>
    /// <returns>True when Docker inspect succeeds.</returns>
    static async Task<bool> ContainerExists(string containerName) {
        using System.Diagnostics.Process process = StartDockerInspect(containerName, false);
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string output = await standardOutput;
        _ = await standardError;
        return process.ExitCode == 0 && output.Trim().Length > 0;
    }

    /// <summary>Checks whether a Docker container with the exact task-owned identity is running.</summary>
    /// <param name="containerName">Compiler-generated unique container name.</param>
    /// <returns>True only when the inspected container state is running.</returns>
    static async Task<bool> ContainerIsRunning(string containerName) {
        using System.Diagnostics.Process process = StartDockerInspect(containerName, true);
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        string result = await standardOutput;
        _ = await standardError;
        return process.ExitCode == 0 && string.Equals(result.Trim(), "true", StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Starts a Docker inspect command for the exact task-owned container without invoking a shell.</summary>
    /// <param name="containerName">Compiler-generated unique container name.</param>
    /// <param name="includeState">Whether to request the running-state value.</param>
    /// <returns>The started Docker CLI process.</returns>
    static System.Diagnostics.Process StartDockerInspect(string containerName, bool includeState) {
        System.Diagnostics.ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("inspect");
        startInfo.ArgumentList.Add("--format");
        startInfo.ArgumentList.Add(includeState ? "{{.State.Running}}" : "{{.Id}}");
        startInfo.ArgumentList.Add(containerName);
        System.Diagnostics.Process process = new() { StartInfo = startInfo };
        if (!process.Start()) throw new InvalidOperationException("Failed to start Docker inspect for the owned compiler container.");
        return process;
    }

    /// <summary>Force-stops only the compiler container named by this test's own job marker.</summary>
    /// <param name="containerName">Unique container name written by the compiler job.</param>
    static async Task KillOwnedContainer(string containerName) {
        System.Diagnostics.ProcessStartInfo startInfo = new("docker") {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            CreateNoWindow = true
        };
        startInfo.ArgumentList.Add("kill");
        startInfo.ArgumentList.Add(containerName);
        using System.Diagnostics.Process process = new() { StartInfo = startInfo };
        if (!process.Start()) throw new InvalidOperationException("Failed to start Docker kill for the unique test container.");
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync();
        Task<string> standardError = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync();
        _ = await standardOutput;
        _ = await standardError;
    }

    /// <summary>Returns the explicitly configured repository root required by integration tests.</summary>
    /// <returns>Repository root containing toolchain-lock.json.</returns>
    static string RequiredRepositoryRoot() {
        string root = Environment.GetEnvironmentVariable("HELENGINE_PS3_REPOSITORY_ROOT");
        Assert.False(string.IsNullOrWhiteSpace(root), "Set HELENGINE_PS3_REPOSITORY_ROOT to the checkout root for Task3 tests.");
        return root!;
    }

    /// <summary>Returns the explicit artifact root for all Task3 test files and logs.</summary>
    /// <returns>Build-owned artifact directory.</returns>
    static string RequiredArtifactsRoot() {
        string root = Environment.GetEnvironmentVariable("HELENGINE_PS3_SHADER_TEST_OUTPUT");
        Assert.False(string.IsNullOrWhiteSpace(root), "Set HELENGINE_PS3_SHADER_TEST_OUTPUT to the Task3 artifact root.");
        string validatedRoot = Ps3Task3TestPaths.ValidateArtifactsRoot(root!, RequiredRepositoryRoot());
        Directory.CreateDirectory(validatedRoot);
        return validatedRoot;
    }
}
