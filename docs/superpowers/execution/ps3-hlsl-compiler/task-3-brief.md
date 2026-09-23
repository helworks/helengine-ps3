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


## Task 3: Locked Docker compiler and RSX binary validation

Required controller evidence: read `sdk-format-evidence.md` in this directory before implementing the parser. It records pinned-header layouts, matrix continuation records, fragment relocation semantics and attribute/sampler/output distinctions verified in the pinned cgcomp writer/parser. Fragment constant index is not a hardware register. Treat these as refinements of the generic DTO terminology below and preserve stage-specific distinctions explicitly.

Task-specific pure filter: `(FullyQualifiedName~Ps3Rsx|FullyQualifiedName~Ps3Docker)&Category!=Docker`; run Category=Docker separately with its required working-root context. The generic Ps3Shader filter above predates these class names and is not sufficient for this task.

Use the already populated managed artifacts path `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3` for focused tests. Existing real SDK specimens and dumps are at `.../task-2-probe/VS.vpo`, `PS.fpo`, `VS.vpo.dump`, `PS.fpo.dump`; use them read-only as evidence, not hardcoded production dependencies. If Docker access is denied in the worker environment, give the parent the exact integration command/environment immediately; parent can run with escalation. Do not silently skip Docker tests or spend repeated attempts on the denied pipe.

Keep implementation within the named compiler/parser responsibility. Do not edit SourceCompiler/reflection files or unrelated build scripts. Helpers needed for typed parameter elements or process testability may be separate documented files. Emit an early bounded checkpoint after pure parser/configuration tests, then finish process/integration tests; report actual failures instead of undertaking broad new research.

**Files:**
- Create: `builder/shaders/IPs3RsxCompiler.cs`
- Create: `builder/shaders/Ps3DockerShaderCompiler.cs`
- Create: `builder/shaders/Ps3ShaderToolchain.cs`
- Create: `builder/shaders/Ps3RsxProgram.cs`
- Create: `builder/shaders/Ps3RsxProgramReader.cs`
- Create: `builder/shaders/Ps3RsxParameter.cs`
- Create: `builder.tests/Ps3DockerShaderCompilerTests.cs`
- Create: `builder.tests/Ps3RsxProgramReaderTests.cs`

**Interfaces:**
- `IPs3RsxCompiler.Compile(Ps3ShaderTranslation translation, string workingRoot, CancellationToken cancellationToken)` returns `Ps3RsxProgram`.
- `Ps3ShaderToolchain.Load(string repositoryRoot)` validates the lock and required mounted Cg library, exposing `ImageId`, `CgMountSource`, `CgMountTarget`, `CompilerPath`, `Identity`.
- `Ps3DockerShaderCompiler(Ps3ShaderToolchain toolchain)` implements IPs3RsxCompiler; no implicit repository discovery or temporary output root.
- Program fields: `ShaderStage Stage`, copied `byte[] Bytecode`, `string Assembly`, `string ToolchainIdentity`, and copied `IReadOnlyList<Ps3RsxParameter> Parameters`.
- `Ps3RsxProgramReader.Read(ShaderStage stage, byte[] bytecode, string assembly, string toolchainIdentity)` validates bytes before returning the program. Parameter fields include exact compiler name, resource category, hardware index, element count, data type and referenced microcode offsets where applicable.

Parameter representation refinement: preserve element boundaries for matrix/array constants. Use a separate documented `Ps3RsxConstantElement` record/class with element ordinal, optional vertex register (nullable value type is allowed with nullable reference annotations disabled), copied fragment relocation offsets and internal constant values as needed. An empty fragment relocation list means no references, never register zero. `Ps3RsxParameter` owns copied elements and represents attribute/sampler indices separately from constants. Flattening all element relocations into one undifferentiated list loses which vector must be patched and is not acceptable. Vertex-output channels remain dump metadata for Task 4, not fabricated binary attributes.

- [ ] **Step 1: Write pure validation and command-construction tests.** Cover wrong magic/stage, truncated header, overflowing offset/count, out-of-bounds name/microcode ranges, invalid alignment, and empty code. Inspect the pinned SDK header definitions read-only to obtain format constants; do not infer structure from one binary size. Fixtures must describe the expected fields explicitly and include at least one valid SDK-produced specimen generated during the integration test.
- [ ] **Step 2: Write Docker execution tests.** Use paths containing spaces, a malformed source, two concurrent jobs, and cancellation. Prepopulate a different output file in the parent directory and confirm it is never returned. Separate these real-Docker tests by `Trait("Category", "Docker")`; missing Docker must fail an explicitly requested Docker test run.
- [ ] **Step 3: Implement deterministic configuration and isolated jobs.** Validate nonempty locked ImageId and Cg library SHA256. Under the provided workingRoot allocate a unique job directory; source and output paths have fixed local filenames. Use ProcessStartInfo.ArgumentList, no shell-assembled command, network disabled, Cg mounted read-only, and only the job output mount writable. Use the existing lock's compiler path and environment. Write compiler-produced .dump and process logs to the job directory.

```text
docker run --rm --network none
  --mount type=bind,source=<validated Cg root>,target=<locked mount>,readonly
  --mount type=bind,source=<unique job>,target=/work
  --env LD_LIBRARY_PATH=<locked mount>/lib64
  <locked ImageId> <locked CgcompPath>
  <-v or -f> -d -Wcg,-oglsl /work/source.glsl /work/program.bin
```

Read stdout/stderr concurrently. Cancellation terminates the owned process/container using a unique container identity; never kill unrelated Docker/RPCS3 processes. Reject nonzero exit, absent output and empty output before parsing. Keep logs on failures; do not rewrite generated files. Avoid implementing a persistent cache in this phase.

- [ ] **Step 4: Implement endian-aware binary validation.** Follow the pinned SDK's definitions and use checked range arithmetic. Validate before reading strings, pointer tables or microcode. Preserve the generated bytes unchanged. Cross-check stage and parameter data with the compiler dump where both contain the same information.
- [ ] **Step 5: Run pure tests and real Docker tests; report exact SDK identity and artifacts.** Prove two independent jobs cannot collide and that malformed output is rejected even if a tool reports success. Parent reviews parser bounds and subprocess lifetime handling.
