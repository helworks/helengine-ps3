# PS3 handler v0.1 — native video and unlit cube

Status: draft plan. Scope confirmed: native cube first, without Core.
Implementation has not started.

## Goal

Build a native PS3 executable in the Helengine PS3 handler repository that initializes video and renders an unlit cube. Follow the sibling handlers' organization, coding, build-output and diagnostic conventions.

Core, generated runtime, authored scenes, editor integration and platform builder integration are later milestones. They are not prerequisites for this cube.

## Reference intake

Read both local references in full: `PlayStation3_Development.md` and `PlayStation3_Rendering.md`. Apply their memory-placement, command-submission, resource-lifetime and synchronization guidance. Defer SPU work until needed.

Standards inspected: core and sibling AGENTS.md files; PS2, PSP and GameCube README/build conventions; PSP boot host; GameCube renderer decomposition; Dreamcast native v0.1 design and subsequent generated-runtime integration. Dreamcast's native-first milestone is the closest scope precedent.

The current shared builder contracts live under `helengine/engine/helengine.baseplatform`. That is context for later integration, not a dependency to introduce in v0.1.

## Approach and conventions

- Use C++ with PSL1GHT and direct RSX as the initial toolchain candidate. Build the SDK and cross-toolchain into a custom Docker image; perform native compilation and SDK shader conversion inside it. Pin versions after proving native build and shader compilation. No host/WSL SDK fallback.
- Keep native types focused and documented under `src/platform/ps3/`, using namespace `helengine::ps3` and one class per file.
- Separate boot/lifecycle, video ownership, raster submission and cube data. Provide a small native rendering interface that can later serve an engine adapter; do not build unused adapter interfaces now.
- Use only the local transform/projection helpers needed for the diagnostic cube. Document conventions so later engine integration can map them explicitly.
- Keep required configuration failures visible and propagate build errors.
- Store builds, generated shader output and logs under `C:/dev/helworks/builds/helengine-ps3/<build-id>`, outside tracked source and Temp.
- Use `scripts/build.ps1` and the sibling launcher convention `scripts/launch_in_emulator.ps1 -ArtifactPath ...`, with fresh-artifact checks. The shared `.heproj` build graph is deferred.
- Recheck repository status and applicable instructions before implementation. No other handler or shared-engine changes are needed for this milestone.

## Visible result

- A centered cube against a dark background with a fixed perspective camera.
- Six opaque face colors, constant as the cube turns; no lighting or textures.
- Fixed source geometry: 24 face-local vertices and 36 indices.
- Depth testing/writes and back-face culling, validated at known orientations.
- First prove a static cube; then add slow elapsed-time rotation to exercise all faces. A fixed-angle diagnostic mode supports repeatable comparisons.
- Initial video target: 720p where supported, reporting the selected mode.

## Proposed source organization

| File | Responsibility |
|---|---|
| `src/main.cpp` | Enter the native PS3 host. |
| `src/platform/ps3/Ps3BootHost.hpp/.cpp` | Startup, callbacks, frame timing, exit and ordered shutdown. |
| `src/platform/ps3/Ps3Video.hpp/.cpp` | RSX context, display mode, color/depth surfaces and presentation. |
| `src/platform/ps3/Ps3RasterRenderer.hpp/.cpp` | Shader/geometry resources, draw state and cube submission. |
| `src/platform/ps3/Ps3CubeMesh.hpp/.cpp` | Immutable diagnostic geometry and face colors. |
| `src/platform/ps3/Ps3CubeTransform.hpp/.cpp` | Small diagnostic model/view/projection calculation. |
| `shaders/` | Minimal unlit vertex/fragment shader sources. |
| `docker/Dockerfile`, `docker/.dockerignore`, `scripts/prepare_toolchain.ps1`, `toolchain-lock.json` | Build and verify the custom Docker SDK image. |
| `Makefile`, `scripts/build.ps1` | In-container native compilation and fresh output validation. |
| `scripts/launch_in_emulator.ps1` | Launch the supplied artifact with RPCS3. |
| `docs/Build.md`, `docs/Validation.md` | Toolchain pins, commands and observed acceptance evidence. |

## Milestones

### 1. Native build and shader toolchain

Create the custom Docker SDK image from a pinned Linux base, build pinned SDK/compiler dependencies into it, and prove an executable built inside the container boots. Verify shader source compilation and binary embedding or loading. PSL1GHT's documented Cg route requires a separate compiler installation; record the actual verified workflow and dependency versions.

Deliverable: a repeatable native build and launch command. No generated Core compilation or engine ABI compatibility gate in this milestone.

### 2. Video and presentation

Initialize the command context and display, allocate two color surfaces and a depth surface, and configure viewport, clear and presentation. Respect SDK pitch, alignment and memory-location requirements. Track ownership and initialize every state field required by the selected surface path.

Deliverable: a stable clear-screen loop with responsive exit callbacks. Verify flip ordering and safe back-buffer reuse before adding geometry.

### 3. Unlit cube

Use a triangle only as an intermediate diagnostic if needed. Upload static cube vertices/indices, bind the unlit shader pair and supply model/view/projection constants. Enable depth and culling after validating matrix upload conventions and triangle winding. Keep allocations out of the normal frame loop.

Deliverable: a visible static cube, followed by a slowly rotating cube with correctly occluded faces and stable colors.

### 4. Lifecycle and focused validation

Handle partial initialization failures and normal exit. Drain outstanding GPU work before releasing resources. Log startup stages, actual video mode, resource allocations, first submission and shutdown without logging every frame.

Use focused host checks for transform/projection and geometry bounds, and actual runtime checks for shader loading, depth/culling, presentation and resource reuse. Confirm a failed build cannot launch stale output. Run repeated launch/exit and a ten-minute sustained run. Record emulator and hardware evidence separately. Screenshots require Helena's explicit permission; submission logs alone do not prove visual correctness.

Deliverable: reproducible executable, observed cube rendering, clean exit and documented validation results. RPCS3 is the proposed initial acceptance target; physical hardware validation remains a separate decision.

## Deferred integration milestone

After native video and cube rendering work, plan the platform builder/definition, shader/material cooking, generated-runtime compatibility, Core lifecycle, content access, authored scene loading and RenderManager adapters using the current shared contracts. Generated output must remain unmodified. Reuse the native video and raster components from v0.1 where appropriate.

Also deferred: SPU jobs, lighting, textures, animation, physics, 2D, audio, streaming, general asset loading and multiple render passes.

## External reference

PSL1GHT build and shader workflow checked against its upstream README:
https://github.com/ps3dev/PSL1GHT#optional-dependencies

