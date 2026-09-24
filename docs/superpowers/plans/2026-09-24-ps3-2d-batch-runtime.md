# PS3 internal 2D batch runtime implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Parent plans; Luna implements in the existing checkout. Execute checkbox steps after user review and prerequisite evidence.

**Goal:** Render the unchanged cube_test sprites, glyphs and rectangles through the shared 2D shader and CPU batch, preserving the working lit cube.

**Architecture:** The PS3 adapter consumes shared CPU runs and supplies real textures, reflected shader bindings and bounded GPU buffers. The existing 3D frame entry point coordinates each camera's clear, 3D and 2D passes. Conservative explicit GPU completion protects reusable memory and texture mutation in this initial implementation.

**Tech Stack:** Generated Core interfaces, C++17, existing P3SP readers/program owners, pinned Docker PS3 SDK and RPCS3, standard platform build driver.

**Spec:** `docs/superpowers/specs/2026-09-24-shared-2d-batch-design.md`.
**Dependency:** `docs/superpowers/plans/2026-09-24-shared-2d-batch.md`, all four tasks reviewed and validated.

## Global Constraints

- Existing authored scenes and components remain unchanged.
- Parent writes the design and implementation plans; Luna implements after review.
- No worktrees, generated-output edits or screenshots.
- No public immediate-mode API, arbitrary user effects, new text shaping engine, batching across cameras, or unrelated global shader-feature enablement.
- Preserve the three preexisting dirty PS3 files and all unrelated shared changes; commit only named validated files, separately per repository; no push.
- Native types remain helengine::ps3, one type/file, substantive /// documentation and PascalCase fields.
- Sources below are relative to `C:/dev/helworks/helengine-ps3`; shared sources may only change within the preceding approved contract, not as native-build workarounds.
- All outputs/logs/generated probes/TEMP/TMP under `C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924`; restore process environment afterward.
- SDK/native compilation only in locked Docker image. No hand-authored Cg equivalent or SDK blob/header rewriting.
- No modification to Makefile, scripts/build.ps1 or scripts/tests/build-contract.tests.ps1. Their preexisting changes belong to the user.
- Keep ForcedDisabledFeatures unchanged. This task is a renderer capability, not completion of all generated shader APIs.

## Review Focus

1. CPU flush is not GPU completion; queued reads must finish before buffer/texture reuse (Task1/2).
2. 2D state must not leak into the next camera's 3D pass, and camera clear occurs only once (Task3).
3. Duplicate texture paths across content sources must not alias, and missing content must fail (Task1).
4. Unsupported camera targets must fail explicitly rather than accidentally drawing into the backbuffer (Task3).
5. Logs can report draws without visible content; real framebuffer evidence must identify glyphs, sprites and shapes separately (Task4).

## Entry gate and fixed contract

Projection convention clarification (measured 2026-09-24): generated Core matrices
retain the source row-vector convention, despite the profile metadata token
`native-column-vector`. Both actual batch vertex programs emit DP4 against c[0..3].
Pack Core columns into those vectors, as the existing 3D adapter does; do not move
shared translation to M14/M24 or change HLSL multiplication. The initial raw-column
probe failure is retained; the corrected boundary probe and later real GPU tests
are separate evidence gates. See workflow `task-3-projection-boundary-correction.md`.

Read the measured `docs/Shared2DBatchContract.md` from the shared plan. Do not invent
generated C++ method spellings from C# names. Confirm generated writer/emitter/sink
types exist and both actual PS3 shader variants have passed SDK compilation.
The planned vertex stride96, six float4 fields/offsets0,16,32,48,64,80 and shader
identities must agree with that measured document; otherwise parent amends this plan.

Initial GPU buffers hold1024quads/4096vertices/6144ushort indices. Synchronous
submission copies borrowed shared arrays to these buffers; the adapter never
retains a pointer into generated arrays after Submit returns. Shader attributes
bind by validated P3SP semantic mapping, never assumed RSX slot numbers.

Blend RGB is SRC_ALPHA/ONE_MINUS_SRC_ALPHA. Alpha is ONE/ONE_MINUS_SRC_ALPHA:
`Aout=As+Ad*(1-As)`. Disable depth test/write and culling for the 2D pass.
Use linear-clamp for textured runs. Shape runs require no dummy sampler.
No offscreen render-target support is introduced here; encountering one must name
the unsupported target before issuing drawing commands. Backbuffer camera viewport
and nested rectangular clips are supported.

