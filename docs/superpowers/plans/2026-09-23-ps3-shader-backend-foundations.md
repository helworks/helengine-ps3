# PS3 Shader Backend Foundations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the shared compile-service cache safely bypassable and expose compiler-declared constant-buffer sizes before wiring the PS3 backend/cooker.

**Architecture:** Add an optional backend policy at the shared cache boundary; existing backends retain their behavior unless they explicitly opt out. Extend PS3 reflection with immutable buffer-level metadata obtained from SPIRV-Cross, never reconstructed from leaf offsets. This is a two-task prerequisite, not backend registration or runtime enablement.

**Tech Stack:** C#/.NET 9, xUnit, Silk.NET.SPIRV.Cross 2.21.0, existing PS3 compiler and pinned Docker SDK.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-hlsl-shaders-design.md`; detailed evidence in `docs/superpowers/execution/ps3-hlsl-compiler/next-phase-inputs.md`.

## Global Constraints

- Parent writes plans and resolves design questions; Luna writes implementation/tests. Explicit model and effort on every dispatch, no nested agents.
- Existing checkouts only; no worktrees, reset, cleanup, push or changes to unrelated native-cache/layout work.
- Shared shader-library changes are already in approved scope. No further baseplatform/editor edits in this plan.
- All outputs, fixtures and logs use `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/backend-foundations`; process-local TEMP/TMP use its `fixture-storage` child and are restored in finally.
- One type per file; substantive XML docs on every new member; PascalCase fields; no tuples, local functions or nullable annotations.
- No synthesized default shader, buffer size, path, binding or failure result. Preserve exceptions from compilation.
- Never rewrite generated sources/binaries. Final SDK conversion runs only in the pinned Docker image, with no host/WSL SDK fallback.
- Keep `shaders` disabled. No material schema, artifact serialization, backend registration or native renderer changes here.
- Cache bypass is a conservative correctness measure, not a dependency-complete cache key. Future PS3 backend must explicitly opt out until full immutable input/toolchain identity is available.
- Workers do not commit. Parent commits only named completed files after tests/review; the current request authorizes commits, not publishing.

## Evidence and scope

Previous work committed as PS3 `96ca68d` and shared engine `b806163d`. Fresh acceptance: 98/98 non-Docker, 7/7 Docker, 12/12 DTO/dependency and 2/2 real editor handoff. Broader editor class has two known missing Windows descriptor failures; it is not a required suite here.

Codebase-memory list_projects returned Transport closed during this planning turn. Graph generation and coverage are unknown. Bounded direct reads verified ShaderCompileService.Compile consults the cache before the backend, while ShaderCompileRequestIdentity omits resolved include contents, debug/optimization/warnings options and toolchain identity. A backend-local key cannot protect this earlier cache hit.

Direct reads also verified Ps3ShaderReflection.ReadUniformMembers already owns each uniform-buffer resource and its BaseTypeId before assigning GLSL names. Ps3ShaderTranslation exposes only leaf records today. The installed Silk 2.21.0 XML confirms CompilerGetDeclaredStructSize(Compiler*, CrossType*, ref nuint).

## Review Focus

1. A pre-existing same-request cache entry must not win for an opt-out backend (Task 1).
2. Include, option and toolchain changes must reach the backend even with identical current cache keys (Task 1).
3. Existing non-policy and explicit-cache-enabled backends must retain cache reads/stores (Task 1).
4. Buffer size must come from the compiled SPIR-V declaration, including matrices/arrays, not maximum leaf end or guessed alignment (Task 2).
5. Empty-resource stages, immutable snapshots and existing physical matrix/leaf metadata must remain correct (Task 2).

## Task 1: Optional shared-backend cache policy

Execution amendment after approval: the shared checkout already contains this policy and early branch using `UseSharedCache`. Preserve those pre-existing files; use that existing property instead of the proposed `UseSharedCompileCache` below. Task 1 now adds the four focused test files and validates actual baseline behavior. Do not remove prior work to manufacture RED. Commit ownership of the pre-existing shared files is being confirmed with Helena; code/tests can proceed without altering them.

