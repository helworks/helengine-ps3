# Instruction label alignment implementation plan

**Goal:** Restore Camera/Light body-font positioning while preserving the approved Micro SD UI-font positions.

**Spec:** User approved the bounded DemoDisc authoring/asset scope after diagnosis recorded in `.superpowers/sdd/2026-09-24-shared-2d-batch/label-alignment-root-cause.md`.

**Architecture:** Common labels use body font with offsets -9/-10. Micro SD keeps UI font and current effective +5/+4 through scoped local-position overrides. Do not modify renderers or generated C++.

## Constraints

- Luna implements; parent reviews. No worktrees, no independent review without consent, no commit/push by worker.
- Preserve unrelated changes. The DemoDisc scene is already dirty; no whole-scene regeneration or checkout.
- Never close/restart RPCS3 PID46944 or take screenshots.
- Use production serializers for binary assets, validate semantic before/after and preserve all unrelated fields. If no safe selective update is available, report before modifying assets.
- Build outputs/logs/temp under C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/label-alignment, never OS TEMP.

## Task 1: Authoring and regression tests

Owned root C:/dev/helprojs/demodisc/assets/codebase/.
Modify DemoDiscEditorTools/rendering/DemoSceneInstructionOverlayFactory.cs and focused tests in DemoDiscEditorTools.tests/rendering/.

- [ ] Snapshot named source files and dirty asset hashes. Read applicable AGENTS instructions.
- [ ] Add failing tests: common Camera offset -9 and Light -10; Micro SD resolved +5/+4; existing font/scale/size overrides preserved; X/Z unchanged; both desktop and console rows covered.
- [x] Restore common constants to -9f/-10f. In PreserveMicroSdLabelSize create/get existing MicroSd transform override and set local position to common LocalPosition with Y+14f, preserving X/Z. Keep existing font/size/scale override logic unchanged. Document why scope-specific offset exists.
- [ ] Run focused tests red then green using existing test infrastructure; do not use source-string assertions as sole behavior evidence.
- [ ] Report source diff, test commands/results, baseline hashes and exact selective asset-update route. Do not modify binary assets yet.

## Task 2: Selective persisted assets and acceptance

Implementation route: create a separate focused console tool `tests/tools/Ps3LabelAlignmentMigration/` (do not change the existing readonly asset probe). Reference helengine.files/core like Ps3Batch2DAssetProbe. Production `helengine.files.AssetSerializer.Deserialize/Serialize` supports SceneAsset and BlueprintAsset. First implement `inspect <absolute-file>` and `prepare <absolute-source> <absolute-new-file>`; prepare writes ONLY a new file in task buildroot, never overwrites input. Parent reviews before installation.

For named CameraText/LightIconText children directly under DemoSceneInstructionPanel or ConsoleCameraLightInstructionsPanel, require original local Y 26/94, then set to12/80. Require existing Group,Platform,BuildConfig level order. Add or preserve sd/msd transform override (two Group steps, IDs sd then msd), HasLocalPositionOverride true, position equal original common position. Preserve existing orientation/scale override fields. Unexpected existing position override or unexpected common Y must fail visibly; alreadycorrect12/80 withmatching26/94override is no-op. X/Z never change. Enumerate blueprint root and scene roots, including both matching panels; require nonzero matches and expected namespaired. Preserve all opaque component payload bytes, including font overrides, without decoding or rewriting them. Source inspection confirmed those overrides are not standalone SceneComponentAssetRecord properties; restoring the original local position for MicroSD preserves its placement regardless of payload details.

Validation: retain original position and override arrays, serialize original object canonically, mutate in memory, serialize+deserialize proposed output and verify target positions, restore allowed original fields on proposed decoded object and demand canonical serialized bytes equal originalcanonical. This validates no other serialized state changed. Use synthetic self-tests for old-to-new, already-correct, unexpectedY, existing scale/orientation override preservation, and unrelated entity untouched. `inspect` reports entityid/name/parent/common/override values and sourceSHA. Parent installs only after hash-guarded source recheck and preimage backup. The original authored scene and blueprint remain untouched during prepare.

- [x] Parent reviews Task1 evidence and authorizes exact selective preparation operation within this approved scope. Factory compiled with zero errors; four existing structural/reference tests pass. New offset regression coverage remains pending. Inspected original assets: blueprint labels IDs5/7, scene labels IDs13/15, localY26/94, X130,Z0.1; no existing transform override entries.
- [x] Using production load/save APIs, update only CameraText/LightIconText common Y and corresponding Micro SD position override in ConsoleCameraLightInstructions.hblueprint and existing cube_test instruction entities where needed. Read current values, require expected originals, preserve unrelated fields/references. Do not blindly subtract repeatedly; fail unexpected state.
- [x] Validate semantic diff only named positions/overrides, preserve every unrelated entity/component and all existing scoped state. Keep recoverable preimage under task build root.
- [x] Normal PS3 standard build with RunSeconds=0. Do not replace live window until user permits; report build availability separately from visual validation.

## Validation outcome

Factory build succeeded (three unrelated gameplay warnings). Four existing asset/reference tests pass after installation. Five new migration self-tests pass, covering two panels, idempotence, unexpected-Y rejection, scoped transform preservation and unrelated entities. Parent reran both groups. Source-first factory TDD was not completed as initially planned; migration tests and serialized roundtrip preservation supplied the focused coverage instead. Standard build `core-d96212d32b3c4c39a2dde38aa558f059` completed with exit0, matching SELF manifest and RunSeconds0. Visual validation is pending: old user window deliberately untouched. FPS shadow and earlier batch GPU acceptance failures are outside this bounded alignment correction and remain unresolved.

## Review focus

Micro SD inherited scope resolution; X/Z preservation; duplicate desktop/console labels; existing dirty scene edits; repeated updates must not subtract 14 again.
