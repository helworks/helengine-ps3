# Building the native PS3 host (v0.1)

This milestone is native-only: no Core, generated runtime, authored scenes, editor or
platform-builder integration. Everything below produces the standalone cube executable.

All commands run from the repository root in Windows PowerShell:

```powershell
cd C:\dev\helworks\helengine-ps3
```

Outputs never go to the worktree or Temp. The external build root is
`C:\dev\helworks\builds\helengine-ps3`:

| Path | Content |
|---|---|
| `dependencies\ps3toolchain`, `dependencies\PSL1GHT` | Read-only reference checkouts of the pinned upstream revisions (not build inputs; the image clones its own). |
| `dependencies\cg\` | NVIDIA Cg toolkit archive and its extracted `usr\` tree, mounted read-only at `/opt/cg` for shader conversion. |
| `toolchain\prepare-<utc>.log`, `toolchain\probe-<utc>\` | SDK image build log and the toolchain probe outputs. |
| `<build-id>\` | One directory per native build (see below). |
| `script-tests-<utc>\` | Fixtures and outputs of the script contract tests. |

## 1. Dependency setup (once per lock change)

### 1.1 Docker

Docker Desktop must run Linux containers (`docker info --format '{{.OSType}}'` prints
`linux`). WSL may back Docker Desktop; it is not a separate SDK or build route. There is
no host or WSL SDK fallback: every PS3 compile and shader conversion happens inside the
image built here.

### 1.2 NVIDIA Cg toolkit (proprietary, user supplied)

PSL1GHT's `.vcg`/`.fcg` shader workflow needs `cgcomp`, which loads NVIDIA's discontinued,
proprietary `libCg.so` at run time. The toolkit is deliberately **not** baked into the
image. Download it yourself, accept NVIDIA's licence (`usr/local/Cg/docs/license.txt`
inside the archive), and place the archive at:

```text
C:\dev\helworks\builds\helengine-ps3\dependencies\cg\Cg-3.1_April2012_x86_64.tgz
```

Source and expected identity (also recorded in `toolchain-lock.json`, `ShaderToolkit`):

| Item | Value |
|---|---|
| URL | `https://developer.download.nvidia.com/cg/Cg_3.1/Cg-3.1_April2012_x86_64.tgz` |
| Archive SHA-256 | `e8ff01e6cc38d1b3fd56a083f5860737dbd2f319a39037528fb1a74a89ae9878` |
| Mount | `dependencies\cg\usr` → `/opt/cg`, read-only, only during shader/native builds |

