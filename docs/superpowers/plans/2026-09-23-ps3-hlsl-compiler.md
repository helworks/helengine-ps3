# PS3 HLSL Compiler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the experimental HLSL conversion into a tested compiler that produces validated RSX program pairs and explicit binding metadata, ready for subsequent cooker/runtime integration.

**Architecture:** Compile authored HLSL with shaderc, reflect SPIR-V and emit unmodified GLSL through SPIRV-Cross. Invoke cgcomp in the locked Docker SDK, validate its binary output, and reconcile compiler reflection with actual RSX bindings. Reject incompatible stage pairs rather than relying on an unavailable runtime GLSL linker.

**Tech Stack:** .NET 9, Silk.NET.Shaderc and Silk.NET.SPIRV.Cross 2.21.0, xUnit, PowerShell, pinned PSL1GHT/Cg Docker SDK.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-hlsl-shaders-design.md` (approved by Helena).

## Execution ownership

The parent writes plans, resolves architectural decisions, reviews results and coordinates verification. Luna writes implementation and test code. Use an explicit `model: gpt-6-luna` and `reasoning_effort: high` for each implementer; the current tool exposes `reasoning_effort`, not `model_reasoning_effort`. Use `xhigh` for bounded reflection/binary-parser work when needed. The installed delegation tool lists gpt-6-luna, not gpt-5.6-luna.

One implementation worker at a time, directly in the existing checkout. No nested delegation. Each worker receives its task brief, the global constraints, relevant interface definitions and a report path. Do not send the whole conversation. Changes stay uncommitted until reviewed; never stage unrelated files. An independent standalone review requires Helena's separate permission and is not part of this workflow.

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

## Phases and scope boundaries

This is the first of three implementation plans. The parent writes the next plans from the verified interfaces, not from guesses about generated RSX bindings.

1. **This plan: compiler and binding contract.** Includes, defines, reflection, Docker compilation, binary validation, stage-pair checks, and real SDK tests. Does not enable platform shaders or change renderer behavior.
2. **Next plan: shared backend and cooker.** Implement IShaderBackend/IShaderBackendRegistryContributor and IPlatformShaderArtifactBuilder; resolve material-selected program/variant/entry-point identities; define the versioned cooked artifact contract; declare/stage content. Fix or explicitly bypass unsafe shared caching for this backend, with tests for include/options/toolchain changes.
3. **Final plan: native loading and runtime acceptance.** Consume the cooked format; bind constants, attributes and texture/sampler resources; connect materials to program pairs; validate cube_test and a textured fixture; enable the feature only when that path works.

The later phases are part of the approved project, not optional completion claims. This plan alone does not satisfy the full runtime spec.

## Read-only evidence and current state

Codebase Memory was attempted again on this turn and returned `Transport closed`; no current graph coverage can be claimed. Use exact source reads for these bounded files:

- `builder/shaders/Ps3ShaderSourceCompiler.cs`: experimental HLSL -> GLSL conversion.
- `builder.tests/Ps3ShaderSourceCompilerTests.cs`: three passing frontend tests; optional probe output.
- `builder/helengine.ps3.builder.csproj`: Silk dependencies and compilation-project reference already added.
- `../helengine/engine/helengine.shader/shaders/runtime/ShaderCompileTarget.cs` and `ShaderTargetDescriptors.cs`: uncommitted Ps3 target addition already present. Preserve existing ordinals.
- `../helengine/engine/helengine.shader/shaders/compilation/IShaderIncludeResolver.cs`, `ShaderIncludeResult.cs`, `ShaderCompileRequest.cs`, `ShaderCompileOptions.cs`: source contracts.
- `../helengine-wiiu/builder/shaders/WiiUGlslShaderBackend.cs`: read-only example of SPIRV-Cross reflection APIs; do not copy its StandardShader-only naming policy.
- `toolchain-lock.json`: authoritative SDK image and Cg mount/hash configuration.
- `scripts/build.ps1`: existing process/toolchain validation patterns; read-only in this phase.

Pre-existing user edits: `Makefile`, `scripts/build.ps1`, `scripts/tests/build-contract.tests.ps1`, and unrelated changes in sibling helengine. Do not revert them. The prototype files above are unfinished task-owned work, not completed production functionality.

## Review Focus

1. Include callbacks: nested/conditional includes, UTF-8 byte lengths and exceptions must not corrupt native memory or bypass diagnostics. Task 1 tests these.
2. Optimization and stage interfaces: an unused vertex output can change slot allocation; names alone do not establish compatibility. Tasks 2 and 4 test this.
3. Resource identity: one texture sampled through two samplers and constant-buffer members with padding must retain distinct mappings. Tasks 2 and 4 test this.
4. Process/path safety: spaces, failed tools, stale output and parallel compile jobs must not return someone else's program. Task 3 tests this.
5. Malformed binaries and dishonest success: zero exit status, header magic or file size alone must not establish a valid program. Tasks 3 and 5 test this.

## Validation commands

Run focused .NET tests with an explicit output root:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet --filter "(FullyQualifiedName~Ps3Shader|FullyQualifiedName~Ps3Rsx|FullyQualifiedName~Ps3Docker)&Category!=Docker" --verbosity quiet
```

