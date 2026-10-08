# PS3 controller and Light icon implementation plan

**Goal:** The first PS3 controller operates the existing cube_test camera and Triangle advances Light once per press; the console overlay displays Triangle.

**Design:** Bounded implementation of the existing IInputBackend contract, not a new input system. Parent plans/reviews; Luna implements. No shared Core API, generated source, renderer, shader, or light-component changes. User approved both corrections; this document records the concrete design for review before implementation.

## Native input

Files: `src/platform/ps3/Ps3InputBackend.hpp/.cpp`; narrowly add SDK `-lio` linkage to `Makefile` and affected native test makefiles, preserving existing dirty changes. Add focused input tests and their Docker runner under `tests/` and `scripts/tests/`.

- Own `ioPadInit(MAX_PORT_NUM)` and `ioPadEnd()` for this backend lifetime. Initialization errors must fail visibly; never silently treat failure as disconnected devices. Do not adopt an already-owned pad service without an explicit ownership design.
- Poll `ioPadGetInfo2` and `ioPadGetData` by stable port index. Report disconnected ports without compacting other ports. A disconnected controller is normal, not an initialization failure.
- The installed SDK explicitly documents `data.len == 0` as no NEW input. Retain that connected port's previous state, so a held button is not released spuriously. Clear cached state on disconnect/reassignment.
- Map Triangle to North, Circle to East, Cross to South, Square to West; map D-pad, L1/R1, L2/R2, L3/R3, Start/Select to their existing InputGamepadButton counterparts. No invented Home/PS event, rumble, sensors, or keyboard handling.
- Convert both stick axes to the existing signed short convention, preserving center and endpoints; verify Y orientation against the shared input consumer. Keep dead-zone policy in the existing shared input layer rather than adding another hidden dead zone.
- Populate trigger values according to actual reported capability/data; digital-only devices expose their real digital endpoint values, not invented pressure precision.
- Keep two backend-owned fixed-capacity gamepad arrays and alternate snapshots: shared InputSystem assigns `previousFrame = CurrentFrame`, so mutating one reused array would erase press/release edges. No per-frame allocation or generated ownership changes. Release arrays only after Core stops consuming them.
- Include focused tests for each mapping, Triangle press/hold/release/press, unchanged packets, center/extreme axes, stable sparse port indices, disconnection/reconnection, init/read failure, and shutdown ownership. Use controlled pad API responses, then compile the actual SDK adapter inside the pinned Docker image. Assert resulting InputFrameState and shared edge detection, not only internal helper calls.

## Authored icon

Files: DemoDisc `assets/codebase/DemoDiscEditorTools/rendering/DemoSceneInstructionOverlayFactory.cs`, `assets/blueprints/ui/ConsoleCameraLightInstructions.hblueprint`, and only the matching console LightIcon override in `assets/scenes/rendering/cube_test.helen`.

- Add PS3 Triangle to ConsoleLightIconSpecs using the existing platform icon mechanism. The desktop LightIconSpecs already contains PS3 Triangle; preserve it.
- Prepare serialized copies with the production serializer. Change only the PS3 LightIcon texture/source rectangle/size override needed to identify Triangle. Preserve common PS2 Circle and every other platform, entity, label alignment, transform, and opaque component payload.
- Compare canonical before/after data excluding only the intended override; reject unexpected targets, duplicate scope entries, or unrelated differences. Back up exact current input bytes and hash-check again immediately before installation. Do not regenerate the entire scene or overwrite another agent's edits.
- Add an authoring regression assertion for the PS3 Triangle override and unchanged PS2 Circle. Follow the existing serializer migration test pattern; do not repurpose the label-position migration command to silently change icons.

## Validation and handoff

- Watch focused tests fail before implementation, then pass. Parent reviews source and input snapshot lifetime, particularly no-new-packet behavior.
- Build through `helengine/scripts/build-platform.ps1`, current standard output/cache roots, Release/release, pinned Docker SDK, and `HELENGINE_PS3_NATIVE_RUN_SECONDS=0`.
- Existing user RPCS3 PID2924 is reserved: do not close or replace it, take screenshots, or start a competing emulator. Ask for a reload only after the build is ready; user decides when to close the current window.
- Verify the new scene loads; user checks Triangle advances the light palette once per press and D-pad/stick camera control. Retain open demo indefinitely. Never claim physical-controller validation from synthetic tests alone.
- No worktrees, unrelated edits, commit, or push. Build outputs/logs only under `C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924`.

## Evidence inspected

Ps3InputBackend currently returns an empty InputFrameState and RuntimeHost installs that backend. Generated DemoDiscLightToggleComponent uses North except on PS2. ConsoleLightIconSpecs lacks PS3 and uses PS2 Circle as its common icon. Shared InputSystem copies the frame shallowly before CaptureFrame. SDK installed in pinned image exposes ioPadInit, ioPadEnd, ioPadGetInfo2 and ioPadGetData, with documented no-change len0 behavior. Graph coverage for input sources was stale/partial; material paths were read directly. DemoDisc/generated paths are outside the indexed native repository and were read directly.
