# Task 1 report: deterministic frontend requests and safe include callbacks

## Result

Implemented the required resolver overload and compile-scoped shaderc include session. Requests are rejected before native compilation when the target is not PS3 or the stage is not Vertex/Pixel. Active include dependencies retain their resolved paths and SHA-256 hashes, repeated identical paths are recorded, and changing content for a path records a fatal error that remains authoritative across shaderc lookup retries. Relative `FileNotFoundException` results remain eligible for shaderc's ordinary include fallback. Resolver failures become compiler diagnostics containing the original include path.

Callback delegates and Silk.NET callback handles are held by the session through shaderc result/options release. Each unmanaged include result and its UTF-8 name/content buffers are registered, released once by the native callback, and any residual allocations are freed when the session is disposed. An emergency include-error result is preallocated before native compilation and callback registration. If normal callback-result allocation fails, the callback returns this non-null emergency result; the release callback recognizes it, and session disposal frees it exactly once. Callback and release exceptions are contained, retaining the original fatal failure until after native cleanup.

The review-fix tests cover deterministic emergency-allocation failure, UTF-8 include path and content, warnings-as-errors, and repeated include consistency even if shaderc retries. Empirical retry characterization against the bundled shaderc observed that the first relative lookup threw `FileNotFoundException`, the second resolver call returned valid source, compilation succeeded, and exactly two resolver requests occurred. Accordingly, relative `FileNotFoundException` remains a nonfatal lookup fallback for this bundled implementation. The changed-content test also supplies a later valid response and verifies the retained consistency failure is not masked.

## TDD evidence

- API red: the first focused build failed with `CS1501` because the resolver overload did not exist (10 call-site errors; no tests ran). Log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task-1-red-api.log`.
- Behavioral red: after adding the overload without callback behavior, 5 tests failed and 5 passed. The failures covered active/nested/define-selected includes, resolver failure, and UTF-8 include content. Log: `task-1-red-behavior.log` in the same build root.
- Retry/consistency red: adding target, stage, and repeated-path tests produced 13 passed / 1 failed. The changed-content case demonstrated shaderc called the resolver three times; a later ordinary miss hid the second callback's inconsistency diagnostic. The focused assertion log recorded `Expected: 2`, `Actual: 3`: `task-1-red-content-change-count.log`.
- Emergency-allocation seam red: deterministic tests initially failed to compile because the allocator-injection constructor was not yet present (`CS1729`). Log: `task-1-red-emergency-seam.log`.
- Retry characterization: bundled shaderc retried after the first relative `FileNotFoundException`; log: `task-1-relative-retry-observation.log`.
- Final review-fix green: 18 passed, 0 failed, 0 skipped after removal of the three redundant catches. The final log, `task-1-review-fix-final.log`, has no `CS1058` warnings.

The final validation command was:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter FullyQualifiedName~Ps3Shader --verbosity quiet
```

The test assembly was written beneath `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\dotnet-green-3`. The required visible build root needed tool escalation because it is outside the workspace write boundary.

## Warnings and limits

Earlier builds reported existing `CS0414` and `IDE0040` warnings in sibling `helengine.core` source files; they were left untouched. An intermediate review-fix run reported three redundant-catch `CS1058` warnings in `Ps3ShaderIncludeSession`; those catches were removed while retaining exception containment. The final focused log has no `CS1058` warnings. Native PS3 SDK/Docker integration validation was not part of this focused .NET run. Warnings-as-errors coverage uses a deterministic `#warning` source.

Codebase Memory was unavailable (`Transport closed`). The listed source contracts and repository files were read directly; no graph coverage is claimed.

## Files

- `builder/shaders/Ps3ShaderSourceCompiler.cs`
- `builder/shaders/Ps3ShaderIncludeSession.cs`
- `builder/shaders/Ps3ShaderIncludeAllocation.cs` (separate documented ownership type)
- `builder/shaders/Ps3ShaderSourceDependency.cs`
- `builder.tests/Ps3ShaderSourceCompilerTests.cs`
- `builder.tests/Ps3ShaderIncludeAllocator.cs`
- `builder.tests/Ps3ShaderIncludeRetryResolver.cs`
- `builder.tests/Ps3ShaderIncludeResolverStub.cs`
- `builder.tests/Ps3ShaderIncludeRequest.cs`
- `docs/superpowers/execution/ps3-hlsl-compiler/task-1-report.md`
