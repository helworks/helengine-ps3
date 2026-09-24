# PS3 standard scene runtime implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Parent plans; Luna implements in the existing checkout, no worktrees.

**Goal:** Load cooked P3MT/P3SP through Core's content source and draw the real `cube_test` using the unchanged authored ForwardStandard HLSL pair, material color, scene basic lighting and camera clear settings.

**Architecture:** The raster renderer owns a cache of authored GPU programs/textures, partitioned by content-source identity. Native Core adapters decode platform materials and supply per-draw standard-shader data; the reusable GPU owners perform validated binding/uploads. The existing embedded Cg path remains diagnostic-only, never a fallback for authored material failure.

**Tech Stack:** C++17, generated Core public APIs (read-only), locked Docker PPU SDK, existing shared shader cooker, existing standard platform build driver and RPCS3 visible launcher.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md`, `docs/Ps3MaterialArtifactFormat.md`, `docs/Ps3ShaderArtifactFormat.md`. Source-backed handoff: `docs/superpowers/execution/ps3-shader-integration/standard-runtime-contract.md`.

## Execution gate

Do not dispatch this plan until native reader, native material reader and authored GPU path pass their task reviews and are committed. The GPU asymmetric matrix/texture test must supply measured packing/readback evidence. This plan is prepared in parallel only; it does not authorize editing those live workers' files.

Scheduling refinement: the Core-independent uniform provider is extracted to `2026-09-24-ps3-standard-uniform-provider.md`. That bounded task may run after reader commit and measured GPU packing evidence, because it consumes neither the material parser nor GPU owners. It owns only its seven new files. The remaining content-cache, geometry and scene integration remains gated as above and consumes the provider after its own review; do not implement the provider twice.

2026-09-24 execution release: prerequisite commits are reader `3b071a6`, authored GPU `718e29b`, material reader `87fe5ee`; all scoped reviews passed. Duration handoff `65d162b` passed18 pure tests. Task1 may now begin with bounded content loading, material/model ownership, geometry and renderer scaffolding. The separate provider's actual PPU suite passed24 cases over41 active uniforms, but its scoped review is still pending: do not modify its seven files or finalize provider-dependent draw wiring until parent explicitly releases that gate. This is sequencing within Task1, not an expanded write set. Parent owns standard build invocation and emulator slot coordination.

## Global constraints

- Native types in helengine::ps3, one type per file, substantive /// documentation for types/functions/fields, PascalCase fields.
- Native integration only in this PS3 repository; no shared HLSL, shared engine, generated Core, SDK output or authored demodisc project edits.
- Preserve unrelated dirty Makefile, scripts/build.ps1 and scripts/tests/build-contract.tests.ps1. Normal wildcard source discovery is sufficient.
- All native compilation and SDK conversion in the locked Docker image; no host/WSL SDK fallback.
- Outputs/logs/fixtures only under C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923, no system TEMP. Redirect .NET TEMP/TMP to an owned fixture-storage directory and restore them afterward.
- No screenshots, nested agents, worktrees or pushes. Parent owns scoped review and named commits. Serialize emulator launches and do not stop unrelated processes.
- Keep `shaders` in ForcedDisabledFeatures during runtime implementation. Removing it requires the final acceptance task and a new standard build; do not conflate successful RSX drawing with complete generated shader API support.
- Missing content, unsupported uniforms/semantics or invalid texture formats are explicit failures, not substitution with the diagnostic shader or a white texture.

## Review focus

1. Identical relative paths in distinct content sources must not reuse the wrong program/texture; partial failure must not publish an initialized cache entry.
2. Core disposal precedes GPU drain; material/model disposal cannot free in-flight authored GPU resources, and drain failure must retain them.
3. Actual translated matrices must produce the measured GPU convention, not a transpose inferred only from field names or RowMajor.
4. Every active standard uniform and sampler must receive explicit current-draw state; successive lit/unlit/material draws cannot inherit stale values.
5. Unsupported authored inputs must fail visibly; a clear screen, draw count, or builtin diagnostic cube is not shader integration acceptance.

## Task 1: Authored content ownership and standard draw path

**Create:**

- `src/platform/ps3/Ps3AuthoredContentCache.hpp/.cpp`: Core-guarded per-source cache, owning program/texture objects and explicit default resources.
- `src/platform/ps3/Ps3RuntimeMaterial.hpp/.cpp`: split existing runtime material definition; retain P3MT identity/state and borrowed stable program/texture references.
- `src/platform/ps3/Ps3RuntimeModel.hpp/.cpp`: split existing runtime model; retain geometry/normal validity and add UV validity.
- `src/platform/ps3/Ps3MeshBuffers.hpp`: move existing descriptor unchanged out of renderer header.
- `src/platform/ps3/Ps3StandardDrawData.hpp`: Core-independent explicit per-draw matrices, camera/color/light values and Lit/DoubleSided flags.
- `src/platform/ps3/Ps3StandardUniformProvider.hpp/.cpp`: Core-independent exact known-member resolver producing SDK-packed float vectors.
- `tests/native/Ps3StandardUniformProviderTests.cpp`, `tests/native/standard-uniforms.Makefile`, `scripts/tests/ps3-standard-uniforms.tests.ps1`: bounded CPU PPU tests using the actual standard shader fixture from the material-reader task.
- `docs/Ps3StandardSceneRuntime.md`.

**Modify:**

- `Ps3RuntimeAssets.hpp/.cpp`: compatibility includes only and remove moved definitions; no duplicated classes.
- `Ps3RasterRenderer.hpp/.cpp`: own per-source caches and expose authored draw entry point, preserving embedded diagnostic rendering.
- `Ps3RenderManager3D.hpp/.cpp`: P3MT loading, UV realization, scene input collection and authored draw routing; update obsolete empty-scene comments.
- `Ps3Vertex.hpp`, `Ps3CubeMesh.cpp`: append UV2 and update diagnostic initialization without changing existing color/normal semantics.

**Interfaces:**

`Ps3StandardDrawData` contains `Ps3Matrix4 World`, `WorldViewProjection`, `NormalMatrix` (already packed SDK vectors), float4 `CameraPosition`, `BaseColor`, `AmbientLight`, `DirectionalColor`, `DirectionalDirection`, plus bool `Lit`, `HasDirectionalLight`, `DoubleSided`. This initial basic-lighting path preserves the existing supported ambient + first directional-light scope; it does not advertise general point/spot/shadow rendering. All fields must be supplied by the caller; no constructor invents valid scene values.

```cpp
class Ps3StandardUniformProvider {
public:
    static bool Resolve(const Ps3CookedShaderUniform& uniform,
        const Ps3StandardDrawData& draw, float* values,
        std::size_t capacity, std::size_t& written, std::string& error);
};
```

`Resolve` validates exact standard buffer/member/binding and metadata shape; output unchanged and written0 on error. Output is at most16 floats (all currently supported standard members are float4 or float4x4, not arrays). It is not a general arbitrary-HLSL material-data provider.

`Ps3AuthoredContentCache` is constructed with `Ps3Video&` and a non-null `IContentStreamSource&`. It owns `map<string,unique_ptr<Ps3AuthoredProgram>>` and `map<string,unique_ptr<Ps3AuthoredTexture>>`, keyed by validated relative path within that one source. It exposes `BuildMaterial(path,error)` returning an owned `Ps3RuntimeMaterial*` or null on failure, and `ShutdownAfterDrain(error)`. Renderer owns `vector<unique_ptr<Ps3AuthoredContentCache>>`, selecting by exact source pointer while Core is alive; shutdown must not dereference a disposed source. New program/texture objects are published only after successful parse/identity/initialization.

Exact renderer handoff signatures (Core-guarded):

```cpp
Ps3RuntimeMaterial* BuildAuthoredMaterial(const std::string& path,
    ::IContentStreamSource& source, std::string& error);