Docker tests must be explicitly identified as integration tests and must run for phase acceptance. Ordinary tests must not silently claim Docker coverage when Docker is absent. Capture potentially large output in build-owned logs and return bounded excerpts. Request tool escalation when the sandbox requires it.

## Task 1: Deterministic frontend requests and safe include callbacks

**Files:**
- Modify: `builder/shaders/Ps3ShaderSourceCompiler.cs`
- Create: `builder/shaders/Ps3ShaderIncludeSession.cs`
- Create: `builder/shaders/Ps3ShaderSourceDependency.cs`
- Modify: `builder.tests/Ps3ShaderSourceCompilerTests.cs`
- Create: `builder.tests/Ps3ShaderIncludeResolverStub.cs`

**Interfaces:**
- Consume existing `ShaderCompileRequest` and `IShaderIncludeResolver.Resolve(string requestingFile, string includePath)`.
- Add `string Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver)`; migrate tests to the required resolver overload, removing the resolver-free prototype overload.
- `Ps3ShaderSourceDependency` has immutable `string Path` and `string ContentHash` properties; constructor requires both.
- `Ps3ShaderIncludeSession` owns shaderc include callbacks and allocations for one compile, records resolved dependency contents/hashes, and implements IDisposable. Exceptions become compiler diagnostics and are never thrown across a native callback boundary.

- [ ] **Step 1: Write request and include regression tests first.** Keep the existing self-contained shader fixture. Add a resolver stub class with a dictionary and a recorded request list. Test an active include, a nested relative include, an inactive missing include under `#if 0`, a missing active include, a throwing resolver, UTF-8 content, and a define-selected branch. Use explicit assertions:

```csharp
Assert.Contains("void main()", output);
Assert.Contains(resolver.Requests, request => request.IncludePath == "color.hlsl");
Assert.DoesNotContain(resolver.Requests, request => request.IncludePath == "inactive.hlsl");
Assert.Contains("missing.hlsl", error.Message);
```

Define the stub's recorded request as its own documented class/file if needed; no tuples/local helper functions. Reuse the existing test CreateRequest method with optional define/source arguments, not new production defaults.

- [ ] **Step 2: Run the targeted tests and record expected failures.** Missing overload compile failures establish the API addition; after adding the signature, demonstrate at least one behavioral failure before implementing callback logic.
- [ ] **Step 3: Implement the callback lifetime boundary.** Keep delegates rooted until shaderc releases the result/options. Allocate name/content using UTF-8 byte lengths; release each include allocation exactly once via shaderc's release callback. A failed resolution returns an error include result. Dispose residual allocations on all exits. Let shaderc handle include guards and conditional branches. Record actual active dependencies, including repeated paths with identical content. Fail if a resolver returns different contents for the same path in one compilation.
- [ ] **Step 4: Validate requests before entering native code.** Reject non-Ps3 targets and stages other than Vertex/Pixel. Preserve request defines/options; request debug names needed for reflection independently of optimization. A warnings-as-errors request must not silently downgrade warnings. Keep the original include path in diagnostics.
- [ ] **Step 5: Run focused tests repeatedly, inspect the diff and report.** Report test counts, command, output path and changed files; do not claim allocation safety from passing tests alone. Parent reviews native callback ownership before the next task.

