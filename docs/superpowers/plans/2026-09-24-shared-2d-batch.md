# Shared internal 2D batch implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Parent plans; Luna implements; preserve the existing checkouts, no worktrees. Execute checkbox steps task-by-task after user review.

**Goal:** Supply a reusable CPU 2D batch and portable built-in HLSL, with real renderer shader dependencies independent of materials.

**Architecture:** Existing drawable APIs feed a codegen-compatible CPU batch. A synchronous sink consumes contiguous runs; GPU ownership stays in each backend. Shared built-in variants travel through the existing shader compilation/cooking flow, with an optional builder capability for renderer-owned dependencies.

**Tech Stack:** Existing C# engine projects and test framework, HLSL, shared ShaderCompileService, PS3 Docker converter, generated C++ read-only.

**Spec:** `docs/superpowers/specs/2026-09-24-shared-2d-batch-design.md` (approved).

## Global Constraints

- Existing authored scenes and components remain unchanged.
- Parent writes the design and implementation plans; Luna implements after review.
- No worktrees, generated-output edits or screenshots.
- No public immediate-mode API, arbitrary user effects, new text shaping engine, batching across cameras, or unrelated global shader-feature enablement.
- Preserve the three preexisting dirty PS3 files and all unrelated shared changes; commit only named validated files, separately per repository; no push.
- Shared paths below are relative to `C:/dev/helworks/helengine/engine`; PS3 paths are relative to `C:/dev/helworks/helengine-ps3`.
- Read each repository's AGENTS.md; one type/file, substantive member comments, PascalCase fields, no tuples/local functions/nullable reference annotations.
- Outputs/logs/TEMP/TMP: `C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924`; save and restore process environment. Shared edits and external build-root writes require sandbox escalation, not a copied workaround.
- PS3 compilation and SDK conversion run only in the pinned Docker image. No host/WSL SDK fallback.

## Review Focus

1. A/B/A translucent texture sequences must remain three runs, not become A/A/B (Task2).
2. Mutation or disposal after enqueue must not alter copied vertices or invalidate GPU reads (Tasks2/3 and native plan).
3. A 2D-only scene must package both shader variants without a material dependency; changing HLSL invalidates cache (Task4).
4. Shape coverage must not multiply alpha twice or put border color over the whole fill (Task1).
5. Text wrapping/effect ordering and inherited scissors must remain shared semantics, not PS3-specific approximations (Task3).

## Fixed handoff contract

Engine support types may be public where generated/native consumers require access; this is not a new gameplay drawing API. Add no Begin/End methods to scene-facing components.

Shader asset `SpriteBatchShader`, source `SpriteBatchShader.hlsl`, program identities
`SpriteBatchShader.vs` / `SpriteBatchShader.ps`, variants `Textured` and `RoundedShape`.
Both use entries VS/PS; RoundedShape defines `HELENGINE_BATCH_ROUNDED=1`.
One constant buffer `BatchCameraBuffer` at b0, member `projection` float4x4.
Textured variant samples `BatchTexture` t0 / `BatchSampler` s0.
RoundedShape has no texture/sampler requirement after optimization.
Registers above are authored logical identities. Vulkan's existing default binding
policy shifts texture/sampler slots to100/200; validate that target's compiled slots
against ShaderBindingPolicies.Default rather than asserting physical slot0 on every
API. PS3 SDK slots still come from its measured binding map.
Registers above are authored logical identities. Vulkan's existing default binding
policy shifts texture/sampler slots to100/200; validate that target's compiled slots
against ShaderBindingPolicies.Default rather than asserting physical slot0 on every
API. PS3 SDK slots still come from its measured binding map.

`Batch2DVertex` consists of six float4 fields in this order: Position, TexLocal,
Color, Shape, Corners, BorderColor. Native packed layout is96bytes; offsets0/16/32/48/64/80.
POSITION0, TEXCOORD0, COLOR0, TEXCOORD1, TEXCOORD2, COLOR1 map respectively.
Position=(pixelX,pixelY,z,1), Y down. TexLocal.xy=UV, zw=local pixel coordinates
relative to shape center. Shape=(halfWidth,halfHeight,radius,borderWidth);
Corners=(TL,TR,BL,BR) as float0/1. Color is textured tint or shape fill; BorderColor
is straight-alpha border. Textured unused fields are explicitly zero.
Converters must prove this ABI before downstream work; failures amend source/ABI,
never compiled blobs, and invalidate the dependent native plan's layout assumptions.

