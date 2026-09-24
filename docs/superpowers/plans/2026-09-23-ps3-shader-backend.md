# PS3 shared shader backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Make the validated PS3 compiler callable through the real shared shader service while retaining the reflection needed by the cooker.

**Architecture:** Implement IShaderBackend directly, not the text-parsing ShaderBackendBase. Return a typed ShaderCompileResult containing the immutable translation and validated RSX program alongside ordinary shared metadata. The cooker will consume these typed results and reconcile pairs with the existing binding-map builder, without recompiling stages.

**Tech Stack:** .NET 9, existing shaderc/SPIRV-Cross frontend, pinned Docker RSX compiler, xUnit.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-hlsl-shaders-design.md`; this plan implements the backend portion, not runtime enablement.

## Global Constraints

- Existing checkout only; no worktrees. Luna implements; parent plans and reviews.
- Native compilation and final RSX conversion remain in the repository's pinned Docker SDK.
- Never modify generated shader text, binaries or headers after compilation.
- Missing includes, toolchain configuration, SDK image, Cg library, invalid source, incompatible stage pairs and unsupported bindings are errors.
- No default shader or default binding is synthesized.
- Keep generated sources and diagnostics in the standard visible build root.
- Do not change other builders or unrelated native-cache work.
- Leave `Makefile`, `scripts/build.ps1`, `scripts/tests/build-contract.tests.ps1`, and all sibling repositories untouched in this task.
- One type per file, substantive XML documentation, PascalCase fields, no tuples/local functions/nullable-reference annotations.
- No screenshots, automatic feature enablement, push, or independent whole-branch review.

## Review Focus

1. Shared default binding policy shifts textures by 100 and samplers by 200; logical metadata must apply these without altering physical RSX bindings.
2. Cbuffer trailing padding and nested paths must survive; use declared buffer size, never infer it from active members.
3. Include changes must reach compilation even when the top-level source is unchanged; UseSharedCache must be false.
4. Scalar/vector/matrix and semantic indices must not be confused with physical hardware slots.
5. Invalid target, stage, model, space, root, empty SDK bytes, or metadata overflow must fail explicitly without a successful result.

### Task 1: Adapt compilation and reflected metadata to the shared backend

**Files:**
- Create `builder/shaders/Ps3ShaderBackend.cs`.
- Create `builder/shaders/Ps3ShaderCompileResult.cs`.
- Create `builder/shaders/Ps3ShaderProgramMetadata.cs`.
- Create `builder.tests/Ps3ShaderBackendTests.cs`.
- Create `builder.tests/Ps3ShaderBackendCompilerStub.cs` if a dedicated recording SDK stub is needed.
- Modify `docs/Ps3ShaderCompiler.md` to document invocation and limitations.
- Do not modify existing pair compilation or builder registration yet; registration will be coupled to cooker configuration in the next plan.

**Interfaces:**

```csharp
public sealed class Ps3ShaderBackend : IShaderBackend, IShaderBackendCachePolicy {
    public Ps3ShaderBackend(Ps3ShaderSourceCompiler sourceCompiler,
        IPs3RsxCompiler rsxCompiler, string workingRoot);
    public ShaderCompileTarget Target { get; } // Ps3
    public ShaderBackendCapabilities Capabilities { get; }
    public bool UseSharedCache { get; } // false
    public ShaderCompileResult Compile(ShaderCompileRequest request,
        IShaderIncludeResolver includeResolver);
    public Ps3ShaderCompileResult Compile(ShaderCompileRequest request,
        IShaderIncludeResolver includeResolver, CancellationToken cancellationToken);
}
public sealed class Ps3ShaderCompileResult : ShaderCompileResult {
    internal Ps3ShaderCompileResult(ShaderCompileRequest request,
        ShaderProgramDefinition definition, Ps3ShaderTranslation translation,
        Ps3RsxProgram program);
    public Ps3ShaderTranslation Translation { get; }
    public Ps3RsxProgram Program { get; }
}
internal static class Ps3ShaderProgramMetadata {
    internal static ShaderProgramDefinition Create(ShaderCompileRequest request,
        Ps3ShaderTranslation translation);
}
```

- [ ] Add failing tests using the real frontend and shared ShaderCompileService. A recording IPs3RsxCompiler returns nonempty immutable bytes for unit tests, records translation/root/token, and can throw a supplied exception. Do not use the existing empty-byte pair stub for successful backend results.

```csharp
Assert.False(((IShaderBackendCachePolicy)backend).UseSharedCache);
ShaderCompileResult result = service.Compile(request, resolver);
Ps3ShaderCompileResult ps3 = Assert.IsType<Ps3ShaderCompileResult>(result);
Assert.Equal(request.ProgramName, ps3.ProgramDefinition.Name);
Assert.Equal(ps3.Program.Bytecode, ps3.Binary.Bytecode);
Assert.Equal(128, frameBinding.Size); // declared padded fixture size
Assert.Equal(100, textureBinding.Slot); // t0 with default policy
Assert.Equal(200, samplerBinding.Slot); // s0 with default policy
```

Use existing cache test store interfaces, not a substitute implementation of ShaderCompileService. Compile twice after changing only resolver include contents and assert two SDK calls and different translated GLSL. Include overload cancellation tests (before any SDK call and exception propagation), malformed HLSL, wrong SDK stage and empty bytes. Run the focused test first and record the actual expected compilation/test failure.

- [ ] Implement request validation before frontend/SDK calls: nonnull dependencies, fully-qualified nonblank working root, PS3 target, vertex/pixel stage, model range 3.0 through 5.0 inclusive, binding policy DefaultSpace == 0. Capabilities advertise this source-language range and vertex/pixel only; explicitly document that accepted HLSL models do not imply equivalent hardware features. SDK output stage must match request and bytecode must be nonempty. Cancellation is checked before frontend, before SDK, and before returning. Preserve original exceptions; do not construct fake failed binaries.

```csharp
cancellationToken.ThrowIfCancellationRequested();
Ps3ShaderTranslation translation = SourceCompiler.Translate(request, includeResolver);
ShaderProgramDefinition definition = Ps3ShaderProgramMetadata.Create(request, translation);
cancellationToken.ThrowIfCancellationRequested();
Ps3RsxProgram program = RsxCompiler.Compile(translation, WorkingRoot, cancellationToken);
cancellationToken.ThrowIfCancellationRequested();
return new Ps3ShaderCompileResult(request, definition, translation, program);
```

Docker compiler already creates a unique job below the explicit root. The backend must not select its own temp directory or load a toolchain implicitly. Shared two-argument Compile delegates to the overload with CancellationToken.None.

- [ ] Build metadata solely from translation. Each reflected uniform buffer produces one ConstantBuffer ShaderBinding using BufferName, checked declared ByteSize and matching leaf members (MemberPath, checked offsets/sizes). Set is 0. Apply checked binding shifts by resource class; do not alter Translation. Guard overflow before calling the shared unchecked GetSlot implementation. Deduplicate texture/sampler logical resources by authored identity and register, while retaining all physical combinations in Translation. Reject conflicting names/registers instead of silently choosing one. Map 2D and Cube texture dimensions only; unsupported shared representations fail with the resource name and dimension.

For float data require ScalarType `FP3232`, widths 1..4, columns 1..4. Metadata type is `float`, `floatN`, or `float{VectorWidth}x{Columns}` in the reflected physical SPIR-V convention, documented explicitly. Preserve exact physical layout in Translation, including strides, RowMajor and ArrayLength; shared metadata is not the runtime upload ABI. Reject unrepresentable scalar types explicitly. Interfaces must be non-array scalar/vectors (Columns=1, ArrayLength=1). Split a trailing decimal semantic index: `TEXCOORD12` becomes `TEXCOORD`, 12; `POSITION` becomes `POSITION`, 0. Do not synthesize builtins omitted by reflection. Variants contain the requested variant and ordered defines (`NAME` for empty value, `NAME=VALUE` otherwise).

```csharp
int slot = checked(checked((int)sourceBinding) + shift);
ShaderBinding binding = new(buffer.BufferName, ShaderResourceType.ConstantBuffer,
    0, slot, checked((int)buffer.ByteSize), members);
