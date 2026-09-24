# PS3 shader cooker and lazy registration implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Parent writes plans; Luna implements. Work in the existing checkout, no worktrees.

**Goal:** Publish deterministic PS3 pair artifacts through the standard platform shader capability, with source provenance and side-effect-free backend discovery.

**Architecture:** The platform builder delegates shader cooking to a focused cooker. Each unique dependency compiles two stages through ShaderCompileService using the existing PS3 backend, reconciles physical bindings once, and serializes the existing P3SP format. A lazy registry adapter constructs the production backend only when Compile is called.

**Tech Stack:** C# net9.0, shared shader service/include resolver, existing shaderc/SPIRV-Cross frontend, pinned Docker cgcomp, xUnit.

**Spec:** docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md, cooker/registration slice only. Material envelope and native renderer are separate required follow-on slices, not claimed delivered here.

## Global constraints

- No shared-engine, authored-project, generated-output, native renderer or existing dirty build-script edits.
- Keep forced-disabled `shaders` feature unchanged until runtime acceptance. Existing material cooker remains unchanged in this slice.
- No SDK/toolchain lock access during builder construction or backend registration. No host/WSL SDK fallback.
- Output root: C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/cooker. Set TEMP/TMP to its fixture-storage for tests and restore them.
- Preserve Makefile, scripts/build.ps1, scripts/tests/build-contract.tests.ps1 and all concurrent backend/artifact/SDK files.
- One type per file, PascalCase fields, substantive XML comments, no local functions/tuples/nullable reference annotations.
- Parent owns review and named-file commit. No push or screenshots.

## Review focus

1. Discovery on a machine without configured SDK must succeed without creating files or launching processes.
2. An invalid later dependency must fail before any final artifact is replaced.
3. Same variant names from different assets/program pairs must not overwrite each other; repeated identical dependencies compile once.
4. Request source text is authoritative even if the source file has different contents; nested relative includes use actual SourcePath.
5. Missing provenance, unsupported variants, or wrong typed backend results must fail visibly, not produce declarations of unusable files.

## Task 1: Standard cooker capability and lazy backend discovery

**Files:**
- Create builder/shaders/Ps3ShaderArtifactCooker.cs.
- Create builder/shaders/Ps3ShaderBackendFactory.cs.
- Create builder/shaders/Ps3LazyShaderBackend.cs.
- Modify builder/Ps3PlatformAssetBuilder.cs only for the two optional interfaces, constructor delegation and documented methods.
- Create builder.tests/Ps3ShaderArtifactCookerTests.cs and builder.tests/Ps3ShaderCookBackendStub.cs.
- Create builder.tests/Ps3ShaderRegistrationTests.cs.
- Create docs/Ps3ShaderCooking.md.

**Interfaces:**

```csharp
public sealed class Ps3ShaderArtifactCooker {
    public Ps3ShaderArtifactCooker();
    public Ps3ShaderArtifactCooker(Func<string, IShaderBackend> backendFactory);
    public PlatformShaderArtifactCookResult Cook(PlatformShaderArtifactCookRequest request);
}
public static class Ps3ShaderBackendFactory {
    public static IShaderBackend Create(string workingRoot);
}
public sealed class Ps3LazyShaderBackend : IShaderBackend, IShaderBackendCachePolicy {
    public Ps3LazyShaderBackend();
    internal Ps3LazyShaderBackend(Func<IShaderBackend> backendFactory);
    // Target Ps3; capabilities model3.0..5.0 Vertex/Pixel; UseSharedCache false.
    public ShaderCompileResult Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver);
}
```

Preserve both existing Ps3PlatformAssetBuilder constructors. Add overload `(IPs3NativeBuildExecutor nativeBuildExecutor, Ps3ShaderArtifactCooker shaderCooker)`, require nonnull. Default constructors create the cooker without invoking its factory. Builder implements `IPlatformShaderArtifactBuilder.CookShaderArtifacts(request)` and `IShaderBackendRegistryContributor.RegisterShaderBackends(registry)`, the latter registers a new Ps3LazyShaderBackend only.

- [ ] Write RED tests first: build requests with explicit roots and source paths under fixture-storage. Test empty dependencies invoke factory zero times; duplicate pair invokes compiler twice total; different assets with same variants generate distinct paths. Use a fake IShaderBackend returning typed Ps3ShaderCompileResult from internally constructed immutable DTOs, and record received requests. Stub can resolve an include explicitly to verify passed resolver semantics; include genuine frontend/Docker test separately.

```csharp
var request = new PlatformShaderArtifactCookRequest(cookRoot, "ps3", "release", "ps3-standard",
    new[] { dependency, dependency }, new[] { source });
var result = new Ps3ShaderArtifactCooker(factory).Cook(request);
Assert.Single(result.CookedArtifactDeclarations);
Assert.Equal(2, stub.Requests.Count);
Assert.All(stub.Requests, r => Assert.Equal(source.SourcePath, r.Source.Path));
```

ShaderSourceInfo.Path and ShaderSourceInfo.Source are the verified shared property names. Do not alter shared types to fit tests.

- [ ] Run focused RED: `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/cooker/dotnet --filter "FullyQualifiedName~Ps3ShaderArtifactCookerTests|FullyQualifiedName~Ps3ShaderRegistrationTests" --verbosity quiet`; retain logs/TRX under cooker/results, cap console output.

