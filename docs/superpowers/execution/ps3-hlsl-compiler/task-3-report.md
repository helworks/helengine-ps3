# Task 3 — PS3 HLSL compiler report

## Delivered

Implemented the Task3 SDK binary reader, pinned-toolchain validation, and Docker-only shader compiler executor for Task2's `Ps3ShaderTranslation` DTOs.

The reader validates the big-endian SDK binary header and every referenced range before reading attributes, constants, relocations, and microcode. It maps parameter data types and element/register bindings, keeping vertex constant registers separate from fragment relocation offsets. It cross-checks stage, active parameter names, types, slots, and bindings against SDK `#profile`/`#var` dump records where both representations provide that information. Inactive `#var` records are not exposed as active parameters; empty/arbitrary dumps are rejected. SDK aliases `COLORn` and `COLn` are normalized when comparing corresponding fragment input slots. Vertex-output linkage remains outside this task.

The toolchain loader validates the immutable image ID and the exact mounted Cg library digest from `toolchain-lock.json`. The executor invokes Docker without a shell, uses `--pull=never` and `--network none`, mounts the Cg tree read-only and a unique job directory for outputs, drains both process streams, and retains per-job source/log/evidence. Cancellation explicitly removes only the job's named container, waits for the Docker CLI, then verifies that the name is absent before returning cancellation. A missing pinned image is not pulled implicitly.

Test output paths are validated before directory creation: test artifacts must be rooted below the sibling `builds/helengine-ps3` directory and must not resolve to a system temporary directory. The checkout path and date are not hardcoded.

## Task3-owned files

- `builder/shaders/IPs3RsxCompiler.cs`
- `builder/shaders/Ps3DockerShaderCommand.cs`
- `builder/shaders/Ps3DockerShaderCompiler.cs`
- `builder/shaders/Ps3RsxConstantElement.cs`
- `builder/shaders/Ps3RsxDataType.cs`
- `builder/shaders/Ps3RsxParameter.cs`
- `builder/shaders/Ps3RsxParameterCategory.cs`
- `builder/shaders/Ps3RsxProgram.cs`
- `builder/shaders/Ps3RsxProgramReader.cs`
- `builder/shaders/Ps3ShaderToolchain.cs`
- `builder.tests/Ps3DockerShaderCompilerTests.cs`
- `builder.tests/Ps3RsxProgramReaderTests.cs`
- `builder.tests/Ps3Task3TestPaths.cs`
- `docs/superpowers/execution/ps3-hlsl-compiler/task-3-report.md`

Task1/Task2 implementation and tests were not edited. Existing unrelated workspace changes were preserved; no commit was created.

## Verification

Test artifacts were written under:

`C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3`

The final approved run used these filters against `builder.tests/helengine.ps3.builder.tests.csproj`:

- `((FullyQualifiedName~Ps3Rsx)|(FullyQualifiedName~Ps3Docker))&Category!=Docker` — **25 passed, 0 failed**.
- `((FullyQualifiedName~Ps3Rsx)|(FullyQualifiedName~Ps3Docker))&Category=Docker` — **6 passed, 0 failed**.

Final run logs:

- `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task3-pure-approved-final.log`
- `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task3-docker-approved-final.log`

The parser/configuration tests followed RED/GREEN: tests first failed to compile/run against the absent Task3 API; they passed after the reader and lock loader were implemented. The pure suite also caught the real SDK fragment alias mismatch (`COLOR0` versus `COL0`) and insufficient dump acceptance; both now have focused regression cases.

