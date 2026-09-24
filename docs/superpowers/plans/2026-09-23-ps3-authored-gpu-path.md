# PS3 Authored GPU Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Load a real P3SP shader pair into reusable native RSX program/texture owners and prove transformed textured draws and consecutive uniform updates through pixel readback.

**Architecture:** Keep GPU ownership separate from the CPU artifact reader and generated Core. A reusable program object owns aligned SDK blobs and an RSX fragment microcode copy; a texture object owns RSX texels. A standalone visible PPU test exercises those exact production objects, using a real Docker-produced HLSL fixture. Core material/scene integration follows this gate; this test does not replace real cube_test acceptance.

**Tech Stack:** C++17, pinned PSL1GHT Docker image, existing Ps3Video, P3SP reader, xUnit fixture producer, RPCS3 launcher.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md`.

## Global Constraints

- Parent plans; Luna implements. Existing checkout only, no worktrees or nested agents.
- No generated Core, authored scene, shared engine, shader binary/header/dump edits. No screenshots.
- SDK compilation and conversion only the immutable image in toolchain-lock.json. No host/WSL SDK fallback.
- All products/logs/fixture TEMP/TMP under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/authored-gpu`.
- One type per production file, substantive documentation, fields before constructors before methods. No silent defaults for missing required resources.
- Preserve dirty Makefile/scripts/build.ps1/scripts/tests/build-contract.tests.ps1. Do not alter the native-reader worker's files; link to its sources only.
- Keep shaders disabled. No commits/push by worker. Parent reviews and commits named files.
- GPU resources are freed only after successful Ps3Video::Drain. Failed initialization may roll back resources that have never been submitted. Never free queued GPU memory from a material destructor.

## Review Focus

- Source artifact mutation: hash fixture files before/after; own runtime copies, never modify source vectors or generated outputs.
- Consecutive draws: fragment updates use ordered SDK FIFO commands, not CPU writes to live fragment memory; a second frame must show distinct tint values.
- Matrix orientation: asymmetric scale/translation tested through known pixel positions, not identity-only success.
- Color/UV mapping: four distinct RGBA texels, expected quadrant colors, nearest filtering and normalized UVs.
- Failed/uninitialized operations: explicit errors; no shader fallback, no resource publication after failure, no release before successful drain.

## Task 1: Reusable GPU owners and visible shader fixture

**Files:**
- Create `src/platform/ps3/Ps3AuthoredProgram.hpp` and `.cpp`.
- Create `src/platform/ps3/Ps3AuthoredTexture.hpp` and `.cpp`.
- Create `tests/native/Ps3AuthoredGpuTests.cpp`.
- Create `tests/native/authored-gpu.Makefile`.
- Create `scripts/tests/ps3-authored-gpu.tests.ps1`.
- Create `builder.tests/Ps3ShaderGpuFixtureTests.cs`.
- Create `docs/Ps3AuthoredGpuPath.md`.

**Consumes:** existing `Ps3ShaderArtifactReader::Read`, `Ps3RsxProgramValidator::Validate`, owning `Ps3CookedShaderProgram` DTOs, `Ps3Video`, locked SDK APIs and existing `scripts/launch_in_emulator.ps1`. The reader task may still be validating; coordinate missing upstream functionality with parent instead of editing its ownership.

**Produces:** exact public APIs in namespace helengine::ps3:

```cpp
class Ps3AuthoredProgram {
public:
    Ps3AuthoredProgram();
    ~Ps3AuthoredProgram();
    Ps3AuthoredProgram(const Ps3AuthoredProgram&) = delete;
    Ps3AuthoredProgram& operator=(const Ps3AuthoredProgram&) = delete;
    bool Initialize(Ps3Video& video, const Ps3CookedShaderProgram& program, std::string& error);
    bool BeginDraw(std::string& error);
    bool UploadUniform(std::uint32_t stage, std::size_t uniformIndex,
                       const float* values, std::size_t valueCount, std::string& error);
    bool FinishUniformUpdates(std::string& error);
    bool ShutdownAfterDrain(std::string& error);
    const Ps3CookedShaderProgram& Program() const;
    std::size_t OwnedGpuBytes() const;
};

class Ps3AuthoredTexture {
public:
    Ps3AuthoredTexture();
    ~Ps3AuthoredTexture();
    Ps3AuthoredTexture(const Ps3AuthoredTexture&) = delete;
    Ps3AuthoredTexture& operator=(const Ps3AuthoredTexture&) = delete;
    bool InitializeRgba2D(Ps3Video& video, std::uint32_t width, std::uint32_t height,
                          const std::uint8_t* rgba, std::size_t byteCount, std::string& error);
    bool InitializeSolidCube(Ps3Video& video, const std::uint8_t rgba[4], std::string& error);
    bool Bind(std::uint32_t textureUnit, std::string& error);
    bool ShutdownAfterDrain(std::string& error);
    std::size_t OwnedGpuBytes() const;
};
```

`Program()` is only valid after Initialize success; guard misuse explicitly rather than return a fabricated valid shader. Destructors must not free live submitted GPU allocations without a successful drain: log the retained resource error and retain GPU memory until process exit if the lifecycle contract was violated. Normal caller shutdown must return owned GPU bytes to zero. CPU copies can be freed once their owner is retired; copying is disabled.

Review clarification: normal release requires explicit `ShutdownAfterDrain` while the borrowed video object is alive. A still-initialized owner's destructor must only log/retain; it must not dereference `VideoValue` even to query drain state, because the video may already have been destroyed. Add a narrow runner preflight source-contract regression that extracts the two destructor bodies and rejects borrowed-video access or resource-release calls there, observe RED on the old bodies and GREEN after correction. This guards the exact unsafe fallback without a fake host SDK or test-only private-state hooks. Keep the real GPU runtime acceptance for normal drain/release behavior.

- [ ] **Step 1: Add real fixture producer and behavior tests, then record RED.**

New Docker-category xUnit test `ProducesAuthoredGpuFixture` uses existing Ps3ShaderPairCompiler and Ps3ShaderArtifactWriter, with explicit `HELENGINE_PS3_REPOSITORY_ROOT` and fully-qualified `HELENGINE_PS3_SHADER_GPU_FIXTURE_ROOT` restricted to a child of the task build root. Source snapshot:

```hlsl
cbuffer Frame : register(b0) { float4x4 Transform; float4 Tint; };
cbuffer Surface : register(b1) { float4 PixelTint; };
Texture2D Albedo : register(t0); SamplerState LinearSampler : register(s0);
struct VIn { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
struct VOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; float4 Color : COLOR0; };
VOut VS(VIn v) { VOut o; o.Position=mul(float4(v.Position,1),Transform); o.Uv=v.Uv; o.Color=Tint; return o; }
float4 PS(VOut v) : SV_Target { return Albedo.Sample(LinearSampler,v.Uv)*v.Color*PixelTint; }
```

Identity `(authored-gpu-fixture, gpu.vs, gpu.ps, default)`, entry points VS/PS, ShaderModel4.0. Assert Frame.Transform/Tint and Surface.PixelTint are active in their intended stages, with sampler Albedo and POSITION/TEXCOORD0 attributes. Write `fixture.ps3sp` and exact raw stage blobs to the explicit fixture root. These are immutable producer outputs.

The PPU test reads fixture bytes bounded to16MiB, invokes the actual native reader and GPU owner APIs. Add negative preflight cases: uninitialized BeginDraw/Bind, invalid stage/index/value count, null values, nonfinite uniform data, invalid texture dimensions/byte count/unit, duplicate Initialize, and attempted shutdown while video has not drained. Do not issue malformed commands to RSX to test rejection. Assert failures preserve resource ownership and expose errors.

