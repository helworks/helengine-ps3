# Task 4 report — paired shader bindings

## Result

Task 4 now translates, compiles and reconciles one compatible PS3 vertex/pixel request pair. Binding reconciliation uses canonical Cg/RSX channel names, then records each stage's own SDK slot; it never treats equal raw slot numbers as proof of a link. The shared Cg dump decoder is extracted from `Ps3RsxProgramReader` without moving or weakening binary-program validation. The unused `foundProfile` local was removed; it no longer appears in the reader and the final build logs contain no `CS0219` warning.

## Task 4 files

- `builder/shaders/Ps3CgDumpVariable.cs`
- `builder/shaders/Ps3CgDumpReader.cs`
- `builder/shaders/Ps3RsxProgramReader.cs` (narrow decoder extraction only)
- `builder/shaders/Ps3ShaderStageLink.cs`
- `builder/shaders/Ps3ShaderBindingMap.cs`
- `builder/shaders/Ps3ShaderBindingMapBuilder.cs`
- `builder/shaders/Ps3ShaderUniformBinding.cs`
- `builder/shaders/Ps3ShaderAttributeBinding.cs`
- `builder/shaders/Ps3ShaderSamplerMapEntry.cs`
- `builder/shaders/Ps3ShaderProgramPair.cs`
- `builder/shaders/Ps3ShaderPairCompiler.cs`
- `builder.tests/Ps3ShaderBindingMapTests.cs`
- `builder.tests/Ps3ShaderPairCompilerTests.cs`
- `builder.tests/Ps3ShaderPairCompilerStub.cs`
- `docs/superpowers/execution/ps3-hlsl-compiler/task-4-report.md`

## Implemented and tested subset

- Active vertex `Position` is mandatory and must be exactly the active `gl_Position : $vout.POSITION : HPOS` Cg route. Fragment `WPOS` is accepted only when source reflection includes `FragCoord`, and is not emitted as an authored inter-stage link. Pixel color outputs are checked against the supported Cg `COL`/`COL0`/`COL1` forms.
- Authored user varyings are currently limited to FP3232 scalar/vector values of width 1–4, with one column and array length 1. Their generated Cg name, Cg type, semantic, scalar/shape and interpolation are checked against source reflection and across stages. Unknown active routes and unsupported types/shapes fail explicitly.
- Canonical routes include the pinned named vertex channels and `TEX0`–`TEX7`; pixel input channels use their separate pinned slot table. An asymmetric `TEX0` test links vertex slot 7 to pixel slot 4. Channel identity, not numeric-slot equality, determines compatibility.
- Active attributes map to the matching SDK attribute record and hardware index. Active uniforms map by generated member identity to actual SDK constants, validate Cg/SDK type and physical element count, and retain stage, authored member path and relocation/register data. Optimized-out members are not synthesized. The decoder recognizes the pinned scalar/vector float forms and Cg matrix tokens `float3x3`, `float3x4`, `float4x3`, and `float4x4`; source scalar and physical shape must match.
- Active combined texture/sampler identities map to their SDK sampler records and distinct texture units. The mapper recognizes 1D, 2D, 3D, cube and rectangle dimensions; Task 4 tests exercised `Dim2D` only, so the other dimensions are not claimed as tested. The real modern fixture exercises a cbuffer member and two sampler identities with distinct units.
- Pair compilation rejects incompatible target, stage ordering, variant, shader model, ordered defines, or options/binding policy before starting work; logical program names are per-stage and need not match. It creates separate unique stage directories, forwards cancellation through both stage jobs, and returns only after both SDK programs and their binding map validate.
- The pinned-Docker pair fixture successfully compiles the modern texture/cbuffer input and validates three attributes, two linked varyings (`COLOR0` through `TEX0`, `TEXCOORD0` through `TEX1`), two sampler units and a fragment constant relocation. This is evidence for that nontrivial two-varying fixture, not a claim of general HLSL interface coverage.

No cache, backend selection, cooker, or runtime behavior is added. Inter-stage integer/non-FP3232 values, matrices, arrays and other unproven active forms remain explicitly unsupported.

## Test evidence

All commands ran from `C:\dev\helworks\helengine-ps3` using the approved artifacts path `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3` and environment values:

```powershell
$env:HELENGINE_PS3_REPOSITORY_ROOT='C:/dev/helworks/helengine-ps3'
$env:HELENGINE_PS3_SHADER_TEST_OUTPUT='C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task4-tests'
```

