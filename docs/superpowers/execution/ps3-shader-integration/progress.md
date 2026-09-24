# PS3 authored shader integration progress

Historical working log. The dated updates below describe intermediate states and are retained for traceability. Current authoritative progress and validation are in [acceptance-checkpoint-20260924.md](acceptance-checkpoint-20260924.md) and [Ps3StandardSceneRuntime.md](../../../Ps3StandardSceneRuntime.md). As of standard05, the actual authored Unlit cube renders and retires normally; lighting acceptance and final shader-feature enablement remain pending. References below to active old sessions or absent implementations are historical, not instructions to restart them.

Active objective: continue until authored HLSL cooks and renders actual scene content; ask only when user feedback/authority is necessary.

## Verified committed base

- PS3 `96ca68d`: frontend, Docker SDK conversion, strict RSX reader, pair binding reconciliation.
- Shared `b806163d`: Ps3 target and real shader source-path handoff.
- Shared `526e967d`: IShaderBackendCachePolicy and ShaderCompileService.UseSharedCache bypass; the user approved incorporating both previously dirty files.
- PS3 `600a664`: declared uniform-buffer sizes and cache-bypass validation.
- PS3 `b8c032b`: bounded deterministic pair artifact identity/writer plus independent tests; parent focused8/8 passed including aggregate16MiB limit. No native loader yet.
- PS3 `1d7d6bd`: optional explicit external builder DLL registration and behavioral tests; parent registration/parent-final test passed. Scoped review accepted; SHA256 preservation check judged sufficient. Parent read ps3-paths.ps1 excluded by reviewer and confirmed Resolve-Ps3StateRoot only resolves paths, no writes.
- Prior foundation acceptance report records 125/125 pure and 7/7 Docker tests. These are historical results, not validation of uncommitted backend work.

## Current work

### Latest checkpoint (supersedes historical updates below)

