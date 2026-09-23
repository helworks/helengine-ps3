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


## Validation commands

Run focused .NET tests with an explicit output root:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet --filter FullyQualifiedName~Ps3Shader --verbosity quiet
```

Docker tests must be explicitly identified as integration tests and must run for phase acceptance. Ordinary tests must not silently claim Docker coverage when Docker is absent. Capture potentially large output in build-owned logs and return bounded excerpts. Request tool escalation when the sandbox requires it.


## Task 4: Validate authored interfaces against actual RSX slots

Controller correction from pinned SDK parsers: compare canonical RSX routing channels, not raw register-number equality. Vertex TEX0 has index 7, fragment TEX0 has index 4, yet they form a valid link. Preserve raw indices in VertexSlot/PixelSlot and canonical strings in VertexChannel/PixelChannel (`TEX0` etc.). Test valid unequal indices and invalid equal indices whose channels differ. This supersedes numeric-slot equality wording in this task; see execution/ps3-hlsl-compiler/sdk-format-evidence.md.

**Files:**
- Modify if needed: `builder/shaders/Ps3RsxProgramReader.cs` only to extract shared dump-declaration parsing while preserving all binary validation.
- Create if needed: `builder/shaders/Ps3CgDumpReader.cs` and `builder/shaders/Ps3CgDumpVariable.cs` for shared Cg declaration parsing used by the binary validator and binding reconciler.
- Create: `builder/shaders/Ps3ShaderProgramPair.cs`
- Create: `builder/shaders/Ps3ShaderPairCompiler.cs`
- Create: `builder/shaders/Ps3ShaderBindingMap.cs`
- Create: `builder/shaders/Ps3ShaderBindingMapBuilder.cs`
- Create: `builder.tests/Ps3ShaderPairCompilerTests.cs`
- Create: `builder.tests/Ps3ShaderBindingMapTests.cs`

**Interfaces:**
- `Ps3ShaderPairCompiler(Ps3ShaderSourceCompiler sourceCompiler, IPs3RsxCompiler rsxCompiler)`.
- `Compile(ShaderCompileRequest vertexRequest, ShaderCompileRequest pixelRequest, IShaderIncludeResolver includeResolver, string workingRoot, CancellationToken cancellationToken)` returns Ps3ShaderProgramPair.
- Pair exposes `Vertex`, `Pixel` (Ps3RsxProgram), `VertexTranslation`, `PixelTranslation` and `Bindings`.
- Binding map associates each active authored uniform member, attribute and combined sampler identity with the actual RSX parameter; represents stage links as authored semantic/type/interpolation plus vertex-output and pixel-input hardware slots. Each mapping record has its own documented type/file. A map never substitutes missing data with register zero.

Task 3 implementation seam: Ps3RsxProgramReader validates `#profile` and `#var` declarations and cross-checks binary parameters. Do not duplicate that parser in the binding builder. If it remains private at handoff, extract only the shared declaration decoding into a focused helper. A declaration needs exact name/type token, semantic binding (`$vin.ATTR0`/`$vout.TEX0`), hardware binding (`ATTR0`/`TEX0`/`c[0], 4`/`texunit 0`), compiler parameter index and activity flag. Preserve inactive declarations as inactive; unknown active routing forms fail. Stage 3 binary bounds validation stays in its reader. Run its regressions when extracting. This is an interface handoff refinement, not permission for a broad parser rewrite.

Observed Task2 metadata contract: 32-bit float leaves have ScalarType `FP3232` (SPIRV-Cross base-type enum text FP32 plus bit width32), not the string `float`. Compare/translate the actual DTO representation explicitly and test it; do not silently guess a type from vector width. Matrix Columns/VectorWidth and RowMajor are physical SPIR-V layout as documented in the DTO. Require the vertex translation to declare built-in Position and its active Cg output to route to position; do not demand user-varying links for compiler built-ins, nor discard unknown active built-ins by name-prefix alone.
- `Ps3ShaderBindingMapBuilder.Build(Ps3ShaderTranslation vertexSource, Ps3RsxProgram vertexProgram, Ps3ShaderTranslation pixelSource, Ps3RsxProgram pixelProgram)` returns the validated map.

- [ ] **Step 1: Add failing linkage tests.** Include: UV + color, reordered struct fields, an unused vertex output, matching names with mismatched hardware slots, width/type/interpolation mismatch, and a fragment input absent from the vertex stage. Add constants with duplicate member names in different buffers, optimized-out constants, and texture/sampler pairs sharing a texture but not a sampler.

```csharp
Assert.Throws<InvalidOperationException>(() => builder.Build(vertexSource, wrongSlotVertex, pixelSource, pixelProgram));
Assert.All(map.StageLinks, link => Assert.Equal(link.VertexChannel, link.PixelChannel));
Assert.All(map.Uniforms, binding => Assert.False(string.IsNullOrWhiteSpace(binding.AuthoredMemberPath)));
```

Define the test fixture objects explicitly using the DTOs from Tasks 2/3; do not make tests pass by assuming semantic numbers equal hardware register indices.

- [ ] **Step 2: Run tests red and establish the hardware-slot evidence.** Read Cg dump interface declarations and the pinned binary reflection. Parse only documented forms required for the accepted subset. Unknown/ambiguous active reflection is an error, not a skipped record.
- [ ] **Step 3: Implement reconciliation.** Match authored metadata to generated names, then to final Cg/RSX parameters. Match inter-stage semantic/type/interpolation and compare actual hardware slots. Unused vertex outputs may remain; every active fragment input needs a matching producer. Allow truly optimized-out uniforms to disappear without inventing bindings. Reject overlapping/conflicting active maps. If legal HLSL cannot be assigned compatible hardware slots through compiler APIs/options, diagnose that limitation; do not patch generated GLSL or assembly.
- [ ] **Step 4: Implement the pair compiler.** Validate both requests use Ps3 with the correct stages and compatible variant identity. Translate and compile each stage into unique children of the supplied work root, then build the map. Do not publish a partially valid pair. Thread cancellation through both jobs. No backend/service cache yet.
- [ ] **Step 5: Run synthetic linkage tests and real-Docker pair tests.** The modern texture/cbuffer fixture must produce a valid map. At least one nontrivial two-varying fixture must compile or produce the explicit tested slot-incompatibility error; record which is supported. Parent decides whether limitations require compiler-option changes before cooker work begins.