Capacity1024 quads,4096 vertices,6144 ushort indices, index pattern0,1,2,0,2,3.
Capacity is configurable1..16384 quads; reject outside bounds. Use fixed arrays and
counts; no object allocation per vertex/glyph. Runs start at index0 of a reused
chunk; flush on key change or capacity. Sink must consume/copy before returning.

`Batch2DRun` value contains Variant, Texture (borrowed RuntimeTexture), and four int
fields ScissorX, ScissorY, ScissorWidth, ScissorHeight. Its constructor takes these
six fields in that order; Variant uses Batch2DVariant.Textured or RoundedShape.
Projection/viewport are fixed for a session, not implicit device state. Sampler is
linear-clamp and blend is straight-alpha throughout this first contract. A future
policy extension must become part of the key before accepting new state.
The pixelViewport float4 is (x,y,width,height), with finite coordinates and strictly
positive width/height. Include a nonzero-origin camera test; zero extents are invalid.
Every ConfigureCamera call establishes a camera-session boundary, even when the
projection and viewport values equal the previous camera's. Flush the old run
before calling SetCamera; never infer camera identity from numeric equality.

## Task 1: Shared shader and converter feasibility

**Create shared:**
- `helengine.editor/shaders/builtin/SpriteBatchShader.hlsl`
- `helengine.shader/shaders/SpriteBatchShaderVariants.cs`
- `helengine.editor.tests/shaders/SpriteBatchShaderTests.cs`
**Modify PS3:** `builder/shaders/Ps3ShaderArtifactCooker.cs` (exact new built-in variant dispatch).
**Create PS3 tests:** `builder.tests/Ps3SpriteBatchShaderTests.cs`.
**Test infrastructure only, if required:** `builder.tests/helengine.ps3.builder.tests.csproj`
may enable unsafe blocks for the existing Silk.NET.SPIRV.Cross API. Do not add a
new compiler package or change production reflection solely for this proof.
**Read:** StandardShaderVariants, EditorProjectShaderSourceResolver, current shader compiler tests; reuse real compiler setup rather than a second compiler executable path.

Measured execution adjustment: ShaderBackendBase currently leaves Inputs/Outputs
empty and parses binding metadata from source. Validate actual DX11 bytecode with
the already referenced SharpDX reflection API. Validate actual Vulkan SPIR-V with
the existing SPIRV-Cross dependency in the PS3 test project, using bytecode exported
by the shared compiler test; do not treat source-derived metadata as reflection.
Keep this distinct from the PS3 frontend and SDK binding proof below. Inspection
of a different backend's compiled bytes is not Vulkan compilation evidence.

- [x] Add failing tests for the two exact variants, entry points, defines and reflection.
  New shader identity must reject unknown variants; existing ForwardStandard and
  custom-default behavior must remain unchanged. Example assertions after compiling:

```csharp
Assert.Equal("Textured", textured.VariantName);
Assert.Equal("RoundedShape", rounded.VariantName);
// Inspect actual compiler reflection: textured POSITION0/TEXCOORD0/COLOR0,
// one projection matrix, one 2D texture/sampler; shape's six attributes and no sampler.
```

- [x] Run RED using the existing focused shader test entry points. A missing source
  file/type is an initial RED; final reflection assertions must exercise the real
  compiled output. This task proves compiler input shape/bindings; Task3's native
  sizeof/offsetof probe validates packed stride96 after Batch2DVertex exists and
  before any native integration is released.
- [x] Implement VS using `mul(input.Position, projection)` and pass required fields
  only for the selected variant. Textured PS is exactly:

```hlsl
return BatchTexture.Sample(BatchSampler, input.texCoord) * input.color;
```

- [x] Implement rounded signed-distance coverage with CPU-decoded corner flags;
  select corner using local X/Y signs (top means localY<0). Radius clamps to the
  smaller half extent, border to nonnegative. Use fixed0.5 pixel coverage half-width
  with transformed geometry dimensions already expressed in pixels. No derivatives,
  bitwise masks or per-rectangle constant-buffer updates. Compose disjoint coverage:

```hlsl
float outer = 1.0f - smoothstep(-0.5f, 0.5f, outerDistance);
float inner = borderWidth > 0.0f
    ? min(outer, 1.0f - smoothstep(-0.5f, 0.5f, innerDistance)) : outer;
float fillAlpha = fillColor.a * inner;
float borderAlpha = borderColor.a * max(outer - inner, 0.0f);
float alpha = fillAlpha + borderAlpha;
float3 rgb = alpha > 0.0001f
    ? (fillColor.rgb * fillAlpha + borderColor.rgb * borderAlpha) / alpha : 0.0f;
return float4(rgb, alpha);
```

  For border covering the entire shape, set inner coverage0 rather than evaluating
  a zero-size inner rectangle. Reject nonfinite source parameters in CPU expansion.