bool DrawAuthored(const Ps3MeshBuffers& mesh, const Ps3RuntimeMaterial& material,
    const Ps3StandardDrawData& draw, std::string& error);
```

`Ps3RuntimeMaterial` owns one `Ps3CookedMaterial Material` CPU value and borrows `Ps3AuthoredProgram* Program`, `Ps3AuthoredTexture* DiffuseTexture`, `RoughnessTexture`, `EmissiveTexture`, `ShadowAtlasTexture`, `PointShadowTexture`. All pointers refer to initialized cache-owned resources and are constructor-required; the one solid cube is intentionally shared for the four inactive shadow samplers. Preserve public existing BaseColorR/G/B/A and Lit access or update every native caller in this task; don't leave duplicate independent mutable color state. RuntimeModel exposes `HasUsableTexCoords()` and `SetHasUsableTexCoords(bool)` alongside existing normal-validity methods.

- [ ] Write PPU provider tests before implementation. Consume the real standard P3SP fixture and iterate all active uniforms in both stages. The resolver must cover every entry without name guessing. Test explicit unknown buffer/member/binding, malformed matrix/vector shape, insufficient output capacity and nonfinite input; output unchanged on failure. Test two successive distinct draw records, lit/unlit, directional presence/absence and nonwhite material color.

```cpp
float output[16];
std::fill(output, output + 16, -91.0f);
std::size_t written = 77;
auto unknown = activeUniform;
unknown.MemberPath = "unprovidedMember";
Require(!Ps3StandardUniformProvider::Resolve(unknown, draw, output, 16, written, error));
Require(written == 0 && !error.empty());
Require(std::all_of(output, output + 16, [](float x) { return x == -91.0f; }));
```

- [ ] Run RED with a test-specific Docker Makefile (reader+validator+provider+test, source readonly, outputs under standard-uniforms). Reuse the established native-reader runner's locked-image/manifest/immutable-fixture/emulator checks, not a host compiler. Print named cases and exact `PS3_STANDARD_UNIFORMS_TESTS passed=N failed=0`, requiring at least15 cases. A missing provider header is valid initial RED; preserve its evidence.

- [ ] Implement exact provider rules from unchanged HLSL. b0 members world/WVP/normal/camera come from draw; materialFlags=(0,0,0,0) explicitly disables unsupported shadows/emissive texture. b1 ambient is scene ambient when Lit, RGB1 when unlit; active light count is1 only for Lit&&HasDirectionalLight. Slot0 uses scene directional color/type0 and direction; all unused slots are explicit inactive zero records. Position/range/spot fields are zero for directional light. b2 metadata/rects are explicit no-shadow zeros and matrices identity. b3 baseColor is draw RGBA; b4=(.4,0,0,0), b5=(0,0,0,0), b6=(.5,0,0,0), b7=(0,0,0,0) are current schema's surface defaults. Do not treat these as defaults for unknown names. Verify binding0..7 and exact HLSL member widths; preserve/read reflected matrix shape, ArrayLength1 and appropriate SDK type/count.

- [ ] Implement bounded content reads using the actual runtime Stream API: `Length()`, `Read(uint8_t*,size_t,size_t)`, `ReadByte()`, `Dispose()`. Open through the provided content source; require P3MT<=1MiB/P3SP<=16MiB before allocation, loop on partial reads, reject zero-progress/truncation and trailing growth. Dispose stream on success and exception; don't rely on seekability or reinterpret a short read as complete. Use standard exceptions converted to contextual error and the existing `he_cpp_raise(new NotSupportedException(...))` at the Core adapter boundary.

```cpp
std::size_t offset = 0;
while (offset < bytes.size()) {
    const auto count = stream.Read(bytes.data(), offset, bytes.size() - offset);
    if (count == 0 || count > bytes.size() - offset) throw std::runtime_error("short or invalid content read");
    offset += count;
}
if (stream.ReadByte() != -1) throw std::runtime_error("content grew while being read");
```

- [ ] Decode P3MT, load its referenced P3SP, require MatchesProgram before GPU initialization/cache publication. Log material path/asset ID and the exact shader asset/programs/variant/source hash. Reject the legacy `BuildMaterialFromCooked(PlatformMaterialAsset*)` overload for authored use because it cannot supply shader identity; do not construct a fake standard material from incomplete input. Missing drawable material is a visible failure, not implicit white.

- [ ] For nonempty diffuse paths, use AssetSerializer through the same content source and require actual `::TextureAsset`, ColorFormat Rgba32=0, dimensions1..4096 and exact checked RGBA byte count. Copy generated array elements to a native byte vector; call `InitializeRgba2D`. Empty path intentionally uses white1x1. Provide white roughness, black emissive, white shadow-atlas2D and white solidCube for all four point-shadow bindings; these represent explicit unused/default resources for the no-shadow standard schema. Match exact texture/sampler logical names and dimensions, then bind the metadata's actual TextureUnit. Unknown active sampler fails.

- [ ] Preserve DoubleSided in raster culling. Reject UseVertexColor=true at material realization with a precise unsupported-feature diagnostic: current ModelAsset has no colors and ForwardStandard VS has no COLOR input. Do not silently discard this flag or edit shared HLSL. Current opaque profile rejects translucent BaseColorA<255 explicitly rather than pretending blend support. These limitations must be documented and must not be encountered by the actual cube_test acceptance unnoticed.

- [ ] Extend Ps3Vertex to48bytes with UV at40; preserve position0/normal12/color24. Update VertexStride, assertions and diagnostic FaceCorner UVs `(uSign+1)*.5,(vSign+1)*.5`. Copy actual finite ModelAsset.TexCoords matching vertex count; record validity. Standard authored draws require valid normals and UVs because the actual active VS consumes them; reject missing inputs, don't manufacture data. Preserve16/32bit index behavior.

- [ ] Implement authored draw: validate semantics POSITION/NORMAL/TEXCOORD0 and shapes, bind HardwareSlot with offsets0/12/40 and stride48; call BeginDraw, resolve/upload every stage's active uniforms, bind every active sampler, FinishUniformUpdates, then rsxDrawIndexArray with original index width. Set depth state and culling explicitly every draw; do not reuse embedded reflected slots/constants. Propagate errors to Core with shader/member context.

- [ ] Collect camera matrices/position and actual scene ambient/directional values in RenderManager. Preserve camera clear handling. For authored Lit=true use inverse-transpose normal matrix and actual scene lighting; Lit=false uses the explicit unlit provider policy. Validate finite values before GPU commands. Retain diagnostic-path behavior separately. Verify matrix packing against the accepted asymmetric GPU fixture and actual standard vertex output; don't change shared HLSL or postprocess SDK blobs.

  First-draw evidence must retain the actual submitted inputs, not just entity translation: inside the existing one-time `LoggedFirstVisit` branch, log `submittedFrame=Video.PresentedFrameCount()`, material identity, Lit/HasDirectionalLight/DoubleSided, SDK-packed World/WVP/NormalMatrix (16 floats each), camera/baseColor/ambient/directionalColor/directionalDirection (4 floats each) with `%.9g` precision. Use a documented namespace-scope helper for bounded float-array logging, no per-frame output or new task files. This belongs to the named RenderManager source and supports Task2's existing lighting/transform acceptance: first submittedFrame0 correlates with the first displayed probe after the first confirmed flip. Do not use BootHost's subsequent diagnostic-cube pose as the authored transform. This diagnostic does not itself prove a correct pixel.

- [ ] Shutdown all per-source caches from renderer shutdown after successful video.Drain; each owner must report zero GPU bytes. On drain failure retain allocations as current boot contract requires. Material disposal never frees shared program/texture state. Log created/reused resource counts and shutdown totals so repeated-material cache/lifetime acceptance is inspectable.

- [ ] Run provider GREEN PPU suite with all actual standard active uniforms resolved. Run a standard build of the actual project using the invocation below, not a manual generated-Core substitute. Fix compile/link/load errors in owned sources only. Launch the resulting SELF with the existing visible launcher in the coordinated slot. This task requires real cube geometry using P3SP in logs and pixels; it still does not enable the shader feature or claim full final lighting acceptance.

## Task 2: End-to-end acceptance and feature decision

### Corrective step: standard material lighting default (user approved 2026-09-24)

The earlier inference that CubeTestSolid was authored Unlit was incorrect. Its common
document contains base-color and casts-shadow, with no lighting-mode. Its Windows
override selects the lit ForwardStandard shader. The editor fills absent fields from
the selected platform schema; our PS3 schema and cooker both chose Unlit. Correct
the PS3 default, not the authored asset or shared editor.

**Luna write scope:** `builder/Ps3PlatformDefinitionFactory.cs`,
`builder/Ps3MaterialCooker.cs`, `builder.tests/Ps3MaterialCookerTests.cs`.

- [ ] First change the no-authored-fields test to require `SurfaceFlags == 1u`.
  Add behavior tests cooking the actual published schema field defaults (expect1),
  common CubeTestSolid-shaped fields base-color=#ffffffff and casts-shadow=false
  without lighting-mode (expect1), explicit lit (expect1), explicit unlit (expect0),
  and blank lighting-mode (expect1, matching the existing Resolve contract).
  Keep unknown lighting choice rejection. Do not derive expected flags from schema.
- [ ] Run the focused Ps3MaterialCookerTests before production edits and retain RED.
- [ ] Change only the schema lighting default and cooker's Resolve fallback from
  `Ps3MaterialSchemaIds.LightingUnlitValue` to `Ps3MaterialSchemaIds.LightingLitValue`.
  Leave choice values, parser, binary layout, explicit overrides and native provider
  untouched. Explain in substantive comments that standard materials receive scene
  lighting unless the platform material explicitly requests Unlit.
- [ ] Run focused GREEN using `dotnet test builder.tests/helengine.ps3.builder.tests.csproj
  -c Release --artifacts-path <owned-root>/material-lighting/tests
  -p:HelengineRoot=C:/dev/helworks/helengine --filter FullyQualifiedName~Ps3MaterialCookerTests`.
  Redirect TEMP/TMP to owned directories and restore them; logs under owned-root.
- [ ] Parent reviews tests and minimal production diff, then rebuilds/registers b4
  and runs the normal platform driver and RPCS3. Require cooked flags1, submitted
  Lit1, actual directional light and shader-equation-derived pixels, not merely
  nonclear geometry. Preserve the authored material bytes and keep shaders disabled
  until the existing final gates pass. Parent commits named files after validation.

**Modify:** `scripts/tests/ps3-runtime-visible-content.tests.ps1` (retain its existing smoke mode; add an explicit authored-shader acceptance mode), `docs/Ps3StandardSceneRuntime.md`; only after all gates pass, `builder/Ps3PlatformDefinitionFactory.cs` and its named existing definition tests to remove just `shaders` from the disabled list and update stale descriptions.

Build root is `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-scene`; output and cache directories must be disjoint siblings. Register the new builder DLL with the existing optional `-BuilderAssemblyPath` and isolated HELENGINE_ENGINE_USER_SETTINGS_ROOT. Set HELENGINE_PS3_REPOSITORY_ROOT explicitly. Exact standard build driver:

Registration is a separate call to this repository's `scripts/register-platform.ps1 -ProjectFile <project> -StateRoot <standard-scene>/state -Configuration Release -BuilderAssemblyPath <builder-dll>`; `-BuilderAssemblyPath` is NOT a parameter of the sibling `build-platform.ps1`. Parent prepared a fresh Release builder at `<standard-scene>/b1/bin/helengine.ps3.builder/release/helengine.ps3.builder.dll` and registered it at `<standard-scene>/state/user-settings/platforms.json` on2026-09-24. Build passed with0errors; existing sibling MouseState accessibility warnings remain. For the standard invocation set process-only settings root to that user-settings directory, repository root explicitly, duration15, and TEMP/TMP to task-owned directories; restore all previous values in finally. Do not pass the deprecated WorkspaceRoot.

For bounded acceptance only, after the independent `2026-09-24-ps3-standard-build-duration.md` task is validated, temporarily set `HELENGINE_PS3_NATIVE_RUN_SECONDS=15` around this same standard invocation and restore its previous value in finally. This forwards the existing native RunSeconds diagnostic; do not manually rebuild generated Core. Require `run-duration-reached`, successful drain, normal exit and authored-owner retirement in the resulting run. Normal builds keep the option unset and remain unlimited.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:/dev/helworks/helengine/scripts/build-platform.ps1 `
  -Project C:/dev/helprojs/demodisc/project.heproj -Platform ps3 `
  -Output C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-scene/output `
  -CacheRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-scene/cache `
  -Configuration Release -BuildProfile release
```