## Task 2: Reflection that survives optimization

**Files:**
- Modify: `builder/shaders/Ps3ShaderSourceCompiler.cs`
- Create: `builder/shaders/Ps3ShaderTranslation.cs`
- Create: `builder/shaders/Ps3ShaderInterfaceVariable.cs`
- Create: `builder/shaders/Ps3ShaderUniformMember.cs`
- Create: `builder/shaders/Ps3ShaderSamplerBinding.cs`
- Create: `builder/shaders/Ps3ShaderReflection.cs`
- Create: `builder.tests/Ps3ShaderReflectionTests.cs`

**Interfaces:**
- Add `Ps3ShaderTranslation Translate(ShaderCompileRequest request, IShaderIncludeResolver includeResolver)`; Compile delegates to Translate and returns GlslSource.
- Translation exposes immutable `Stage`, `GlslSource`, `Inputs`, `Outputs`, `UniformMembers`, `Samplers`, `Dependencies`. Collections are copied at construction, not externally mutable.
- Interface variable fields: `Semantic` (authored HLSL semantic), `Location` (SPIR-V location), `ScalarType`, `VectorWidth`, `Columns`, `ArrayLength`, `Interpolation`, `GeneratedName`. Represent built-ins separately from user varyings; do not invent POSITION/TEXCOORD identities from ordinal order.
- Uniform member fields: `BufferName`, `BufferBinding`, `MemberPath`, `ByteOffset`, `ByteSize`, `ArrayStride`, `MatrixStride`, `RowMajor`, `GeneratedName`, plus scalar/vector/matrix shape. Nested members retain complete member paths and offsets.
- Sampler binding fields: `TextureName`, `TextureBinding`, `SamplerName`, `SamplerBinding`, `GeneratedName`, `Dimension`. Each combined texture/sampler pair is distinct.
- New DTO files each contain exactly one documented type; use existing ShaderStage, strings and unsigned integers rather than adding shared-engine enums.

- [ ] **Step 1: Add reflection tests.** Compile optimized and unoptimized variants of the same source and compare active authored identities. Add two cbuffers with the same member name, a padded float3/scalar/matrix buffer, reordered UV/color varyings, and one texture used with two sampler states. Assert:

```csharp
Assert.Equal(2, translation.Samplers.Count);
Assert.NotEqual(translation.Samplers[0].GeneratedName, translation.Samplers[1].GeneratedName);
Assert.All(translation.UniformMembers, member => Assert.False(string.IsNullOrWhiteSpace(member.MemberPath)));
Assert.Contains(translation.Outputs, variable => variable.Semantic == "TEXCOORD0");
```

Also assert exact expected offsets/strides from the fixture declarations, not merely nonempty values. For unsupported storage images, runtime arrays and nonzero resource spaces, assert explicit unsupported-resource diagnostics until those cases have a tested RSX mapping.

- [ ] **Step 2: Run the tests red.** Record which reflection facts the prototype loses.
- [ ] **Step 3: Implement SPIRV-Cross reflection before emission.** Use `CompilerCreateShaderResources`, `ResourcesGetResourceListForType`, member/type/layout queries and decoration accessors. Obtain semantic identities from compiler-provided HLSL metadata; if unavailable for an active interface, fail with an actionable diagnostic rather than parsing generated GLSL or guessing. Inspect installed API bindings for exact signatures.
- [ ] **Step 4: Assign stable names through compiler APIs.** Namespace generated names by resource category and binding/member identity; record the mapping. Name combined samplers by both resource bindings. Use `CompilerSetName`/member naming APIs before `CompilerCompile`; never perform string replacement on emitted source. Retain optimizer support but preserve the reflection data it needs. Check every native API result.
- [ ] **Step 5: Run tests green and re-run the existing real SDK probe.** Both stages must still compile with `cgcomp -Wcg,-oglsl`. Inspect the dump to establish that named uniforms/attributes survive into Cg reflection. Parent reviews the metadata contract and documents any unsupported cases before Task 3 consumes it.

