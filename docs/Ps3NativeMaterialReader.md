# PS3 Native Material Reader

`Ps3MaterialArtifactReader` decodes the exact version-one `P3MT` envelope into an owning `Ps3CookedMaterial` DTO. It accepts the PS3 standard renderer's fixed `ForwardStandardShader` pair and requires the canonical shader lookup path:

```text
cooked/shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp
```

The wire contract and producer-side semantics are defined in [Ps3MaterialArtifactFormat.md](Ps3MaterialArtifactFormat.md). The native reader checks a 12-byte header, exact total length, a 1 MiB envelope limit, a 65,536-byte per-string limit, strict UTF-8 without NUL, and exact end-of-input. It retains the four raw RGBA bytes and every supported surface flag: bit 0 lit, bit 1 double-sided, and bit 2 vertex-color. Any other flag bit fails.

All required identity strings must be nonblank. The optional diffuse texture string may be empty. A nonempty texture path must use forward slashes and cannot contain absolute or drive-relative forms, backslashes, colons, control characters, empty or whitespace-only segments, or `.` and `..` segments. Valid spelling and case are preserved. Failures return a field-specific diagnostic and leave the caller's destination unchanged.

`MatchesProgram` rechecks the fixed material contract and compares shader asset, vertex program, pixel program, and variant names by exact string equality against a `Ps3CookedShaderProgram` already decoded by `Ps3ShaderArtifactReader`. It does not open files, load a texture, select a fallback, or perform GPU work.

## Producer fixture and PPU target

The current fixture uses fixtureRoot/cooked as the production shader CookRootPath;
the shader declaration and P3MT reference therefore resolve beneath fixtureRoot at
cooked/shaders/<hash>.ps3sp. material.p3mt stays directly under fixtureRoot. Prior
producer-002 fixtures remain immutable historical inputs and must not be used with
the new canonical-path reader. The additional malformed case explicitly rejects
the old unprefixed shaders/<hash>.ps3sp reference. No binary patching or native
lookup fallback is used. This parser fixture retains vertex-color/translucent flags
to exercise wire preservation; it is not a drawable standard-scene material.

The Docker-category `ProducesNativeMaterialFixture` test uses the production `Ps3MaterialCooker` and shader artifact cooker. It reads `ForwardStandardShader.hlsl` from the explicitly resolved sibling `HELENGINE_ROOT`, passes the material's sole returned shader dependency to the cooker, verifies the production identity path, and writes `material.p3mt` with the real cooked P3SP artifact beneath the fixture root. No texture file is needed for this CPU parsing gate.

The fixture test requires explicit absolute paths in `HELENGINE_PS3_REPOSITORY_ROOT`, `HELENGINE_ROOT`, and `HELENGINE_PS3_MATERIAL_NATIVE_FIXTURE_ROOT`. The fixture root must be an empty strict child of:

```text
C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material
```

Example PowerShell setup and producer command (use fresh directory names for each run):

```powershell
$nativeRoot = 'C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material'
$fixtureRoot = Join-Path $nativeRoot 'fixtures\producer-example'
$producerRoot = Join-Path $nativeRoot 'producer-example'
New-Item -ItemType Directory -Path $fixtureRoot
New-Item -ItemType Directory -Path $producerRoot
$env:HELENGINE_PS3_REPOSITORY_ROOT = (Resolve-Path '.').Path
$env:HELENGINE_ROOT = (Resolve-Path '..\helengine').Path
$env:HELENGINE_PS3_MATERIAL_NATIVE_FIXTURE_ROOT = $fixtureRoot
$env:TEMP = Join-Path $producerRoot 'tmp'
$env:TMP = $env:TEMP
$env:DOTNET_CLI_HOME = Join-Path $producerRoot 'dotnet-home'
New-Item -ItemType Directory -Path $env:TEMP
New-Item -ItemType Directory -Path $env:DOTNET_CLI_HOME
dotnet test builder.tests/Helengine.Ps3.Builder.Tests.csproj `
  --filter 'FullyQualifiedName~Ps3MaterialNativeFixtureTests.ProducesNativeMaterialFixture' `
  --artifacts-path (Join-Path $producerRoot 'artifacts') `
  -p:HelengineRoot=$env:HELENGINE_ROOT