- Material preliminaryRPCS3 run aborted by parent after8m45 withoutTTY, only verifiedownedPID124068 stopped to release logs. Session19118 terminalexit1; no65-case acceptance. Logs030539 prove missing process-param ELFsegment and Unexpectedsegmentsstate compared to successfulreader. Socrates owns single-variable SYS_PROCESS_PARAM test-harness correction/experiment; GPU Zeno nowhasRPCS3slot.
- Readerfix round1 pure root tests5/5 passed but worker violated noTEMPoutputs. Parent moved exact3ownedoutputs to native-reader/fixture-root-tests-20260924-01, preservedloghash, toldworker nofurtherTEMPwrites. Newrealfixture1/1 created at native-reader/fixture-review-round1-20260924-01; SDKmappingnegativePPU RED binary built but runtimeawaitingslot, validatorfixnotyetverified.
- Native reader review found SDK->mapping completeness missing and fixture-root strictchild/fullyqualified gaps; Raman owns fix round1 plus new pure fixture-root tests. Reviewer corrected its false claim that xUnit StartsWith arguments were reversed after parent verified local2.9.0 XML; historical producer1/1 and PPU42/42 remain genuine, but pre-fix. No reader commit yet.
- Socrates material-only report now exists: real standard shader/material producer1/1, P3SP37501bytes + P3MT275bytes; BuildOnly exit0,65 named PPU cases awaiting emulator. Parent requested a short slot from GPU owner Zeno before authorizing launch. Reader cpp extraction still gated until readercommit.
- Parent completed the next scene-integration plan `docs/superpowers/plans/2026-09-24-ps3-standard-scene-runtime.md`, NOT dispatched. It waits for reader/material/GPU reviews and commits; source-backed cache/stream/UV/uniform contracts, exact renderer handoff, native provider tests and actual standard-build/lighting pixel gates are specified. No shaders feature enablement or real scene acceptance claimed.
- Parent fresh standalone PPU acceptance exit0,42PASS/0FAIL at `native-reader/parent-acceptance/parent-reader-final-20260924-01/emulator-runs/20260924-024354/TTY.log`; session95762 terminal. Worker whole-native Cube compile reports exit0 and parent inspected actual manifest/signing log. Ohm01a0d14d-eb93-7860-a4f1-caae3156746e (Terra xhigh) reviews20-file reader snapshot; no reader commit until verdict/fixes. RPCS3 slot released to GPU worker Zeno; coordinate any later parent run.
- Native reader fixed3 PPU gate now42PASS/0FAIL, parent read the exact TTY at `native-reader/reader-acceptance-fixed3-20260924-01/emulator-runs/20260924-023758/TTY.log`. Prior sessions/results below are historical. Whole-native compile uses existing supported `-Mode Cube` (compile only); plan's invalid `Native` corrected without touching the dirty build script. Review/final parent gate still required.
- Parent recorded source-backed standard runtime handoff constraints in `standard-runtime-contract.md`: exact shader uniforms, actual TextureAsset type/format, UV layout and cache/drain lifecycle. This is evidence for the remaining integration plan, not completed runtime support.
- Fresh observed result for `reader-acceptance-fixed-20260924-01`: parent read TTY `emulator-runs/20260924-023135/TTY.log`, 38 PASS / 4 FAIL. All four failed the test helper's prefix-preservation assertion because it included deliberately updated SDK header count/offset fields. Raman has narrowed that assertion to preserve all original bytes except the intentional header updates; another fresh build/run is required. Session67315 below is historical/completed, not a live process to restart.
- Resumed checkpoint: native material reader is now dispatched to Socrates `01a0d13a-19ee-7301-bdec-a70243aba8a3`; new-file work may proceed, but edits to `Ps3ShaderArtifactReader.cpp` remain gated until Raman's task is committed and ownership explicitly released.
- Parent's first actual PPU reader run produced 41 PASS / 1 FAIL: a test-only relocation helper overwrote a sampler name at offset72. The validator correctly rejected it. The helper now preserves the original prefix/names and moves exact microcode after a new aligned constant table; negative relocation tests also assert their intended diagnostics. Five internal validator types now have dedicated headers. No post-fix success is claimed yet.
- Raman owns the RPCS3 slot for fresh acceptance, session67315, build `reader-acceptance-fixed-20260924-01`. Do not restart or overlap that run. Standard native compile, scoped review and parent acceptance remain required before commit/releasing the reader source.
- User repeated approval to incorporate the two shared cache-policy files; this is already satisfied by shared commit526e967d, not authorization to absorb other dirty shared files.

- Next native material-reader plan is written at `docs/superpowers/plans/2026-09-23-ps3-native-material-reader.md`, NOT dispatched. It waits for shader-reader commit/released ownership because it extracts shared BE/UTF-8 primitives from that reader into a header-only utility. Real standard material/shader producer fixture and PPU identity-match tests are required before Core attachment. Canonical fixed standard hash independently computed as76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.
- Native reader attempt2 has emitted ELF/SELF/fakeSELF and its compile log reaches completed signing; this is build evidence only. Parent has not observed an RPCS3 test result yet. Do not claim native tests pass from executable existence.

- Material producer committed `8180b3d` after path-defaulting fix, parent55/55 fresh gate and scoped re-review acceptance. Earlier complete pure suite240/240 precedes the small fix; no claim of post-fix fullpure run. P3MT remains producer-only until native consumption; feature disabled.
- Reusable authored GPU path plan dispatched to Zeno01a0d129-3a53-7993-8c36-9ccc8d6eb83e, Luna xhigh, new files only. It must prove asymmetric transform, four-color texture sampling and consecutive vertex/fragment uniform updates through visible RPCS3 pixel probes, not draw counts. Actual cube_test remains a separate required final gate.
- Native reader worker checkpoint confirms parser/validator/tests implemented, no active owned process at checkpoint, Makefile/runner/compile+RPCS3 acceptance next. No native acceptance claimed yet. Exact producer scalar fixture label FP3232 verified; four incorrect FP32 fixture expectations routed to worker for correction.