## Task 3: Locked Docker compiler and RSX binary validation

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

## Task 4: Validate authored interfaces against actual RSX slots

Controller correction from pinned SDK parsers: compare canonical RSX routing channels, not raw register-number equality. Vertex TEX0 has index 7, fragment TEX0 has index 4, yet they form a valid link. Preserve raw indices in VertexSlot/PixelSlot and canonical strings in VertexChannel/PixelChannel (`TEX0` etc.). Test valid unequal indices and invalid equal indices whose channels differ. This supersedes numeric-slot equality wording in this task; see execution/ps3-hlsl-compiler/sdk-format-evidence.md.

**Files:**
- Create: `builder/shaders/Ps3ShaderProgramPair.cs`
- Create: `builder/shaders/Ps3ShaderPairCompiler.cs`
- Create: `builder/shaders/Ps3ShaderBindingMap.cs`
- Create: `builder/shaders/Ps3ShaderBindingMapBuilder.cs`
- Create: `builder.tests/Ps3ShaderPairCompilerTests.cs`
- Create: `builder.tests/Ps3ShaderBindingMapTests.cs`

**Interfaces:**
- `Ps3ShaderPairCompiler(Ps3ShaderSourceCompiler sourceCompiler, IPs3RsxCompiler rsxCompiler)`.
- `Compile(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest, IShaderIncludeResolver includeResolver, string workingRoot, CancellationToken cancellationToken)` returns Ps3ShaderProgramPair.
- Pair exposes `Vertex`, `Pixel` (Ps3RsxProgram), `VertexTranslation`, `PixelTranslation` and `Bindings`.
- Binding map associates each active authored uniform member, attribute and combined sampler identity with the actual RSX parameter; represents stage links as authored semantic/type/interpolation plus vertex-output and pixel-input hardware slots. Each mapping record has its own documented type/file. A map never substitutes missing data with register zero.
- `Ps3ShaderBindingMapBuilder.Build(Ps3ShaderTranslation vertexSource, Ps3RsxProgram vertexProgram, Ps3ShaderTranslation pixelSource, Ps3RsxProgram pixelProgram)` returns the validated map.

- [ ] **Step 1: Add failing linkage tests.** Include: UV + color, reordered struct fields, an unused vertex output, matching names with mismatched hardware slots, width/type/interpolation mismatch, and a fragment input absent from the vertex stage. Add constants with duplicate member names in different buffers, optimized-out constants, and texture/sampler pairs sharing a texture but not a sampler.

```csharp
Assert.Throws<InvalidOperationException>(() => builder.Build(vertexSource, wrongSlotVertex, pixelSource, pixelProgram));
Assert.All(map.StageLinks, link => Assert.Equal(link.VertexChannel, link.PixelChannel));
Assert.All(map.Uniforms, binding => Assert.False(string.IsNullOrWhiteSpace(binding.AuthoredMemberPath)));
```

Define the test fixture objects explicitly using the DTOs from Tasks 2/3; do not make tests pass by assuming semantic numbers equal hardware register indices.