**Files:**
- Create: `../helengine/engine/helengine.shader/shaders/compilation/IShaderBackendCachePolicy.cs`
- Modify: `../helengine/engine/helengine.shader/shaders/compilation/ShaderCompileService.cs`
- Create: `builder.tests/ShaderBackendCachePolicyTests.cs`
- Create: `builder.tests/ShaderCacheTestBackend.cs`
- Create: `builder.tests/ShaderCachePolicyTestBackend.cs`
- Create: `builder.tests/ShaderCacheTestStore.cs`
- Read-only: `builder.tests/Ps3ShaderIncludeResolverStub.cs`, existing shared cache/request/result contracts.

Tests intentionally live in the PS3 builder test project, which already references the shared compile service; do not create another project or attach them to the editor dependency graph.

**Interfaces:**

```csharp
namespace helengine {
    public interface IShaderBackendCachePolicy {
        bool UseSharedCompileCache { get; }
    }
}
```

An IShaderBackend without this optional interface behaves exactly as before. The policy is evaluated for each Compile call before key creation, lookup or storage. It is not inferred from target enum values.

- [ ] **Step 1: Build focused test fixtures and assertions.** ShaderCacheTestBackend implements IShaderBackend, exposes Calls, mutable ToolchainIdentity and optional Failure, and records supplied options/include text in returned test bytes. Constructor takes an explicit ShaderCompileTarget. Capabilities use ShaderModel(4,0), vertex/pixel, false, as the existing test backend does. Its Compile body is:

```csharp
Calls++;
if (Failure != null) throw Failure;
ShaderIncludeResult include = includeResolver.Resolve(request.Source.Path, "common.hlsli");
string text = string.Join("|", include.Source, ToolchainIdentity,
    request.Options.GenerateDebugInfo, request.Options.Optimize,
    request.Options.TreatWarningsAsErrors);
byte[] bytes = System.Text.Encoding.UTF8.GetBytes(text);
ShaderProgramDefinition definition = new(request.ProgramName, request.Stage,
    request.EntryPoint, [], [], [], [new ShaderVariant(request.Variant, [])]);
return new ShaderCompileResult(request, definition,
    new ShaderCompiledBinary(request.Target, request.Stage, request.EntryPoint,
        request.Variant, bytes), [], true);
```

Use explicit constructor-supplied toolchain identity; no default valid value. ShaderCachePolicyTestBackend inherits that test backend and implements IShaderBackendCachePolicy with a constructor-supplied bool. ShaderCacheTestStore implements IShaderCompileCache with a Dictionary<string, ShaderCompileResult>(StringComparer.Ordinal), TryGetCalls and StoreCalls. Index all operations using key.ToString(): ShaderCompileCacheKey is a reference type without value equality. Provide Seed(key,result) that populates without incrementing counters. No filesystem writes needed: use Ps3ShaderIncludeResolverStub.Files["common.hlsli"] with controlled contents.

Test request helper signature: `static ShaderCompileRequest Request(ShaderCompileTarget target, bool debug, bool optimize, bool warnings)`; use source path `Path.GetFullPath("cache-policy/main.hlsl")`, text `#include "common.hlsli"`, program `CacheProbe.ps`, entry `PS`, pixel stage, model 4.0, variant `default`, no defines, and zero-shift ShaderBindingPolicy(0,0,0,0,0). This is controlled test input, not an invented production source path.

Tests must exercise the actual ShaderCompileService:

```csharp
// Arrange an explicit false-policy backend, request, resolver and store;
// seed the request key with a result from a distinct stale backend instance.
ShaderCompileService service = new(resolver, store, new ShaderSourceHasher());
service.RegisterBackend(backend);
ShaderCompileResult result = service.Compile(request);
Assert.NotSame(staleResult, result);
Assert.Equal(1, backend.Calls);
Assert.Equal(0, store.TryGetCalls);
Assert.Equal(0, store.StoreCalls);
```

Add independent tests for: unchanged repeated requests still call twice; include content changes produce different Bytecode; each of the three options changes reaches the backend; replacing ToolchainIdentity reaches the backend; a backend exception propagates with zero cache interactions; null request and unregistered target keep existing exceptions. For each mutation, assert equality of ShaderCompileRequestIdentity.CreateCacheKey(...).ToString() values to demonstrate the stale-key case rather than comparing key-object references or accidentally testing a different key.