Run the new PPU test compile against missing production APIs or explicit throwing/false stubs; retain a genuine expected RED. Missing unrelated upstream files are a coordination blocker, not evidence of this task's RED.

- [ ] **Step 2: Implement program ownership and exact uniform routing.**

Validate the complete CPU program first. Allocate 128-aligned owned CPU copies of SDK stage blobs; inspect the current locked SDK APIs and existing Ps3RasterRenderer initialization for signatures. Obtain ucode only after validation. Allocate fragment ucode with rsxMemalign(64,size), copy once before submitting it, resolve RSX offset with explicit error handling. Keep metadata and cached SDK parameter pointers valid until shutdown. No per-draw GPU allocations.

`BeginDraw` loads the vertex program and fragment program and resets per-draw active-uniform completion flags. Uniform selection is the index in `Program().Vertex.Uniforms` or `.Pixel.Uniforms`, using stage0/1 respectively. Resolve by GeneratedName against the validated stage blob, cross-check SDK type/count, and use its actual hardware records; never use authored binding as a physical RSX register.

`UploadUniform` accepts already packed SDK element vectors, not raw little-endian authored bytes. Exactly `SdkElementCount * lanesPerElement` finite float values are required. SDK float/float1 use1 lane, float2/3/4 use2/3/4, matrix3x3/4x3 use3, matrix3x4/4x4 use4, per pinned SetFragmentProgramParameter implementation. Reject unsupported types. Iterate every validated SDK constant element, including vector arrays: SDK vector cases ignore count. Use a local copied `rsxProgramConst` descriptor with one-element scalar/vector type and count1 for each element so matrix/array continuation records do not trigger another grouped upload; never modify bytes of the original SDK record. Its real index remains unchanged. Stage-specific SDK calls upload the packed vector; fragment calls use the owned fragment offset/location and FIFO InlineTransfer internally. Validate every parameter/count before command submission.

`FinishUniformUpdates` rejects missing active uniforms and emits rsxUpdateFragmentProgramLocation after all fragment writes. Unknown stage/index/count and nonfinite values fail before that upload emits commands. Require BeginDraw before upload/finish. A new BeginDraw after a completed draw reloads required programs and requires values again, preventing inherited uniform state from silently satisfying a new draw.

The API deliberately separates authored-layout interpretation from SDK upload. Core integration will supply matrix vectors from actual authored transforms using reflected mapping; don't introduce a guessed raw constant-buffer deserializer here.

- [ ] **Step 3: Implement explicit RGBA texture ownership.**

