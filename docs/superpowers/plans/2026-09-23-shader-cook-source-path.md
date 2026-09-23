# Shader Cook Source Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the real authored/built-in HLSL file path across the editor-to-platform shader cook handoff, without breaking existing source-only callers.

**Architecture:** Extend the immutable shared source DTO with explicitly optional legacy path metadata and a strict path-bearing constructor. Forward the already-resolved editor path unchanged in meaning. Verify the real cook-service handoff and relative/nested include resolution using the existing filesystem resolver; do not add another resolver or a PS3 cooker in this small prerequisite plan.

**Tech Stack:** C#/.NET 9, xUnit, existing helengine.baseplatform/editor/shader compilation contracts.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-hlsl-shaders-design.md`, plus Helena's 2026-09-23 explicit approval: “Sim, autorizar essa extensão pontual” for baseplatform/editor to transport the real HLSL path.

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

## Evidence and boundaries

Codebase-memory `list_projects` was retried and returned `Transport closed`. Graph coverage is unknown. Exact source inspection established:

- `PlatformShaderArtifactCookSource` currently holds only asset id/hash/text and has one three-string constructor.
- `EditorProjectShaderSource.SourcePath` is already absolute; its resolver reads real project or built-in files.
- `EditorPlatformAssetCookService.CookPlatformShaderArtifacts` currently drops that path when constructing the shared DTO.
- `ShaderFilesystemIncludeResolver.Resolve(requestingFile, includePath)` already resolves relative to the requesting file, with a root fallback. No resolver modification is needed.
- `ShaderArtifactTestPlatformMaterialAssetBuilder.LastShaderArtifactCookRequest` already exposes the actual request sent by the service. No production extraction solely for test access is needed.

This plan implements the newly approved prerequisite only. The later backend/cooker plan still needs cache policy, artifact format, material-selected programs/variants and staging. Native rendering remains a separate milestone.

## Review Focus

1. Existing plugins/tests constructing three-argument sources remain compatible; unknown path is explicit, never a fabricated filename (Task 1).
2. Root-relative or drive-relative Windows paths must not be normalized against process state and accepted as absolute (Task 1).
3. Project override and built-in sources must preserve their actual resolved path, not just a plausible path reconstructed from asset id (Task 2).
4. Relative/nested includes must use the transported path even when the resolver fallback root points elsewhere; missing includes must still fail (Task 2).
5. Neither path transport nor normalization changes source text/hash or leaks host paths into the declared cooked artifact payload (Tasks 1 and 2).

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

- [ ] **Step 1: Write the contract tests before adding the overload.** Assert the original fields, canonical absolute path, explicit legacy absence, unchanged source text/hash, rejection of blank/relative paths, and no disk existence requirement. Use an absolute metadata-only fixture path constructed with `Path.GetFullPath(Path.Combine("shader-source-tests", "shaders with spaces", "ação.hlsl"))`; do not create that file.