`prepare_toolchain.ps1` verifies the archive hash, extracts `usr\` next to it if needed
and records the `libCg.so` hash. `build.ps1` refuses to run if that hash changes.

### 1.3 Build and verify the SDK image

```powershell
powershell -NoProfile -File scripts/prepare_toolchain.ps1
```

What it does:

1. Reads the `Inputs` section of `toolchain-lock.json` (Debian base digest, apt snapshot
   timestamp, ps3toolchain and PSL1GHT commits, toolchain source archive hashes) and
   records the Dockerfile/.dockerignore hashes into it.
2. Computes the SHA-256 of the canonical (sorted-key, compact) JSON of `Inputs`. That hash
   names the local tag `helengine-ps3-sdk:<inputs-hash>`; it never includes the resulting
   image ID, so the tag is not circular.
3. Builds `docker/Dockerfile` (context = `docker/` only, with `--provenance=false` so cached
   rebuilds keep the same image ID) with every pin passed as a build argument. The builder
   stage runs the pinned ps3toolchain PPU steps (binutils 2.22, gcc 13.2.0 + newlib 1.20.0,
   symlinks), applies the repository patches under `docker/patches/` to the pinned PSL1GHT
   commit (each recorded by hash in `Inputs.Psl1ghtPatches`) and builds the PSL1GHT PPU
   libraries, sprx stubs and host tools (`cgcomp`, `sprxlinker`, `make_self`, `fself`,
   `bin2s`, …). The runtime stage keeps only `/opt/ps3dev` plus the shared libraries those
   tools need.
4. Verifies the compiler, readelf, SDK libraries, rules and tools inside the image and
   reads `/opt/ps3dev/helengine/toolchain-record.txt`.
5. Compiles a probe vertex/fragment pair through the pinned `ppu_rules` (with the Cg
   mount) and a probe PPU ELF, and checks the ELF header is PowerPC64 big endian.
6. Writes `ResolvedImage` (tag, image ID, compiler versions, ABI flags, probe hashes) into
   `toolchain-lock.json`. Normal builds are invoked by that image ID.

The SPU toolchain and SPU libraries are not built in v0.1; SPU work is deferred.

## 2. Native build

```powershell
powershell -NoProfile -File scripts/build.ps1 -BuildId <id> [-Mode Clear|Cube] [-FixedPose] [-RunSeconds <0-3600>] [-FailurePoint None|AfterVideo|AfterRenderer]
```

- `BuildId` names a fresh directory `C:\dev\helworks\builds\helengine-ps3\<id>`; an
  existing directory is rejected.
- The locked image runs with `--network none`, the repository mounted read-only at `/src`,
  the build directory read-write at `/out` (working directory) and the Cg tree read-only
  at `/opt/cg`. No host SDK or Docker socket is mounted.
- Mode, pose, duration and failure point become explicit compile definitions
  (`HELENGINE_PS3_CLEAR_ONLY`, `HELENGINE_PS3_FIXED_POSE`, `HELENGINE_PS3_FIXED_POSE_RADIANS`,
  `HELENGINE_PS3_RUN_SECONDS`, `HELENGINE_PS3_FAILURE_POINT`); PS3 process arguments are not
  used. `-FixedPoseRadians` (default 0.6, the documented pose) is a diagnostic that only
  applies with `-FixedPose`.
- `Makefile` compiles `src/**.cpp` with `-std=c++17 -Wall -Wextra -Werror -mcpu=cell` plus
  the SDK `MACHDEP` flags, converts `shaders/*.vcg|*.fcg` with `cgcomp`, embeds the
  binaries with `bin2s`, links with the SDK C++ driver and writes a link map, then makes
  the SELF from a stripped staged copy exactly as the pinned `ppu_rules` `%.self` recipe.

Artifacts in the build directory:

| File | Meaning |
|---|---|
| `helengine_ps3.elf` | Unstripped PPU ELF with symbols (primary artifact). |
| `helengine_ps3.self` | SELF produced by the SDK homebrew rules (`make_self`). |
| `helengine_ps3.fake.self` | Fake-signed SELF produced by `fself` (SDK rule side product). |
| `helengine_ps3.map` | Link map. |
| `build.log` | Full container output, prefixed with the exact docker command. |
| `elf-header.txt` | Pinned `ppu-readelf -h` output. |
| `shaders\*.vpo`, `*.fpo`, `*_vpo.h`, `*_fpo.h` | Shader conversion outputs (never edited). |
| `build-manifest.json` | Written only after a zero exit and nonempty artifacts (temporary name, then renamed). Records BuildId, source commit/dirty flag, mode settings, lock hash, image ID, `LaunchArtifact` and SHA-256 per artifact. |

## 3. Launch in RPCS3

```powershell
powershell -NoProfile -File scripts/launch_in_emulator.ps1 -ArtifactPath C:\dev\helworks\builds\helengine-ps3\<id>\helengine_ps3.self [-RpcS3Path <rpcs3.exe>] [-Visible]
```

The launcher validates, before starting anything, that the artifact lives under the
external build root, that the directory's manifest names it as `LaunchArtifact` with a
matching `BuildId`, that its SHA-256 matches, and that the manifest's lock hash matches
the current `toolchain-lock.json`. The emulator path comes from `-RpcS3Path` or
`HELENGINE_PS3_RPCS3_PATH`. Background runs use `--headless` in a hidden window, which
forces RPCS3's Null renderer (logs only, nothing is drawn); `-Visible` uses `--no-gui`
with the real renderer and shows the emulation window. Each run gets a per-run game
config passed with `--config` (exit when the process finishes, windowed, color-buffer
write-back for the host pixel probe) and is recorded under `<build>\emulator-runs\<utc>\`
(command, stdout/stderr, copies of `RPCS3.log` and `TTY.log` from the emulator's `log\`
directory). The launcher never edits the emulator's own `config.yml`.

RPCS3 setup (once): extract the pinned Windows build under `C:\dev\helworks\emus\` and
install the official system software with `rpcs3.exe --headless --installfw <PS3UPDAT.PUP>`.
To let a window close deliver the system exit request instead of a confirmation dialog,
set `confirmationBoxExitGame=false` under `[main_window]` in that install's
`GuiConfigs\CurrentSettings.ini`. Versions, hashes and the verified command line are
recorded in `docs/Validation.md`.

## 4. Host-side checks

```powershell
powershell -NoProfile -File scripts/test-native.ps1 -BuildId <id>
powershell -NoProfile -File scripts/tests/build-contract.tests.ps1
```

`test-native.ps1` compiles the SDK-free geometry/transform sources and their tests with
the host `g++` into `C:\dev\helworks\builds\helengine-ps3\<id>` and runs them.
`build-contract.tests.ps1` exercises the build wrapper and launcher against test-owned
fake docker/emulator fixtures under `script-tests-<utc>` (it needs a prepared lock).
