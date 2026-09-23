# Task 1 report: shader source path on cook DTO

## Changed paths

- `C:/dev/helworks/helengine/engine/helengine.baseplatform/Requests/PlatformShaderArtifactCookSource.cs`
- `C:/dev/helworks/helengine/engine/helengine.baseplatform.tests/Requests/PlatformShaderArtifactCookSourceTests.cs`
- `C:/dev/helworks/helengine-ps3/docs/superpowers/execution/shader-cook-source-path/task-1-report.md`

No Task 2 files were changed. Existing unrelated edits in both checkouts were preserved.

## Validation

All test processes set `TEMP` and `TMP` to `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/fixture-storage` and restore their prior process values in `finally`. Managed artifacts and complete command logs are under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path`.

### RED

Command, run from `C:/dev/helworks/helengine-ps3`:

```powershell
dotnet test ../helengine/engine/helengine.baseplatform.tests/helengine.baseplatform.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~PlatformShaderArtifactCookSourceTests' --verbosity quiet
```

Result: exit code 1 during compilation, with the expected missing-contract errors: the 4-argument constructor and `SourcePath`/`HasSourcePath` were absent. The errors referenced the new test file and these missing members, not a namespace or import issue. Complete output: `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/task-1-red-baseplatform.log`.

### GREEN: DTO and dependency contracts

Command, run from `C:/dev/helworks/helengine-ps3`:

```powershell
dotnet test ../helengine/engine/helengine.baseplatform.tests/helengine.baseplatform.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~PlatformShaderArtifactCookSourceTests|FullyQualifiedName~PlatformShaderDependencyTests' --verbosity quiet
```

Result: exit code 0; 12 passed, 0 failed, 0 skipped. Complete output: `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/task-1-green-baseplatform.log`.

### Additional editor environment evidence (outside Task 1 acceptance filter)

Command, run from `C:/dev/helworks/helengine-ps3`:

```powershell
dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter FullyQualifiedName~EditorPlatformAssetCookServiceTests --verbosity quiet
```

Result: project built, but tests exited 1; 15 passed and 2 failed. Both failures are blocked by the current platform installation state: `EditorProjectBootstrapContext.ResolvePlatformDescriptor("windows")` throws `InvalidOperationException: No installed platform descriptor is registered for 'windows'.` The failures are `Cook_when_using_committed_point_shadow_scene_with_windows_builder_metadata_succeeds` at test line 421 and `Cook_when_builder_definition_publishes_standard_material_schema_cooks_material_with_base_color_buffer` at line 456. This is additional environment baseline evidence; neither failure is a DTO assertion. Full output: `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/task-1-green-editor.log`.

The two failing cases were rerun with detailed console logging and `--no-build` to capture their exceptions and stack traces:

```powershell
dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~Cook_when_builder_definition_publishes_standard_material_schema_cooks_material_with_base_color_buffer|FullyQualifiedName~Cook_when_using_committed_point_shadow_scene_with_windows_builder_metadata_succeeds' --logger 'console;verbosity=detailed' --no-build
```

Result: 0 passed, 2 failed, same missing Windows descriptor error. Output: `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/task-1-editor-failure-details.log`.

Build output included existing IDE0040 and xUnit analyzer warnings in other source and test files. Neither changed source nor changed test file appears as a warning location in the captured logs.

## Self-review and concerns

- The old three-argument constructor and its validations are unchanged. The new overload chains through it, rejects missing or non-fully-qualified paths, then stores `Path.GetFullPath(sourcePath)`.
- The path is host-side provenance only. The DTO does not check disk existence, read source text, recalculate hashes, scan includes, or derive an asset ID from a path.
- The legacy constructor leaves the documented `SourcePathValue` null; `HasSourcePath` reports that absence explicitly.
- Tests cover supplied snapshot values, legacy absence, an absolute non-existent metadata path with spaces and non-ASCII text, invalid path forms, Windows-only drive-relative/rooted paths, and existing invalid asset/hash/text behavior on both constructors.
- Task 1 implementation was completed by the requested `gpt-6-luna` model at high reasoning effort, as confirmed by the controller. No nested agent was used.
- Codebase-memory search returned the DTO symbol in the `helengine` index. The catalogued skill file itself was inaccessible, so structural exploration used the brief-authorized exact-source fallback.