```csharp
PlatformShaderArtifactCookSource legacy = new("asset", "supplied-hash", "source snapshot");
Assert.False(legacy.HasSourcePath);
Assert.Null(legacy.SourcePath);

string path = Path.GetFullPath(Path.Combine("shader-source-tests", "shaders with spaces", "ação.hlsl"));
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

## Task 2: Preserve the editor handoff and verify relative includes

**Files:**
- Modify: `../helengine/engine/helengine.editor/managers/project/EditorPlatformAssetCookService.cs`
- Modify: `../helengine/engine/helengine.editor.tests/managers/project/EditorPlatformAssetCookServiceTests.cs`
- Read-only: `../helengine/engine/helengine.editor.tests/testing/ShaderArtifactTestPlatformMaterialAssetBuilder.cs`
- Read-only: `../helengine/engine/helengine.editor/shaders/EditorProjectShaderSourceResolver.cs`
- Read-only: `../helengine/engine/helengine.shader/shaders/compilation/ShaderFilesystemIncludeResolver.cs`

**Interfaces:** Task 1's four-argument DTO overload; the existing real `service.Cook(...)` flow; existing recorded `builder.LastShaderArtifactCookRequest`. Do not bypass the service by testing a manually fabricated request as the only handoff evidence.

- [ ] **Step 1: Strengthen the existing built-in cook test and add authored-override coverage.** In `Cook_whenPlatformBuilderSupportsShaderArtifacts_resolvesSourcesAndCollectsDeclaredBundle`, retain its declared-bundle assertion and add:

```csharp
string expectedPath = BuiltInShaderAssetLibrary.ResolveShaderPath("ForwardStandardShader.hlsl");
Assert.True(source.HasSourcePath);
Assert.Equal(Path.GetFullPath(expectedPath), source.SourcePath);
Assert.Equal(File.ReadAllText(expectedPath), source.SourceText);
Assert.Equal(Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(expectedPath))), source.SourceHash);
```

For the authored case, reuse the same existing material/scene/service fixture setup in a second test. Write `ProjectRootPath/assets/ForwardStandardShader.hlsl` with distinct content before cooking; this exact assets-root path gives asset id `ForwardStandardShader` and intentionally overrides the built-in. Assert the received path equals this authored file, differs from the built-in path, and text/hash match its bytes. Retain the normal shader artifact declaration assertion. Do not modify the production resolver or the test builder to force this result.

- [ ] **Step 2: Make that authored case prove nested relative include context.** Under the test-owned project write these nonempty files using the existing test fixture's normal file APIs:

```text
assets/ForwardStandardShader.hlsl -> #include "inc/common.hlsli"
assets/inc/common.hlsli         -> #include "math/value.hlsli"
assets/inc/math/value.hlsli     -> float4 NestedValue() { return float4(1, 2, 3, 4); }
math/value.hlsli                -> float4 WrongFallback() { return 0; }
```

After obtaining `source` from the actual captured cook request, use the existing resolver:

```csharp
ShaderFilesystemIncludeResolver resolver = new(ProjectRootPath);
ShaderIncludeResult common = resolver.Resolve(source.SourcePath, "inc/common.hlsli");
ShaderIncludeResult nested = resolver.Resolve(common.Path, "math/value.hlsli");
Assert.Equal(Path.GetFullPath(Path.Combine(ProjectRootPath, "assets", "inc", "math", "value.hlsli")), Path.GetFullPath(nested.Path));
Assert.Contains("NestedValue", nested.Source);
Assert.DoesNotContain("WrongFallback", nested.Source);
Assert.Throws<FileNotFoundException>(() => resolver.Resolve(common.Path, "missing.hlsli"));
```

This verifies handoff plus the existing include resolver, not compilation or a new cooker. Do not claim HLSL preprocessing coverage from manually resolving two include names; the accepted PS3 compiler suite already owns preprocessing tests.

- [ ] **Step 3: Run the two cook handoff tests RED before editing production.** The new DTO exists from Task 1 but the old service still calls its legacy constructor, so HasSourcePath/path assertions must fail. Existing unrelated failures must be diagnosed separately, not worked around by disabling assertions.

- [ ] **Step 4: Apply the minimal production change.** In the existing resolvedSources loop:

```csharp
shaderSources[index] = new PlatformShaderArtifactCookSource(
    resolvedSource.ShaderAssetId,
    resolvedSource.SourceHash,
    resolvedSource.SourceText,
    resolvedSource.SourcePath);
```

No new path discovery, source reread, default path, serialized runtime field, schema, artifact name or feature flag.

- [ ] **Step 5: Run GREEN and narrowly cover existing behavior.** First run the new/strengthened cook tests. Then run `EditorPlatformAssetCookServiceTests` once; it covers non-shader builders and existing declarations. Run the Task 1 DTO/dependency tests against the final code if changed since their accepted run. Do not rerun the PS3 Docker suite for a metadata-only change.

- [ ] **Step 6: Report and review.** Report named changed files, exact RED/GREEN evidence and the fact that include-aware consumers must reject `HasSourcePath == false` rather than invent a filename. Ordinary task reviewer checks DTO compatibility, source preservation and actual service handoff. Independent whole-branch review still requires Helena's separate permission. No commits/pushes.

## Validation commands and environment

Run from `C:/dev/helworks/helengine-ps3`; request filesystem escalation for sibling project edits and build outputs when necessary. User scope approval does not itself expand the sandbox.

Use `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet` for all managed artifacts, and a created `source-path/fixture-storage` for TEMP/TMP in the test process. Save and restore those environment variables in try/finally, including on test failure. Do not change user-wide environment variables or global config. The existing editor fixture owns and disposes its GUID child within that root; never recursively delete the root itself.

```powershell
dotnet test ../helengine/engine/helengine.baseplatform.tests/helengine.baseplatform.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter 'FullyQualifiedName~PlatformShaderArtifactCookSourceTests|FullyQualifiedName~PlatformShaderDependencyTests' --verbosity quiet

dotnet test ../helengine/engine/helengine.editor.tests/helengine.editor.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/source-path/dotnet --filter FullyQualifiedName~EditorPlatformAssetCookServiceTests --verbosity quiet
```

Redirect full output to uniquely named logs in `source-path`, display capped excerpts, check exit codes and nonzero test counts. Do not suppress existing analyzer warnings; distinguish them from new warnings. If the existing shared test dependency graph cannot build, report its exact blocker and do not substitute a passing PS3-only unit test for editor handoff validation.

## Self-review and next handoff

- Scope is the explicitly approved path transport, not the entire backend/cooker milestone.
- All interfaces above refer to inspected existing types or Task 1's additive members.
- Task 2 consumes exactly Task 1's four-argument constructor and explicit path-presence contract.
- Every Review Focus item has named assertions above.
- Existing shaders, source hashes, program identities, stage binding maps, artifact declaration formats and runtime feature flags remain unchanged.
- After this prerequisite, parent writes the backend/cooker plan using `next-phase-inputs.md`: cache bypass/full identity, complete uniform-buffer size, platform artifact encoding, material-selected stage names/variants, and normal staging. Path availability is not a substitute for those contracts.
