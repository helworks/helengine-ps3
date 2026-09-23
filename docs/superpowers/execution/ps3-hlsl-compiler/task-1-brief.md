## Global Constraints

- Work stays in the existing checkouts, without worktrees.
- Native compilation and final RSX conversion remain in the repository's pinned Docker SDK.
- Never modify generated shader text, binaries or headers after compilation.
- There is no host SDK fallback or implicit temporary-directory output.
- No default shader or default binding is synthesized.
- Use the shared include resolver and compiler preprocessor rather than textual include replacement.
- Do not change other builders or unrelated native-cache work.
- Enable the platform shader feature only after the cooking-to-rendering path passes.
- No screenshots without permission.
- All generated outputs/logs/jobs use `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923` or an explicit child build ID; do not use `%TEMP%`.
- One class per file; substantive XML/`///` comments; explicit failures; preserve user changes. C# follows sibling AGENTS.md conventions.


## Read-only evidence and current state

Codebase Memory was attempted again on this turn and returned `Transport closed`; no current graph coverage can be claimed. Use exact source reads for these bounded files:

- `builder/shaders/Ps3ShaderSourceCompiler.cs`: experimental HLSL -> GLSL conversion.
- `builder.tests/Ps3ShaderSourceCompilerTests.cs`: three passing frontend tests; optional probe output.
- `builder/helengine.ps3.builder.csproj`: Silk dependencies and compilation-project reference already added.
- `../helengine/engine/helengine.shader/shaders/runtime/ShaderCompileTarget.cs` and `ShaderTargetDescriptors.cs`: uncommitted Ps3 target addition already present. Preserve existing ordinals.
- `../helengine/engine/helengine.shader/shaders/compilation/IShaderIncludeResolver.cs`, `ShaderIncludeResult.cs`, `ShaderCompileRequest.cs`, `ShaderCompileOptions.cs`: source contracts.
- `../helengine-wiiu/builder/shaders/WiiUGlslShaderBackend.cs`: read-only example of SPIRV-Cross reflection APIs; do not copy its StandardShader-only naming policy.
- `toolchain-lock.json`: authoritative SDK image and Cg mount/hash configuration.
- `scripts/build.ps1`: existing process/toolchain validation patterns; read-only in this phase.

Pre-existing user edits: `Makefile`, `scripts/build.ps1`, `scripts/tests/build-contract.tests.ps1`, and unrelated changes in sibling helengine. Do not revert them. The prototype files above are unfinished task-owned work, not completed production functionality.


## Validation commands

Run focused .NET tests with an explicit output root:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet --filter FullyQualifiedName~Ps3Shader --verbosity quiet
```

Docker tests must be explicitly identified as integration tests and must run for phase acceptance. Ordinary tests must not silently claim Docker coverage when Docker is absent. Capture potentially large output in build-owned logs and return bounded excerpts. Request tool escalation when the sandbox requires it.

## Task 1: Deterministic frontend requests and safe include callbacks

**Files:**
- Modify: `builder/shaders/Ps3ShaderSourceCompiler.cs`
- Create: `builder/shaders/Ps3ShaderIncludeSession.cs`
- Create: `builder/shaders/Ps3ShaderSourceDependency.cs`
- Modify: `builder.tests/Ps3ShaderSourceCompilerTests.cs`
- Create: `builder.tests/Ps3ShaderIncludeResolverStub.cs`

**Interfaces:**
- Consume existing `ShaderCompileRequest` and `IShaderIncludeResolver.Resolve(string requestingFile, string includePath)`.
- Add `string Compile(ShaderCompileRequest request, IShaderIncludeResolver includeResolver)`; migrate tests to the required resolver overload, removing the resolver-free prototype overload.
- `Ps3ShaderSourceDependency` has immutable `string Path` and `string ContentHash` properties; constructor requires both.
- `Ps3ShaderIncludeSession` owns shaderc include callbacks and allocations for one compile, records resolved dependency contents/hashes, and implements IDisposable. Exceptions become compiler diagnostics and are never thrown across a native callback boundary.

- [ ] **Step 1: Write request and include regression tests first.** Keep the existing self-contained shader fixture. Add a resolver stub class with a dictionary and a recorded request list. Test an active include, a nested relative include, an inactive missing include under `#if 0`, a missing active include, a throwing resolver, UTF-8 content, and a define-selected branch. Use explicit assertions:

```csharp
Assert.Contains("void main()", output);
Assert.Contains(resolver.Requests, request => request.IncludePath == "color.hlsl");
Assert.DoesNotContain(resolver.Requests, request => request.IncludePath == "inactive.hlsl");
Assert.Contains("missing.hlsl", error.Message);
```

Define the stub's recorded request as its own documented class/file if needed; no tuples/local helper functions. Reuse the existing test CreateRequest method with optional define/source arguments, not new production defaults.

- [ ] **Step 2: Run the targeted tests and record expected failures.** Missing overload compile failures establish the API addition; after adding the signature, demonstrate at least one behavioral failure before implementing callback logic.
- [ ] **Step 3: Implement the callback lifetime boundary.** Keep delegates rooted until shaderc releases the result/options. Allocate name/content using UTF-8 byte lengths; release each include allocation exactly once via shaderc's release callback. A failed resolution returns an error include result. Dispose residual allocations on all exits. Let shaderc handle include guards and conditional branches. Record actual active dependencies, including repeated paths with identical content. Fail if a resolver returns different contents for the same path in one compilation.
- [ ] **Step 4: Validate requests before entering native code.** Reject non-Ps3 targets and stages other than Vertex/Pixel. Preserve request defines/options; request debug names needed for reflection independently of optimization. A warnings-as-errors request must not silently downgrade warnings. Keep the original include path in diagnostics.
- [ ] **Step 5: Run focused tests repeatedly, inspect the diff and report.** Report test counts, command, output path and changed files; do not claim allocation safety from passing tests alone. Parent reviews native callback ownership before the next task.


