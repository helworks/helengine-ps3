# PS3 authored standard scene runtime

## Standard03 runtime-path diagnosis

Parent reported standard03 compiled, but run03 stopped before drawing: its P3MT
requested shaders/<hash>.ps3sp while normal packaging staged cooked/shaders/<hash>.ps3sp.
The fix is in the producer contract. Ps3ShaderArtifactIdentity.RelativePath remains
cook-root-relative and is still the stable logical artifact ID; RuntimeRelativePath
adds cooked/ for artifact declarations and P3MT references. Physical shader output
and P3SP bytes are unchanged. The native material reader requires the canonical
runtime-relative reference; the content cache does not rewrite or retry paths.
Materials must be recooked. Standard04 subsequently resolved that packaged reference
and rendered the actual authored cube, as recorded below.

## Bounded native review follow-up

The validator now proves reverse coverage: every parsed raw SDK vertex input
and pixel sampler must have an artifact mapping, using SDK type classification
and the existing generated-name/physical-slot uniqueness checks. It does not
assume a fixed StandardShader input or sampler count. The reader regression
removes one mapping at a time in memory, repairs container lengths and checks
that the raw SDK bytes remain unchanged: before the fix both malformed artifacts
were accepted (43/2); after the fix the complete reader suite passes45/0.

Failure-only texture/model/stream cleanup helpers preserve the pending primary
exception. Secondary generated Exception pointers are logged and deleted; ordinary
secondary exceptions are logged and suppressed. Successful-path Dispose errors
still propagate. Partial RuntimeModel disposal during unwinding uses the same
nonthrowing cleanup rule. This was source-checked, not cleanup fault-injection tested.
ReadCookedAsset also failure-disposes an already-deserialized ModelAsset when
stream disposal fails, releasing its owned geometry arrays before object deletion
while preserving the original stream-disposal error.

The reviewed terminal fail-fast draw contract is unchanged. A failed authored draw
ends the runtime frame loop, followed by Core disposal and post-drain retirement;
there is no AbortDraw/retry API. Full standard-scene build/runtime remains the
parent's separate integration gate.

## Status

Parent standard04 compiled with exit0. Its immutable SELF rendered the unchanged
cube_test through the authored ForwardStandard program with unlit white material.
Lighting acceptance and feature enablement remain pending; shaders stays forced-disabled.

### Standard04 real runtime evidence

TTY read directly from:
C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-scene/output/core-175efd53ce1145cc927634c5eaaf73f9/emulator-runs/20260924-061502/TTY.log.

The run opened cooked/shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp
(37501 bytes), realized 24 vertices/36 16-bit indices with valid normals/UVs, and
submitted material Materials.rendering.cube_test.CubeTestSolid with RGBA FFFFFFFF,
Lit=0, HasDir=1, Double=0. First and final probes reported center0xFFFFFF and
corner0x6495ED. Exit was run-duration-reached, failed=0, frames=840, draws=840,
followed by Core disposal, successful drain, authored cache ownedGpuBytes=0 and
PS3_SHUTDOWN exitCode=0.

This proves real authored unlit geometry/content lookup and normal retirement for
this run, not lit-material behavior, nonempty diffuse-texture loading or shader
feature enablement. The source-backed lighting/feature gates remain pending.

This PPU printf printed literal zu for the two size_t diagnostic calls, including
a misleading PS3_AUTHORED_DRAW indices=3; the independent mesh/core draw markers
both report36. Only those two calls now use %lu with explicit unsigned-long casts
(four binding counts and vertex stride). No draw behavior changes. The old SELF/TTY
is immutable. Parent subsequently confirmed standard05/run05 PASS with the corrected
diagnostics, as attributed below.

### Standard05 final diagnostic evidence

Parent-reported terminal PASS: standard-scene/output/core-9ba5b9386cec4ff4a7668217fa9c2574/emulator-runs/20260924-062547.
The run reported895frames/895draws, center0xFFFFFF, corner0x6495ED, normal
run-duration exit, successful drain and authored cache ownedGpuBytes=0.
Corrected diagnostics are stride48, attributes3, vertexUniforms3, pixelUniforms38,
samplers8 and indices36.

SELF SHA256: e90b030472e49964e6247c22b0306468d90977786e5e8a0c9e5798bbc5ad03af.
The HLSL source-file SHA256 matches the draw's source hash:
BF2EE7E830BE15CF6F91793C8ACC0C1C8067A0D4A7D79F527A2E09AA39480C77.
This closes diagnostic-format validation for the real authored unlit white cube.
The lighting acceptance choice and shader feature enablement remain pending;
this result does not claim lit-material or general texture-content acceptance.

Prerequisites reported approved and committed by the parent: shader reader
`3b071a6`, authored GPU owners `718e29b`, material reader `87fe5ee`,
duration handoff `65d162b`, and uniform provider `b6cf5af`.
The provider's parent-reported final gate is 26 PPU cases over exactly 41 distinct
active standard uniforms. Those tests are not a substitute for the scene build.

## Content and lifetime

The Core adapter passes the actual `IContentStreamSource` to the renderer.
Each exact source pointer selects its own `Ps3AuthoredContentCache`; equal paths
in different sources never share a shader or texture entry.

