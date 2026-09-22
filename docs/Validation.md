# PS3 v0.1 validation record

All evidence below was produced on 2026-09-15/16 on the development PC with the
artifacts under the then-current external build root. New builds and runs use the
repo-local `.ps3\builds\` root. Emulator results are RPCS3
results; nothing has run on PS3 hardware. Screenshots were not taken. Visual observation
by a person: Helena confirmed the rotating cube visually in RPCS3 on 2026-09-16
(`acceptance-cube-02`, `-Visible`). The host's own pixel readback of the displayed frame
is the automated rendering evidence.

## Environment

| Item | Value |
|---|---|
| SDK image | `helengine-ps3-sdk:2f38118b9cce65751080e3c6849b2338b9c93f9d529f36a270af05490622c779`, ID `sha256:b19ffd3cfb54c04835a0e32989dfa59205764802469a2faa89aa2519734b8b3f` |
| Base image | `debian:12-slim@sha256:88200866dfff7ea7f5cbcb6ec7c8a701889efe6fe859fe64d6990e4b07ea4171`, apt snapshot `20260901T000000Z` |
| ps3toolchain | `e13aa539c07abe86cdbcd095dc1340492dbca213` (steps 001 binutils 2.22, 002 gcc 13.2.0 + newlib 1.20.0, 004 symlinks; SPU deferred) |
| PSL1GHT | `f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe` + `docker/patches/0001-psl1ght-rsx-context-callback-memory-clobber.patch` (sha256 `d639fe776f4e3abb05b3a37a1eb20e7b97458ba1cb68fe9c596116d904cf1433`) |
| Compiler | `ppu-gcc (GCC) 13.2.0`, target `powerpc64-ps3-elf`, `-mcpu=cell -mhard-float -fmodulo-sched -ffunction-sections -fdata-sections`, project code `-std=c++17 -Wall -Wextra -Werror -O2 -g` |
| Cg toolkit | 3.1 April 2012 x86_64, archive sha256 `e8ff01e6cc38d1b3fd56a083f5860737dbd2f319a39037528fb1a74a89ae9878`, `libCg.so` sha256 `01c8bbbd3c373cb5f337ab0fd7a252b9d01212d35ffc1e2ea2ea8de9d1e557d3`, mounted read-only at `/opt/cg` |
| Host compiler (math tests) | `g++.exe (MinGW-W64 x86_64-ucrt-mcf-seh) 16.1.0` |
| RPCS3 | `0.0.42-20013-2d103407 Alpha`, `C:\dev\helworks\emus\rpcs3-v0.0.42-20013-2d103407\rpcs3.exe` (archive sha256 `38a5120ac26f112da0f39b93a9ac072447aa393e91f9b766b9726f9450810fa6`, matches the published `.sha256`) |
| PS3 system software | official Sony update `PS3UPDAT.PUP` sha256 `158471fd834f8ea8036136b6aab43cd86c7ba73d79ca30e0af3c0fe0001cf365`, installed with `rpcs3.exe --installfw` (see `C:\dev\helworks\emus\ps3-firmware\SOURCE.txt`) |
| RPCS3 renderer | Vulkan on NVIDIA GeForce RTX 4090 for `-Visible` runs; headless runs force the Null renderer |

RPCS3 launch command verified with this build:

```text
rpcs3.exe --no-gui --config <run>\rpcs3-run-config.yml <artifact>.self    (visible, renders)
rpcs3.exe --headless --config <run>\rpcs3-run-config.yml <artifact>.self  (logs only, Null renderer)
```

The per-run config sets `Exit RPCS3 when process finishes: true`, windowed mode and
`Write Color Buffers: true`. RPCS3's own `config.yml` is untouched; the fresh install's GUI
ini has `confirmationBoxExitGame=false` so closing the emulation window delivers the
system exit request instead of a dialog. RPCS3 boots the SDK `make_self` SELF directly;
the ELF was not needed as launch artifact.

## Acceptance matrix

| Gate | Result | Evidence |
|---|---|---|
| Toolchain | Passed | `toolchain\prepare-20260916-025552.log`, probe `toolchain\probe-20260916-025552\` (probe.vpo 336 B, probe.fpo 80 B, PowerPC64 big-endian ELF); image build log `toolchain\image-build-40omic6zdrrg95qopbx2sfhi0.log` (first image, 9m49s); `toolchain-lock.json` |
| Host math | Passed | `acceptance-math-01\cube-tests.log`: 10 checks (layout, winding, identity, center projection, +X, finite vertices, depth order, aspect, rotation, invalid input) |
| Build safety | Passed | `script-tests-20260916-030237\`: 28 contract checks (existing BuildId, nonzero build, zero-byte ELF, manifest fields/defines/mounts, launcher rejects missing artifact, wrong name, outside root, missing emulator, missing manifest, BuildId mismatch, wrong hash, stale lock; valid launch with quoted path with spaces) |
| Video | Passed | `clear-30-01\emulator-runs\20260916-004736\TTY.log`: 720p 16:9 selected (`resolutionId=2 width=1280 height=720 aspect=1.7778`), 11,059,200 RSX bytes, 1803 vsync frames in 30 s, drain and shutdown, exit 0 |
| Cube (fixed) | Passed (readback) | `cube-fixed-04\emulator-runs\20260916-025726\TTY.log`: 1800 frames / 30 s, no FIFO errors, one ring wrap; probe center `0x0000FF` (+Z blue) first and final frame, background `0x101010` at quarter points |
| Cube (rotating) | Passed (readback) | `cube-rot-02\emulator-runs\20260916-025920\TTY.log`: 3600 frames / 60 s, three ring wraps, first frame center blue (+Z at pose 0), final frame center `0xFF0000` (+X red at pose 4.87 rad), exit 0 |
| Depth/culling at known poses | Passed (readback) | `pose-0-01` center `0x0000FF` (+Z), `pose-halfpi-01` center `0x00FFFF` (-X), `pose-pi-01` center `0xFFFF00` (-Z); each 480 frames / 8 s, exit 0. The back face never wins at center, so LESS depth and CCW back-face culling are consistent with the Y-inverted viewport |
| Lifecycle | Passed | timed exits above; `fail-video-01\...\20260916-004824` (AfterVideo: drain, shutdown, exit 1); `fail-renderer-02\...\20260916-030031` (AfterRenderer: renderer frees 760 B after drain, exit 1); `cube-exit-01\...\20260916-030907` (system exit request on window close: `reason=system-exit-request`, drain, exit 0 after 729 frames) |
| Endurance | Passed | `acceptance-cube-02\emulator-runs\20260916-030956\TTY.log`: 35,723 frames in 600 s, 33 ring wraps, no FIFO errors, constant owned bytes, timed exit 0 (details below) |
| Hardware | Not attempted | No PS3 hardware was used; emulator success is not hardware validation |

Application-owned allocations are constant for the whole run: video 11,059,200 RSX bytes
(two 1280x720 X8R8G8B8 color surfaces + one Z24S8 depth surface, 3,686,400 bytes each)
plus 4 MiB host I/O, renderer 760 RSX bytes (112 B vertex ucode stays embedded, 16 B
fragment ucode, 672 B vertices, 72 B indices). Nothing is allocated per frame; the
`PS3_FRAME_STATS` lines report the same owned byte counts every 5 s. RPCS3's own memory
and shader cache growth is the emulator's, not the application's.

## Endurance run (acceptance-cube-02, 600 s)

Run `acceptance-cube-02\emulator-runs\20260916-030956\` (Vulkan, `-Visible`), built from
commit `afa8b08` with the manifest above.

| Measure | Value |
|---|---|
| Frames / draws | 35,723 / 35,723 in 600.0 s (about 59.5 frames per second, vsync) |
| Frame stats cadence | 5 s intervals, 300 to 301 frames per interval throughout |
| Command ring wraps | 33 (every 1,073 frames; offsets return to 0x108C..0x13CC) |
| RSX FIFO errors | 0 |
| Owned bytes | video 11,059,200 and renderer 760 on every stats line |
| First frame probe | center `0x0000FF` (+Z) at pose 0 |
| Final frame probe | center and top `0xFFFF00` (-Z) at pose 2.419 rad, background elsewhere |
| Exit | `reason=run-duration-reached`, drain complete, renderer freed 760 B, exit 0, `_sys_process_exit(status=0)` at emulator time 10:08 |

RPCS3's reported "RAM Usage" (43 to 45 GB) is the whole PC, not the application, and
is excluded from the application-owned stability claim.

Repeated short build/run cycles during the session: `pose-0-01`, `pose-halfpi-01`,
`pose-pi-01` (three fresh builds, three runs, 480 frames each, exit 0) plus
`cube-fixed-04` and `cube-rot-02`; all exited normally.

## v0.2 acceptance matrix (generated Core boot gate)

Same environment as above, same pinned image
`sha256:b19ffd3cfb54c04835a0e32989dfa59205764802469a2faa89aa2519734b8b3f`, toolchain lock
`bfa8e28c477187091d5b8ef64a94fadb7158538793e6cf314825fca6d467db79`. The generated Core
tree under test is the snapshot `generated-core-snapshot-20260917-160012`, 772 files,
content fingerprint `1c0ee6504151029fd51699ab58d4c54cee350f8d5c3c8b6f0b9f3fd17eb4e079`,
unity translation unit `helengine_core_unity.cpp`.

| Gate | Result | Evidence |
|---|---|---|
| Core ABI | Passed | `docs\CoreAbiProbe.md`: pointer 8 bytes, long 8, size_t 8, big-endian confirmed three ways and cross-checked against the ELF header; exceptions unwind and `dynamic_cast` resolves at run time on GameOS |
| Compiles | Passed | `core-boot-05\build.log`: 338 generated sources as one unity translation unit, 0 separate units, 0 errors, 62 warnings, `generated-overlap.txt` empty |
| Links | Passed | `core-boot-05\`: ELF 17,181,167 bytes, SELF 765,408 bytes, map 3,742,426 bytes |
| Initializes | Passed | `core-boot-05\emulator-runs\20260917-173747\TTY.log`: `PS3_CORE_READY`, stages AdaptersReady, CoreConstructed, CoreInitialized |
| Runs | Passed | Same log: 719 complete update/draw/frame-boundary cycles in 12 s; Core drove the 3D render manager 719 times; the v0.1 cube kept drawing throughout (`PS3_FRAME_PROBE` blue center) |
| Unwinds | Passed | `core-unwind-02\emulator-runs\20260917-174953\TTY.log`: a raise from the native 3D adapter travelled back through generated `Core::Draw` frames and was caught at the host boundary with its message intact, then Core disposed and the process exited 1 |
| Disposes | Passed | `core-boot-05` log: `PS3_CORE_DISPOSED frames=719`, then CleanShutdown, then the unchanged v0.1 drain and shutdown, exit 0 |
| Deliberate failure | Passed | `core-fail-01\emulator-runs\20260917-174512\TTY.log`: stage AfterCoreInitialize, code 8, Core still disposed, exit 1 |
| v0.1 regression | Passed | `v01-regression-01\emulator-runs\20260917-175038\TTY.log`: `coreEnabled=0`, no Core stages, 598 frames in 10 s, exit 0 |
| Build safety | Passed | `scripts\tests\build-contract.tests.ps1`: 42 checks, including that `-CoreFailurePoint` is refused outside Core mode and that a successful Core build records it |
| End-to-end editor build | Passed | `platform-build-09`: the editor platform build ran codegen, packaged demodisc and produced `helengine_ps3.self` (824,080 bytes), exit 0. That artifact then ran 36,430 frames with Core driving every one, 33 ring wraps, no FIFO errors, and exited 0 on a window close (`core-07d11c3ad52a4a4db8677ea65353c6af\emulator-runs\20260918-001134\TTY.log`) |
| Material cooking | Passed | `builder.tests`: 10 checks covering colour translation, schema defaults, surface state, the texture refusal and the schema being published. End to end, `platform-build-10` cooked ten materials carrying the `ps3-standard` renderer family |
| Hardware | Not attempted | No PS3 hardware was used |

### Heap accounting

Free heap in bytes, from `mallinfo().fordblks`, in the passing run:

| Point | Free bytes |
|---|---|
| Adapters constructed, before Core | 1,055,296 |
| Core constructed | 1,053,232 |
| Core initialized | 1,045,552 |
| Core disposed | 1,046,048 |
| Adapters released, clean shutdown | 1,046,496 |

Disposal returns what Core allocated across 719 frames: free heap after disposal exceeds
the value at initialization. About 8,800 bytes are not returned relative to the
pre-Core figure. `fordblks` counts free-block bytes, so fragmentation alone can account
for that; this is reported as measured and is not claimed to be leak-free.

### Findings worth keeping (v0.2)

- **Generated code raises by pointer.** The generated tree throws `new SomeException(...)`
  at 581 of its 741 throw sites. A `catch` on a `std::exception` reference
  never matches those, so the first boundary caught every generated failure as an unknown
  type and discarded its message. The host now catches the pointer form first, reads the
  message, and deletes it. The gate's first real error message, "ContentStreamSource must
  be provided.", only became visible after that change.
- **Core requires a content stream source.** `CoreInitializationOptions::Normalize`
  rejects a null content source, even for a run that reads no content. Every other option
  has a usable default. The host supplies `Ps3ContentStreamSource` through the options
  overload of `Initialize`.
- **Core never calls the 2D render manager for an empty scene.** The unwinding gate first
  armed its raise in the 2D adapter's draw and never fired; the log showed the 2D manager
  at zero draws while the 3D manager had 360. The raise moved to the 3D adapter, which
  Core drives every frame.
- **Toolchain accommodations, all build-level.** Compiling the generated tree on the PPU
  needed the GNU C++20 dialect (for `requires` expressions, and because strict ISO mode
  hides newlib's POSIX declarations), `_GLIBCXX_USE_C99_MATH_TR1` to place 28 math
  functions in `std`, a small header supplying five long-double functions newlib omits,
  and `-isystem` so the project's `-Werror` does not apply to generated headers. No
  generated file was modified.

### Closed gap: the editor path demanded a shader id PS3 never authors

The end-to-end editor build first failed with "Shader asset id is required", which read as
a shader problem but was not one. Scene packaging recorded a shader dependency for every
material and took the id straight off the authored asset. That happens on the fallback
used when a platform has no material builder, and the PS3 definition lands there because
it declares no material schemas. Demodisc keeps each material's shader in a per-platform
override file and has none for PS3, so the id was empty and the guard rejected it.

Fixed in the engine repository, commit 609ec449, which records a dependency only when the
material actually resolves an id, with a regression test that reproduced the exact
exception beforehand. The generated tree used above is still the snapshot taken before
that fix, so the compile and run evidence is unchanged.

That inconsistency is now closed. The definition publishes the `ps3-standard-v1` schema
and the builder cooks against it, so the editor uses this builder for material work rather
than the fallback. A rerun of the end-to-end build cooked ten materials carrying the
`ps3-standard` renderer family, and its artifact booted Core for 2,391 frames and exited 0
(`platform-build-10`). Adding the schema makes code generation emit a cooked-material
entry point, which is why the generated tree now produces 79 warnings rather than 62; the
extra ones are unused-parameter notices in generated stubs.

Textures remain refused by the cooker. The build stages none, so a bound texture would
write a path that resolves to nothing on the console.
## Findings worth keeping

- **PSL1GHT librsx wrap bug.** The pinned librsx keeps `context->current` cached across
  the command-buffer wrap callback because `rsxContextCallback`'s inline asm has no
  `"memory"` clobber (verified in `toolchain\librsx-disasm\librsx.dis`: after
  `bl rsxContextCallback` the code branches back to the stale register). With cube-sized
  frames the wrap landed inside `rsxLoadVertexProgramParameterBlock` around frame 480 and
  RPCS3 reported `RSX: FIFO error: possible desync event (last cmd = 0xbf2cab88)` (a matrix
  float) then a dead FIFO. The repository patch adds the clobber; the patched disassembly
  (`librsx-patched.dis`) reloads the pointer after the call and all runs above are clean.
- **Flip ordering.** `gcmSetFlip` (RPCS3 HLE `cellGcmSetFlip`) advances PUT by the flip
  command size assuming a flushed stream, so `Ps3Video::Present` flushes before queuing
  the flip and again after it. The pinned sample's order (flip, flush, wait) is otherwise
  kept.
- **Headless RPCS3 does not render.** `--headless` forces the Null renderer; the pixel
  probe reads zeros there. Rendering evidence requires `-Visible` (`--no-gui`, Vulkan).
- **Matrix upload layout.** cgcomp's assembly dump (`toolchain\shader-asm-01\unlit.vpo.dump`)
  shows `mvp` in `c[0..3]` with `DP4 result.position.x, R0, c[0]`: register n is matrix
  row n, so the row-major `Ps3Matrix4` is uploaded without transposing.

## Pending

- Hardware validation.
- SPU toolchain and libraries (not built into the image).