For2D: nonnull RGBA, dimensions1..4096 (this implementation's bounded policy), exact checked width*height*4 bytes. Allocate128-aligned RSX storage. Convert each source RGBA texel explicitly to a native big-endian A8R8G8B8 word; linear format, normalized UVs (do not set UNRM), pitch=width*4, one mip level, depth1, dimension2D, locationRSX. Configure symbolic identity remap, nearest min/mag, clamp-to-edge, maxLOD0, no gamma. Read the current SDK constants/macros inside the locked image before implementation; do not guess enum numbers.

Solid cube: six swizzled1x1 faces, each one ARGB texel at a128-byte boundary; allocate768 bytes at128 alignment, cubemap flag enabled, dimension2D, one mip, normalized coordinates. All faces share the supplied color, so no orientation claim follows. This represents an intentional default for disabled shadow resources, not missing authored textures. Validate input/state before allocation. Bind supports actual mapped units0..15 and invalidates texture cache when required after initial upload. GPU memory is immutable after upload in this slice.

Pinned RPCS3 `TextureUtils.cpp` at revision2d103407 confirms swizzled layer alignment128; linear layers use pitch without that alignment. Evidence is in `docs/superpowers/execution/ps3-shader-integration/sdk-runtime-contract.md`. Emulator behavior does not prove physical hardware acceptance.

- [ ] **Step 4: Prove transformed texture sampling and consecutive updates visibly.**

Use production Ps3Video and the new GPU owners, not Ps3RasterRenderer's embedded programs. Test-only quad vertex layout is position3+UV2, stride20, with vertices top-left(-1,+1,0)/(0,0), top-right(+1,+1,0)/(1,0), bottom-left(-1,-1,0)/(0,1), bottom-right(+1,-1,0)/(1,1); two indexed triangles, culling disabled. Bind physical attribute slots from the artifact's full semantics; bind the texture unit from its Albedo mapping. Allocate test geometry through RSX and release only after drain.

Texture2x2 row0 red/green, row1 blue/yellow, all alpha255. Clear cornflower blue0xFF6495ED. The matrix supplied as four SDK dot-product vectors is:

```cpp
const float transform[16] = {
    0.5f, 0, 0, 0.25f,
    0, 0.75f, 0, -0.125f,
    0, 0, 1, 0,
    0, 0, 0, 1
};
```

This moves quad bounds to clip x[-0.25,0.75], y[-0.875,0.625]. Existing Ps3Video viewport has negative Y scale. Sample safely inside the four quadrants at screen fractions (0.5,0.375),(0.75,0.375),(0.5,0.75),(0.75,0.75); expected phaseA colors red/green/blue/yellow. Sample background at(0.1,0.1) expecting(100,149,237). Use rounding appropriate to pixel coordinates and per-channel tolerance2; don't sample boundaries.

PhaseA vertex Tint and PixelTint are(1,1,1,1). PhaseB vertex Tint=(1,0.5,1,1), PixelTint=(0.5,1,1,1), expected quadrants(128,0,0),(0,128,0),(0,0,255),(128,128,0), tolerance2. The second phase uses the same program/texture allocations and must therefore prove both vertex and fragment updates affect subsequent draws. Pixel reads must occur only after BeginFrame has waited for the previous flip, per Ps3Video::ReadDisplayedPixel; keep frame/phase association explicit. Never accept only draw counts or a non-clear center.

Log artifact identity, mapped slots/unit, phase/sample coordinates, expected/actual RGB, and terminal `PS3_AUTHORED_GPU_TESTS passed=<n> failed=0`. Fail on unavailable/stale readback instead of treating it as success. After final draw Drain successfully, release test geometry/program/textures, assert owned bytes zero, then video shutdown. No screenshots.

- [ ] **Step 5: Add isolated runner, execute GREEN, document boundaries.**

New test-only Makefile links the new program/texture sources, reader/validator, existing Ps3Video.cpp and test main with required pinned SDK libraries discovered from the repository Makefile. Use the established standalone reader Makefile's /src read-only and /out writable rules, PPU compiler, strip/sprxlinker/make_self steps. Do not modify the production Makefile or reader runner.

Runner parameters: `-FixtureRoot` (required fully-qualified input containing real three fixture files), `-OutputRoot` (required strict descendant of the task build root), optional `-RpcS3Path`. Validate current locked image ID, source/fixture paths and hashes before creating output. Use a fresh build-id subdirectory, network-disabled Docker, read-only checkout/fixtures, writable owned output. Stage fixture files byte-for-byte under USRDIR/content. Write the honest schema1 manifest expected by existing launcher with current lock SHA256 and actual artifact hashes; never bypass launcher checks. Invoke existing launcher with `-Visible` to enable color-buffer writeback and collect its fresh-run logs. Refuse to reuse an already running RPCS3 process. Parse this run's TTY terminal marker and exact pixel assertions, not stale logs.

Example final command after fixture generation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/ps3-authored-gpu.tests.ps1 -FixtureRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/authored-gpu/fixture -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/authored-gpu/acceptance -RpcS3Path C:/dev/helworks/emus/rpcs3-v0.0.42-20013-2d103407/rpcs3.exe
```

Record RED/GREEN commands, pixel results, image identity, input hashes and files changed in the task report. Document API packing/lifetime/error contracts and explicitly state this is a reusable GPU path gate, not actual cube_test/Core acceptance. Parent will review and run fresh acceptance before committing.
