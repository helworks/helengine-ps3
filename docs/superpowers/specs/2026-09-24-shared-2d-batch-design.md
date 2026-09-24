# Shared internal 2D batch and PS3 cube_test rendering

Status: proposed design for user review; no implementation authorized by this document alone.

## Intent and approved boundaries

Render the existing cube_test sprites, text and rectangles on PS3 using the shared
HLSL compilation/cooking system. The user selected an internal SpriteBatch-style
batch, not a new public Begin/Draw/DrawString/End API. Existing authored scenes and
components remain unchanged. Shared-engine shader and narrowly required integration
changes are authorized. Parent writes the design and implementation plans; Luna
implements after review. No worktrees, generated-output edits or screenshots.

## Source-backed starting point

Codebase-memory list_projects currently fails with Transport closed. These findings
come from targeted source reads, not a complete graph or cross-platform audit.

- Core RenderManager2D already exposes sprite, text, rounded-rectangle and texture
  creation/update/release operations. Keep these public contracts.
- IRenderQueue2D.VisitOrdered and RenderList2D preserve submission order. Core has
  ClipScissorStack2D, TextLayoutUtils and TextRenderEffectPassBuilder to reuse.
- DX11 DirectX11Renderer2D traverses each camera's ordered queue. SpriteShader.fx
  performs textured/tinted rendering using per-draw constants; UIShapeShader.fx
  handles rounded shapes. They are reference behavior, not a portable batch ABI.
- PS3 Ps3RenderManager2D currently returns placeholder textures and skips drawing.
  Ps3RenderManager3D currently renders the first camera's 3D queue only; the existing
  2D stub Draw is not enough to establish a correctly scheduled camera UI pass.
- The cube_test source factory includes desktop-presentation roots for instructions
  and UI. Actual packaged scene contents and their camera/layer routing must be
  inspected before acceptance; the factory is not proof of the serialized contents.
- Ps3AuthoredProgram/Texture and P3SP provide real compiled shader/texture ownership.
  Existing texture owners support initialization and final-drain retirement, not
  general in-flight updates. Dynamic font texture updates need explicit handling.

## Alternatives and selection

1. Continue separate PS3-only sprite/text/shape programs: smallest local patch, but
   does not satisfy the shared-shader requirement. Rejected.
2. One portable HLSL source with textured and rounded-shape variants, a shared CPU
   batch contract, and device-specific submission. Recommended.
3. One textured shader plus CPU tessellation of every rounded border: possible, but
   adds geometry/coverage complexity and changes the existing shape rendering model.

## Shared CPU contract

Keep drawable APIs and authoritative queue ordering. A shared, codegen-compatible
CPU layer expands drawables into indexed triangle geometry and contiguous runs.
It owns no GPU handles; native backends implement resource submission and lifetime.
Reuse existing layout, effect-pass and clipping rules instead of maintaining a
second text-layout implementation in PS3 C++.

Each run captures shader variant, texture identity, sampler/blend policy, camera
projection/viewport and resolved scissor. Only adjacent compatible commands merge.
Never sort by texture across intervening drawables. Flush on state changes, camera
boundaries, capacity exhaustion, and before texture updates affecting queued draws.
Copy render-relevant geometry/state when submitted; do not defer reads of mutable
drawables until GPU submission. Exclude invalid/disposed resource use explicitly.

Textured vertices contain position, UV and tint. CPU expansion applies destination
size, source rectangle and authored transform using existing engine conventions.
Text emits atlas quads from the existing font metrics, alignment/wrapping and effect
passes, preserving their order. Plain solid rectangles may use the intentional
renderer-owned white texture. Rounded shapes retain fill, border, radius, selected
corners and transforms; they must not degrade into square rectangles.

Use bounded reusable buffers. Split runs before index/vertex capacity overflow;
16-bit indices are sufficient for each bounded 2D chunk. Exact capacities and vertex
layouts belong in the implementation plan after converter/codegen feasibility tests.
Do not change the existing model cooker or its 16/32-bit index behavior.

## Portable shader contract

Add a shared built-in HLSL source alongside the existing shared built-ins, with two
statically selected program variants: textured tint and rounded shape. Both use
orthographic pixel-space projection. Textured output is sampled RGBA multiplied by
vertex tint; glyphs use the same path. Rounded-shape data varies per primitive so
shape parameters do not force a constant-buffer update for every rectangle.