- [x] Compile both variants via available DX11/Vulkan/Vita/WiiU/PS3 backends.
  PS3 must reach immutable P3SP and validated SDK bindings, not just intermediate
  GLSL. Store named target results and source/toolchain hashes in build-root logs.
  Missing SDKs are explicit gaps; do not install SDKs or claim execution on them.
- [x] Test CPU coverage oracle at opaque center, transparent exterior, half-alpha
  edge, border-only and asymmetric selected corners. Native plan supplies GPU proof.
- [x] Parent reviews exact files and compiler evidence before named commits.
  Gate Task2 on PS3 conversion success; no guessed hardware attribute mappings.

## Task 2: Bounded CPU batch and synchronous sink

**Create shared under `helengine.core/managers/rendering/batch2d/`:**
`Batch2DVertex.cs`, `Batch2DVariant.cs`, `Batch2DRun.cs`, `Batch2DBuffer.cs`,
`Batch2DSink.cs`, `Batch2DWriter.cs`.
**Create tests under `helengine.core.tests/managers/rendering/`:**
`Batch2DWriterTests.cs`, `RecordingBatch2DSink.cs`.

**Interfaces (all required constructor inputs validated):**

```csharp
// Batch2DBuffer owns the arrays/counts; sink borrows it only during Submit.
public abstract class Batch2DSink {
    public abstract void SetCamera(float4x4 projection, float4 pixelViewport);
    public abstract void Submit(Batch2DBuffer buffer, Batch2DRun run);
}
// Batch2DWriter owns one buffer and one borrowed sink.
public Batch2DWriter(Batch2DSink sink, int quadCapacity);
public void ConfigureCamera(float4x4 projection, float4 pixelViewport);
public void AppendQuad(Batch2DRun run, Batch2DVertex a,
    Batch2DVertex b, Batch2DVertex c, Batch2DVertex d);
public void Flush();
public void Dispose(); // releases CPU containers; never implicitly submits
```

- [x] RED tests use a recording sink that copies values during Submit. Append two
  identical-key quads then Flush: one run,8vertices,12indices. A/B/A: three runs in
  original order. Capacity1: two identical-key quads produce two submissions.
  ConfigureCamera is required before AppendQuad; a camera change flushes the old
  run before SetCamera and never merges across the boundary. Empty Flush produces
  none. Different scissor/variant splits a run. Literal index
  expectations for the second quad are4,5,6,4,6,7.
- [x] Implement equality using texture reference identity plus exact variant/scissor
  values. Validate state/finite vertices and live textured resource before mutation.
  Shape has no texture requirement; textured missing resource is an error.
  Validate capacity before arrays are allocated. Copy structs, never retain drawable
  references. Reset counts only after successful Submit; a failed sink faults the
  writer until disposal, preventing an accidental partial retry.

```csharp
if (HasRun && (!SameRun(CurrentRun, run) || QuadCount == QuadCapacity)) {
    Flush();
}
// Append four copied values and six checked ushort indices.
```

- [x] Add mutation test changing the original vertex/drawable after append; recorded
  positions/colors must not change. Reject append/flush after disposal, invalid
  capacity, nonfinite vertices and invalid scissor extents. Zero-area scissor skips
  the draw deterministically; it must not become an unrestricted scissor.
- [x] GREEN: `dotnet test helengine.core.tests/helengine.core.tests.csproj
  --filter FullyQualifiedName~Batch2DWriterTests -c Release --artifacts-path <root>/shared-tests`.
  Run in shared engine directory; preserve output/TEMP rules. Parent checks counts,
  ownership annotations and source diff, then commits named files.

## Task 3: Shared drawable expansion and generated reachability

**Create shared in the batch2d directory:** `Batch2DGeometry.cs`,
`Batch2DTextEmitter.cs`, `Batch2DDrawableEmitter.cs`, `Batch2DCameraProjection.cs`.
**Create tests:** `Batch2DGeometryTests.cs`, `Batch2DTextEmitterTests.cs`,
`Batch2DDrawableEmitterTests.cs` in the Task2 test directory.
**Read/reuse:** TextLayoutUtils, TextLineOffsets2D, TextRenderEffectPassBuilder,
ClipScissorStack2D, DirectX11Renderer2D draw methods and the actual component transforms.