P3MT is bounded to 1 MiB and P3SP to 16 MiB before allocating the byte vector.
Reads loop over the actual Stream partial-read API, reject zero progress and
over-reported counts, and verify EOF using ReadByte. Owned streams are disposed
on success and exceptions. Generated pointer exceptions and native standard
exceptions become contextual material errors at the Core boundary.

P3MT/P3SP identities are matched before shader GPU initialization. Cache entries
are inserted only after their individual parse/validation/initialization succeeds.
A failed later material dependency does not invalidate an already complete shared
resource; the renderer retains it until shutdown. Shader and texture keys are
validated UTF-8 relative paths, without traversal or ambiguous separators.

Nonempty diffuse paths must deserialize as TextureAsset, Rgba32, dimensions
1..4096, with exactly width*height*4 bytes. Their generated array elements are
copied into native storage before texture initialization. Missing/non-RGBA content
is an error, never white substitution. Empty diffuse uses explicit white 1x1.
White roughness, black emissive, white shadow-atlas 2D and one shared white cube
are the standard schema's explicit inactive/default inputs.

RuntimeMaterial retains one immutable cooked material and required borrowed
resource references. RuntimeModel borrows a mesh descriptor; the renderer retains
uploaded allocations. Core disposal therefore cannot free queued GPU resources.
Renderer shutdown requires successful video drain, retires every authored owner,
and releases uploaded mesh allocations. Cache shutdown does not dereference its
possibly disposed Core source. Drain failure retains GPU storage.

## Geometry and draw contract

Ps3Vertex is 48 bytes: POSITION at 0, NORMAL at 12, diagnostic COLOR at 24,
and TEXCOORD0 at 40. Authored geometry copies real finite positions and UVs and
records whether every source normal is finite and nonzero. The standard path
rejects missing/invalid normals or UVs instead of manufacturing them.
Original 16/32-bit indices are retained, checked for range and uploaded as triangles.

Core's RuntimeModel constructor creates a non-null empty submesh collection.
Realization uses the actual ModelSubmeshResolver.BuildRuntimeSubmeshes API and
SetSubmeshes (which copies the container and retains item pointers). Temporary
items transfer only after SetSubmeshes succeeds. The initial whole-model path
requires one range covering all indices and exactly one realized P3MT material;
unsupported range/material layouts fail rather than being silently flattened.

The manager preserves camera clear settings and collects actual camera
world/view/projection, model world/inverse-transpose, material RGBA, first ambient
light and first directional light. Absent ambient is zero, not an invented fill
light. All draw floats must be finite. Lit=false selects the provider's explicit
unlit policy, not the diagnostic Cg program.

The renderer preflights every active attribute, uniform and sampler before
BeginDraw. POSITION/NORMAL/TEXCOORD0 use the reflected physical HardwareSlot,
not embedded diagnostic slots. Both stages' complete active uniform lists are
resolved for every draw, uploaded, and completed with FinishUniformUpdates.
Exact texture/sampler names, logical bindings and dimensions select resources;
the physical TextureUnit always comes from P3SP. Depth/write/blend/cull state is
set explicitly; DoubleSided disables culling.

UseVertexColor is rejected because ModelAsset has no color array and the actual
ForwardStandard VS has no COLOR input. Translucent base alpha is rejected by this
opaque profile. General point/spot/shadow rendering, arbitrary HLSL materials and
multi-range rendering are not claimed. Legacy PlatformMaterialAsset realization
fails because it cannot provide P3MT shader identity.

## Matrix evidence

The unchanged StandardShader HLSL multiplies a row vector by world/WVP/normal.
The real producer-002 vertex dump
`native-material/fixtures/producer-002/.ps3-shader-work/ps3-shader-169bd0eec5854c99a3cbd699f94b6b16/program.bin.dump`
under the shared shader-integration build root shows clip X/Y/Z/W as DP4 with
c[4]/c[5]/c[6]/c[7], world XYZ with c[0..2], and normals with c[8..10].
Thus each uploaded vector is a Core matrix column:
(M11,M21,M31,M41), then the next column, and so on. This agrees with the accepted
asymmetric GPU fixture's scale/translation packing. No SDK blob is transposed or
postprocessed. This source/dump evidence does not replace the pending real-scene
matrix and pixel verification.

## Subsequent build handoff

Parent owns the standard project invocation, fresh manifest/fingerprint,
read-only generated Core inspection and coordinated runtime, with isolated
registration in standard-scene/state/user-settings per the approved plan.
This printf-only follow-up launched no build or emulator.

Expected attribution markers include PS3_AUTHORED_MATERIAL (full shader identity,
source hash, RGBA/flags), PS3_AUTHORED_CACHE (create/reuse counts and GPU bytes),
PS3_CORE_MODEL_REALIZED (normal/UV validity, stride/index width),
PS3_AUTHORED_DRAW, PS3_CORE_AUTHORED_DRAW and
PS3_AUTHORED_CACHE_SHUTDOWN with ownedGpuBytes=0.
The first submitted drawable additionally logs Frame=Video.PresentedFrameCount(),
material identity and Lit/HasDir/Double flags, followed by eight named SDK-packed
World/WVP/Normal float16 and camera/base/ambient/directional-color/direction float4
sequences at %.9g. These bounded one-time records support later pixel derivation;
they do not assert a pixel pass or describe every subsequent frame.
Standard04/05 establish the unlit cube and normal-retirement evidence above.
Lit-material pixel derivation, texture-content coverage and feature enablement
remain separate acceptance gates.