For legacy/no-interface backends (use DirectX11 and Vulkan enum values in a theory; no native compiler invocation) and explicit true policy, assert first miss/store, second hit, one backend call, two lookups and one store, with Assert.Same on returned result. Also seed a same-key result for these cases and assert it is returned without a compile/store. Do not change other platform backend implementations.

- [ ] **Step 2: Run RED.** `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path <root>/dotnet --filter FullyQualifiedName~ShaderBackendCachePolicyTests --verbosity quiet`. Initial failure must be the missing policy API, not an incorrect namespace or project reference. Preserve log and exit code.

- [ ] **Step 3: Add the interface and minimal early branch.** Immediately after GetBackend in ShaderCompileService.Compile:

```csharp
if (backend is IShaderBackendCachePolicy policy && !policy.UseSharedCompileCache) {
    return backend.Compile(request, includeResolver);
}
```

Leave the existing key/read/compile/store path below this branch unchanged. No PS3 special-case, key schema changes, global invalidation, new failure catches, or caching of the opt-out result.

- [ ] **Step 4: Run GREEN and task review.** Same filter, record exact count/exit code/log. The parent reviews the two shared changes and test behavior; no repeated editor suite or Docker run for this task alone. Document that the future real PS3 backend must implement UseSharedCompileCache=false; this task does not claim that backend already exists.

## Task 2: Reflect declared constant-buffer size

**Files:**
- Create: `builder/shaders/Ps3ShaderUniformBuffer.cs`
- Modify: `builder/shaders/Ps3ShaderReflection.cs`
- Modify: `builder/shaders/Ps3ShaderTranslation.cs`
- Modify: `builder/shaders/Ps3ShaderSourceCompiler.cs`
- Modify: `builder.tests/Ps3ShaderReflectionTests.cs`
- Modify: `builder.tests/Ps3ShaderBindingMapTests.cs` (explicit synthetic translation metadata only)
- Modify: `builder.tests/Ps3DockerShaderCompilerTests.cs` (translation constructor call only)
- Create: `builder.tests/Ps3ShaderUniformBufferTests.cs`
- Modify: `docs/Ps3ShaderCompiler.md` (new guarantees and cache policy limitation)

**Interfaces:** New sealed immutable Ps3ShaderUniformBuffer with constructor `(string bufferName, uint bufferBinding, uint byteSize)` and get-only BufferName, BufferBinding, ByteSize. Blank name and zero size throw ArgumentException/ArgumentOutOfRangeException respectively. Size describes the actual compiled SPIR-V struct, not necessarily the full unoptimized authored declaration and not an RSX register count.

Append required `IEnumerable<Ps3ShaderUniformBuffer> uniformBuffers` to the existing Ps3ShaderTranslation constructor. Expose `IReadOnlyList<Ps3ShaderUniformBuffer> UniformBuffers`, snapshot with the existing Copy helper, reject null collections/items. Update every named in-repo constructor call; no overload silently supplies missing buffer metadata for production translations. Synthetic fixtures pass explicit metadata corresponding to their deliberately authored test buffers; stages with no buffers pass an explicit empty array. Do not reconstruct production metadata from leaf member maxima.

- [ ] **Step 1: Add failing DTO and real reflection tests.** Constructor tests cover blank names/zero sizes; translation tests cover null list/items and copying against later mutation of the supplied list. Extend existing ReflectsPaddedAndNestedUniformMembers assertions:

```csharp
Ps3ShaderUniformBuffer frame = Assert.Single(translation.UniformBuffers,
    buffer => buffer.BufferName == "Frame");
Assert.Equal(0u, frame.BufferBinding);
Assert.Equal(128u, frame.ByteSize);
Assert.Equal(16u, Assert.Single(translation.UniformBuffers,
    buffer => buffer.BufferName == "MaterialPrimary").ByteSize);
Assert.Equal(16u, Assert.Single(translation.UniformBuffers,
    buffer => buffer.BufferName == "MaterialSecondary").ByteSize);
```

Use the existing request helper for a new optimized/unoptimized theory with this fully used padded fixture:

```hlsl
cbuffer Padded : register(b3) {
    float3 Direction; float Intensity;
    float4 Colors[2];
};
float4 PS() : SV_Target {
    return float4(Direction * Intensity, 1) + Colors[0] + Colors[1];
}
```

Assert exactly one Padded buffer, binding 3, size 48 for both modes, and preserve the existing member offset/stride tests. Add a constant-only `float4 PS() : SV_Target { return float4(1,2,3,4); }` fixture and assert empty UniformBuffers. Do not claim this metadata proves runtime constant upload or recovery of members removed by optimization.

- [ ] **Step 2: Run RED.** Use filter `FullyQualifiedName~Ps3ShaderUniformBufferTests|FullyQualifiedName~Ps3ShaderReflectionTests` with the same explicit artifacts root. Missing DTO/property/constructor is expected.

- [ ] **Step 3: Query each declared struct at the existing resource traversal.** In ReadUniformMembers after checking buffer binding/name and before generated renaming, obtain the size directly:

```csharp
CrossType* bufferType = Api.CompilerGetTypeHandle(Compiler, resource.BaseTypeId);
nuint declaredSize = 0;
Check(Api.CompilerGetDeclaredStructSize(Compiler, bufferType, ref declaredSize));
uint byteSize = checked((uint)declaredSize);
buffers.Add(new Ps3ShaderUniformBuffer(bufferName, binding, byteSize));
```

Add documented reflection UniformBuffers property and publish a read-only buffer collection after successful traversal. Forward reflection.UniformBuffers from EmitGlsl into the new required translation argument. Do not change generated GLSL, member naming, matrix major conventions, active resource selection or the binding-map algorithm. Retain duplicate binding and nondefault resource-space rejection.

- [ ] **Step 4: Update fixture call sites without changing their assertions.** Ps3DockerShaderCompilerTests.CreateTranslation has no reflected buffers and passes an empty buffer array. Binding-map synthetic fixtures declare their fixed buffer metadata directly with named literal sizes; extend their local fixture helper to take an explicit buffer collection when members exist. Do not add a production size-guessing helper for test convenience.

- [ ] **Step 5: Run focused GREEN, then one final compiler acceptance.** Focused filter adds Ps3ShaderBindingMapTests and Ps3RsxProgramReaderTests to the new/reflection tests. Then run the existing acceptance script once in a separate PowerShell process with OutputRoot `<root>/acceptance`; it covers the full pure set (including Task 1) and the seven Docker cases, with nonzero class coverage checks. Record new exact totals, SDK image identity, run id and logs. Do not assert the old 98 count as the new expected total.

- [ ] **Step 6: Document and review.** Update Ps3ShaderCompiler.md to distinguish declared compiled-buffer size, physical member layout and pending runtime packing; explain the opt-out seam without claiming registration or cache completeness. Parent performs scoped review, resolves findings with Luna, and commits only named completed files after validation.

## Validation environment

`<root>` in commands is exactly `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/backend-foundations`. Create it and its fixture-storage child explicitly. Save/restore process TEMP/TMP in try/finally around test commands. Use separate logs for RED and GREEN, capped excerpts, and check process exit codes plus test counts. Request filesystem escalation for sibling source writes and approved build output when needed. Do not run the acceptance script in the parent process because its existing environment-variable restoration limitation is deferred from the compiler task.

## Self-review / next handoff

Both tasks have independent acceptance gates. Optional cache policy preserves all unmodified backends; no active backend behavior is silently changed. Buffer-level reflection remains PS3-owned and does not alter shared runtime schemas. All new interfaces are defined above, and the installed Silk API signature was checked. Every Review Focus item maps to explicit tests.

This plan intentionally completes only the two backend safety prerequisites. The subsequent backend/cooker plan must define stage metadata payload encoding, reflected program definitions, supported binding policies, selected material programs/StandardShaderVariants, pair validation, normal artifact declarations/staging and contributor configuration with an explicit build root. Runtime loading, endianness/matrix uploads and texture rendering remain separate acceptance work. No additional editor/baseplatform changes are implied.

Status: both tasks implemented and scoped-review approved. Final acceptance125/125 pure +7/7 Docker; execution, amendments and remaining boundaries tracked in `docs/superpowers/execution/backend-foundations/progress.md`.