Use arithmetic and varyings supported by the existing conversion backends. Decode
corner selection on CPU into float-compatible data rather than copying the DX11
shader's integer bit operations blindly. Define shape coverage and straight-alpha
output consistently; do not copy any accidental double-alpha behavior from the old
shader. Blend RGB as source-alpha / inverse-source-alpha; specify and test the
destination-alpha equation separately, especially for offscreen targets.

No handwritten Cg equivalent or patched compiler output. Register built-in shader
dependencies from the 2D pipeline, not from a dummy 3D material. Normal packaging
must include both required variants and their metadata even for a 2D-only scene.
Shader source/dependency changes must invalidate cooked caches. Missing variants,
resources or unsupported bindings fail with actionable diagnostics.

## PS3 backend and scheduling

Integrate the 2D pass after the corresponding camera's 3D content and before
presentation. Respect camera ordering, layer routing, viewport and nested scissors;
do not clear the surface again between that camera's 3D and UI passes. Rendering
only the first camera is not sufficient if cube_test uses another UI camera.
Establish explicit 2D blend/depth/cull/sampler state and restore/rebind all needed
state at the next pass boundary.

Replace placeholder texture creation with actual cooked/raw RGBA texture loading
through the owning content source. Preserve dimensions, row pitch and UV orientation.
Support validated region updates used by font atlases. Flushing CPU commands alone
does not make queued GPU reads safe: use completion-tracked buffer/texture generations
or explicit GPU synchronization before overwriting referenced storage. Resource
release must defer GPU reclamation until completion; shutdown follows the established
Core disposal, drain, owner-retirement sequence. No per-frame allocation leaks.

Reuse validated P3SP program loading/binding where applicable, while keeping the
existing 3D standard-uniform provider separate from the new 2D contract. Preserve
the working lit cube, camera clear color, normal failure behavior and standard
Docker/toolchain/build path.

## Scope and delivery boundaries

Deliver in two dependent parts, each with a separate implementation plan:

1. Shared CPU batch/shader/dependency contract, including tests and compiler probes.
2. PS3 resource/submission/camera integration and actual cube_test acceptance.

The shared source must be checked through the available DX11, Vulkan, Vita, Wii U
and PS3 conversion backends. Report separately what compiled and what executed;
an unavailable toolchain is a validation gap, not a passing portability claim.
This milestone does not migrate every platform's runtime renderer. It establishes
the shared contract and the first PS3 consumer without changing other renderers'
behavior. No public immediate-mode API, arbitrary user effects, new text shaping
engine, batching across cameras, or unrelated global shader-feature enablement.

## Acceptance

- Shared behavioral tests: ordered overlapping translucency, adjacent run merging,
  texture/variant/scissor boundaries, empty input and bounded-capacity splitting.
- Geometry/layout tests: source rectangles, tint, scaling/rotation, glyph atlas UVs,
  wrapping/alignment, text effects, rounded corners/borders and nested clipping.
- Shader probes: both variants compile with expected reflected attributes,
  resources and uniforms; portability results identify exact tested backends.
- GPU fixtures: nonuniform texture orientation, straight-alpha overlap, rounded
  coverage, scissor, multiple frames and texture-region updates without corruption.
- Standard build of the unchanged DemoDisc cube_test: verify the actual packaged
  sprites/fonts/shapes and routing, show all three drawable categories in RPCS3,
  and validate framebuffer samples selected from authored geometry/known texture
  data. Do not accept counters alone or alter the scene to make the test pass.
- Keep existing cube lighting/clear-color coverage using sample points outside UI
  coverage where necessary. No silent SKIPPED 2D path, shader fallback or missing
  texture substitutions. Missing glyph behavior follows the existing font contract.
- Record batch/draw counts and normal disposal/drain/retirement; demonstrate fewer
  submissions for adjacent compatible quads without sacrificing order.
- Preserve the three preexisting dirty PS3 files and all unrelated shared changes;
  commit only named validated files, separately per repository; no push.

## Design self-review

The requested feature is internal and uses existing component APIs. Text, shape,
texture mutation, GPU lifetime and shader packaging are explicit rather than hidden
behind the term batch. Shape alpha and multi-camera scheduling need targeted tests.
Exact scene inventory, ABI sizes and backend compilation results are investigation
gates for the subsequent plans, not presumed completed evidence. Scope excludes
all-platform runtime migration and preserves the authored scene.