## Task 1: Program manifest and real 2D texture lifetime

**Create:** `builder/Ps3RendererShaderManifestWriter.cs`,
`builder.tests/Ps3RendererShaderManifestWriterTests.cs`,
`src/platform/ps3/Ps3RuntimeTexture.hpp`, `.cpp`,
`src/platform/ps3/Ps3Batch2DResources.hpp`, `.cpp`.
**Modify:** `builder/Ps3PlatformAssetBuilder.cs`, `src/platform/ps3/Ps3Video.hpp`, `.cpp`,
`src/platform/ps3/Ps3AuthoredTexture.hpp`, `.cpp`.
**Create tests:** `tests/ps3_batch2d_resources_tests.cpp`,
`tests/Makefile.batch2d-resources`, `scripts/tests/ps3-batch2d-resources.tests.ps1`.

**Interfaces:**

```cpp
// Ps3Video: a runtime completion point, not final shutdown.
bool WaitForSubmittedWork();
// Ps3AuthoredTexture: caller already validated the region and owns the CPU bytes.
bool UpdateRgbaRegion(std::uint32_t x, std::uint32_t y,
    std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba,
    std::size_t byteCount, std::size_t rowPitch, std::string& error);
bool RetireAfterIdle(std::string& error);
// Ps3Batch2DResources: source-scoped shader/texture ownership, no global cache.
// Core texture wrappers expose dimensions and disposal; resources own GPU storage.
```

- [ ] RED host tests: manifest writer derives two RuntimeRelativePath values via
  Ps3ShaderArtifactIdentity for exact SpriteBatchShader variant identities; rejects
  missing/duplicate cooked declarations and paths not staged under cooked/shaders.
  Use the existing startup-manifest generation pattern, not a manually created file
  inside generated output. Generate `ps3_renderer_shaders.hpp` beside startup catalog
  with named Textured/RoundedShape path constants. Native code consumes this header.
- [ ] Implement writer and invoke it beside Ps3StartupManifestWriter before native
  build. No changes to the build scripts are necessary. Validate declared manifest
  entries before generation; dependency absence is an error, never a builtin fallback.
- [ ] RED resource tests cover actual raw RGBA creation, exact width/height/row-pitch,
  2x2 four-color texture orientation, update with padded input rows and out-of-bounds
  rejection. Include repeated updates/disposal with no GPU allocation growth.
- [ ] Implement `WaitForSubmittedWork` using existing SignalAndWaitLabel timeout/
  callback/error handling. It must NOT set final Drained=true. A timeout latches the
  existing fatal/retention state; never reclaim or overwrite memory after failure.
- [ ] UpdateRgbaRegion validates byte extents/overflow first, then waits for queued
  reads before copying into the existing RGBA texture allocation and invalidating
  texture cache. RetireAfterIdle waits before releasing; ShutdownAfterDrain retains
  its existing final-drain requirement. Do not weaken old program-owner lifetime rules.
- [ ] Build source-scoped bounded P3SP reads for both variants and real cooked/raw
  texture reads using IContentStreamSource/AssetSerializer. Reuse existing validated
  read/identity behavior, but do not invoke the fixed ForwardStandard material reader
  for 2D programs. Validate all stage bindings before publishing initialized owners.
- [ ] RuntimeTexture wrapper owns no independently freed GPU pointer. ReleaseTexture
  flushes pending CPU runs, finishes GPU references, retires the resource, marks the
  wrapper disposed and deletes it using the generated ownership contract. Wrapper
  Dispose is idempotent while the wrapper is alive; final deletion occurs exactly
  once through its owning release path, never through a stale borrowed pointer.
  Resources themselves survive Core disposal
  until final video drain, as do shared program owners.
- [ ] GREEN host manifest tests and Docker/RPCS3 resource tests. Record explicit
  pixels after updates and timeout/failure-path fixture behavior, not just counters.
  Parent reviews the new synchronization primitive's relationship to final Drain
  and preserved 3D ownership, then commits named files.

## Task 2: Reflected 2D GPU submission

**Create:** `src/platform/ps3/Ps3Batch2DRenderer.hpp`, `.cpp`,
`src/platform/ps3/Ps3Batch2DUniformProvider.hpp`, `.cpp`,
`tests/ps3_batch2d_gpu_tests.cpp`, `tests/Makefile.batch2d-gpu`,
`scripts/tests/ps3-batch2d-gpu.tests.ps1`.
**Modify:** `src/platform/ps3/Ps3RasterRenderer.hpp`, `.cpp` only for owned batch
renderer construction/access and post-drain shutdown. Do not merge providers.