- [ ] **Step 2: Run tests red and establish the hardware-slot evidence.** Read Cg dump interface declarations and the pinned binary reflection. Parse only documented forms required for the accepted subset. Unknown/ambiguous active reflection is an error, not a skipped record.
- [ ] **Step 3: Implement reconciliation.** Match authored metadata to generated names, then to final Cg/RSX parameters. Match inter-stage semantic/type/interpolation and compare actual hardware slots. Unused vertex outputs may remain; every active fragment input needs a matching producer. Allow truly optimized-out uniforms to disappear without inventing bindings. Reject overlapping/conflicting active maps. If legal HLSL cannot be assigned compatible hardware slots through compiler APIs/options, diagnose that limitation; do not patch generated GLSL or assembly.
- [ ] **Step 4: Implement the pair compiler.** Validate both requests use Ps3 with the correct stages and compatible variant identity. Translate and compile each stage into unique children of the supplied work root, then build the map. Do not publish a partially valid pair. Thread cancellation through both jobs. No backend/service cache yet.
- [ ] **Step 5: Run synthetic linkage tests and real-Docker pair tests.** The modern texture/cbuffer fixture must produce a valid map. At least one nontrivial two-varying fixture must compile or produce the explicit tested slot-incompatibility error; record which is supported. Parent decides whether limitations require compiler-option changes before cooker work begins.

## Task 5: Reproducible compiler acceptance and handoff

**Files:**
- Create: `scripts/tests/ps3-hlsl-compiler.tests.ps1`
- Create: `docs/Ps3ShaderCompiler.md`
- Modify only if necessary: `builder.tests/Ps3ShaderSourceCompilerTests.cs` to remove environment-dependent side effects from ordinary unit tests once integration tests own probe output.
- Verify existing changes: `builder/helengine.ps3.builder.csproj`, sibling `ShaderCompileTarget.cs`, `ShaderTargetDescriptors.cs`.

**Interfaces:**
- Acceptance script requires `-OutputRoot` as an absolute build-owned path and resolves the repository relative to PSScriptRoot. Runs the full focused shader tests and the Docker category, then fails unless both reports are successful. Reports compiler identity and output locations; no implicit toolchain download or host fallback.

- [ ] **Step 1: Add target compatibility assertions.** Existing enum numeric values remain unchanged; Ps3 descriptor has name `ps3` and define `HEL_API_PS3`. Verify target-specific defines reach shaderc. Run the whole builder test suite, not only the new tests.
- [ ] **Step 2: Add failure-path script tests and real acceptance invocation.** Missing output root, invalid lock/Cg library and failed tests must return nonzero. Do not execute a second identical suite solely to produce a PASS marker. Acceptance command:

```powershell
./scripts/tests/ps3-hlsl-compiler.tests.ps1 -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/acceptance
```

- [ ] **Step 3: Document verified behavior.** Include exact preparation prerequisites from the existing repository docs, the command above, supported stages/resources, include/define behavior, source-to-RSX pipeline, generated artifact locations and explicit limitations. Say that this is compiler infrastructure, not enabled authored shader rendering. Do not label a limitation supported because a program compiled once.
- [ ] **Step 4: Parent reviews phase evidence and writes the next plan.** Record types/format evidence, commands and test results in the task report. The next plan must address the shared cache identity gap, concrete material/program selection and cooker staging before any native runtime feature is enabled.

## Handoff and approval checkpoint

Helena has approved the design and selected Luna implementation. This plan is presented for review under writing-plans; preserve that execution method after approval. No implementation agent has been launched by writing this document. Once approved, execute tasks continuously unless a genuine authority/safety blocker or an unresolved load-bearing contract requires intervention.

## Parent self-review

- Spec coverage: this plan covers the compiler, source metadata, Docker boundary and pair validation. Backend/cooker/cache and runtime acceptance are explicitly assigned to subsequent plans, not reported complete here.
- Interface consistency: Task 1 adds resolver support; Task 2 adds Translate; Task 3 consumes Translation and produces RsxProgram; Task 4 consumes both; Task 5 validates them without introducing a second compiler path.
- Shared files: SourceCompiler/its tests change serially in Tasks 1, 2 and 5. New files have one task owner. No concurrent writers are permitted.
- Review focus: all five failure classes above have named tests in their owning tasks.
- Scope safety: shader feature remains disabled, no new worktree, no changes to other platform builders, no user build-cache changes reverted, no independent review launched.
