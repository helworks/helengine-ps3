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

## Task 1: Optional shared-backend cache policy

## Execution amendment: pre-existing implementation

Baseline before dispatch already contains IShaderBackendCachePolicy.UseSharedCache and the matching early branch in ShaderCompileService. Preserve both files unchanged. Use the existing UseSharedCache property everywhere the plan below names UseSharedCompileCache; semantic contract is identical. Own only the four NEW test files and report. Do not delete, rename or alter existing shared work to manufacture RED. Run the new tests against the baseline first and report actual outcome, including initial GREEN if behavior already exists. Parent requested Helena's permission to incorporate the shared files at commit time; tests can proceed without that decision. If a real behavior gap emerges, report it before touching the shared implementation.

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

## Validation environment

`<root>` in commands is exactly `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/backend-foundations`. Create it and its fixture-storage child explicitly. Save/restore process TEMP/TMP in try/finally around test commands. Use separate logs for RED and GREEN, capped excerpts, and check process exit codes plus test counts. Request filesystem escalation for sibling source writes and approved build output when needed. Do not run the acceptance script in the parent process because its existing environment-variable restoration limitation is deferred from the compiler task.
