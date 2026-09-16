# Helengine PS3 Host (v0.1, native cube)

This repository contains the native PlayStation 3 host for Helengine. Version 0.1 is
native-only: it initializes video through PSL1GHT/RSX and renders a rotating, unlit,
six-color cube **without Core**. There is no generated runtime, authored scene loading,
editor integration or platform-builder support in this milestone.

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

Options: `-Mode Clear|Cube` (default Cube), `-FixedPose` (documented pose 0.6 rad;
`-FixedPoseRadians` selects another diagnostic orientation), `-RunSeconds 0-3600`
(0 = until the system requests exit), `-FailurePoint None|AfterVideo|AfterRenderer`.
Outputs land in `C:\dev\helworks\builds\helengine-ps3\<BuildId>\` with a hashed
`build-manifest.json`.

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
- Visual confirmation by a person is pending; the host's own pixel readback of the
  displayed frame is the automated evidence (see docs/Validation.md).
- The image carries one local patch to PSL1GHT (`docker/patches/`) fixing a wrap
  callback codegen bug in librsx; it is pinned by hash in `toolchain-lock.json`.