- [ ] Require a freshly produced manifest/current source fingerprint and actual staged P3MT/P3SP; inspect the staged material as evidence, never edit it. Require startup cube_test, authored program selection logs, exact camera clear corner0x6495ED, valid nonclear geometry readback, no shader/load/fallback errors and finite actual matrix/material/light diagnostics.
- [ ] Establish lit/material pixel expectations from the actual staged cube, first-frame transforms, camera and light values, applying the unchanged shader equations. Record expected values/tolerance before accepting samples. The check must distinguish unlit color/clear color from scene-lit material output. If first-frame timing makes a fixed expected pixel ambiguous, add a native diagnostic log of the submitted frame's transform and sample association, not a different scene or shader. Do not weaken the test to draw counts or center!=corner alone.
- [ ] Require the committed nonuniform GPU fixture's actual four-color orientation and consecutive vertex/fragment-update proof, plus runtime texture binding/content-load coverage when exercising a nonempty material texture path. Do not modify authored project content just to make the test pass.
- [ ] Only after real scene and texture gates pass, remove the forced-disabled `shaders` entry, rerun the named definition tests and a fresh standard build/run. Generated shader API reachability may expose additional native interface work: report concrete missing methods instead of declaring success or patching generated output. Keep the goal active until that build/run also passes.
- [ ] Parent checks final source diff, validation evidence and scoped review, then commits only named completed files; no push. Document honest limits: basic ambient/directional lighting, opaque standard materials, RGBA32 2D input and solid-cube defaults, no general shadow/point/spot/vertex-color feature claim.

