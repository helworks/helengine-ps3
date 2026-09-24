# PS3 Material Shader Envelope Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Make the existing PS3 standard material cooker emit an explicit, bounded shader-pair reference and its authored surface state for the forthcoming native loader.

**Architecture:** Replace the generic PlatformMaterialAsset serialization produced by the PS3 cooker with a PS3-owned P3MT version-1 envelope. The existing PS3 standard schema selects the actual shared ForwardStandardShader pair; its dependency and serialized identity must be identical. This task supplies the producer contract, not renderer acceptance or feature enablement.

**Tech Stack:** C# net9.0, xUnit, existing shared material-cook interfaces and Ps3ShaderArtifactIdentity.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md`.

## Global Constraints

- Parent writes plans; Luna implements. Same checkout, no worktrees, no nested agents.
- No Core, generated serializer, editor, authored scene, shared shader, SDK or renderer edits in this task.
- Preserve unrelated Makefile/build-script edits and concurrent native-reader work.
- Keep the shaders feature disabled. Changing the material format is an intermediate integration step, not an assertion that existing native loading accepts it.
- Only named validated files may be committed, by the parent; no push.
- One type per file, substantive documentation for every type/function/field, PascalCase fields, no tuples/local functions/nullable-reference annotations.
- Outputs and redirected TEMP/TMP belong under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/material-envelope`.
- No generated-output edits. This is a source producer; negative tests use in-memory values.

## Review Focus

- Dependency and payload identity drift: assert all four identity strings and hash-derived path match the returned dependency.
- Loss of authored surface state: independently decode every byte channel and all three choice bits, including non-default combinations.
- Unsafe texture paths: reject absolute, drive-relative, traversal, backslash, empty segments and NUL before producing bytes; absence remains valid.
- Invalid text: strict UTF-8, reject NUL and unpaired surrogates, explicit byte-size limits, no replacement characters.
- Accidental custom-shader fallback: this schema selects the standard pair; an explicit enabled custom-shader request is rejected, not silently replaced.

## Wire contract

All numbers below are U32 big-endian except the four literal magic bytes and four color bytes. Strings are strict UTF-8 preceded by U32 byte length, with no terminator/padding. Maximum entire envelope is 1 MiB; maximum any string is 65536 UTF-8 bytes. Required strings must be nonempty/non-whitespace, no embedded NUL or invalid UTF-16 input. Texture path is the only optional string.

| Order | Field | Value/meaning |
|---|---|---|
| 1 | Magic | ASCII P3MT |
| 2 | Version | 1 |
| 3 | TotalByteLength | Exact envelope length including header |
| 4 | MaterialAssetId | Exact request ID |
| 5 | RendererFamilyId | ps3-standard |
| 6 | ShaderAssetId | ForwardStandardShader |
| 7 | VertexProgramName | ForwardStandardShader.vs |
| 8 | PixelProgramName | ForwardStandardShader.ps |
| 9 | VariantName | ForwardStandard |
| 10 | ShaderRelativePath | Ps3ShaderArtifactIdentity.RelativePath for fields 6-9 |
| 11 | BaseColor | R,G,B,A, four raw bytes |
| 12 | SurfaceFlags | bit0 Lit, bit1 DoubleSided, bit2 UseVertexColor; other bits zero |
| 13 | DiffuseTextureRelativePath | Exact validated content-relative path, or empty |

No roughness/metallic/specular fields are added to the existing PS3 schema in this slice. Runtime provisioning must use the documented shared standard defaults for fields the schema does not expose; any future authored extension needs a separate explicit contract. No implicit shader fallback is encoded. Surface flags are preserved even where runtime support still needs implementation; the reader/renderer must not silently discard them.

Texture paths use forward slashes and are relative to the supplied content stream. Reject leading/trailing slash, backslashes, colon, NUL, blank path components, `.` or `..` components, and control characters. Do not normalize invalid paths into valid ones. Preserve valid case and spelling. Empty means the intentional default white diffuse texture; nonempty means required content, never fallback on load failure.

The editor's nonstandard-schema `variant=Mesh` handoff is not a PS3 variant selector. This fixed PS3 standard schema owns its pair selection and always returns ForwardStandard. Do not reinterpret an unrelated `variant` field. An explicitly true `use-custom-shader` field is unsupported in this schema and must fail with an actionable message; malformed nonblank boolean values also fail. False/absent leaves the standard selection. No claim of general custom-material provisioning follows from the existing general-purpose compiler.

## Task 1: Produce P3MT and its exact shader dependency

**Files:**
- Create `builder/materials/Ps3MaterialArtifactWriter.cs`.
- Modify `builder/Ps3MaterialCooker.cs`.
- Modify `builder.tests/Ps3MaterialCookerTests.cs`.
- Create `builder.tests/Ps3MaterialArtifactWriterTests.cs`.
- Create `docs/Ps3MaterialArtifactFormat.md`.

**Interfaces:**
- Consume `PlatformMaterialCookRequest`, `PlatformMaterialCookResult`, `PlatformShaderDependency` from the existing baseplatform project.
- Consume `Ps3ShaderArtifactIdentity` without changing it.
- Produce `public static byte[] Ps3MaterialArtifactWriter.Write(string materialAssetId, byte red, byte green, byte blue, byte alpha, bool lit, bool doubleSided, bool useVertexColor, string diffuseTextureRelativePath)`.
- Produce `public static Ps3ShaderArtifactIdentity Ps3MaterialArtifactWriter.CreateShaderIdentity()` so dependency selection and serialization share exactly one source of identity constants. Return an immutable identity instance; no global mutable state.
- Preserve `Ps3MaterialCooker.Cook(PlatformMaterialCookRequest)` signature and current platform/schema/profile/color/choice validation.

- [ ] **Step 1: Write producer and cooker regression tests before implementation.**

Tests independently decode bytes using `BinaryPrimitives.ReadUInt32BigEndian` plus `new UTF8Encoding(false, true)`, with a cursor that asserts final offset equals total length. Do not use a production reader to prove the writer. Pin the first 12 bytes, exact field order and final offset.

```csharp
Ps3ShaderArtifactIdentity identity = Ps3MaterialArtifactWriter.CreateShaderIdentity();
Assert.Equal("ForwardStandardShader", identity.ShaderAssetId);
Assert.Equal("ForwardStandardShader.vs", identity.VertexProgramName);
Assert.Equal("ForwardStandardShader.ps", identity.PixelProgramName);
Assert.Equal("ForwardStandard", identity.VariantName);
byte[] bytes = Ps3MaterialArtifactWriter.Write("Materials/Cube", 192, 32, 16, 64,
    true, true, true, "cooked/textures/fixture.hasset");