- [ ] Validate complete input before factory or final file writes. Require platform ps3, graphics profile ps3-standard (blank allowed as unspecified consistent with existing material cooker), every dependency HasProgramPair, exact matching source with nonblank SourceHash and HasSourcePath, nonempty source text, existing source parent directory. Validate all identities with Ps3ShaderArtifactIdentity. Missing source file itself is not failure because text snapshot is authoritative. Empty dependency request returns empty result without requiring environment/SDK.

- [ ] Resolve exactly requested variants, not all possible variants: if ShaderAssetId equals `ForwardStandardShader` ordinal, select matching `StandardShaderVariants.All` record ordinal, error for unknown name. Preserve logical program names from dependency; entrypoints/defines from record. Other assets support only exact variant `default` with VS/PS and no variant defines. Parse shared define strings at first `=` (no `=` means value1), reject empty name/value. Use ShaderPlatformDefines.BuildDefines(Ps3, new ShaderModel(4,0), variantDefines); do not add Wii U specializations.

- [ ] Deduplicate by identity.RelativePath, preserving first-request order. Working root is Path.Combine(request.CookRootPath, ".ps3-shader-work"); backend factory receives this explicit absolute root, never current directory/system temp. For each source use ShaderFilesystemIncludeResolver(Path.GetDirectoryName(source.SourcePath)); create ShaderCompileService(resolver, new ShaderMemoryCompileCache(), new ShaderSourceHasher()); register backend. Production backend bypasses shared cache. Build each request as below:

```csharp
var compileRequest = new ShaderCompileRequest(
    new ShaderSourceInfo(source.SourcePath, source.SourceText), programName, entryPoint,
    stage, ShaderCompileTarget.Ps3, new ShaderModel(4, 0), dependency.VariantName,
    defines, new ShaderCompileOptions(ShaderBindingPolicies.Default, false, true, false));
ShaderCompileResult result = service.Compile(compileRequest);
if (result is not Ps3ShaderCompileResult typed || !result.Success)
    throw new InvalidOperationException("PS3 cooking requires a successful typed PS3 result.");
```

ShaderCompileResult.Success and Request are the verified shared properties. Validate returned request identity/stage against the request; no accepting unrelated cached or injected results. Reconcile once with Ps3ShaderBindingMapBuilder.Build(vertex.Translation,vertex.Program,pixel.Translation,pixel.Program). Construct existing Ps3ShaderProgramPair and call Ps3ShaderArtifactWriter.Write(identity,source.SourceHash,vertexEntry,pixelEntry,pair). Never call Ps3ShaderPairCompiler here.

- [ ] Prepare every pair byte array in memory before publishing final paths; invalid later request/compiler failure leaves preexisting artifact bytes untouched. Retain ordered declarations plus Dictionary<string,byte[]> keyed by safe hash path. Write each final file atomically via a uniquely named temporary file in the same shaders directory and File.Move(overwrite:true); clean only that exact temporary file on failure. No batch-wide transaction claim for filesystem failures. Result contains only finished final pair files, not compiler jobs. Declaration: RelativePath=identity.RelativePath, LogicalArtifactId=identity.RelativePath, ArtifactKind="shader", VariantId=dependency.VariantName. Identifiers are collision-safe and deterministic.

- [ ] Production factory reads required fully-qualified `HELENGINE_PS3_REPOSITORY_ROOT`, loads Ps3ShaderToolchain there, and returns new Ps3ShaderBackend(new Ps3ShaderSourceCompiler(),new Ps3DockerShaderCompiler(toolchain),workingRoot). Validate workingRoot fully qualified. Lazy registry backend reads required fully-qualified `HELENGINE_PS3_SHADER_WORK_ROOT` only during Compile, then calls factory. No cwd fallback, cached lock, or silent defaults. Missing env errors name the variable. Internal factory constructor permits tests without Docker; reject null/wrong target backend and forward exact request/resolver. Backend capabilities access cannot invoke factory.

- [ ] Tests cover all five review inputs plus wrong platform, profile, absent program selection, legacy no-path source, unknown standard/default variants, exact StandardShaderVariants entrypoints/defines, distinct pair names within same asset, failed/wrong typed result, exact source hash in emitted P3SP, preserved blobs, factory throwing before publication, and preexisting sentinel artifact survives a later compile failure. Discovery test clears/restores env, constructs builder/registers into real registry, asserts ContainsTarget(Ps3), zero factory calls on Target/Capabilities/UseSharedCache; then compile triggers exactly one configured factory. Use existing test isolation collection for env where applicable.

- [ ] Add Docker-category real small pair cooking test using a source with relative include located beside SourcePath under fixture-storage; actual PS3 backend/toolchain, full P3SP header and source identity assertions, two declarations for two asset IDs and deterministic recook. Do not run until parent reports SDK lock ready. Preserve explicit failing real StandardShader acceptance elsewhere; never skip or convert it to expected failure.

- [ ] Run focused pure GREEN and `--filter Category!=Docker` once with HELENGINE_PS3_REPOSITORY_ROOT and HELENGINE_PS3_SHADER_TEST_OUTPUT under cooker root. Document how registry compile work root differs from cooker-derived root and why shaders feature remains disabled. Return report and files; parent review/verification/commit follows.

## Self-review

This plan implements only cookbook registration/artifact publication, not material selection/native support. Artifact paths/DTO names match existing serializer. Both stage results come from the shared service, no duplicated compilation. Source provenance and standard variant definitions remain shared authoritative inputs. All five review failure classes have test steps. SDK acceptance awaits corrected pinned image; pure tests can run independently without modifying SDK work.