- Cooker/lazy registry committed `c3e9cd5` after parent fresh 32/32 (no skips, Docker included) and James scoped path-fix re-review accepted the current source. Evidence `relocation/results/parent-cooker-path-final.trx`; named nine-file commit only. Review snapshot was stale; reviewer explicitly inspected current source, so use commit c3e9cd5 as authoritative review target.
- Material-envelope single-task plan written and dispatched to Kepler `01a0d10b-3703-7223-b51e-2a8e2c8a6895`, Luna xhigh, disjoint producer/tests scope. Exact P3MT v1 wire table, fixed standard identity and texture-path validation; feature stays disabled.

- Shared cache-policy incorporation approved by Helena is already committed as shared `526e967d`.
- PS3 backend committed `71e9ccd`; repeated same-element relocation validation committed `9360514`; SDK parser-capacity fix and locked image committed `6313a94`.
- Parent fresh backend/map gate passed 57/57 with no skips, including real Standard Shader and textured pair Docker tests: `relocation/results/parent-backend-map-final.trx` under the standard integration build root.
- Parent final SDK regression passed with unchanged original input hash and 1354 emitted instructions: `cgcomp-capacity/parent-reviewed-final/run-20260923-221406-225e1bd788bf4203859d5d2ad9be5bb4`.
- Cooker worker reports 25/25 focused pure, 193/193 full non-Docker, and 1/1 Docker. Scoped reviewer James found one important issue: relative cook/source paths implicitly use CWD. Parent verified it and returned the task to Feynman for explicit fully-qualified-path prevalidation, regression tests and normalized publication roots. No cooker commit until fix review and fresh parent validation.
- Native reader plan is assigned to Raman `01a0d0f6-2f82-76d3-95e2-ac4cafa0ad8c`; fixture implementation has started. No native-reader acceptance yet.
- Backend, SDK, artifact and registration workers/reviewers are closed. Cooker reviewer closed pending fix round. Parent continues planning material/runtime integration; authored shader rendering remains incomplete and the feature remains disabled.

- Backend plan: `docs/superpowers/plans/2026-09-23-ps3-shader-backend.md`.
- Its live ledger/report are in `.superpowers/sdd/2026-09-23-ps3-shader-backend/`.
- Parent plans; Luna implements. No worktrees and no broad unrelated commits.
- Read-only native seams exploration by Halley completed; report `native-seams.md`, agent closed. Existing runtime lacks UV and sampler/texture upload; native source loading and pixel probes are available.
- Backend implementer: Laplace 01a0d0a4-d19b-70f0-8449-e917a4fcfecd.
- Artifact serializer implementer: Galileo 01a0d0b4-3075-7152-8d3a-bf6c81bca1fd, Luna high; independent new files and separate test-output root. Its plan/ledger: 2026-09-23-ps3-shader-artifact.
- Next contract draft: `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md` (not yet a worker brief).
- Locked Docker SDK headers inspected directly; evidence and remaining API questions are in `sdk-runtime-contract.md`.
- Codebase-memory retry returned Transport closed; freshness/coverage unknown. Exact-source fallback only.
- Artifact writer committed b8c032b after scoped review and parent focused8/8 including aggregate16MiB limit. Historical pure suite161/161 predates that extra test. Galileo closed. No cooker/native integration claimed.
- Original fragment139 is fixed by parser capacity patch in prepared image63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f. Parent same-input regression exit0, 1354 instructions, input SHA unchanged60771d1e29f134e01878c4f9c7024668739564623135c577676d3ab39af198f0. Worker newline-boundary1025 passed; task report under cgcomp-capacity. Bohr scoped SDK review active; no commit yet.
- Parent actual StandardShader rerun compiles both stages but map fails at duplicate relocation3072. Raw tables prove every duplicate is within one constant element; pinned compiler appends per operand. New plan2026-09-23-ps3-fragment-relocation-identity assigned to resumed Laplace, limited to bindingmapbuilder/maptests, preserve true cross-element collisions and untouched SDK data. Parent TRX backend/results/parent-standard-capacity.trx.
- Scoped backend/artifact reviews found no blocking code defects; minor coverage added (DimCube mapping and aggregate16MiB limit). Artifact original RED blocked by concurrent compilation is recorded as a historical process gap, not misreported as serializer RED. Backend remains uncommitted pending real acceptance.
- Cooker/lazy registration plan 2026-09-23-ps3-shader-cooker dispatched to Feynman 01a0d0d3-fc75-7370-b854-1d984c730a99, Luna xhigh; exact source provenance and only requested variants, no feature enablement or material/native edits yet.
- External builder registration plan2026-09-23-ps3-external-builder-registration implemented by Bernoulli01a0d0dd-5b8e-7fd2-8c13-0ef1f0a90510; parent behavioral test passed under registration/parent-final. Ampere01a0d0e2-99cb-7b03-904d-66e4656dc4ea scopedreview active, no commit yet. Only2scripts; cooker worker owns cooking docs.
- Update: registration committed1d7d6bd; Bernoulli and Ampere closed. Relocation fix code+five tests completed and Hegel scoped review accepted without findings (reviewer closed). Genuine RED20pass1fail; GREEN blocked by current cooker tests CS0030 before builder interfaces implemented. Parent must rerun focused map tests, full pure suite and actual StandardShader after cooker compile is ready; do not claim map fix verified yet. Laplace report .superpowers/sdd/2026-09-23-ps3-shader-backend/relocation-report.md.

