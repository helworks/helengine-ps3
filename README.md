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
2. RPCS3 with official PS3 system software installed (`rpcs3.exe --installfw <PS3UPDAT.PUP>`).
   Set `HELENGINE_PS3_RPCS3_PATH` or pass `-RpcS3Path`.

```powershell
cd C:\dev\helworks\helengine-ps3
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_toolchain.ps1
```

The setup script downloads and verifies the proprietary NVIDIA Cg 3.1 archive into
`.ps3\dependencies\cg`, extracts its `usr\` tree, and builds the pinned Docker image.
All generated toolchain state stays under `.ps3`.

### Imported SDK images on macOS

Docker's classic image store can address an imported OCI image by its config digest
instead of the manifest digest pinned in `toolchain-lock.json`. Do not edit the lock.
Set `HELENGINE_PS3_SDK_IDENTITY_ROOT` to an absolute external directory containing
the original raw `manifest.json` and `config.json` from the pinned image's OCI archive.
The build, ABI probe and shader compiler verify the manifest hash, config hash and
size, linux/amd64 platform, and ordered filesystem DiffIDs against Docker inspection.
Invalid proof or Docker errors fail the build. The verified local ID is used only to
execute Docker; cache identities and the build manifest's `ImageId` retain the pin.
Native receipts also record `LocalImageId`, `ImageManifestSha256` and `ImageConfigSha256`.

For example, on macOS:

```powershell
$env:HELENGINE_PS3_SDK_IDENTITY_ROOT = '/tmp/helengine/ps3/sdk-identity'
```

Without this variable, the existing strict canonical image lookup is unchanged.

## Build

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -BuildId acceptance-cube-01 -RunSeconds 600
```

Options: `-Mode Clear|Cube|Core` (default Cube), `-FixedPose` (documented pose 0.6 rad;
`-FixedPoseRadians` selects another diagnostic orientation), `-RunSeconds 0-3600`
(0 = until the system requests exit), `-FailurePoint None|AfterVideo|AfterRenderer`.
Outputs land in `.ps3\builds\<BuildId>\` with a hashed `build-manifest.json`.

To build with Core compiled in, name a generated tree:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -BuildId core-boot-01 -Mode Core -RunSeconds 12 -GeneratedCoreRoot <path to generated tree>
```

`-CoreFailurePoint None|AfterCoreInitialize|DuringCoreDraw` exercises the lifecycle gates.
`DuringCoreDraw` raises from the native 3D adapter that Core calls, which proves an
exception unwinds through generated frames and is reported at the host boundary. Both
Core parameters are refused outside Core mode.

## Run in RPCS3

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/launch_in_emulator.ps1 -ArtifactPath .ps3\builds\acceptance-cube-01\helengine_ps3.self -Visible
```

`-Visible` is required to see (and to render) anything: RPCS3's headless mode forces the
Null renderer. Without `-Visible` the launcher runs headless and only the TTY/RPCS3 logs
are useful. Each run is recorded under `<build>\emulator-runs\<utc>\`.

## Checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test-native.ps1 -BuildId acceptance-math-01
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/build-contract.tests.ps1
```

## Known blockers and limits

- Emulator evidence only: nothing has run on PS3 hardware.
- Visually confirmed by Helena in RPCS3 on 2026-09-16; the host's own pixel readback of
  the displayed frame is the automated evidence (see docs/Validation.md).
- The image carries one local patch to PSL1GHT (`docker/patches/`) fixing a wrap
  callback codegen bug in librsx; it is pinned by hash in `toolchain-lock.json`.