- [ ] RED GPU fixture uses real Task1 P3SPs and shared-plan layout. Four-color textured
  quad establishes UV orientation; a tinted second draw establishes vertex color.
  Red alpha0.5 followed by blue alpha0.5 over black expects RGB(.25,0,.5), within
  byte rounding. Reverse order must differ. A third intervening texture verifies
  the fixture cannot pass after texture sorting.
- [ ] Uniform provider accepts only exact BatchCameraBuffer/projection/b0 matrix
  metadata, validates finite16floats and SDK shape/count; rejects unknown uniforms
  rather than supplying zero. Pack matrices consistently with the measured SDK DP4
  convention; test viewport translation and non-square scaling, not identity alone.
- [ ] Implement submission: validate full run/resource/input mapping, wait before
  reusing buffers or patching fragment program constants referenced by prior draws,
  copy vertices/ushort indices, set explicit state/scissor/viewport, BeginDraw,
  upload every active uniform, bind actual sampler unit when required,
  FinishUniformUpdates, rsxDrawIndexArray. Treat failure as terminal, never retry
  with diagnostic programs. Conservative wait-per-flush is intentional for this
  milestone; optimize only in a separately validated follow-up.
- [ ] GREEN tests prove: same-texture adjacent quads one draw; A/B/A three ordered
  draws;1025quads split into two bounded submissions; shape fill/border corners,
  transparent outside, selected square corners, zero/full border, nested scissor,
  empty scissor no draw, two frames with changed projection/tint and atlas update.
  Check alpha-channel equation on a controlled destination as well as RGB.
- [ ] Require clean final drain, batch buffers/programs/textures retired, no growing
  GPU bytes across repeated fixture frames. Existing authored-GPU and provider
  regression checks cover any changed shared owner/state behavior. Parent reviews
  actual pixels and scoped diff before named commit.

## Task 3: Core adapters and per-camera scheduling

**Modify:** `src/platform/ps3/Ps3RenderManager2D.hpp`, `.cpp`,
`src/platform/ps3/Ps3RenderManager3D.hpp`, `.cpp`,
`src/platform/ps3/Ps3RuntimeHost.cpp`, `src/platform/ps3/Ps3RuntimeHost.hpp`.
**Create:** `src/platform/ps3/Ps3Batch2DSink.hpp`, `.cpp`,
`src/platform/ps3/Ps3ClipScissorStack2D.hpp`, `.cpp`,
`tests/ps3_batch2d_camera_tests.cpp`, `tests/Makefile.batch2d-camera`,
`scripts/tests/ps3-batch2d-camera.tests.ps1`.
**Read:** measured generated signatures, RuntimeTexture ownership, Core.Draw,
ObjectManager camera ordering and generated IRenderVisitor2D/ClipScissorStack2D.

**Interfaces:** native Ps3RenderManager2D gains `RenderCamera(::ICamera*)` and
`FlushPending()`; implements the generated visitor and forwards DrawSprite/Text/
RoundedRect to shared emitters. Ps3RenderManager3D receives a required reference
to the 2D manager through its native constructor. Host constructs both after raster
resources exist and destroys them according to existing Core ownership rules.

- [ ] RED generated-Core fixture with two backbuffer cameras: first3D+UI, secondUI
  only with no clear. Expect call sequence clearA,3DA,2DA,clear-policyB,2DB,present.
  A no-clear camera must preserve previous pixels. Camera-disabled/layer-excluded
  drawables must not submit. Offscreen targets must fail explicitly before drawing.
- [ ] Implement native frame coordination inside existing Ps3RenderManager3D.Draw;
  do not add an unconditional RenderManager2D.Draw call to Core, which could double
  draw on other platforms. Traverse cameras in engine camera order and each camera's
  authoritative queues. Validate viewport against backbuffer dimensions. Use viewport
  aspect for 3D projection. Skip perspective setup when that camera has no 3D work.
- [ ] Apply each camera's clear once with correct viewport/scissor policy; render
  its3D queue, then call RenderCamera for2D. Flush at camera end. Explicitly rebind
  3D depth/cull/blend/scissor on the next camera and frame. Never redraw UI at end
  using the wrong camera projection.
