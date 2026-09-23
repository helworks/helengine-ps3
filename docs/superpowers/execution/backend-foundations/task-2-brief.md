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

## Task 2: Reflect declared constant-buffer size

Execution context: Task 1 uses existing shared IShaderBackendCachePolicy.UseSharedCache (not the originally proposed UseSharedCompileCache name), now authorized for incorporation by Helena. Document that exact actual API. Task 2 owns only the files listed below; keep Task 1 tests/shared code unchanged. Reject null elements specifically in the new UniformBuffers collection without opportunistically changing unrelated collection contracts.

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