**Interfaces:** emitter constructed with `Batch2DWriter` and the owning RenderManager2D
for intentional white-pixel acquisition. Methods `EmitSprite(ISpriteDrawable2D,
Batch2DRun)`, `EmitText(ITextDrawable2D,Batch2DRun)`,
`EmitRoundedRect(IRoundedRectDrawable2D,Batch2DRun)` snapshot all state and append.
Projection utility returns float4x4 from a validated pixel viewport; native code
uses the same coefficients and reflected matrix layout.

- [ ] RED tests pin a sprite destination(10,20,30,40): corners(10,20),(40,20),
  (40,60),(10,60); source(.25,.5,.5,.25) yields UV(.25,.5)..(.75,.75).
  Add nonuniform parent scale and rotation about the existing sprite center, tint
 255/128/0/64, empty/disabled drawables, texture-sized fallback when Size is nonpositive.
  Validate shader output position against viewport offset, not just origin0.
- [ ] Implement shared geometry with double intermediate math and float output;
  match current component transform conventions rather than inventing pivot APIs.
  Projection maps viewport top-left to(-1,+1) and bottom-right to(+1,-1).
  Rounded local coordinates remain unrotated while screen positions rotate.
- [ ] RED text tests construct a two-glyph test font with literal metrics: atlas
  16x16, glyphA UV(0,0,.25,.5), advance5, offsetY2, lineHeight10, space3;
  at scale2, glyph size8x16, advance10, baseline offset4. Test `A A\nA`, wrapping,
  left/center/right offsets using existing helpers and missing-character behavior.
  Verify per-glyph shadow, four outline copies, then main glyph order matches DX11.
- [ ] Implement text using the existing helpers and font atlas dimensions. Do not
  rasterize or shape fonts again, replace missing glyph policy, or duplicate layout
  in native C++. Dispose helper-owned lists/arrays under native ownership rules.
- [ ] Test solid and rounded fills, all16 corner masks, border0, border>=half-size,
  rotation and alpha. Decode mask in CPU; emit per-quad shape attributes. Do not
  use per-object uniforms. Reuse clip-chain resolution; scissor is supplied as a
  snapshot and affects run breaks rather than modifying already queued commands.
- [ ] GREEN focused Batch2D tests plus existing text/region tests affected by actual
  changes. Generate Core using the existing standard codegen path and compile a
  Docker probe that constructs writer/emitter and a recording native sink, checks
  value layout and invokes text expansion. Record actual generated signatures in
  `docs/Shared2DBatchContract.md` in the PS3 repo for the native handoff. If reachability
  omits types, fix the owning source/codegen-root declaration; never patch output.
  No native integration dispatch until this probe passes.
- [ ] Parent reviews and commits exact task files and measured contract document.

## Task 4: Renderer shader dependencies and packaging

**Create shared:** `helengine.baseplatform/Builders/IPlatformRendererShaderDependencyProvider.cs`,
`helengine.editor/managers/project/EditorRendererShaderDependencyResolver.cs`.
**Modify shared:** `helengine.editor/managers/project/EditorPlatformAssetCookService.cs`.
**Read/test without modifying:** `helengine.editor/managers/project/EditorScenePackageCache.cs`;
it already fingerprints both packaged and source built-in shader directories and
the builder assembly identity. Preserve this behavior; do not add a second cache policy.
**Create tests:** `helengine.editor.tests/managers/project/EditorRendererShaderDependencyTests.cs`.
**Modify PS3:** `builder/Ps3PlatformAssetBuilder.cs`.
**Create PS3 tests:** `builder.tests/Ps3RendererShaderDependencyTests.cs`.

```csharp
public interface IPlatformRendererShaderDependencyProvider {
    IReadOnlyList<PlatformShaderDependency> RendererShaderDependencies { get; }
}
```

- [ ] RED: opted-in builder with zero material dependencies still delivers both
  SpriteBatchShader variants to the real shader cook request; non-opted-in builders
  preserve prior behavior. Duplicate identities deduplicate ordinally; conflicting
  identities or null entries fail explicitly. Do not mock the merge under test.
- [ ] Implement resolver merging renderer dependencies before the existing
  zero-dependency early return. Reuse EditorProjectShaderSourceResolver and real
  SourcePath/hash forwarding. PS3 supplies the two identities fixed above. Missing
  shader artifact capability with declared dependencies is an explicit error.
- [ ] Test real source resolution, two declared P3SP files in packaged `cooked/shaders`,
  source changes invalidate output, variant define changes invalidate the compile
  cache, and unchanged inputs remain deterministic. Use build-owned copied fixture
  sources for mutation tests, never authored or SDK output files.
- [ ] Run focused editor and PS3 dependency/cooker tests; preserve prior material
  cooking tests and no-shader builder behavior. Real converter test must prove both
  P3SPs contain expected identities/reflection and differ in program payload.