Docker-backed tests exercised ordinary fragment output, a constant-bearing fragment program and its relocations, malformed-shader diagnostics, concurrent isolated jobs (including each job's own output and preservation of an unrelated sentinel), and cancellation of an observed running container. The vertex conversion uses the locked SDK image and a shader with `uniform mat4 MVP`, `attribute vec3 position`, `gl_Position = MVP * vec4(position, 1.0)`, and an additional literal `vec4` to exercise an emitted unnamed internal constant. The real result asserts `MVP` is `Float4x4`, has four elements at vertex registers 0–3, and preserves the literal's four binary values in the unnamed constant record. This supplements synthetic reader fixtures with real SDK vertex and fragment conversions.

The SDK identity used was image `sha256:da929def76aacc0a5f499e1b8be143912062ab4a93b206277ead4de9530fe411`; mounted `libCg.so` SHA-256 was `01c8bbbd3c373cb5f337ab0fd7a252b9d01212d35ffc1e2ea2ea8de9d1e557d3`. All native conversions were run inside Docker; no host/WSL SDK fallback was used and no generated shader output was edited.

## Cancellation diagnosis

An earlier complete Docker run had 5/6 passing; `CancelsActiveDockerJob` failed at `Assert.False(ContainerExists(containerName))` after receiving the expected `OperationCanceledException`. At that assertion, the test's exact-name `docker inspect --format {{.Id}}` returned an ID, so the container object still existed. A later inspect of that same recorded name returned “no such object.” The original failing run did **not** capture `.State.Status` or `.State.Running` at the assertion; whether it was running, exited, or being removed is unknown and is not inferred here.

The executor was changed to explicitly `docker rm -f` the owned name, await the CLI, and inspect for absence before returning cancellation. With that implementation and the original harness requirement that the named container be observed in Docker's running state, the focused cancellation test passed 1/1 (`cancellation-fixed.log`), and the final Docker filter passed 6/6. The original failed run is preserved in `cancellation-diagnostic.log` and `task3-docker-verified.log` under the same task artifact directory.

One attempted harness refinement required `docker top` to observe `cgcomp`; that attempt failed because the compile task completed before the extra process observation (`cancellation-root-fix.log`). Those test edits were reverted. No shader-size or polling-delay increase was retained. The final harness uses the already-required running-container observation and exact-name removal assertion.

## Evidence and coverage limitation

The format interpretation follows `sdk-format-evidence.md` and the real read-only SDK probes supplied for this task. A codebase-memory graph query was unavailable: the parent-reported `list_projects` request ended with `Transport closed`, and graph coverage is therefore unknown. This did not block implementation; no broader research was performed.

## Review fix round 1

Addressed the cleanup ordering race with a narrowly scoped internal lifecycle seam in `Ps3DockerShaderCompiler`. Cancellation now attempts the owned-container removal, waits for the Docker CLI to exit or be killed, repeats the idempotent removal, and only then checks that the exact container is absent. This covers a create/start request that completes during CLI termination. The existing Docker cancellation integration test still waits for the named container to be running before cancellation and still asserts its removal.

Added `RemovesContainerCreatedDuringDockerCliTermination`, using controlled callbacks rather than Docker, sleeps, or a larger shader. It models the first removal observing no container, makes the container appear during the CLI-exit callback, and asserts the second removal occurs before the final absence check. TDD evidence: `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter 'FullyQualifiedName~RemovesContainerCreatedDuringDockerCliTermination' --logger 'console;verbosity=normal'` first failed to compile because `CompleteOwnedContainerCleanupAsync` did not exist (`task3-review-cleanup-red.log`), then passed 1/1 after implementation (`task3-review-cleanup-green.log`).

Corrected the synthetic matrix fixture so only the first constant record carries the `FLOAT4x4` type; continuation records now encode type zero, with zero count and name as required by the SDK representation.

Final review-fix verification used these commands and the approved artifact root `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3`:

- `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter '((FullyQualifiedName~Ps3Rsx)|(FullyQualifiedName~Ps3Docker))&Category!=Docker' --verbosity quiet` — **26 passed, 0 failed**, log `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task3-review-round1-pure.log`.
- `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter '((FullyQualifiedName~Ps3Rsx)|(FullyQualifiedName~Ps3Docker))&Category=Docker' --verbosity quiet` — **6 passed, 0 failed**, including the real active-container cancellation regression, log `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task3-review-round1-docker.log`.

The earlier `docker top` synchronization experiment remains reverted; it failed because compilation ended before that additional process observation. No shader-size or polling-delay increase was retained. The original failed cleanup run did not record whether Docker reported the container as running, exited, or removing; that state remains unknown.
