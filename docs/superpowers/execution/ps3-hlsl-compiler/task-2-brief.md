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


## Task 2: Reflection that survives optimization

Controller interface clarification: represent compiler built-ins separately using immutable `IReadOnlyList<string> InputBuiltIns` and `OutputBuiltIns` on Translation, containing official SPIR-V BuiltIn names such as Position from reflection. User Inputs/Outputs exclude them. Add a fixture assertion that the vertex output includes built-in Position. No extra class hierarchy is needed; later pair validation needs this fact to distinguish a vertex program that writes position from one that only emits user varyings.

Layout boundary: a single ArrayStride cannot describe multiple independent nested array strides. Reject any layout not exactly representable by the current DTO with a specific NotSupportedException and test, rather than multiplying lengths and silently overwriting strides. Do not introduce a multi-stride schema in this task. Preserve full authored/generated nested-member paths and cache authored names before renaming any shared SPIR-V member type.

Controller guidance: shaderc's official C API exposes `shaderc_compile_options_set_hlsl_functionality1(options, bool)` to enable SPV_GOOGLE_hlsl_functionality1. Check the installed Silk.NET wrapper (`CompileOptionsSetHlslFunctionality1`) and use compiler-produced HlslSemanticGOOGLE decorations; do not infer semantics from variable order. Preserve debug names while optimizing. Reference: https://github.com/google/shaderc/blob/main/libshaderc/include/shaderc/shaderc.h. Installed package behavior still needs tests; the current upstream header is not proof of the bundled version's behavior.

**Files:**
- Modify: `builder/shaders/Ps3ShaderSourceCompiler.cs`
- Create: `builder/shaders/Ps3ShaderTranslation.cs`
- Create: `builder/shaders/Ps3ShaderInterfaceVariable.cs`
- Create: `builder/shaders/Ps3ShaderUniformMember.cs`
- Create: `builder/shaders/Ps3ShaderSamplerBinding.cs`
- Create: `builder/shaders/Ps3ShaderReflection.cs`
- Create: `builder.tests/Ps3ShaderReflectionTests.cs`

**Interfaces:**
- Add `Ps3ShaderTranslation Translate(ShaderCompileRequest request, IShaderIncludeResolver includeResolver)`; Compile delegates to Translate and returns GlslSource.
- Translation exposes immutable `Stage`, `GlslSource`, `Inputs`, `Outputs`, `UniformMembers`, `Samplers`, `Dependencies`. Collections are copied at construction, not externally mutable.
- Interface variable fields: `Semantic` (authored HLSL semantic), `Location` (SPIR-V location), `ScalarType`, `VectorWidth`, `Columns`, `ArrayLength`, `Interpolation`, `GeneratedName`. Represent built-ins separately from user varyings; do not invent POSITION/TEXCOORD identities from ordinal order.
- Uniform member fields: `BufferName`, `BufferBinding`, `MemberPath`, `ByteOffset`, `ByteSize`, `ArrayStride`, `MatrixStride`, `RowMajor`, `GeneratedName`, plus scalar/vector/matrix shape. Nested members retain complete member paths and offsets.
- Sampler binding fields: `TextureName`, `TextureBinding`, `SamplerName`, `SamplerBinding`, `GeneratedName`, `Dimension`. Each combined texture/sampler pair is distinct.
- New DTO files each contain exactly one documented type; use existing ShaderStage, strings and unsigned integers rather than adding shared-engine enums.

- [ ] **Step 1: Add reflection tests.** Compile optimized and unoptimized variants of the same source and compare active authored identities. Add two cbuffers with the same member name, a padded float3/scalar/matrix buffer, reordered UV/color varyings, and one texture used with two sampler states. Assert:

```csharp
Assert.Equal(2, translation.Samplers.Count);
Assert.NotEqual(translation.Samplers[0].GeneratedName, translation.Samplers[1].GeneratedName);
Assert.All(translation.UniformMembers, member => Assert.False(string.IsNullOrWhiteSpace(member.MemberPath)));
Assert.Contains(translation.Outputs, variable => variable.Semantic == "TEXCOORD0");
```

Also assert exact expected offsets/strides from the fixture declarations, not merely nonempty values. For unsupported storage images, runtime arrays and nonzero resource spaces, assert explicit unsupported-resource diagnostics until those cases have a tested RSX mapping.

- [ ] **Step 2: Run the tests red.** Record which reflection facts the prototype loses.
- [ ] **Step 3: Implement SPIRV-Cross reflection before emission.** Use `CompilerCreateShaderResources`, `ResourcesGetResourceListForType`, member/type/layout queries and decoration accessors. Obtain semantic identities from compiler-provided HLSL metadata; if unavailable for an active interface, fail with an actionable diagnostic rather than parsing generated GLSL or guessing. Inspect installed API bindings for exact signatures.
- [ ] **Step 4: Assign stable names through compiler APIs.** Namespace generated names by resource category and binding/member identity; record the mapping. Name combined samplers by both resource bindings. Use `CompilerSetName`/member naming APIs before `CompilerCompile`; never perform string replacement on emitted source. Retain optimizer support but preserve the reflection data it needs. Check every native API result.
- [ ] **Step 5: Run tests green and re-run the existing real SDK probe.** Both stages must still compile with `cgcomp -Wcg,-oglsl`. Inspect the dump to establish that named uniforms/attributes survive into Cg reflection. Parent reviews the metadata contract and documents any unsupported cases before Task 3 consumes it.