- [ ] Parent verifies source fingerprint coverage, packaged path domain and exact
  two variant identities, then commits separately per repo. This releases native
  plan Task1 only after Tasks1-3 also pass.

## Parent self-review and execution handoff

Task1 establishes the ABI consumed by Tasks2/3 and native submission. Task2 owns
buffering and Task3 owns expansion; they share types, so implementation is sequential.
Task4 shares only shader identities with Task1 and must not run a competing cooker
edit concurrently. Tests cover all five review risks; native lifetime/pixel evidence
is explicitly delegated to the dependent plan, not claimed here. Inspect actual
generated signatures before native implementation; ABI failures require a plan
amendment and revalidation rather than guesses. Both plans approved by the user on
2026-09-24; Tasks1/2 are reviewed and committed. Task3 CPU expansion is in progress;
the generated native ABI probe remains a required gate before native integration.

## Execution evidence — Task1 complete

Source SHA256: `3D5A8BFA97C2F94F973F9198200C5601F74809E7D628F96BE0FFDFA78D57E0D2`.
All artifacts below use the approved `2d-batch-20260924` build root.

| Target | Evidence | Remaining limitation |
|---|---|---|
|DX11|Both variants/stages compiled; actual bytecode reflection checks six declared float4 attributes, variant-specific read masks, projection and sampler resources. Shared focused tests3/3.|No runtime rendering in this task.|
|Vulkan|Both variants/stages compiled and exported; PS3 test project inspects actual SPIR-V with SPIRV-Cross, including projection64bytes, texture100/sampler200 and sampler-free shape.|No Vulkan runtime migration or rendering.|
|WiiU|Existing backend produced four GLSL stages; real CafeGLSL in existing image compiled both pairs, exit0.|Compilation only, no GPU run.|
|PS3|Real locked Docker cooker produced both P3SPs with validated SDK maps; new class6/6, focused existing/default variant regression run9/9.|Native batching/geometry/lifetime/runtime still unimplemented.|
|Vita|Backend and device-exchange requirements inspected; exchange environment unset.|No device compiler result; portability not claimed.|

PS3 P3SPs in `ps3-tests/artifacts/real-docker-cooker/cbca98d1b28540abb689093a4dc87005`:
Textured1598bytes SHA256 `334E2D0C3696EFF45EE699CE5600D52A01113D13DED9741102A3C9F27B98679A`;
RoundedShape3757bytes SHA256 `F63E05AFC928B7347946C78459DC03B760748908AA8E276B4FB19A4F6E4E40BA`.
SDK dumps preserve authored float4 inputs as ATTR0..2 (textured) and ATTR0..5
(rounded), projection c0..c3 in both vertex stages, sampler0 only for textured.
The native consumer must read those maps; these observations are not permission
to replace reflected binding with hardcoded slots.

Logs: `shared-tests/SpriteBatchShaderTests-7.log`,
`ps3-tests/SpriteBatch-Full-class-1.log`, `ps3-tests/SpriteBatch-Cooker-regressions-1.log`,
`wiiu-probe/Textured.cafeglsl.exit.txt` and `wiiu-probe/RoundedShape.cafeglsl.exit.txt`.
Scoped review approved Task1 without findings. Parent final verification passed
3 shared tests and 11 combined PS3 shader/regression tests; logs are
`commit-verification/shared.log` and `commit-verification/ps3.log`.
Commits: shared `49b69e5f6f0c461eb8ff95545d0d904a5a59baa4`,
PS3 `96c508f874db3110b4b596f7afbf1372082d6362`. Task2 is released.

## Execution evidence — Task2 complete

Shared commit `7c95bf0d0bf3bbb0e60fdaa36eaeb3d6ec726597` contains only the eight
named writer/buffer/sink/value/test files. Every ConfigureCamera call is an explicit
session boundary; equal-valued cameras do not merge. A regression test reproduced
the old behavior (expected two submissions, actual one) before the fix.
Scoped review and fix re-review accepted the implementation. Parent rebuilt against
the concurrent shared HEAD and verified 23/23 focused tests, with only existing
unrelated IDE0040 warnings. Evidence: `commit-verification/task2-fix-1-red.log`,
`task2-fix-1-green.log`, and `task2-current-head.log` beneath the approved build root.
One minor documentation wording nit remains: ConfigureCamera's summary says
"before a change" rather than "before each camera session"; behavior is covered.
Native generated layout, lowering, projection and GPU lifetime are not established
by these C# tests and remain in the following gates.
