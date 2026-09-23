## Global Constraints

- Work stays in the existing checkouts, without worktrees.
- Native compilation and final RSX conversion remain in the repository's pinned Docker SDK.
- Never modify generated shader text, binaries or headers after compilation.
- There is no host SDK fallback or implicit temporary-directory output.
- No default shader or default binding is synthesized.
- Use the shared include resolver and compiler preprocessor rather than textual include replacement.
- Do not change other builders or unrelated native-cache work.
- Enable the platform shader feature only after the cooking-to-rendering path passes.
- No screenshots without permission.
- All generated outputs/logs/jobs use `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923` or an explicit child build ID; do not use `%TEMP%`.
- One class per file; substantive XML/`///` comments; explicit failures; preserve user changes. C# follows sibling AGENTS.md conventions.


## Validation commands

Run focused .NET tests with an explicit output root:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet --filter FullyQualifiedName~Ps3Shader --verbosity quiet
```

Docker tests must be explicitly identified as integration tests and must run for phase acceptance. Ordinary tests must not silently claim Docker coverage when Docker is absent. Capture potentially large output in build-owned logs and return bounded excerpts. Request tool escalation when the sandbox requires it.


## Task 5: Reproducible compiler acceptance and handoff

Controller correction: the acceptance unit-test filter must include Ps3Rsx and Ps3Docker class prefixes in addition to Ps3Shader, and exclude Category=Docker from the pure run. Run Category=Docker explicitly as the second required run. A Ps3Shader-only filter silently misses the binary reader tests. Assert both result reports have tests, pass and contain the expected test classes, not just zero process exit codes.

Exact shared-target compatibility values verified by parent: DirectX9=0, DirectX11=1, DirectX12=2, Vulkan=3, Metal=4, PsVita=5, WiiU=6, appended Ps3=7. `ShaderTargetDescriptors.Get(ShaderCompileTarget.Ps3)` must expose Name `ps3` and DefineName `HEL_API_PS3`. This is a compatibility assertion, not permission to renumber old targets. Existing sibling `helengine.editor.tests/ShaderTargetDescriptorTests.cs` also checks name round trips and table uniqueness; equivalent focused assertions can run from the PS3 builder suite without building the whole editor test graph.

**Files:**
- Create: `scripts/tests/ps3-hlsl-compiler.tests.ps1`
- Create: `builder.tests/Ps3ShaderTargetCompatibilityTests.cs`
- Create as needed: `scripts/tests/ps3-hlsl-compiler-contract.tests.ps1` and a focused acceptance helper script for injected failure-path tests.
- Create: `docs/Ps3ShaderCompiler.md`
- Modify only if necessary: `builder.tests/Ps3ShaderSourceCompilerTests.cs` to remove environment-dependent side effects from ordinary unit tests once integration tests own probe output.
- Verify existing changes: `builder/helengine.ps3.builder.csproj`, sibling `ShaderCompileTarget.cs`, `ShaderTargetDescriptors.cs`.

**Interfaces:**
- Acceptance script requires `-OutputRoot` as an absolute build-owned path and resolves the repository relative to PSScriptRoot. Runs the full focused shader tests and the Docker category, then fails unless both reports are successful. Reports compiler identity and output locations; no implicit toolchain download or host fallback.

Failure-path harness may add a focused companion script under scripts/tests/ to exercise acceptance without corrupting the real lock or running repeated full suites. Missing OutputRoot must fail noninteractively, never wait for a parameter prompt. Use isolated build-owned fixture locks/Cg bytes or narrow injectable validation/process helpers, not edits to repository toolchain-lock.json. Verify a nonzero test process, a zero-exit process with no results, and a results report with zero tests or missing required classes cannot produce acceptance. Preserve the caller's environment when dot-sourcing helpers; avoid publishing a PASS marker before all required runs are checked.

- [ ] **Step 1: Add target compatibility assertions.** Existing enum numeric values remain unchanged; Ps3 descriptor has name `ps3` and define `HEL_API_PS3`. Verify target-specific defines reach shaderc. Run the whole builder test suite, not only the new tests.
- [ ] **Step 2: Add failure-path script tests and real acceptance invocation.** Missing output root, invalid lock/Cg library and failed tests must return nonzero. Do not execute a second identical suite solely to produce a PASS marker. Acceptance command:

```powershell
./scripts/tests/ps3-hlsl-compiler.tests.ps1 -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/acceptance
```

- [ ] **Step 3: Document verified behavior.** Include exact preparation prerequisites from the existing repository docs, the command above, supported stages/resources, include/define behavior, source-to-RSX pipeline, generated artifact locations and explicit limitations. Say that this is compiler infrastructure, not enabled authored shader rendering. Do not label a limitation supported because a program compiled once.

Distinguish synthetic binding-map coverage from actual HLSL-to-SDK integration coverage. For example, a synthetic sampler-dimension mapping test does not prove that a modern HLSL source using that dimension compiles on the pinned SDK. List the real modern fixture separately (Texture2D, two samplers, constant buffer, COLOR0 and TEXCOORD0 links). Read the final Task4 report after review fixes before writing supported-subset claims.

Host boundary to document: the current source frontend explicitly loads the packaged `runtimes/win-x64/native/spirv-cross.dll`. State the tested Windows x64/.NET9 host requirement; using a Linux SDK container does not by itself establish a cross-platform host frontend. Do not add host portability work to this milestone merely to broaden the documentation claim.

Prerequisite precision: managed projects require the sibling helengine checkout, or an explicit HelengineRoot/HELENGINE_ROOT override. The acceptance output boundary is the explicit sibling builds/helengine-ps3/<build-id> contract in this brief and Ps3Task3TestPaths; do not copy README or Resolve-Ps3OutputRoot's legacy default of .ps3/builds. Assert-Ps3PathInsideRoot also allows the root itself, whereas acceptance needs a strict descendant. Toolchain preparation is powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_toolchain.ps1, invoked explicitly by a developer, not automatically by acceptance. RPCS3/firmware is not a prerequisite for compiler-only tests. Document these distinctions in the new compiler document; do not broaden into rewriting the existing README or unrelated scripts.
- [ ] **Step 4: Parent reviews phase evidence and writes the next plan.** Record types/format evidence, commands and test results in the task report. The next plan must address the shared cache identity gap, concrete material/program selection and cooker staging before any native runtime feature is enabled.