## Required next integration work

1. Validate shared backend, including actual ForwardStandardShader source compile; preserve any real unsupported-feature diagnostic.
2. Specify versioned PS3-owned cooked pair format with source/program/variant identity, untouched RSX binaries, authored buffer layouts and physical bindings. Reader must bound lengths/counts, validate offsets, stage and layout before allocation/upload.
3. Add cooker capability/registration without discovery-time SDK work; use existing HELENGINE_PS3_REPOSITORY_ROOT configuration and explicit build working roots. Preserve source.SourcePath; use shared include resolver. Never manufacture a fake shader filename or substitute canned shaders.
4. Material payload must name the selected pair and variants; standard variants derive from StandardShaderVariants.All. Collision-safe paths must account for shader asset, program pair and variant, not only variant name.
5. Native program ownership/loading, physical attribute/uniform/sampler mapping, texture fixture, then real cube_test acceptance. Keep shader feature disabled until complete.

## Constraints and unresolved facts

- Unrelated dirty PS3 files: Makefile, scripts/build.ps1, scripts/tests/build-contract.tests.ps1. Preserve.
- Shared shader target/descriptor files are dirty again after the previous commit; do not absorb them into this work without understanding ownership.
- Standard shader has 8 cbuffers, normal/world transforms, PBR-style light code and multiple texture samplers; compilation and runtime uniform provisioning must be tested, not presumed from tiny probes.
- SDK conversion and native build only locked Docker; all outputs below C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923. TEMP/TMP redirected only to owned fixture-storage when running .NET tests.
- No screenshots allowed. Emulator launch was previously authorized; runtime observation must use allowed probes/logs or explicit user visual feedback.
- No independent whole-branch review without asking. No push.

## Standard build input recovered from authoritative local state

`.ps3/builds/platform-lighting-20260922-04/.helengine-build-state.json` records a successful standard build of `C:/dev/helprojs/demodisc/project.heproj`, platform ps3, profile release, configuration Release. The project file still exists. Do not edit that authored project.

Prior native manifest: `.ps3/builds/platform-lighting-20260922-04/core-af607f6231a44680befff25a0824111b/build-manifest.json`. Generated Core root was `.ps3/platform-cache/v2/d87d222cd166d9049967e97c50c8e301/b/ps3/release/release/generated-core`; staged content was the adjacent `native/ps3-authored-content`. These are historical outputs, not source files to patch or a substitute for a new standard build.

Use sibling `helengine/scripts/build-platform.ps1` with explicit -Project, -Platform ps3, -Output, -Configuration Release, -BuildProfile release and -CacheRoot under the current standard visible build root; -WorkspaceRoot is deprecated, so omit it. Output and cache roots must not overlap. Repo-local registration uses HELENGINE_ENGINE_USER_SETTINGS_ROOT. The current register-platform.ps1 hardcodes builder/bin/Release; integration needs a scoped optional explicit assembly-path parameter (or an equivalent isolated registration producer) before it can use build-root-owned .NET output without editing shared/global registry state.
