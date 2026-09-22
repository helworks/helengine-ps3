# PS3 Repo-Local Docker Bootstrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make a clean `helengine-ps3` clone able to bootstrap the pinned PS3 Docker SDK and keep all generated PS3 state under an ignored `.ps3` directory in that repository.

**Architecture:** Add one small PowerShell path helper that resolves the repository-local `.ps3` state root while preserving explicit path overrides. Update each PS3 script to consume that helper, make toolchain preparation download and hash-check the NVIDIA Cg archive before probing, and migrate the lock/docs/contract tests to the new portable paths. The Docker image remains built from the tracked `docker/Dockerfile`; Cg remains a downloaded read-only mount rather than an image layer or Git artifact.

**Tech Stack:** Windows PowerShell 5.1, Docker Desktop Linux containers, Dockerfile/BuildKit, pinned PSL1GHT/PS3 toolchain, PowerShell contract tests, JSON lock metadata.

**Spec:** `docs/superpowers/specs/2026-09-22-ps3-repo-local-docker-bootstrap-design.md`

## Global Constraints

- All default PS3 state is under `C:\dev\helworks\helengine-ps3\.ps3`.
- The NVIDIA Cg archive is downloaded from the locked URL, verified against the locked SHA-256, extracted under `.ps3\dependencies\cg`, and never committed.
- The normal native build container continues to run with `--network none`.
- Generated shader output is produced by SDK tools and is never hand-edited.
- No worktrees are created or used.
- No default script path contains `C:\dev\helworks\builds`.
- Existing build IDs remain non-reusable, and failures remain explicit rather than best-effort.
- Do not write build outputs, generated sources, or logs to `%TEMP%` or `AppData`.

## Review Focus

- A missing Cg archive must trigger an automatic locked download, while an existing wrong-hash archive must fail without being overwritten; test the helper with a local fixture and a fake download response.
- A relative path in `toolchain-lock.json` must resolve from the repository, not the caller's current directory; test from a different working directory.
- Explicit output-root overrides used by contract fixtures must continue to work without changing the repo-local defaults; test both default and override paths.
- Build and launch containment must reject an artifact outside the selected output root before invoking Docker or RPCS3; retain the negative launcher tests.
- A failed setup/build must leave no manifest and must not delete an existing archive, build, or generated tree; test failure paths before the positive cases.

---

### Task 1: Add the repository-local state path contract (complete)

**Files:**
- Create: `scripts/ps3-paths.ps1`
- Modify: `.gitignore`
- Test: `scripts/tests/ps3-paths.tests.ps1`

**Interfaces:**
- Consumes: the repository root and an optional caller-provided state/output root.
- Produces: `Resolve-Ps3StateRoot`, `Resolve-Ps3OutputRoot`, and `Assert-Ps3PathInsideRoot` functions for the other scripts.

- [ ] **Step 1: Write contract assertions for the default state root and containment.**

Create a focused test script that dot-sources the helper and verifies:

```powershell
. (Join-Path $repoRoot 'scripts\ps3-paths.ps1')
$expectedStateRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.ps3'))
$defaultStateRoot = Resolve-Ps3StateRoot -RepoRoot $repoRoot
Assert-True 'Default state root is repo-local' ($defaultStateRoot -eq $expectedStateRoot)
Assert-True 'Default output root is repo-local' (Resolve-Ps3OutputRoot -RepoRoot $repoRoot -RequestedRoot '' -eq (Join-Path $expectedStateRoot 'builds'))
Assert-True 'Contained artifact path is accepted' ((Assert-Ps3PathInsideRoot -Path (Join-Path $expectedStateRoot 'builds\one\artifact.self') -Root (Join-Path $expectedStateRoot 'builds')) -like '*artifact.self')
```

The helper must be side-effect-free when dot-sourced; importing it must not create directories or execute production setup.

