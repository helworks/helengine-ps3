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