- [ ] Replace every 2D SKIPPED entry with real behavior. Visitor resolves clip chain
  before invoking the drawable's existing Draw method; shared emitters populate
  writer and sink. Texture update/release calls FlushPending before resource mutation.
  Renderer-owned white texture remains explicit; missing authored textures do not
  become white. Ensure base default textures/font wrappers are released exactly once.
- [ ] Add bounded attribution logs: camera/viewport, each drawable category count,
  glyph/quad count, batch count, variant identity/source hash, texture dimensions,
  first relevant sample coordinates and shutdown bytes. No per-glyph per-frame spam.
- [ ] GREEN camera fixture, text-layout probe through generated code, and normal
  source build. For codegen failures fix shared source/owning root declarations,
  then regenerate through standard path. Parent checks no generated files changed
  and no project content was modified, reviews and commits named files.

## Task 4: Actual cube_test acceptance

**Create:** `scripts/tests/ps3-runtime-2d-content.tests.ps1`,
`scripts/tests/ps3-runtime-2d-log.tests.ps1`, `docs/Ps3Batch2DValidation.md`.
**Modify:** `docs/Ps3StandardSceneRuntime.md` and this plan's completion record only.
Parent owns build/launch coordination; Luna implements checker/doc changes from
parent-provided real run evidence. No screenshots or authored scene modifications.

- [ ] Inventory the actual packaged cube_test scene, camera routes, sprite paths,
  font atlas metrics and rounded rectangles through production serializers. Record
  hashes of original authored files first. Use existing serialized presentation
  roots; if they are lost in packaging, trace that boundary and fix the engine
  packaging within approved scope, not the scene. Report any larger design change.
- [ ] Parent builds/registers a fresh builder DLL in an isolated state root and uses
  the normal project command, with process-only native duration15 and saved/restored
  environment. No direct generated-Core build as a replacement for this gate:

```powershell
powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File C:/dev/helworks/helengine/scripts/build-platform.ps1 `
  -Project C:/dev/helprojs/demodisc/project.heproj -Platform ps3 `
  -Output C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/standard/output `
  -CacheRoot C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/standard/cache `
  -Configuration Release -BuildProfile release
```

- [ ] Before accepting readback, derive sample expectations from actual submitted
  geometry, texture/font data and shader math. Pick separate sprite, glyph ink/gap,
  shape fill/border/exterior and uncovered3D/background samples. With linear filtering,
  prefer texel interiors and record tolerances before observing measured output.
  Existing six fixed probe positions may be covered by UI: add narrowly scoped
  source diagnostics for explicit sample coordinates if required; never weaken
  lighting expectations to nonclear pixels or infer expected colors from measured ones.
- [ ] Launch the verified SELF through scripts/launch_in_emulator.ps1 -Visible,
  using the existing RPCS3 path and approved root. Check launcher AND application
  result, immutable manifest/hash, real source hash and both variant identities.
  Require all three drawable categories visibly contribute and all pixel expectations
  pass. Preserve cube lighting and clear color outside UI coverage.
- [ ] RED/GREEN checker synthetic logs: valid run passes; missing sprite/text/shape,
  duplicate contradictory records, malformed/nonfinite values, wrong source/variant,
  failed readback, stale sample frame, SKIPPED markers and incomplete shutdown fail.
  Checker requires explicit expected samples; it never learns them from the log.
- [ ] Actual run must show normal duration exit, Core disposal, successful drain,
  final owners zero and stable allocation usage. Confirm authored hashes and three
  protected dirty-file hashes unchanged. Record limitations and exact converter
  matrix from shared plan; absence of Vita/WiiU execution is not hidden.
- [ ] Parent runs scoped review and commits only completed named files separately
  per repository, no push. Do not declare global shader API enablement complete.

## Parent self-review and task boundaries

Task1 owns mutable resource lifecycle; Task2 consumes its safe reuse primitive;
Task3 consumes the GPU submitter and shared generated emitter; Task4 accepts only
their real packaged scene. These interfaces are sequential, not parallel writers.
The existing3D program provider and material envelope are not modified. Current
native texture/program users remain governed by their previous shutdown contracts.
The five review risks each have a named fixture or real acceptance check. Shared
codegen/ABI proof is a hard entry gate; any measured difference requires updating
both plans before dispatch. Both plans approved by the user on 2026-09-24. Native
implementation remains gated on completion of the shared plan.