## Parent self-review

### Standard04 diagnostic follow-up

Real run061502 rendered the unchanged Unlit white material correctly (FFFFFF center,6495ED corner,840frames,normaldurationexit,drain,authoredGPU0). It exposed that this PPU printf prints literal `zu` for `%zu`, shifting subsequent diagnostic arguments: only the diagnostic PS3_AUTHORED_DRAW indices count is wrong; PS3_CORE_AUTHORED_DRAW and actual mesh/draw counts are36. Luna may change only the two affected logging calls in Ps3RasterRenderer.cpp and Ps3RenderManager3D.cpp to `%lu` with explicit unsigned-long casts for each size_t count, as other existing PS3 logs already do. No rendering or format behavior changes. Update docs/Ps3StandardSceneRuntime.md with this real standard04 evidence and limitations (Unlit, no lighting/feature-enable claim), replacing the stale first-build-only status. Parent verifies exact format/argument pairing; next standard build/runtime must validate the diagnostic output. Never patch the existing SELF/TTY.

The plan preserves the real authored HLSL and standard platform build path. It binds CPU reader/material/GPU owner contracts already specified by preceding tasks. No intermediate fixture replaces real cube_test. The unsupported material choices fail explicitly; basic lighting scope is unchanged from the existing PS3 milestone. Resource cache publication, per-source identity, shutdown ordering and per-draw completeness are named review risks. Task2's actual-scene pixel values must be derived from staged content and submitted transforms during execution, not guessed in advance from the scene factory. This plan is not dispatched while its prerequisites are still being validated.