The test-first logs record the initial missing-map RED, the expanded mapping and pair REDs, the WPOS RED, then the intermediate GREEN runs: `task4-red-1.log`, `task4-green-1.log` (22/22), `task4-red-2.log`, `task4-pair-red-1.log`, `task4-pair-red-2.log`, `task4-wpos-red.log`, `task4-green-3.log` (35/35), `task4-pure-1.log` (41/41), and `task4-docker-pair-1.log` (1/1). All are under `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923`.

The final focused commands were:

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter '((FullyQualifiedName~Ps3ShaderBindingMapTests|FullyQualifiedName~Ps3ShaderPairCompilerTests|FullyQualifiedName~Ps3RsxProgramReaderTests)&Category!=Docker)' --verbosity quiet
```

Result: 46 passed, 0 failed. Log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task4-final-pure.log`. This includes the prior binary-reader regressions after decoder extraction.

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/dotnet-green-3 --filter '((FullyQualifiedName~Ps3DockerShaderCompilerTests|FullyQualifiedName~Ps3ShaderPairCompilerTests)&Category=Docker)' --verbosity quiet
```

Result: 7 passed, 0 failed (the six prior Docker compiler tests plus the real paired-shader integration). Log: `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923\task4-final-docker.log`. The SDK conversions ran through the pinned Docker toolchain.

## Interim scalar assertion diagnosis

The first final-pure run was 45/46; its sole failing test was `RejectsVaryingScalarTypeMismatch`. That test expected `InvalidOperationException` for a reflected pixel `SINT32` varying. After adding source/Cg declaration agreement validation, the earlier unsupported-type check runs first: `GetFloatTypeToken("SINT32", ...)` throws `NotSupportedException` with `Active PS3 source scalar type 'SINT32' has no documented Cg/RSX mapping.` The assertion's expected exception was stale; this was not a failed valid FP3232 link. The regression was changed to `RejectsUnsupportedPixelVaryingScalarType`, expecting `NotSupportedException`; a separate regression confirms matching SINT32 metadata on both stages is rejected as well. The focused scalar test passed 1/1 in `task4-scalar-type-green.log`. The initial 45/46 console log was written to `task4-final-pure.log`, then overwritten by the green rerun at that same path, so the failure is described here rather than retained as a separate log.

## Review fix round 1

- `BuildAttributes` now rejects active vertex input reflection with `ArrayLength != 1` using `NotSupportedException` before mapping to an SDK attribute. It does not add vertex attribute array support. The regression uses array length 2 and preserves the existing scalar/vector attribute mapping test. RED: `task4-review-r1-attribute-red.log`; focused GREEN: `task4-review-r1-targeted-green-2.log` (2/2, together with the pair-name regression).
- Pair compatibility no longer requires equal logical `ProgramName` values, which are per-stage. The shared variant requirement and target, stage ordering, shader model, ordered defines, options and binding-policy checks remain. `CompilesPairWithDistinctStageProgramNames` uses `asset.vs`/`asset.ps`, reaches both stage jobs, and returns a valid pair. RED before removing the equality check: `task4-review-r1-program-name-red.log`.
- The first version of that pair regression used a vertex fixture with an active `POSITION` input, but the pair stub intentionally emits only `gl_Position`; after the name check was removed, map construction correctly failed with `Active vertex attribute 'hel_ps3_input_location_0_POSITION' has no Cg declaration.` The test was narrowed to an input-free vertex source; no product change was made for this fixture mismatch. The diagnostic is preserved in `task4-review-r1-program-name-diagnostic.log`; the interim 1/2 run is `task4-review-r1-targeted-green.log`; corrected targeted run is 2/2 in `task4-review-r1-targeted-green-2.log`.
- Coverage wording is corrected: dimension-token mapping recognizes 1D, 2D, 3D, cube and rectangle, while Task4 tests exercised `Dim2D` only. No extra dimension tests or features were added.
- Final post-fix pure binding/pair plus binary-reader suite: 48 passed, 0 failed, `task4-review-r1-final-pure.log`. Final pinned-Docker suite: 7 passed, 0 failed (six prior compiler tests plus the real pair integration), `task4-review-r1-final-docker.log`. All new logs are in `C:\dev\helworks\builds\helengine-ps3\shader-integration-20260923`; no previous failure logs were overwritten during this fix round.

## Remaining boundary

There is no Task 4 blocker. Any expansion to inter-stage arrays/matrices, integer or other scalar encodings, or additional active Cg routes needs new SDK-format evidence and belongs to a later scoped change. No generated GLSL, dumps or SDK binaries were rewritten.
