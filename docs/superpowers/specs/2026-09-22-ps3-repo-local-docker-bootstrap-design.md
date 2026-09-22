# PS3 repo-local Docker and build bootstrap

## Status

Design for review. No implementation changes are included in this document.

## Intent

Make the PS3 repository self-contained from a developer workflow perspective: a
developer should clone `helengine-ps3`, run the repository's setup command, and
have the Docker SDK, shader compiler input, generated Core input, native build
artifacts, emulator logs, and host-test outputs rooted under that checkout.

The repository must stop defaulting to or requiring
`C:\\dev\\helworks\\builds\\helengine-ps3`.

## Constraints

- The tracked Dockerfile and toolchain pinning remain the source of truth.
- The NVIDIA Cg toolkit is proprietary and is not committed to Git or copied
  into the Docker image layer. The setup script may download the pinned Linux
  x86_64 archive from the URL already recorded in `toolchain-lock.json`, verify
  its SHA-256, and extract it into an ignored repository-local cache.
- The normal build container continues to run with network disabled after image
  preparation.
- Generated shader output is still produced by the SDK tools and is never
  hand-edited.
- No worktrees are created or used.
- No files outside the repository are read or written by the PS3 scripts unless
  an explicit user-supplied input requires it, such as the RPCS3 executable or
  authored project path.

## Repository-local layout

```text
.ps3/
  dependencies/cg/
    Cg-3.1_April2012_x86_64.tgz   # downloaded, ignored
    usr/                           # extracted, ignored
  generated-core/                  # generated input, ignored
  builds/<BuildId>/                # native artifacts and emulator runs, ignored
  toolchain/                       # image logs and ABI/probe evidence, ignored
  user-settings/                   # isolated platform registry, ignored
```

The `.ps3` directory is created on demand. `.gitignore` protects all generated
and proprietary contents while keeping the tracked Dockerfile, patches, scripts,
lock file, and documentation reviewable.

## Workflow

`scripts/prepare_toolchain.ps1` becomes the one-time setup entry point. It will:

1. Resolve the repository root and use `.ps3` as its default state root.
2. Create the Cg cache directory.
3. Download the exact archive from the locked NVIDIA URL when absent.
4. Verify the archive hash before extraction and verify `libCg.so` after
   extraction.
5. Build and verify the pinned Docker image from the tracked Dockerfile.
6. Write toolchain logs and resolved metadata using repository-relative paths
   where metadata is persisted.

`build.ps1`, `test-native.ps1`, `probe-core-abi.ps1`, and
`register-platform.ps1` use `.ps3` defaults for their generated inputs and
outputs. `launch_in_emulator.ps1` validates artifacts under `.ps3/builds` by
default. Explicit path parameters remain available for deliberate overrides,
but the default workflow is repository-local.

The generated Core directory used by registration and Core builds is
`.ps3/generated-core`, so a developer does not need to find a generated tree in
another checkout or cache.

## Failure behavior

- A failed download, hash mismatch, extraction failure, missing Docker daemon, or
  missing locked image fails with an explicit message and does not start a
  native build.
- A missing RPCS3 executable remains an explicit launch error; emulator software
  is not silently downloaded.
- Existing build IDs are still rejected to prevent accidental overwrite.
- The setup script does not commit, modify, or delete user-authored files.

## Files affected

- `scripts/prepare_toolchain.ps1`: repo-local state and automatic Cg acquisition.
- `scripts/build.ps1`, `scripts/test-native.ps1`,
  `scripts/probe-core-abi.ps1`: repo-local output defaults.
- `scripts/launch_in_emulator.ps1`, `scripts/register-platform.ps1`: repo-local
  artifact and isolated-settings defaults.
- `toolchain-lock.json`, `.gitignore`, `README.md`, `docs/Build.md`, and related
  validation/build-contract documentation: portable paths and setup contract.
- `scripts/tests/build-contract.tests.ps1`: assertions for repo-local defaults
  and containment.

## Acceptance criteria

1. A clean clone with Docker and RPCS3 installed can run the setup command; the
   only non-repository download is the locked NVIDIA Cg archive.
2. No default script path contains `C:\\dev\\helworks\\builds`.
3. All generated build state from setup, native builds, tests, probes, and
   registration is under `.ps3` in the repository.
4. The Cg archive and extracted toolkit are ignored and hash-verified.
5. Build and launcher containment checks reject artifacts outside the repository
   `.ps3` root by default.
6. Existing explicit path overrides remain deterministic and are covered by the
   contract tests.
7. The repository remains clean apart from intended tracked script, lock,
   documentation, and ignore-rule changes after validation.
