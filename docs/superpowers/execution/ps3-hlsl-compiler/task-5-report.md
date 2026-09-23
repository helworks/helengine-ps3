# Task 5 report — compiler acceptance and handoff

## Result

Added a reproducible PS3 compiler acceptance command and contract tests for its preflight and report validation. Added shared-target compatibility assertions and documented the verified compiler path and its limits. No shader compiler/parser, shared target enum/descriptor, generated shader output, runtime feature, or build integration was changed.

The acceptance runner checks an explicit output path is an absolute strict descendant of the sibling `builds/helengine-ps3` root, requires the sibling `helengine` checkout or `HELENGINE_ROOT`, validates the pinned immutable Docker image ID and mounted Cg library digest, and runs the non-Docker builder suite followed by the Docker category. Each run writes reports and logs under a unique `runs/<run-id>` directory, preserving reports from earlier invocations. The TRX validator requires a zero process exit, a report with nonzero total/executed tests, all tests passed, and each required class to have at least one linked passing result (`UnitTestResult.testId` → `UnitTest.id` → `TestMethod.className`). It handles actual VSTest `Completed` summaries and fully qualified class names. It does not invoke toolchain preparation or use a host SDK fallback.

## Changed files

- `scripts/tests/ps3-hlsl-compiler.tests.ps1` — acceptance entry point and two required test runs.
- `scripts/tests/ps3-hlsl-compiler-acceptance.helpers.ps1` — strict path, lock/Cg, per-run artifact path, and linked TRX validation helpers.
- `scripts/tests/ps3-hlsl-compiler-contract.tests.ps1` — missing-input, bad-lock/Cg, failed-process, missing/empty/stale report, zero-test, unexecuted-class, and missing-class failure checks; verifies dot-sourcing leaves the caller environment unchanged.
- `builder.tests/Ps3ShaderTargetCompatibilityTests.cs` — numeric enum compatibility, `ps3` / `HEL_API_PS3` descriptor checks, and a shaderc conditional-include test using the PS3 define.
- `builder.tests/Ps3ShaderSourceCompilerTests.cs` — removed optional environment-driven GLSL probe file writes from ordinary frontend tests; Docker integration tests retain ownership of their output directory.
- `docs/Ps3ShaderCompiler.md` — prerequisites, acceptance invocation, verified stages/resources, pipeline, output paths, and explicit unsupported cases.

The shared enum values and descriptor were read and asserted through tests; no shared-target files were edited. PS3 remains appended at value 7, with descriptor name `ps3` and define `HEL_API_PS3`.

## Test-first and acceptance evidence

The acceptance contract first failed because the helper file was absent (`task5-contract-red.log`). The initial report validator rejected a VSTest `Completed` summary even with all counters passed (`task5-contract-completed-red.log`). After that was corrected, `task5-contract-completed-green.log` failed because the synthetic required-class expectation used a short name while the report definition held a fully qualified name. Matching namespace-qualified names fixed that fixture issue.

The added testId-link regression then failed because the old validator accepted `Ps3RsxProgramReaderTests` from an unused `UnitTest` definition with no linked result (`task5-trx-link-red.log`). The final validator requires a linked `Passed` result; `task5-trx-link-green.log` passed. Its stale-report case places a valid report at the former static path, leaves the new invocation path absent, and confirms the zero-exit/no-report case still fails. Every full acceptance now uses a fresh GUID run directory and leaves prior reports intact.

The target compatibility command was run with:

```powershell
$env:HELENGINE_PS3_REPOSITORY_ROOT='C:/dev/helworks/helengine-ps3'
$env:HELENGINE_PS3_SHADER_TEST_OUTPUT='C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task4-tests'
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter FullyQualifiedName~Ps3ShaderTargetCompatibilityTests --verbosity quiet
```

Result: **3/3 passed**. Log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task5-target-redgreen.log`.

The final acceptance invocation was:

```powershell
./scripts/tests/ps3-hlsl-compiler.tests.ps1 -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/acceptance
```

Environment was set to `HELENGINE_PS3_REPOSITORY_ROOT=C:/dev/helworks/helengine-ps3` and `HELENGINE_PS3_SHADER_TEST_OUTPUT=C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/acceptance/shader-artifacts`.

- Non-Docker builder suite: **98/98 passed**. TRX and log: `acceptance\runs\17e32b778ea047cfbcfaa6ab45c235b1\results\pure\pure.trx` and `acceptance\runs\17e32b778ea047cfbcfaa6ab45c235b1\pure-tests.log`.
- Docker integration category: **7/7 passed**. TRX and log: `acceptance\runs\17e32b778ea047cfbcfaa6ab45c235b1\results\docker\docker.trx` and `acceptance\runs\17e32b778ea047cfbcfaa6ab45c235b1\docker-tests.log`.
- Final command output: `acceptance-command-task5-final.log`.
- Contract suite: passed, with output in `task5-trx-link-green.log`.

Pinned identity printed by acceptance: image `sha256:da929def76aacc0a5f499e1b8be143912062ab4a93b206277ead4de9530fe411`, compiler `/opt/ps3dev/bin/cgcomp`, Cg library SHA-256 `01c8bbbd3c373cb5f337ab0fd7a252b9d01212d35ffc1e2ea2ea8de9d1e557d3`.

The first complete pure run reported **98/98 passed**, but its static TRX report was rejected by the then-current `Completed` summary check and acceptance stopped before Docker. That diagnostic is preserved in `acceptance-command.log`. After adding the summary, freshness, and testId-link regressions and correcting report paths/validation, the final acceptance run reported both groups passed. The first compilation emitted existing sibling `helengine.input/MouseState.cs` IDE0040 analyzer warnings. They were not changed or suppressed; the final incremental run did not re-emit them.

## Verified boundary

The managed frontend is tested on Windows x64 with .NET 9 and explicitly loads the packaged `runtimes/win-x64/native/spirv-cross.dll`. The SDK conversion uses the pinned Linux Docker image and verified Cg mount. Toolchain preparation remains an explicit developer command: `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_toolchain.ps1`. Acceptance output must be a strict child of the sibling `builds/helengine-ps3` root; RPCS3 and firmware are not prerequisites.

Task 4 evidence covers vertex/pixel compilation and a concrete modern paired fixture using `Texture2D`, two samplers, a cbuffer member, `COLOR0`, and `TEXCOORD0`. Its two varying links use canonical channel identities and stage-specific SDK slots; its active attributes, two distinct sampler units, and fragment constant relocation validate. Synthetic sampler-dimension mapping includes 1D, 2D, 3D, cube, and rectangle tokens, but real HLSL-to-SDK integration exercises `Texture2D`/`Dim2D` only. User varyings remain limited to one-column, non-array FP3232 scalar/vectors of width 1–4; unsupported active types, shapes, or routes fail rather than receiving default bindings. No result here enables authored shader rendering or changes runtime behavior.
