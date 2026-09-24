# Standard scene bounded review fixes

Parent-authored follow-up to standard-scene Task 1. Luna implements in the main checkout, without worktrees.

## Scope

Accepted review findings: incomplete reverse SDK attribute/sampler coverage and secondary generated cleanup exceptions. Preserve the terminal fail-fast draw contract; do not add AbortDraw/retry behavior. Parent is obtaining reviewer disposition of that separate finding.

Ownership extends the existing scene task only to `src/platform/ps3/Ps3RsxProgramValidator.cpp` and `tests/native/Ps3ShaderArtifactReaderTests.cpp`. Cleanup edits remain in `Ps3AuthoredContentCache.cpp` and `Ps3RenderManager3D.cpp`. Update the scene documentation and task report. No protected Makefile/build-script/provider/shared changes.

## Steps and acceptance

1. Add two malformed P3SP tests that remove one serialized vertex-input mapping or pixel-sampler mapping while preserving raw SDK bytes and repairing container lengths. Mutate in-memory copies only. Confirm the current validator incorrectly accepts those artifacts before fixing it.
2. Require reverse coverage of every raw SDK vertex input and pixel sampler, using the parsed SDK classification and existing uniqueness checks. Do not assume a fixed standard-shader input count; preserve supported valid fixtures and explicit diagnostics.
3. Preserve primary read/validation exceptions during failure cleanup. Mirror DisposeFailedStream: catch generated `::Exception*` secondary errors, log context, delete them; catch ordinary secondary exceptions without replacing the pending primary error. Cover texture disposal, model asset disposal and model stream disposal failure handlers. Successful-path disposal errors must still propagate.
4. Run the reader harness (expected 45 cases after the two additions) and the existing standard-uniform harness (26 cases/41 identities) using the unchanged immutable fixtures and pinned Docker. The serialized RPCS3 slot is assigned to this task; no other build/emulator is active. No screenshots. All outputs and TEMP/TMP belong under the existing shader-integration build root, with restoration in finally.
5. Report exact commands, source/fixture hashes, logs and results. Do not claim cleanup fault-injection coverage unless actually exercised. Explain the pointer ownership and primary-exception preservation by exact source checks. Parent owns full standard-build integration, rereview and commits.

Fixtures: reader `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/native-reader/parent-fixture`; standard shader `native-material/fixtures/producer-002/shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp` under that same build root. Reuse the existing runners; do not rewrite generated outputs.

## Follow-up after standard03 terminates

Rereview found one remaining manager failure path: after successful Deserialize, stream Dispose can throw while the completed ModelAsset is still owned by the local Asset pointer. Deletion of that pointer does not release NativeOwnedMember geometry. In ReadCookedAsset's catch, failure-dispose the already-deserialized ModelAsset with the existing nonthrowing helper before bare rethrow. Preserve the stream-disposal error as primary and leave success disposal behavior unchanged. Only manager.cpp and report/doc edits are needed. Do not edit while standard03 session76965 is compiling the frozen sources; parent will explicitly release the hold after its terminal result. Verify source ownership and use the subsequent standard native build for generated-API compilation. Existing reader/provider gates remain valid because their source closure is unchanged.