Assert.Equal(new byte[] { 80, 51, 77, 84 }, bytes.Take(4));
Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(4, 4)));
Assert.Equal((uint)bytes.Length, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(8, 4)));
```

The complete test matrix must include deterministic repeated writes; exact Unicode material-ID roundtrip; all flags individually and combined; transparent RGBA; optional empty texture; valid nested texture path; every invalid path class above; material ID blank/NUL/unpaired-surrogate/over-limit; texture over-limit; null material ID and null texture path rejected; and maximum allowed string boundary. The aggregate limit must be enforced in production even though this fixed small record cannot exceed 1 MiB when all individual limits hold.

Adapt existing cooker tests to decode P3MT instead of AssetSerializer. Preserve all current color/default/foreign schema/profile/invalid-choice coverage. Replace the old no-dependency assertion with exactly one complete dependency; replace texture-refusal with preserved valid path and invalid-path rejection. Add explicit custom=true rejection, custom=false success, malformed custom boolean rejection, and unrelated editor variant=Mesh still choosing ForwardStandard. Assert the dependency's four fields equal the decoded envelope and its path recomputes identically with Ps3ShaderArtifactIdentity.

- [ ] **Step 2: Run the focused RED gate and retain its exact failure.**

```powershell
dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/material-envelope/dotnet --filter 'FullyQualifiedName~Ps3MaterialCookerTests|FullyQualifiedName~Ps3MaterialArtifactWriterTests' --verbosity quiet --logger 'trx;LogFileName=material-red.trx' --results-directory C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/material-envelope/results
```

Redirect TEMP/TMP to the task's fixture-storage first, restoring in finally. If missing production symbols prevent compiling, temporarily add only signatures throwing NotImplementedException, then run a behavioral RED. An unrelated concurrent compile error is not valid RED; report and coordinate without editing the other worker's files.

- [ ] **Step 3: Implement the bounded writer and integrate the cooker.**

Use checked arithmetic, strict UTF-8, explicit byte limits and manual BE writes (or a private writer helper within the same class). Validate all inputs before publishing the resulting byte array. Never use BinaryWriter's platform-endian numeric methods. Do not serialize through generic AssetSerializer.

```csharp
Ps3ShaderArtifactIdentity shaderIdentity = Ps3MaterialArtifactWriter.CreateShaderIdentity();
byte[] payload = Ps3MaterialArtifactWriter.Write(request.MaterialAssetId,
    red, green, blue, alpha, lit, doubleSided, useVertexColor, texturePath);
PlatformShaderDependency dependency = new(shaderIdentity.ShaderAssetId,
    shaderIdentity.VertexProgramName, shaderIdentity.PixelProgramName, shaderIdentity.VariantName);
return new PlatformMaterialCookResult(payload, new[] { dependency });
```

Preserve the current six PS3 schema defaults and existing parsers. Remove only the obsolete no-texture rejection and generic asset construction. Correct comments claiming that the standard packager does not stage textures: its current handoff already provides a canonical cooked texture path. Do not change the published schema or builder's optional-interface registration.

- [ ] **Step 4: Run focused GREEN and the non-Docker builder suite.**

Repeat Step 2 with `material-green.trx`; then run the same project/artifacts root with `--filter 'Category!=Docker'` and `material-pure.trx`. Confirm the actual repository's Docker trait convention before using that filter; use its established filter if different. Report exact passed/failed/skipped counts and command lines. No SDK execution is required for this byte producer.

- [ ] **Step 5: Document and report for parent review.**

Document the exact wire table, bounds, path rules, fixed standard selection, defaults, dependency identity and native migration requirement in `docs/Ps3MaterialArtifactFormat.md`. State that native loading/rendering is not yet accepted. Report changed files, RED/GREEN evidence and concerns under this plan's `.superpowers/sdd` directory. Do not commit or edit any generated outputs.

## Parent self-review and next boundary

This plan intentionally changes only the material producer and tests; the next native material-reader/program-cache integration must consume the exact P3MT table above. It must retain surface flags, verify P3SP identity against P3MT, fail required texture loads visibly, and load via IContentStreamSource. A shader-enabled build is not accepted until real cube_test and the texture fixture pass. No generated Core change is required by this envelope.