```

The PPU runner consumes that fixture and requires a fresh strict-child output directory. `-BuildOnly` compiles and packages the CPU test SELF without starting RPCS3:

```powershell
$outputRoot = Join-Path $nativeRoot 'ppu-runs'
New-Item -ItemType Directory -Path $outputRoot
& scripts/tests/ps3-material-reader.tests.ps1 `
  -FixtureRoot $fixtureRoot `
  -OutputRoot $outputRoot `
  -BuildOnly
```

To execute the PPU cases, pass `-RpcS3Path` only after the parent has coordinated the shared RPCS3 slot. The runner checks the pinned Docker image and lock, stages fixture files read-only by hash, writes a schema-one manifest with the current lock hash, and requires one terminal `PS3_MATERIAL_READER_TESTS passed=<n> failed=0` line with at least 25 matching named passes.

## Validation boundary

Runtime-path follow-up (2026-09-24): focused host suites passed80/0 after four
path-domain assertions were observed failing against the old producer. Fresh real
fixture producer-runtime-path-003 passed1/1. Material session18211 passed66/0 at
native-material/ppu-runs/runtime-path-material-001/emulator-runs/20260924-060446/TTY.log,
including rejects_legacy_cook_relative_shader_path. The fresh P3SP SHA256 remains
233615d7dc7cce7ccd3f2d57bfd1b67a969437c22be35e37033da1312a7f7cb3,
identical to producer-002. The pure runner path suite still passes15/0.

The native suite reads actual producer material and shader artifacts, validates their identity pair, and exercises malformed in-memory copies. It does not load texture content or draw a Core scene. Those remain later native material-loader and renderer work; this reader alone does not enable PS3 shader runtime support.

The runner's pure path contract test extracts only the `Assert-StrictChildPath` function AST; it does not execute the Docker/build body or create test files. Run it under the supported Windows PowerShell 5.1 with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/tests/ps3-material-reader-paths.tests.ps1
```

## Historical native evidence before runtime-path recook (2026-09-24)

- The production producer fixture test passed 1/1 under `fixtures/producer-002`. Its actual P3MT and standard P3SP outputs were consumed by the native test; no fixture bytes were authored by the native harness.
- Path-test RED against the old validator was 13/15: the root-relative `Path` and root-relative `Parent` cases were incorrectly accepted. After the lexical drive-absolute/complete-UNC guard was added, Windows PowerShell 5.1 reported `PS3_MATERIAL_READER_PATH_TESTS passed=15 failed=0`. The suite also verifies child acceptance, equality with both trailing separator forms, ancestor/sibling-prefix rejection, relative and drive-relative rejection, and complete-UNC acceptance.
- Locked-Docker BuildOnly session `82099` completed with exit 0 at `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material\ppu-runs\reviewfix-material-001`.
- Fresh build-and-runtime session `81372` completed with exit 0. Its run directory was `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-material\ppu-runs\reviewfix-material-002\emulator-runs\20260924-043500`; the TTY contains `PASS rejects_trailing_byte` and `PS3_MATERIAL_READER_TESTS passed=65 failed=0`. That malformed case now appends a byte, repairs the declared total length, and reaches the exact-end/trailing-byte rejection. The wrong-total-length case remains separate.
- The post-extraction shader regression had already completed independently at `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\native-reader\parent-acceptance\parent-shader-shared-final-01\emulator-runs\20260924-041726`, parent session `88158`, exit 0, `PS3_SHADER_READER_TESTS passed=43 failed=0`. The shader reader and its tests were unchanged by this review-fix round.

## Startup diagnostic limits

Adding the missing `SYS_PROCESS_PARAM` metadata produced the expected 32-byte process-parameter segment, but that experiment still stalled before `main`. A separate first-line marker build retaining iostream also produced no marker. In the one-variable harness diagnostic, removing `<iostream>`/`std::cout` and using `printf` with explicit flush allowed the marker to appear; that run reached 42 pass / 23 fail because of two test-harness defects (field offsets and diagnostic expectations), not a parser change. The harness fixes then passed all 65 cases, and the fresh post-review run above again passed 65/0.

This evidence localizes the observed pre-main stall to a test-harness change associated with iostream startup in this SDK/runtime setup. It does not identify the exact CRT/SDK initialization mechanism, and it is not evidence of a material-reader parsing defect. No rendering, texture-content load, or Core scene behavior is claimed.