- [ ] **Step 2: Run the focused contract test and observe the missing helper failure.**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/ps3-paths.tests.ps1
```

Expected: FAIL because `scripts/ps3-paths.ps1` and its functions do not exist yet.

- [ ] **Step 3: Implement the path helper.**

Create a side-effect-free script containing functions with these behaviors:

```powershell
function Resolve-Ps3StateRoot {
    param([Parameter(Mandatory)][string]$RepoRoot, [string]$RequestedRoot = '')
    if ([string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return [IO.Path]::GetFullPath((Join-Path $RepoRoot '.ps3'))
    }
    return [IO.Path]::GetFullPath($RequestedRoot)
}

function Resolve-Ps3OutputRoot {
    param([Parameter(Mandatory)][string]$RepoRoot, [string]$RequestedRoot = '')
    $stateRoot = Resolve-Ps3StateRoot -RepoRoot $RepoRoot -RequestedRoot $RequestedRoot
    if ([string]::IsNullOrWhiteSpace($RequestedRoot)) {
        return [IO.Path]::GetFullPath((Join-Path $stateRoot 'builds'))
    }
    return $stateRoot
}

function Assert-Ps3PathInsideRoot {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Root, [string]$Label = 'Path')
    $resolvedPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $resolvedRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $prefix = $resolvedRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $resolvedPath.Equals($resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -and
        -not $resolvedPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label must be inside $resolvedRoot: $resolvedPath"
    }
    return $resolvedPath
}
```

Keep the helper free of file creation, network access, and output at import time.

- [ ] **Step 4: Add ignore rules for repo-local generated state.**

Replace the old external-build comment with rules that ignore `.ps3` contents while preserving the tracked directory-free layout:

```gitignore
# Repo-local PS3 toolchain caches, generated trees, builds and logs.
/.ps3/*
!/.ps3/.gitkeep
```

Retain the existing generated-artifact patterns as defense in depth.

- [ ] **Step 5: Run the focused contract checks again.**

Run the same focused command and expect all path assertions to pass. The full fake-Docker suite remains for Task 3 after the production scripts consume the helper.

- [ ] **Step 6: Commit the path-contract task.**

```powershell
git add scripts/ps3-paths.ps1 scripts/tests/ps3-paths.tests.ps1 .gitignore
git commit -m "build: add repo-local PS3 state paths"
```

### Task 2: Make toolchain preparation download the locked Cg input (complete)

**Files:**
- Modify: `scripts/prepare_toolchain.ps1`
- Create: `scripts/ps3-cg.ps1`
- Modify: `toolchain-lock.json`
- Test: `scripts/tests/prepare-toolchain.tests.ps1` (create)

**Interfaces:**
- Consumes: `ShaderToolkit.Url`, `Archive`, `ArchiveSha256`, `LibraryPath`, and `LibCgSha256` from `toolchain-lock.json`.
- Produces: `.ps3\dependencies\cg\Cg-3.1_April2012_x86_64.tgz`, extracted `.ps3\dependencies\cg\usr`, and `.ps3\toolchain\prepare-<utc>.log`.

- [ ] **Step 1: Add a fixture-driven test for download, hash validation, and idempotence.**

Create `scripts/ps3-cg.ps1` as a side-effect-free helper exposing `Ensure-Ps3CgArchive`, whose parameters are the locked shader-toolkit object, the resolved state root, and an optional `DownloadFile` scriptblock. The production default downloader calls `Invoke-WebRequest -UseBasicParsing`; the test injects a scriptblock that copies a fixture archive. The test must assert:

```powershell
Assert-True 'Missing archive is downloaded' ($downloadCalls -eq 1 -and (Test-Path $archivePath))
Assert-True 'Downloaded archive hash is checked' ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $lock.ShaderToolkit.ArchiveSha256)
Assert-True 'Existing valid archive is reused' ($downloadCalls -eq 1)
Assert-Rejected 'Wrong archive hash is rejected' { Ensure-Ps3CgArchive -ShaderToolkit $wrongToolkit -StateRoot $fixtureRoot -DownloadFile $fakeDownloader } 'hash mismatch'
```

The production command must not expose a skip-verification flag and the fixture must never change the committed lock values.

- [ ] **Step 2: Run the focused preparation test to verify it fails before implementation.**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/prepare-toolchain.tests.ps1
```

Expected: FAIL because preparation still requires a manually placed external archive and has no repo-local download path.

- [ ] **Step 3: Switch preparation to the repo-local state root.**

Dot-source `scripts/ps3-paths.ps1` and `scripts/ps3-cg.ps1`, resolve `$stateRoot` from a new optional `-StateRoot` parameter, and derive:

```powershell
$toolchainRoot = Join-Path $stateRoot 'toolchain'
$cgRoot = Join-Path $stateRoot 'dependencies\cg'
$cgArchivePath = Join-Path $cgRoot $shaderToolkit.Archive
$cgMountSource = Join-Path $cgRoot 'usr'
```

Do not use the old absolute `ExternalRoot`, `MountSource`, or `PrepareLog` values as defaults. Persist repository-relative metadata such as `.ps3/toolchain/prepare-<utc>.log` in the lock.

- [ ] **Step 4: Implement automatic Cg acquisition with hash-first failure behavior.**

Call `Ensure-Ps3CgArchive` to create the parent directory, download only when the archive is absent, and verify the archive before extraction. Its production behavior is:

```powershell
$cgArchivePath = Ensure-Ps3CgArchive -ShaderToolkit $shaderToolkit -StateRoot $stateRoot
```

Extract only after the archive hash succeeds, verify `libCg.so`, and fail on an extracted-library hash mismatch. Keep Cg outside the Docker image layer and mount `$cgMountSource` read-only for the probe.

- [ ] **Step 5: Update lock path fields to repository-relative values.**

Use `.ps3/dependencies/cg` for the toolkit state and `.ps3/toolchain/prepare-<utc>.log` plus `.ps3/toolchain/core-abi-<utc>` for preparation/probe metadata. Keep the download URL and both expected hashes unchanged.

- [ ] **Step 6: Run the focused preparation test and inspect the generated path assertions.**

Run the test from both the repo root and a different working directory. Expect all fixture checks to pass and no external path to appear in the output.

- [ ] **Step 7: Commit the preparation task.**

```powershell
git add scripts/prepare_toolchain.ps1 scripts/tests/prepare-toolchain.tests.ps1 toolchain-lock.json
git commit -m "build: bootstrap PS3 Cg in repo-local state"
```

### Task 3: Migrate native build, test, probe, registration, and launch scripts (complete)

**Files:**
- Modify: `scripts/build.ps1`
- Modify: `scripts/test-native.ps1`
- Modify: `scripts/probe-core-abi.ps1`
- Modify: `scripts/register-platform.ps1`
- Modify: `scripts/launch_in_emulator.ps1`
- Modify: `scripts/tests/build-contract.tests.ps1`

**Interfaces:**
- Consumes: the path helper and the repo-local `.ps3` layout.
- Produces: `.ps3/builds/<BuildId>`, `.ps3/generated-core`, `.ps3/user-settings`, and `.ps3/toolchain/core-abi-*` by default.

- [ ] **Step 1: Add default-path assertions to the contract test.**

Extend the successful fake-Docker test to invoke `build.ps1` without `-OutputRoot` in a dedicated repo-local fixture mode, then assert its manifest and fake-Docker `/out` mount are beneath `.ps3/builds`. Add equivalent default checks for the launcher and native-test command construction without starting a real emulator or compiler.

- [ ] **Step 2: Run the contract suite and capture the expected old-default failures.**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/build-contract.tests.ps1
```

Expected: the new default-path assertions fail while existing explicit `-OutputRoot` cases remain green.

- [ ] **Step 3: Migrate `build.ps1` and `test-native.ps1`.**

Dot-source the helper after calculating `$repoRoot`. Change the default output parameter to an empty string and resolve it with `Resolve-Ps3OutputRoot`. Remove the old unconditional rejection of repo-local output roots; retain explicit build-ID freshness checks and allow the default `.ps3/builds` root.

For Core mode, default `-GeneratedCoreRoot` to `.ps3/generated-core` only when the caller omits it, while retaining the unity-file validation. Mount the resolved generated root read-only at `/generated`.

- [ ] **Step 4: Migrate `probe-core-abi.ps1` and `register-platform.ps1`.**

Rename their default state parameter to `-StateRoot` (or preserve a compatibility alias) and resolve `.ps3` when omitted. Write ABI probes under `.ps3/toolchain/core-abi-*`, platform settings under `.ps3/user-settings`, and generated Core under `.ps3/generated-core`. Keep explicit authored project and builder paths as caller-supplied inputs.

- [ ] **Step 5: Migrate `launch_in_emulator.ps1`.**

Resolve the default artifact root as `.ps3/builds`, update the containment error to name the selected root rather than “external build root,” and keep the manifest hash/lock validation and per-run emulator logs unchanged.

- [ ] **Step 6: Run the complete fake contract suite.**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/build-contract.tests.ps1
```

Expected: all existing validation cases plus the new repo-local default cases pass, with zero failures. The test must leave only ignored `.ps3` fixtures and no worktree changes.

- [ ] **Step 7: Commit the script migration.**

```powershell
git add scripts/build.ps1 scripts/test-native.ps1 scripts/probe-core-abi.ps1 scripts/register-platform.ps1 scripts/launch_in_emulator.ps1 scripts/tests/build-contract.tests.ps1
git commit -m "build: use repo-local PS3 state by default"
```

### Task 4: Update Docker/build documentation and portable metadata (complete)

**Files:**
- Modify: `README.md`
- Modify: `docs/Build.md`
- Modify: `docs/Validation.md`
- Modify: `docs/CoreAbiProbe.md`
- Modify: `docs/superpowers/plans/2026-09-15-ps3-v01-native-cube.md` and `docs/superpowers/plans/2026-09-16-ps3-v02-core-boot.md` only where active commands would otherwise prescribe the removed external root

**Interfaces:**
- Consumes: the finalized script parameter names and `.ps3` layout from Tasks 1–3.
- Produces: clone/setup/build/run instructions that do not require a manually prepared external directory.

- [ ] **Step 1: Replace setup instructions with the one-command bootstrap.**

Document:

```powershell
cd C:\dev\helworks\helengine-ps3
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\prepare_toolchain.ps1
```

Explain that the first run downloads and verifies NVIDIA Cg into ignored `.ps3\dependencies\cg`; subsequent builds use the prepared image with networking disabled.

- [ ] **Step 2: Rewrite build, Core, test, and launch examples.**

Use `.ps3\builds\<BuildId>`, `.ps3\generated-core`, and `.ps3\user-settings` in all default examples. Keep explicit `-OutputRoot`, `-StateRoot`, and `-GeneratedCoreRoot` examples only where they demonstrate deliberate overrides.

- [ ] **Step 3: Update validation path references without rewriting historical evidence.**

Historical runs may retain their original absolute paths as evidence, but current workflow sections must identify the repo-local path. Do not claim new runtime validation until a real SDK build is run.

- [ ] **Step 4: Run repository-wide path search.**

Run:

```powershell
rg -n "C:\\dev\\helworks\\builds|external PS3 build root|ExternalRoot|MountSource" README.md docs scripts Makefile toolchain-lock.json .gitignore
```

Expected: only intentional compatibility notes or historical evidence remain; no default command points at the removed external root.

- [ ] **Step 5: Commit documentation and metadata updates.**

```powershell
git add README.md docs docker/Dockerfile toolchain-lock.json .gitignore
git commit -m "docs: document repo-local PS3 Docker workflow"
```

### Task 5: Validate the complete repository-local workflow

**Files:**
- Test: all changed scripts and contract tests
- Inspect: `git diff`, `.ps3` ignored-state behavior, and final worktree

**Interfaces:**
- Consumes: the completed repo-local setup and script contracts.
- Produces: fresh evidence for setup validation, contract validation, and repository cleanliness.

- [ ] **Step 1: Validate PowerShell syntax for every changed script.**

Run a parser pass that reports all syntax errors without executing network or Docker actions:

```powershell
$scripts = @('scripts/ps3-paths.ps1','scripts/prepare_toolchain.ps1','scripts/build.ps1','scripts/test-native.ps1','scripts/probe-core-abi.ps1','scripts/register-platform.ps1','scripts/launch_in_emulator.ps1','scripts/tests/build-contract.tests.ps1')
foreach ($script in $scripts) {
    $tokens = $null; $errors = $null
    [Management.Automation.Language.Parser]::ParseFile((Join-Path (Get-Location) $script), [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count -gt 0) { throw "$script has PowerShell parse errors: $($errors -join '; ')" }
}
```

- [ ] **Step 2: Run the fake contract suite from the repository root.**

Run the Task 3 command and require zero failures.

- [ ] **Step 3: Run the fake contract suite from another working directory.**

Run:

```powershell
Push-Location C:\Windows
try { powershell -NoProfile -ExecutionPolicy Bypass -File C:\dev\helworks\helengine-ps3\scripts\tests\build-contract.tests.ps1 }
finally { Pop-Location }
```

Require the same pass count and confirm every generated fixture remains under the repository `.ps3` root.

- [ ] **Step 4: Run `git diff --check` and inspect ignored state.**

Run:

```powershell
git diff --check
git status --short --ignored
```

Require no whitespace errors, no tracked generated files, and no files outside the repository created by the scripts.

- [ ] **Step 5: Run the real setup/build only when Docker, network access, and the NVIDIA download are available.**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\prepare_toolchain.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\build.ps1 -BuildId index-width-01 -Mode Core -GeneratedCoreRoot .ps3\generated-core -RunSeconds 12
```

Require the setup image verification and a zero-exit native build before claiming compilation. If the proprietary download or Docker daemon is unavailable, report that exact blocker without substituting another platform’s generated tree.

- [ ] **Step 6: Commit only the named completed implementation files.**

Stage the changed scripts, lock, docs, tests, and ignore rules explicitly; never use `git add .` and never push.
