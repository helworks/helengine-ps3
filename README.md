# Helengine PS3 Host (v0.2, generated Core boot gate)

This repository contains the native PlayStation 3 host for Helengine.

Version 0.1 is native-only: it initializes video through PSL1GHT/RSX and renders a
rotating, unlit, six-color cube **without Core**. That path is unchanged and still builds
and runs on its own.

Version 0.2 adds a boot gate: the generated Core C++ tree is compiled into the same
executable, and the host constructs Core, initializes it against native adapters, drives
update, draw and frame-boundary every frame, and disposes it. The cube keeps drawing
throughout and remains the visible output. Core renders no authored content yet; that
arrives with the scene milestone.

Everything is built inside a custom Docker SDK image (ps3dev PPU toolchain + PSL1GHT,
pinned and hash-verified) and run in RPCS3. See [docs/Build.md](docs/Build.md) for the
full toolchain, build and launch contract and [docs/Validation.md](docs/Validation.md)
for the observed evidence.

## Setup (once)

1. Docker Desktop with Linux containers.
2. NVIDIA Cg Toolkit 3.1 archive (proprietary, user supplied, never embedded in the image) at
   `C:\dev\helworks\builds\helengine-ps3\dependencies\cg\Cg-3.1_April2012_x86_64.tgz`.
3. RPCS3 with official PS3 system software installed (`rpcs3.exe --installfw <PS3UPDAT.PUP>`).
   Set `HELENGINE_PS3_RPCS3_PATH` or pass `-RpcS3Path`.

```powershell
cd C:\dev\helworks\helengine-ps3
powershell -NoProfile -File scripts/prepare_toolchain.ps1
```

## Build

```powershell
powershell -NoProfile -File scripts/build.ps1 -BuildId acceptance-cube-01 -RunSeconds 600
```

Options: `-Mode Clear|Cube|Core` (default Cube), `-FixedPose` (documented pose 0.6 rad;
`-FixedPoseRadians` selects another diagnostic orientation), `-RunSeconds 0-3600`
(0 = until the system requests exit), `-FailurePoint None|AfterVideo|AfterRenderer`.
Outputs land in `C:\dev\helworks\builds\helengine-ps3\<BuildId>\` with a hashed
`build-manifest.json`.

To build with Core compiled in, name a generated tree:

```powershell
powershell -NoProfile -File scripts/build.ps1 -BuildId core-boot-01 -Mode Core -RunSeconds 12 -GeneratedCoreRoot <path to generated tree>
```

`-CoreFailurePoint None|AfterCoreInitialize|DuringCoreDraw` exercises the lifecycle gates.
`DuringCoreDraw` raises from the native 3D adapter that Core calls, which proves an
exception unwinds through generated frames and is reported at the host boundary. Both
Core parameters are refused outside Core mode.

## Run in RPCS3

```powershell
powershell -NoProfile -File scripts/launch_in_emulator.ps1 -ArtifactPath C:\dev\helworks\builds\helengine-ps3\acceptance-cube-01\helengine_ps3.self -Visible
```

`-Visible` is required to see (and to render) anything: RPCS3's headless mode forces the
Null renderer. Without `-Visible` the launcher runs headless and only the TTY/RPCS3 logs
are useful. Each run is recorded under `<build>\emulator-runs\<utc>\`.

## Checks

```powershell
powershell -NoProfile -File scripts/test-native.ps1 -BuildId acceptance-math-01
powershell -NoProfile -File scripts/tests/build-contract.tests.ps1
```

## Known blockers and limits

- Emulator evidence only: nothing has run on PS3 hardware.
- Visually confirmed by Helena in RPCS3 on 2026-09-16; the host's own pixel readback of
  the displayed frame is the automated evidence (see docs/Validation.md).
- The image carries one local patch to PSL1GHT (`docker/patches/`) fixing a wrap
  callback codegen bug in librsx; it is pinned by hash in `toolchain-lock.json`.
