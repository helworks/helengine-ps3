# Canonical shader paths through standard packaging

Parent plan; Luna implements in the existing checkout. No worktrees or shared-engine edits.

## Evidence and decision

Standard03 compiled successfully, then RPCS3 run055324 loaded cube_test but failed at material realization: P3MT requested shaders/76a43...ps3sp, while the standard package contains cooked/shaders/76a43...ps3sp. EditorPlatformAssetCookService.AddDeclaredCookedArtifacts expects runtime-relative declarations and strips cooked/ for physical cook-root lookup. Its scanner prefixes cooked/. EditorPlatformBuildGraphRunner.NormalizeStagedManifestRelativePath also prefixes cooked/ for staged cook-root files, and ResolveStagedCookedArtifacts filters declarations against those runtime paths. Thus the producer currently confuses cook-root-relative output paths with runtime-relative references/declarations.

Fix that producer contract, not the package, generated files, runtime search fallbacks or shared editor. Preserve P3SP identity hash, SDK bytes and physical location under CookRootPath/shaders. P3MT and declaration RelativePath must explicitly name cooked/shaders. Declaration LogicalArtifactId remains the existing stable identity path. No compatibility fallback: all material envelopes are recooked.

## Ownership

- builder/shaders/Ps3ShaderArtifactIdentity.cs: keep RelativePath cook-root-relative; add documented RuntimeRelativePath = cooked/ + RelativePath.
- builder/shaders/Ps3ShaderArtifactCooker.cs: write physical output using RelativePath; declare RuntimeRelativePath, preserving LogicalArtifactId.
- builder/materials/Ps3MaterialArtifactWriter.cs: serialize RuntimeRelativePath.
- src/platform/ps3/Ps3MaterialArtifactReader.cpp: fixed canonical material shader path becomes cooked/shaders/...; no cache path rewriting.
- Focused tests: builder.tests/Ps3ShaderArtifactCookerTests.cs, Ps3MaterialArtifactWriterTests.cs, Ps3MaterialCookerTests.cs, Ps3MaterialNativeFixtureTests.cs; tests/native/Ps3MaterialArtifactReaderTests.cpp; scripts/tests/ps3-material-reader.tests.ps1.
- docs/Ps3MaterialArtifactFormat.md, docs/Ps3NativeMaterialReader.md, docs/Ps3StandardSceneRuntime.md and task report.

Do not touch protected dirty Makefile/build.ps1/build-contract tests, the unchanged P3SP writer/reader/provider, or authored DemoDisc content. If another test has an exact stale assertion, report its path before expanding ownership.

## Implementation and evidence

1. Add focused failing assertions distinguishing cook-root-relative identity, runtime-relative declaration and P3MT reference. Prove cooker writes only CookRootPath/shaders/... while declaration RelativePath is cooked/shaders/... and LogicalArtifactId is unchanged. Use existing fake backend tests, not Docker, for this unit boundary.
2. Implement the small production changes. Update existing exact assertions without weakening them. Add a native malformed material case rejecting the old unprefixed path with a precise diagnostic, retaining all existing cases.
3. Fresh producer fixture under native-material/fixtures/producer-runtime-path-003. Its shader cook root should be fixtureRoot/cooked so the real runtime path exists beneath fixtureRoot. Material stays fixtureRoot/material.p3mt. Keep all earlier fixtures immutable. Runner stages the declared cooked/shaders fixture path beneath USRDIR/content; no binary editing. Produce shader through the existing real cooker/pinned Docker.
4. Run bounded host tests then fresh native material suite (expected66 including legacy-path rejection). Existing shader45/provider26 remain valid if their source and P3SP bytes are unchanged; compare P3SP hash with producer-002 to prove no shader-byte change. Serialize emulator use: parent run03 is terminal and this task owns the next slot. No screenshots.
5. Freeze sources/report exact files, tests, hashes and paths. Parent reviews, rebuilds/registers builder b3, performs standard04 and launches its immutable SELF. No commit by worker.

Use task-owned build root shader-integration-20260923, short .NET artifact paths and TEMP/TMP restored in finally. No system TEMP outputs. Apply_patch edits, substantive docs, no local C# functions/tuples/nullable references, no nested agents. Runtime scene acceptance remains incomplete until the normal build/run proves the reference matches the staged file and authored draws succeed.
