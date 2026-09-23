## Global Constraints

- Parent writes plans and resolves design questions; Luna writes implementation/tests. Use the available Luna model with explicit model and reasoning effort, no nested agents.
- Work stays in the existing checkouts, without worktrees. No commits, pushes or unrelated cleanup in this plan.
- Approved shared edits are limited to the source DTO, the exact editor handoff, and their focused tests. Other platform builders remain unchanged.
- Preserve unrelated dirty files in both checkouts. The three existing source/test files listed below were clean during planning; recheck before editing.
- All build outputs, fixtures and logs stay under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path`.
- Existing editor test fixtures use `Path.GetTempPath()`. For these test processes only, redirect TEMP/TMP to the explicitly created `source-path/fixture-storage` child, preserving/restoring prior environment values. Never direct fixtures to AppData/Local/Temp. Do not refactor unrelated fixture infrastructure.
- One type per file; substantive XML docs for new members; PascalCase fields; no tuples/local functions or nullable reference annotations.
- Do not manufacture a source path from asset id, current directory, cooked destination or a default shader. A new path-bearing constructor requires a fully qualified path.
- Do not reread source text in the DTO or alter SourceHash. Text/hash are the supplied snapshot; path is host-side provenance/include context, not cooked runtime data or a dependency-complete cache key.
- Native compilation and final RSX conversion remain in the repository's pinned Docker SDK. This path-only plan does not need native compilation or another full Docker suite.
- No generated-output rewrites, screenshots, runtime changes, shader feature enablement, backend registration or cache redesign.


## Task 1: Extend the shared DTO without breaking source-only callers

**Files:**
- Modify: `../helengine/engine/helengine.baseplatform/Requests/PlatformShaderArtifactCookSource.cs`
- Create: `../helengine/engine/helengine.baseplatform.tests/Requests/PlatformShaderArtifactCookSourceTests.cs`

**Interfaces:**

```csharp
// Retained unchanged for existing source-only callers.
public PlatformShaderArtifactCookSource(string shaderAssetId, string sourceHash, string sourceText);

// New overload: append the path so the original three arguments keep their meaning.
public PlatformShaderArtifactCookSource(string shaderAssetId, string sourceHash, string sourceText, string sourcePath);

// Null only for the legacy constructor; explicit absence, not a valid path.
public string SourcePath { get; }
public bool HasSourcePath { get; }
```

Keep existing immutable field/property style. Nullable reference annotations remain disabled; XML docs explain the legacy null state. Do not mark the old constructor obsolete or add warnings to other builders.

- [ ] **Step 1: Write the contract tests before adding the overload.** Assert the original fields, canonical absolute path, explicit legacy absence, unchanged source text/hash, rejection of blank/relative paths, and no disk existence requirement. Use an absolute metadata-only fixture path constructed with `Path.GetFullPath(Path.Combine("shader-source-tests", "shaders with spaces", "aÃ§Ã£o.hlsl"))`; do not create that file.

```csharp
PlatformShaderArtifactCookSource legacy = new("asset", "supplied-hash", "source snapshot");
Assert.False(legacy.HasSourcePath);
Assert.Null(legacy.SourcePath);

string path = Path.GetFullPath(Path.Combine("shader-source-tests", "shaders with spaces", "aÃ§Ã£o.hlsl"));
PlatformShaderArtifactCookSource source = new("asset", "supplied-hash", "source snapshot", path);
Assert.True(source.HasSourcePath);
Assert.Equal(path, source.SourcePath);
Assert.Equal("supplied-hash", source.SourceHash);
Assert.Equal("source snapshot", source.SourceText);
Assert.Throws<ArgumentException>(() => new PlatformShaderArtifactCookSource("asset", "hash", "text", "relative/main.hlsl"));
```

Add null/empty/whitespace sourcePath cases. On Windows also reject `C:main.hlsl` and `\main.hlsl`; test these only when `OperatingSystem.IsWindows()`. Retain existing invalid asset/hash/text behavior for both overloads. Paths containing `..` may normalize if already fully qualified; do not impose a new assets-root restriction on built-in shaders.

- [ ] **Step 2: Run the new DTO tests RED.** Use the command in Validation with filter `FullyQualifiedName~PlatformShaderArtifactCookSourceTests`. Missing API is the intended initial failure, not a wrong namespace/import.

- [ ] **Step 3: Implement the additive contract.** Add documented readonly `SourcePathValue`, the two properties, and this validation pattern in the new overload; the existing constructor retains its validation and initializes no fake path.

```csharp
public PlatformShaderArtifactCookSource(string shaderAssetId, string sourceHash, string sourceText, string sourcePath)
    : this(shaderAssetId, sourceHash, sourceText) {
    if (string.IsNullOrWhiteSpace(sourcePath)) {
        throw new ArgumentException("Shader source path is required.", nameof(sourcePath));
    } else if (!Path.IsPathFullyQualified(sourcePath)) {
        throw new ArgumentException("Shader source path must be fully qualified.", nameof(sourcePath));
    }
    SourcePathValue = Path.GetFullPath(sourcePath);
}
```

`SourcePath` returns `SourcePathValue`; `HasSourcePath` returns `SourcePathValue != null`. Do not add File.Exists, source loading, include scanning, hash recomputation or path-to-id conversion.

- [ ] **Step 4: Run GREEN plus existing dependency contract tests.** Filter `FullyQualifiedName~PlatformShaderArtifactCookSourceTests|FullyQualifiedName~PlatformShaderDependencyTests`. Record exact counts/commands/logs; preserve failing logs separately. Hand off the additive API to Task 2 and request ordinary task review before proceeding.


## Validation commands and environment

Run from `C:/dev/helworks/helengine-ps3`; request filesystem escalation for sibling project edits and build outputs when necessary. User scope approval does not itself expand the sandbox.

Use `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet` for all managed artifacts, and a created `source-path/fixture-storage` for TEMP/TMP in the test process. Save and restore those environment variables in try/finally, including on test failure. Do not change user-wide environment variables or global config. The existing editor fixture owns and disposes its GUID child within that root; never recursively delete the root itself.

```powershell
dotnet test ../helengine/engine/helengine.baseplatform.tests/helengine.baseplatform.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~PlatformShaderArtifactCookSourceTests|FullyQualifiedName~PlatformShaderDependencyTests' --verbosity quiet

dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter FullyQualifiedName~EditorPlatformAssetCookServiceTests --verbosity quiet
```

Redirect full output to uniquely named logs in `source-path`, display capped excerpts, check exit codes and nonzero test counts. Do not suppress existing analyzer warnings; distinguish them from new warnings. If the existing shared test dependency graph cannot build, report its exact blocker and do not substitute a passing PS3-only unit test for editor handoff validation.


