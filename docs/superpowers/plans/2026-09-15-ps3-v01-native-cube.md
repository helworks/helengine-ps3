# PS3 v0.1 Native Cube Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Use subagents only under Helena's standing opt-in/model rules. This handoff is not authorization to launch an independent review or create additional Codex tasks.

**Goal:** Produce a native PS3 executable that initializes video and renders a rotating, opaque, six-color unlit cube, without Core.

**Architecture:** A small native boot host owns video and renderer lifetimes. Immutable cube geometry and minimal camera/transform math are SDK-independent; a direct RSX renderer handles shaders, GPU resources and drawing. The structure follows Helengine handlers and can later support an engine adapter.

**Tech Stack:** C++17 for project sources, PSL1GHT/direct RSX, ps3dev cross-toolchain, SDK shader conversion rules, GNU Make, PowerShell, a custom Linux Docker SDK image, RPCS3 for initial execution. Exact dependency revisions are an explicit Task 1 deliverable, not asserted as tested here.

**Spec:** `../../PS3-v01-handler-plan.md` (from this plan's directory). Read it and both `docs/PlayStation3_Development.md` and `docs/PlayStation3_Rendering.md` before implementing.

**Repository:** `C:/dev/helworks/helengine-ps3`. At planning time it contains documentation only, HEAD `aba5b17`; the scope document is untracked. Recheck before editing. This is an implementation handoff, not evidence of a working build.

## Global Constraints

The following scope statements are copied from the spec:

- "Core, generated runtime, authored scenes, editor integration and platform builder integration are later milestones. They are not prerequisites for this cube."
- "Keep native types focused and documented under `src/platform/ps3/`, using namespace `helengine::ps3` and one class per file."
- "Keep required configuration failures visible and propagate build errors."
- "Store builds, generated shader output and logs under `C:/dev/helworks/builds/helengine-ps3/<build-id>`, outside tracked source and Temp."
- "Fixed source geometry: 24 face-local vertices and 36 indices."
- "Initial video target: 720p where supported, reporting the selected mode."
- "Screenshots require Helena's explicit permission; submission logs alone do not prove visual correctness."

Additional execution constraints:

- The PS3 SDK and cross-toolchain must be built into a custom Docker image. All PS3 native compilation and SDK shader conversion run inside that image; no host/WSL SDK fallback.
- No shared-engine, other-handler or game-project edits. No C# builder, `.heproj` fixture, generated Core probe, scene loader, ECS or engine adapter in this milestone.
- No lighting, textures, SPU jobs, audio, physics, 2D/debug font, streaming, generic asset pipeline, HDR, MSAA or generalized renderer framework.
- Follow sibling AGENTS.md conventions relevant to native work: focused documented types, readable names, explicit errors, smallest meaningful validation. Add repository-local AGENTS.md carrying these applicable rules if none exists; preserve any file added by another task.
- Native cube helpers are diagnostic-only; do not claim their conventions are Helengine's generated ABI.
- No modification of generated shader binaries/headers after compilation. Fix source/build rules instead.
- Preserve other users' work. Commit only named task files when task validation passes; never `git add .`. Do not push/publish.
- Use read-only native source mounts and writable external build mounts where feasible. Never recursively clean a computed directory without confirming it is a child of the PS3 build root.
- Tools currently on PATH: Docker, WSL, Git and g++. Their functional state and the PS3 SDK are unverified. RPCS3 was not found in the top-level emulator directory inventory; this is not proof it is absent elsewhere.
- Sandbox processes failed during planning with `helper_unknown_error: setup refresh had errors`; read/write shell operations succeeded with normal escalation. Treat that as environment history, not authorization to bypass approval.

## Source references and confidence

Use the selected immutable SDK checkout as implementation authority. These live sources were inspected to ground the plan; they are not version pins:

- https://github.com/ps3dev/ps3dev — environment setup.
- https://github.com/ps3dev/PSL1GHT — SDK and separate Cg requirement.
- https://github.com/ps3dev/PSL1GHT/blob/master/samples/graphics/rsxtest/Makefile — shader/object build integration.
- https://github.com/ps3dev/PSL1GHT/blob/master/samples/graphics/rsxtest/source/rsxutil.cpp — display/surface/presentation reference.
- https://github.com/ps3dev/PSL1GHT/blob/master/samples/graphics/rsxtest/source/main.cpp — native shader and draw reference.
- https://github.com/ps3dev/PSL1GHT/blob/master/ppu/include/rsx/rsx.h — context and memory constraints.

The research docs contain illustrative snippets and old citation tokens; do not treat them as compilable production code. Follow upstream license/notice requirements for adapted sample code. Do not import its textures, fonts or lighting.

Sibling references, read-only:

- `../helengine-dc/docs/superpowers/specs/2026-09-15-dreamcast-v01-design.md`: native-first scope precedent.
- `../helengine-gc/src/platform/gamecube/GameCubeRenderManager3D.hpp`: responsibility separation, not a required base class.
- `../helengine-ps2/README.md`, `../helengine-gc/README.md`, `../helengine-psp/README.md`: build/launcher conventions.
- `../helengine/AGENTS.md`: documentation, visible build paths and screenshot constraints.

## File map

All paths below are relative to the PS3 repository unless absolute.

| Task | Files to create | Ownership |
|---|---|---|
| 1 | `AGENTS.md`, `.gitignore`, `Makefile`, `toolchain-lock.json`, `scripts/prepare_toolchain.ps1`, `scripts/build.ps1`, `scripts/launch_in_emulator.ps1`, `scripts/tests/build-contract.tests.ps1`, `docs/Build.md`, `src/main.cpp` | Reproducible native build and boot. Also create required `docker/Dockerfile` and `docker/.dockerignore` for the custom SDK image. |
| 2 | `src/platform/ps3/Ps3Vertex.hpp`, `Ps3CubeMesh.hpp/.cpp`, `Ps3Matrix4.hpp`, `Ps3CubeTransform.hpp/.cpp`, `tests/native/Ps3CubeTests.cpp`, `scripts/test-native.ps1` | SDK-independent geometry and transforms; all short native names in this row share the preceding directory. |
| 3 | `src/platform/ps3/Ps3Video.hpp/.cpp`, `Ps3RunOptions.hpp`, `Ps3BootHost.hpp/.cpp` | Context, display, callbacks and clear-screen loop. |
| 4 | `src/platform/ps3/Ps3RasterRenderer.hpp/.cpp`, `shaders/unlit.vcg`, `shaders/unlit.fcg` | GPU resource ownership and unlit draw. |
| 5 | `docs/Validation.md`, `README.md` | End-to-end evidence and handoff instructions. |

Later tasks modify the Makefile/build scripts and main/host created earlier. No generated files are tracked.

## Task 1 — Reproducible native executable and boot route

**Consumes:** Existing docs and a verified host environment.
**Produces:** The script/artifact contracts below, pinned toolchain evidence and native entry point. This task resolves tool availability before rendering work.

### Steps

- [ ] Inspect `git status --short`, repository instructions, `docker info`, `docker buildx version`, `g++ --version`, and RPCS3 candidates under `C:/dev/helworks/emus`. Cap output; do not search unrelated personal directories. Host SDK installations are not build inputs.
- [ ] Require Docker with Linux containers and build a custom SDK image from the repository Dockerfile. WSL may back Docker Desktop but is not a separate SDK/build route. If Docker is unavailable, finish the Dockerfile and independent native-host tests, then report the Docker prerequisite; do not switch to a host SDK.
- [ ] Verify public ps3dev build prerequisites, then create `docker/Dockerfile` that installs pinned build dependencies, builds the PPU toolchain and PSL1GHT from immutable revisions, and installs them under `/opt/ps3dev`. Export `PS3DEV`, `PSL1GHT` and compiler/tool paths. Start from a Linux base image pinned by digest; pin upstream source revisions, downloaded-source hashes and package versions/snapshot where required. Inspect ps3dev scripts for nested floating downloads: pin or vendor build inputs rather than assuming one top-level commit pins everything. Record resolved source revisions, base digest, custom image ID, compiler versions, ABI flags and shader-tool hashes in `toolchain-lock.json`. The preparation script builds and verifies the custom image, and captures its build log under the external PS3 build root. Use separate `Inputs` and `ResolvedImage` sections in the lock. Compute the image tag hash from canonicalized `Inputs` only, excluding the resulting image ID to avoid a circular hash. Tag locally as `helengine-ps3-sdk:<inputs-hash>` and invoke normal builds by the verified image ID. The final build manifest hashes the complete resolved lock file. No floating `latest`, undocumented prebuilt SDK download, or host SDK mount.
- [ ] Resolve the separate Cg compiler and SDK shader conversion tools inside the container workflow. Keep proprietary Cg acquisition explicit: use a user-supplied installation mounted read-only at build time if redistribution/embedding is not permitted; do not silently bake proprietary downloads into image layers. SDK and cross-toolchain remain in the custom image. Record Cg version/hash and mount contract. Verify a minimal vertex/fragment pair compiles in the image using pinned `ppu_rules` before declaring the toolchain usable. If compiler acquisition is blocked, report that gate; do not replace direct RSX with another graphics stack without discussing scope.
- [ ] Create the build wrapper contract:

```powershell
# scripts/build.ps1 public parameters
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._-]*$')]
    [string]$BuildId,
    [ValidateSet('Clear','Cube')][string]$Mode = 'Cube',
    [switch]$FixedPose,
    [ValidateRange(0,3600)][int]$RunSeconds = 0,
    [ValidateSet('None','AfterVideo','AfterRenderer')]
    [string]$FailurePoint = 'None'
)
```

Use a fresh build directory named by BuildId and reject an existing directory. `build.ps1` runs the locked custom image with source mounted read-only at `/src`, the selected output directory read-write at `/out`, and working directory `/out`. Run normal native builds with `--network none`; SDK preparation owns dependency downloads. Never mount a host SDK or Docker socket into the build container. Emit objects, shader intermediates, ELF, SELF, link map and logs there. Select mode/pose/duration/failure settings using explicit compile definitions; PS3 process arguments are not assumed.

- [ ] Implement `scripts/prepare_toolchain.ps1` to validate Docker Linux mode, resolve the lock, build the repository Dockerfile, verify compiler/SDK executables inside the resulting image, and write the verified image identity. Its command contract is:

```powershell
powershell -NoProfile -File scripts/prepare_toolchain.ps1
```

The Docker context contains only Docker build inputs, not the whole workspace. Use a multi-stage image if useful to remove toolchain build scratch. Preserve SDK/toolchain license notices. Local image preparation is required; pushing an image is outside scope.

- [ ] Base Makefile rules on the pinned sample, but supply absolute external `BUILD` and `OUTPUT` paths. Keep the source tree read-only. Use the SDK C++ linker and PPU ABI flags; add `-std=c++17 -Wall -Wextra` to project code, dependency tracking and a link map. Do not assume the sample's recursive relative-path layout will work unchanged.
- [ ] Start `src/main.cpp` as a native PPU entry point that logs `PS3_BOOT` then exits successfully. Use the pinned SDK's process parameter declaration. Build and inspect ELF architecture/endianness with that toolchain's readelf. Retain symbols in the ELF.
- [ ] Final artifact names: `helengine_ps3.elf`, `helengine_ps3.self`, `helengine_ps3.map`, `build.log`, `build-manifest.json`. Make the SELF from the ELF through the SDK's homebrew build rules. The launcher initially tries the SELF; verify the actual RPCS3 launch command using the installed version's help and document it. If the tested loader requires ELF instead, record that choice in the manifest's LaunchArtifact property; do not require PKG/disc production just for this milestone.
- [ ] Write the success manifest only after native build returns zero and all required files are nonempty. Its fields are `SchemaVersion` (1), `BuildId`, `SourceCommit`, `SourceDirty`, `Mode`, `FixedPose`, `RunSeconds`, `FailurePoint`, `ToolchainLockSha256`, `LaunchArtifact`, and `Artifacts` (relative path/SHA256 records). Write to a temporary filename inside the build directory, then rename to the success filename. A process exit code alone is insufficient.
- [ ] Implement `launch_in_emulator.ps1` with mandatory `-ArtifactPath`, optional `-RpcS3Path` (default environment variable `HELENGINE_PS3_RPCS3_PATH`), and `-Visible`. Validate artifact containment, manifest identity and hash before starting anything. Missing emulator or mismatch is an error. Background launch uses a hidden window; `-Visible` is explicitly interactive. Use correctly quoted argument arrays for paths with spaces. Do not alter a user's existing RPCS3 profile/firmware configuration silently.
- [ ] Add script tests for missing artifact/manifest, wrong hash, nonzero build, zero-byte output, existing BuildId and path-with-spaces. An injected fake process runner for tests must stay internal; it must not create a production skip-verification flag. Use this negative-case pattern with test-owned fixtures:

```powershell
$rejected = $false
try {
    & $launcherPath -ArtifactPath $missingArtifact -RpcS3Path $fakeEmulator
} catch {
    $rejected = $true
}
if (-not $rejected) { throw 'Missing artifact was accepted.' }
if (Test-Path -LiteralPath $fakeLaunchMarker) {
    throw 'Emulator launched before artifact validation.'
}
```

All fixture/marker files belong under a fresh external `script-tests-<id>` directory. Repeat this assertion structure for each malformed manifest fixture.

- [ ] Run `powershell -NoProfile -File scripts/tests/build-contract.tests.ps1`, then `powershell -NoProfile -File scripts/build.ps1 -BuildId native-boot-01 -Mode Clear`. Expected: tests pass, build exits zero, fresh complete manifest/artifacts.
- [ ] Launch that artifact through the wrapper and verify the native boot log/exit. Record RPCS3 version, launch path and prerequisites. If RPCS3 is unavailable, complete build evidence and report runtime acceptance pending; do not claim a boot.
- [ ] Write exact working preparation/build/launch commands in `docs/Build.md`, with dependency setup separate from normal builds. Commit only this task's named files after passing available checks.

## Task 2 — Cube geometry and transform contract

**Consumes:** Host C++ compiler and external build conventions from Task 1.
**Produces:** These SDK-independent interfaces. Place each type in its listed file and document ownership/layout. Their names and signatures are shared by Tasks 3–4.

```cpp
// Ps3Vertex.hpp
struct Ps3Vertex { float Position[3]; float Color[4]; };
// Ps3Matrix4.hpp: mathematical row-major storage, column-vector multiplication.
struct Ps3Matrix4 { float Values[16]; };
// Ps3CubeMesh.hpp: process-lifetime immutable storage.
class Ps3CubeMesh {
public:
    static const Ps3Vertex* Vertices(); // exactly 24
    static const std::uint16_t* Indices(); // exactly 36
};
// Ps3CubeTransform.hpp
class Ps3CubeTransform {
public:
    static Ps3Matrix4 Build(float radians, float aspect);
    static void Transform(const Ps3Matrix4& matrix,
                          const float input[4], float output[4]);
};
```

All definitions live in `helengine::ps3`. Headers include their own needed standard headers. Vertex size is 28 bytes with color offset 12; assert both at compile time.

### Steps

- [ ] Add a dependency-free native test executable using active checks in all build modes (do not rely on disabled `assert`). Test before filling in implementation. Example projection check:

```cpp
const auto m = Ps3CubeTransform::Build(0.0f, 16.0f / 9.0f);
const float origin[4] = {0, 0, 0, 1};
float clip[4] = {};
Ps3CubeTransform::Transform(m, origin, clip);
if (std::fabs(clip[0]) > 1e-5f || std::fabs(clip[1]) > 1e-5f ||
    clip[3] <= 0 || clip[2] < -clip[3] || clip[2] > clip[3]) {
    std::fprintf(stderr, "Cube center projection failed\n");
    return 1;
}
```

- [ ] Define a side-length-2 cube at the origin. Create each face's four vertices as `normal - u - v`, `normal + u - v`, `normal + u + v`, `normal - u + v`; `normal`, `u`, `v` are unit axis vectors and `cross(u,v)=normal`. Use local triangles `(0,1,2)` and `(0,2,3)` plus face base index. The six frames are:

```text
normal     u          v          RGB
( 1,0,0)   (0,1,0)    (0,0,1)    (1,0,0)
(-1,0,0)   (0,0,1)    (0,1,0)    (0,1,1)
(0, 1,0)   (0,0,1)    (1,0,0)    (0,1,0)
(0,-1,0)   (1,0,0)    (0,0,1)    (1,0,1)
(0,0, 1)   (1,0,0)    (0,1,0)    (0,0,1)
(0,0,-1)   (0,1,0)    (1,0,0)    (1,1,0)
```

Alpha is 1. Emit fixed arrays; no heap allocation or procedural asset system. Test every index is below 24, each face has one color, and each triangle satisfies `dot(cross(b-a,c-a),a+b+c)>0` to prove outward winding.

- [ ] Implement the matrix calculation with fixed arrays and documented multiplication; use scalar float arithmetic. Camera is at `(0,0,5)`, looking down -Z, +Y up. Let `M=Ry(radians)*Rx(0.4)`, `V=translation(0,0,-5)`, `P` be the matrix below, and return `P*V*M`. `Build` rejects non-finite radians/aspect and nonpositive aspect with a diagnostic failure (host test may use `std::invalid_argument`; native boot catches it).

```text
near=0.1, far=100, verticalFov=pi/3, f=1/tan(verticalFov/2)
P = [ f/aspect  0  0                         0
      0         f  0                         0
      0         0  -(far+near)/(far-near)    -2*far*near/(far-near)
      0         0  -1                        0 ]

Multiply(A,B): result[r*4+c] = sum(A[r*4+k]*B[k*4+c], k=0..3)
Transform(A,v): output[r] = sum(A[r*4+k]*v[k], k=0..3)
```

`Rx` and `Ry` use conventional right-handed rotations. Keep helpers as documented private static members of Ps3CubeTransform, with explicit declarations in its header; no new public math framework. RSX viewport maps clip-space -1..1 depth into 0..1 in Task 3.

- [ ] Add tests for multiplication/transform identity, center projection, greater positive X yielding greater projected X, finite clip coordinates for all 24 vertices at 0/pi/2/pi, aspect change affecting horizontal scale, invalid aspect rejection, and geometry winding. Tests must check expected behavior, not compare the implementation to itself.
- [ ] Implement `scripts/test-native.ps1 -BuildId <id>` to compile only SDK-free sources/tests with host g++ into a new external directory, run the binary and propagate failure. Command shape:

```text
g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/native/Ps3CubeTests.cpp src/platform/ps3/Ps3CubeMesh.cpp src/platform/ps3/Ps3CubeTransform.cpp -o <external-build-directory>/cube-tests.exe
```

The script substitutes its validated absolute external path and does not direct outputs to Temp. Run with BuildId `cube-math-01`; record passing results and commit named geometry/test files.

## Task 3 — Native video owner and clear-screen lifecycle

**Consumes:** Task 1 native build and callback facilities; Task 2 math is not needed for clear mode.
**Produces:** Ps3Video and Ps3BootHost; main delegates to the host. API below is project-owned, not a quotation of SDK interfaces.

```cpp
// Ps3RunOptions.hpp
struct Ps3RunOptions {
    bool ClearOnly;
    bool FixedPose;
    unsigned RunSeconds; // 0 = until system exit request
    unsigned FailurePoint; // 0 none, 1 after video, 2 after renderer
};
// Ps3Video.hpp (includes pinned SDK rsx header)
class Ps3Video {
public:
    bool Initialize();
    bool BeginFrame(); // waits safely, selects surface, clears
    bool Present();
    bool Drain();
    void Shutdown();
    gcmContextData* Context() const;
    unsigned Width() const;
    unsigned Height() const;
    float Aspect() const;
};
// Ps3BootHost.hpp
class Ps3BootHost {
public:
    static int Run(const Ps3RunOptions& options);
};
```

Implement private ownership fields/helpers as needed, documented in each header. Disable copying for resource owners. Track initialized state so Shutdown is idempotent after partial initialization. SDK types remain inside native files.

### Steps

- [ ] Register a sysutil callback for exit; it only sets an exit flag. Pump callbacks during frames and presentation waits. Use the pinned SDK's monotonic clock; do not infer elapsed time from frame count. main constructs Ps3RunOptions from build definitions and returns Run's exit code.
- [ ] Allocate an initial 1 MiB command-buffer region within a 4 MiB host I/O allocation, aligned to 1 MiB, then call the pinned `rsxInit`. Check every allocation and error-returning SDK call. Log the operation and numeric error. If the pinned SDK needs a different command partition, document its requirement and the measured allocation.
- [ ] Select 720p if reported available; otherwise use the currently active supported mode, with a diagnostic explaining the choice. Never treat an unavailable resolution as successful configuration. Query display aspect and preserve it in Aspect(), including non-square-pixel fallback modes; reject unknown aspect rather than silently stretching.
- [ ] Allocate two 32-bit color surfaces and one D24S8 depth surface in RSX local memory with SDK-required alignment/pitches. Register color buffers and initialize the full surface descriptor, including unused color slots. Start with linear, single-sample surfaces. Calculate bytes from pitch*height, not nominal width alone. At 720p, three tightly packed four-byte surfaces total 11,059,200 bytes before padding; log actual totals.
- [ ] Set viewport/scissor and clear state explicitly. Desired viewport scale is `(width/2, -height/2, 0.5, 0)` and offset `(width/2, height/2, 0.5, 0)` for the Task 2 clip convention. Use a dark opaque clear color and far depth. Disable blending/stencil tests; enable normal color writes. Check against pinned constants and source rather than guessing enum names.
- [ ] Implement presentation using pinned public sample ordering and document first-frame ownership. The project algorithm is:

```text
BeginFrame:
  if a prior flip is pending, wait for completion while pumping callbacks
  bind the non-visible color target and shared depth surface
  set viewport/scissor, color/depth write masks, clear both surfaces
Present:
  reset flip status at the verified point before queueing a new flip
  queue flip of the current render target; on error return failure
  flush command stream and enqueue the SDK flip wait
  rotate render-target index; record that a flip is pending
```

Confirm both GPU command ordering and scan-out ownership; merely toggling an index is insufficient. Ordered use permits a single shared depth buffer. Do not add a full GPU finish to every normal frame. Give flip waits a diagnostic timeout (five seconds), sleep briefly between polls, and report exit/timeout distinctly.

- [ ] Implement Drain using the SDK's proven completion primitive and only after successful context initialization. Separate render completion from display scan-out lifetime. Do not free a registered displayed buffer or mapped host command memory while display/RSX can reference it. If the SDK has no supported full unregister/teardown path, retain process-lifetime display/context backing until process exit and document this; free ordinary renderer resources only after successful drain. Never free GPU-owned memory after a failed drain.
- [ ] Run Clear mode for 30 seconds and exit. Exercise FailurePoint AfterVideo to verify partial cleanup. Expected logs include `PS3_VIDEO_READY`, `PS3_FIRST_PRESENT`, `PS3_SHUTDOWN`; failure run logs its deliberate stage and returns nonzero. Record actual video mode and output artifact hash.
- [ ] Commit named video/host/build changes after clear-screen evidence. If runtime cannot be observed, mark this gate unverified and continue only work independent of that evidence.

## Task 4 — Shader pair, GPU resources and cube draw

**Consumes:** Ps3Video's live context and aspect, Ps3CubeMesh arrays, Ps3CubeTransform::Build output.
**Produces:** Ps3RasterRenderer, used directly by the native host (no RenderManager inheritance).

```cpp
class Ps3RasterRenderer {
public:
    bool Initialize(Ps3Video& video);
    bool Draw(const Ps3Matrix4& mvp);
    void Shutdown(); // caller has successfully drained GPU work
};
```

Resource owner is noncopyable; initialize handles to empty values. Shutdown releases only acquired renderer resources. Ps3Video outlives it.

### Steps

- [ ] Compile the following original shader contract through the pinned SDK rules, embedding the resulting vertex and fragment program objects. Check reflected POSITION and COLOR0 attributes and `mvp` constant exist. Fail initialization if reflection, binary conversion or allocation fails.

```c
// shaders/unlit.vcg
void main(float3 position : POSITION, float4 color : COLOR0,
          uniform float4x4 mvp,
          out float4 clip : POSITION, out float4 tint : COLOR0) {
    clip = mul(mvp, float4(position, 1.0));
    tint = color;
}
// shaders/unlit.fcg
float4 main(float4 tint : COLOR0) : COLOR {
    return tint;
}
```

- [ ] Add `.vcg -> .vpo`, `.fcg -> .fpo` and binary-to-object/header dependencies using the pinned `ppu_rules`. Build generated headers before compiling their consumers; all outputs remain external. Keep compiler-generated program data immutable after embedding.
- [ ] Allocate static vertex/index buffers and fragment microcode in SDK-required RSX memory, copy once, and convert pointers through the SDK address-to-offset API. Record allocation sizes. Do not cast CPU pointers into GPU offsets. Retain embedded vertex-program storage for its required lifetime.
- [ ] Bind vertex attributes as three F32 position components and four F32 colors, 28-byte stride, offsets 0 and 12. Use 16-bit indices and 36 triangle indices. Validate upload bounds. On the big-endian target, native float/index arrays already have native representation; do not byte-swap them speculatively.
- [ ] Bind both programs, set `mvp`, then emit an indexed triangle draw. Task 2 matrices are row-major mathematical rows. Verify the pinned parameter API's expected register layout using an asymmetric transform and source inspection; transpose in exactly one named upload helper if required. Do not copy a sample's transpose blindly or change host math to compensate twice.
- [ ] Explicitly enable depth test/write with LESS, disable blending, and cull back faces. Start with the SDK's CCW front-face convention and verify known outward faces with the configured Y-inverted viewport. A culling-disabled diagnostic can isolate errors; final Cube mode must enable culling and must not repair one pose by arbitrary index reversal.
- [ ] Integrate the host loop below; operations return failures to one cleanup path. Actual elapsed time uses a monotonic source, with each animation step clamped to 0.1 seconds after pauses. Rotation speed is 0.5 radians/second, wrapped around 2*pi; fixed pose uses 0.6 radians. Duration-based shutdown uses unclamped wall-clock elapsed time.

```text
initialize video
if FailurePoint == AfterVideo: fail
if Cube mode: initialize renderer
if FailurePoint == AfterRenderer: fail
while no exit request and duration not reached:
  pump callbacks; compute delta and pose
  if BeginFrame fails: stop
  if Cube mode: Draw(Build(pose, video.Aspect()))
  if draw or Present fails: stop
on exit:
  attempt video.Drain()
  if drain succeeds: renderer.Shutdown()
  video.Shutdown() respecting retained process-lifetime buffers
  return nonzero for failure, zero for normal exit
```

- [ ] Prove fixed cube first (`-FixedPose -RunSeconds 30`), then rotating mode (`-RunSeconds 60`). Observe six faces over rotation, correct occlusion and no corruption. Do not allocate per frame or patch fragment constants per frame; color is supplied by the vertex data. Log first draw and periodic aggregate frame/owned-byte counters.
- [ ] Run FailurePoint AfterRenderer and confirm acquired renderer allocations are released after drain. Commit named shader/renderer/integration files after validation.

## Task 5 — Final acceptance and runnable handoff

**Consumes:** Native build, host tests, clear/cube modes, launcher and diagnostics.
**Produces:** `README.md`, `docs/Build.md`, `docs/Validation.md`, a reproducible successful artifact and explicit observed/pending evidence.

### Steps

- [ ] Run host geometry tests and script contract tests once after final changes. Fix failures at their source. Confirm no generated files or build logs were added to Git.
- [ ] Build a fresh final rotating artifact using the exact command below. Its build settings must be recorded in the manifest. Do not reuse an earlier fixed-pose binary as final evidence.

```powershell
powershell -NoProfile -File scripts/test-native.ps1 -BuildId acceptance-math-01
powershell -NoProfile -File scripts/tests/build-contract.tests.ps1
powershell -NoProfile -File scripts/build.ps1 -BuildId acceptance-cube-01 -RunSeconds 600
powershell -NoProfile -File scripts/launch_in_emulator.ps1 -ArtifactPath C:/dev/helworks/builds/helengine-ps3/acceptance-cube-01/helengine_ps3.self -Visible
```

If Task 1 proved ELF is the launch artifact, use the manifest-recorded ELF path and write that exact tested command in README. Configure the emulator path through `HELENGINE_PS3_RPCS3_PATH` or the explicit launcher parameter.

- [ ] Run ten minutes, then verify timed clean shutdown. Repeat a short build/run three times and test system-requested exit at least once. Record artifact hashes, SDK/host/RPCS3 versions, mode/aspect, frame count, timing range, application-owned allocation counters, observed graphics behavior and exit result. Separate app-owned resource stability from RPCS3's shader-cache/memory growth.
- [ ] Visual evidence must be actual observation. Use a permitted live viewer or ask Helena to confirm the visible cube if no permitted observation tool is available. Do not capture screenshots without her explicit permission. Mark visual checks pending if they cannot be observed.
- [ ] Verify depth/culling with fixed poses at 0, pi/2 and pi through a temporary diagnostic build setting or debugger-controlled pose; record the visible expected face colors and orientation. Keep one documented fixed pose in the final build interface. Confirm color remains unchanged by orientation and there is no texture/lighting dependency.
- [ ] Write this acceptance matrix, filling observed results and evidence paths, not guessed successes:

| Gate | Required evidence |
|---|---|
| Toolchain | Custom SDK image identity, immutable inputs and successful in-container shader/native build |
| Host math | Passing projection, invalid-input, geometry/winding checks |
| Build safety | Missing/stale/invalid outputs rejected before emulator launch |
| Video | Configured mode, stable clear, correct flip behavior |
| Cube | Actual visible unlit cube with depth/culling and rotation |
| Lifecycle | Normal, timed, system exit and staged failure outcomes |
| Endurance | Ten-minute run, owned allocation stability and no observed corruption |
| Hardware | Separate optional evidence; emulator success is not hardware validation |

- [ ] README contains native setup/build/run commands only; it must not imply platform-builder or Core support. List remaining runtime/environment blockers explicitly. Commit task-owned documentation and final fixes; do not push.

## Completion report expected from the implementing agent

Return: commit IDs, changed files, exact build/test/launch commands, artifact and log paths, verified custom Docker SDK image identity and toolchain pins, observed test/runtime results, pending evidence and any deviations. State explicitly that v0.1 renders without Core. If a prerequisite is blocked, finish independent authorized work and identify the precise gate left incomplete.

## Plan self-review

Coverage: Task 1 covers native/shader reproducibility and fresh artifacts; Task 2 covers cube data/math; Task 3 covers video, presentation and lifecycle; Task 4 covers unlit RSX drawing; Task 5 covers sustained observation and delivery. Shared runtime and builder work are explicitly deferred. API contracts use the same names across tasks; SDK-specific signatures are resolved against a pinned checkout rather than fabricated. No implementation, toolchain installation or emulator run was performed while writing this plan.


