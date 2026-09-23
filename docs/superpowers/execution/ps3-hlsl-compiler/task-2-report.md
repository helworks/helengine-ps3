# Task 2 — Reflection that survives optimization

## Result

Task 2 implementation and focused .NET validation are complete. Reflection is read from compiler metadata before GLSL emission; identifiers are assigned through SPIRV-Cross APIs, and no generated shader text is rewritten.

`Ps3ShaderSourceCompiler.Translate(request, includeResolver)` returns an immutable translation containing stage, GLSL, user interfaces, SPIR-V built-ins, uniform members, sampler pairs, and copied include dependencies. `Compile` delegates to `Translate`. The Task 1 include resolver, fatal callback-consistency checks, allocation fallback, and uppercase SHA-256 dependency hashes are preserved.

## Reflection details

- Active vertex interfaces retain compiler-provided `HlslSemanticGOOGLE` semantics and SPIR-V locations under optimization, including when the caller disables debug info. Built-ins come from SPIR-V reflection and are separate from user varyings; the vertex output contains `Position` in `OutputBuiltIns`.
- Uniform member paths preserve nested authored paths. Generated names include the unique buffer root and every generated struct-member segment. Original member names are cached by SPIR-V type before member renaming, so the shared `SharedTint` type keeps the correct `Primary.Tint` and `Secondary.Tint` paths.
- Uniform offsets, sizes, array/matrix strides, shape, and `RowMajor` describe the physical SPIR-V layout reported by SPIRV-Cross. They do not reproduce HLSL matrix-major spelling: glslang documents the HLSL Mrc versus SPIR-V Mcr convention and the corresponding reversal of row/column-major sense in its `pack_matrix` handling ([official glslang source](https://github.com/KhronosGroup/glslang/blob/main/glslang/HLSL/hlslParseHelper.cpp)). In this fixture, authored `column_major float4x4 Transform` reflects as `RowMajor=true`, columns 4, vector width 4; authored `row_major float3x4 RowMajorTransform` reflects as `RowMajor=false`, columns 3, vector width 4. The latter is at byte offset 80, size 48, matrix stride 16.
- One-dimensional uniform arrays are reflected with their exact compiler stride. Multidimensional arrays and arrays nested under an array-of-struct are rejected before recursive struct-name requirements when the DTO's single `ArrayStride` cannot represent the layout exactly.
- Each active combined image/sampler pair has a name derived from both bindings. Unsupported storage images, runtime arrays/storage buffers, nonzero descriptor sets, missing bindings, and ambiguous binding identities fail explicitly rather than receiving synthesized metadata.
- The installed `ResourcesGetResourceListForType` wrapper returns `Result`; every callsite now passes its result through `Check`. The final focused build emitted no `CS0162` warning.

## Probe output

The explicit opt-in reflection test exported the compiler-returned GLSL unchanged and a metadata-only JSON projection:

| File | Size | SHA-256 |
| --- | ---: | --- |
| `VS.glsl` | 1,894 bytes | `D6FE4B67D7391AF364A5469D4613396AAB69369C86C7BF623082B3ECCC0FEE44` |
| `PS.glsl` | 1,143 bytes | `6EE2F33C9D69ABA93AC187F1B9E8FF4C60F24BFF24607147972919EE4DFC0FC6` |
| `reflection.json` | 8,992 bytes | `6832C2BA9B9D42A390AAA951CADDA0EA1D251FE44D76FB2807A9CFEDEF79F7EA` |

Output root: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task-2-probe`.

The JSON records vertex inputs `POSITION`, `TEXCOORD0`, `COLOR0`, user outputs `COLOR0`, `TEXCOORD0`, and built-in output `Position`. The nested uniform generated paths are:

- `MaterialPrimary@b1.Primary.Tint` → `hel_ps3_uniform_buffer_binding_1.hel_ps3_uniform_type_MaterialPrimary_member_0.hel_ps3_uniform_type_SharedTint_member_0`
- `MaterialSecondary@b2.Secondary.Tint` → `hel_ps3_uniform_buffer_binding_2.hel_ps3_uniform_type_MaterialSecondary_member_0.hel_ps3_uniform_type_SharedTint_member_0`

The pixel samplers are `Albedo@t0 + LinearSampler@s0` → `hel_ps3_sampler_t0_s0` and `Albedo@t0 + PointSampler@s1` → `hel_ps3_sampler_t0_s1`, both `Dim2D`. The probe directory also contains the parent's `.vpo`, `.fpo`, and `.dump` files; this run wrote only the two GLSL files and `reflection.json`, leaving SDK outputs untouched. The parent subsequently reran pinned Docker `cgcomp -v/-f -d -Wcg,-oglsl` validation against these final GLSL files at 13:33: both stage conversions exited 0 (`VS.vpo` 1,296 bytes; `PS.fpo` 272 bytes), and the parent confirmed the final dumps/hashes. This agent did not execute Docker. The parent-created `task-2-review-snapshot.md` is the review snapshot for this handoff.

### Reproduce the explicit export and focused suite

```powershell
$env:HELENGINE_PS3_SHADER_REFLECTION_OUTPUT = 'C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task-2-probe'
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter FullyQualifiedName~Ps3Shader --logger "console;verbosity=normal" --verbosity quiet
```

The explicit opt-in test is `Ps3ShaderReflectionTests.EmitsComplexFixtureGlslForNativeSdkProbeWhenOutputRootIsSet`; when the environment variable is unset it writes no probe files.

## Verification

Final focused command above: **29 passed, 0 failed**. This includes the Task 1 source-compiler/include tests and Task 2 reflection, layout-boundary, sampler, unsupported-resource, and opt-in export tests. Log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task-2-focused-final.log`. The parent's final pinned Docker run also passed both stage conversions as noted above.

Known handoff note: the pixel translation currently lists constant-buffer metadata also present in the module but unused by the pixel stage. The parent's Cg dump marks those entries inactive (`final=0`); the Task 4 reconciler owns that filtering, and no Task 3/4 behavior was added here.

## Round 1/5 review fix — reflection probe test and export-path guard

`EmitsComplexFixtureGlslForNativeSdkProbeWhenOutputRootIsSet` now always compiles both optimized stages and asserts nonempty GLSL, the vertex `Position` built-in (separate from user outputs), both nested uniform generated paths in the vertex GLSL, and both pixel sampler generated names in the pixel GLSL. The environment variable now controls only whether the already-validated results are exported.

Export roots are accepted only when their canonical absolute path is a descendant of the standard sibling `builds/helengine-ps3` directory. The repository root is derived from the compiled test source with `CallerFilePath`; containment uses a directory-separator boundary and an OS-appropriate path comparison. Pure path tests cover valid build-id roots and nested roots, an in-root canonicalized path, and rejection of relative paths, the build base itself, traversal outside, a prefix-sibling path, and the system temp root. The rejected-path checks do not create directories.

Final test commands (the build artifacts remained in the approved `dotnet-green-3` directory):

```powershell
$env:HELENGINE_PS3_SHADER_REFLECTION_OUTPUT = $null
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter FullyQualifiedName~Ps3ShaderReflectionTests --logger "console;verbosity=normal" --verbosity quiet

$env:HELENGINE_PS3_SHADER_REFLECTION_OUTPUT = 'C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task-2-probe'
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter FullyQualifiedName~Ps3ShaderReflectionTests --logger "console;verbosity=normal" --verbosity quiet
```

Both final runs passed **13/13**, with no compiler warnings. Logs: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task2-fix-round1-env-unset-final.log` and `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task2-fix-round1-export-final.log`. The explicit run refreshed only `VS.glsl`, `PS.glsl`, and `reflection.json` under `task-2-probe`; test code did not alter emitted GLSL, so no Docker rerun was requested. The parent's final pinned-SDK conversion evidence above remains applicable.
