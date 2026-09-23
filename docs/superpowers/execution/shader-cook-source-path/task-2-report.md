# Task 2 report: editor shader source path handoff

## Changes

- `../helengine/engine/helengine.editor/managers/project/EditorPlatformAssetCookService.cs`: pass the resolver's existing `SourcePath` into the four-argument `PlatformShaderArtifactCookSource` constructor.
- `../helengine/engine/helengine.editor.tests/managers/project/EditorPlatformAssetCookServiceTests.cs`: assert built-in source path/text/hash, and add authored override plus nested relative include coverage through the actual recorded service request.

No Task 1 DTO/dependency files or other editor/platform files were changed.

## TDD and validation

For each command below, `TEMP` and `TMP` were temporarily set to `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/fixture-storage` and restored in `finally`. Managed outputs and logs stayed under that build root.

RED, before the production edit:

```powershell
dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~Cook_whenPlatformBuilderSupportsShaderArtifacts_resolvesSourcesAndCollectsDeclaredBundle|FullyQualifiedName~Cook_whenPlatformBuilderSupportsShaderArtifacts_prefersAuthoredSourceAndPreservesNestedIncludeContext' --verbosity quiet -p:CopyLocalLockFileAssemblies=true --logger 'console;verbosity=detailed'
```

Result: 2 failed, 0 passed. Both failed at `Assert.True(source.HasSourcePath)` (built-in test line 533; authored override test line 587), as expected. Log: `source-path/task-2-red-detail.log`.

GREEN, after the production edit:

```powershell
dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~Cook_whenPlatformBuilderSupportsShaderArtifacts_resolvesSourcesAndCollectsDeclaredBundle|FullyQualifiedName~Cook_whenPlatformBuilderSupportsShaderArtifacts_prefersAuthoredSourceAndPreservesNestedIncludeContext' --verbosity quiet -p:CopyLocalLockFileAssemblies=true --logger 'console;verbosity=normal'
```

Result: 2 passed, 0 failed. Log: `source-path/task-2-green-editor-handoff.log`.

Final required service-class run:

```powershell
dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~EditorPlatformAssetCookServiceTests' --verbosity quiet -p:CopyLocalLockFileAssemblies=true --logger 'console;verbosity=normal'
```

Result: 18 total; 16 passed and 2 failed. The failures are the pre-existing `Cook_when_builder_definition_publishes_standard_material_schema_cooks_material_with_base_color_buffer` and `Cook_when_using_committed_point_shadow_scene_with_windows_builder_metadata_succeeds`; both throw `No installed platform descriptor is registered for 'windows'` from `EditorProjectBootstrapContext.ResolvePlatformDescriptor`. This matches the pre-Task 2 15/17 baseline and does not involve the shader handoff. Log: `source-path/task-2-final-editor-class.log`.

The first focused attempt aborted before tests because the editor artifact directory lacked `Microsoft.TestPlatform.CoreUtilities.dll`; that run is preserved in `source-path/task-2-red-editor-handoff.log`. Adding only `-p:CopyLocalLockFileAssemblies=true` for these invocations supplied the test host dependencies under the prescribed artifact root; no shared tooling or dependency files were edited.

## Consumer constraint

Consumers that need filesystem include resolution must reject `HasSourcePath == false` rather than infer a filename. The legacy source-only DTO constructor remains valid for consumers that do not need source-path context.

## Process-local TEMP/TMP wrapper used for GREEN and class-suite runs

This is the PowerShell wrapper from the final combined command, including saved values, `finally` restoration, and exit-code handling:

```powershell
$oldTemp=$env:TEMP; $oldTmp=$env:TMP; $greenExit=1; $suiteExit=1; try { $env:TEMP='C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\fixture-storage'; $env:TMP=$env:TEMP; dotnet test ..\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --artifacts-path C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\dotnet --filter 'FullyQualifiedName~Cook_whenPlatformBuilderSupportsShaderArtifacts_resolvesSourcesAndCollectsDeclaredBundle|FullyQualifiedName~Cook_whenPlatformBuilderSupportsShaderArtifacts_prefersAuthoredSourceAndPreservesNestedIncludeContext' --verbosity quiet -p:CopyLocalLockFileAssemblies=true --logger 'console;verbosity=normal' *> C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\task-2-green-editor-handoff.log; $greenExit=$LASTEXITCODE; dotnet test ..\helengine\engine\helengine.editor.tests\helengine.editor.tests.csproj --artifacts-path C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\dotnet --filter 'FullyQualifiedName~EditorPlatformAssetCookServiceTests' --verbosity quiet -p:CopyLocalLockFileAssemblies=true --logger 'console;verbosity=normal' *> C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\task-2-final-editor-class.log; $suiteExit=$LASTEXITCODE } finally { $env:TEMP=$oldTemp; $env:TMP=$oldTmp }; '--- focused handoff ---'; Get-Content C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\task-2-green-editor-handoff.log | Select-String 'Passed!|Failed!|Total tests:|Failed:|Passed:|Error Message:' | Select-Object -Last 12; '--- final class suite ---'; Get-Content C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\source-path\task-2-final-editor-class.log | Select-String 'Passed!|Failed!|Total tests:|Failed:|Passed:|No installed platform descriptor|Error Message:' | Select-Object -Last 22; "Focused exit: $greenExit; class exit: $suiteExit"; if($greenExit -ne 0){ exit $greenExit }; exit $suiteExit
```