```

- [ ] Test metadata with real reflected padded/nested cbuffers, multi-digit semantic indices, repeated texture with two samplers, nonzero shifts, and custom zero-shift policy. Use synthetic immutable translations only for unreachable boundary values (overflow, unsupported shapes/types, conflicts). Verify exact shared metadata and unchanged physical reflection. Check both vertex and pixel backends, request/variant/entrypoint identity, null arguments and validation cases listed above.

- [ ] Run focused tests, then the full pure suite once. Add one Docker-category test compiling a real matrix/tint/Texture2D HLSL pair through two backend calls and reconcile the returned translations/programs using Ps3ShaderBindingMapBuilder. Validate RSX bytes with the existing reader and compare the base Binary bytes with the actual SDK output. This proves the shared-service path reaches the SDK; it does not prove rendering.

Validation uses `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/backend/dotnet --filter FullyQualifiedName~Ps3ShaderBackendTests --verbosity quiet` with repository/test-output environment variables patterned after existing compiler tests. TEMP/TMP point to `.../backend/fixture-storage`, restored in finally; artifact root is a sibling of fixture-storage. Request escalation when writes to the standard build root are denied. Capture capped output and TRX under that build root. Native/SDK work only Docker.

- [ ] Document the shared backend API, explicit injected work root/compiler, no shared cache, logical-versus-physical metadata, and remaining cooker/runtime work. Self-review and report tests/files/concerns. Parent reviews before staging only named task files and committing `feat(ps3): expose reflected compiler through shared shader backend`.

## Plan self-review

The default policy's nonzero shifts are supported rather than rejected. Source metadata and RSX metadata remain separate, so cooker pair reconciliation does not lose matrix or sampler information. Constructor injection avoids platform-discovery side effects. This is the backend slice of the approved integration, not a claim that cooker/runtime are finished; subsequent work remains required by the active goal.
