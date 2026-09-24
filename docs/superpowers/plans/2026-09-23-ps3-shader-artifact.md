# PS3 cooked shader artifact Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Serialize an already validated authored shader pair into a deterministic, versioned artifact that the PS3 native loader can consume.

**Architecture:** A small identity type calculates the material lookup path; a writer consumes the existing Ps3ShaderProgramPair without compiling anything. Big-endian metadata surrounds untouched SDK stage blobs. This slice is independent of the new shared backend and does not edit it.

**Tech Stack:** .NET 9, System.Buffers.Binary, SHA-256, strict UTF-8, xUnit.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md`; the exact wire table below freezes its proposal for format version 1.

## Global Constraints

- Work directly in the existing PS3 checkout; no worktrees, sibling repository writes or push.
- Luna implements. Parent owns review and commit. No nested agents.
- Never modify generated shader text, binaries or headers after compilation.
- Only the files named in this task may be edited; backend worker owns separate files.
- Do not compile or invoke Docker in this serializer task; consume already validated pair objects.
- All test outputs/logs go below C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/artifact. Redirect TEMP/TMP to its fixture-storage child while testing and restore afterwards.
- One type per file, substantive XML documentation for all members, PascalCase fields, no tuples/local functions/nullable-reference annotations.

## Review Focus

1. Different component strings containing delimiters must never alias the same lookup key.
2. UTF-8 lengths count bytes, not UTF-16 characters; reject malformed Unicode and embedded NUL.
3. Physical source offsets/strides and actual hardware slots must be preserved, without shared-policy shifts.
4. Length/count overflow must fail before a successful artifact is returned; no partial output files exist because the writer returns bytes.
5. Stage blobs and compiled metadata must remain unchanged; changing a returned byte array must not mutate the pair.

### Task 1: Identity and deterministic pair writer

**Files:**
- Create `builder/shaders/Ps3ShaderArtifactIdentity.cs`.
- Create `builder/shaders/Ps3ShaderArtifactWriter.cs`.
- Create `builder.tests/Ps3ShaderArtifactTests.cs`.
- Create `builder.tests/Ps3ShaderArtifactTestFixture.cs` only if needed to keep synthetic fixture construction separate.
- Create `docs/Ps3ShaderArtifactFormat.md` with the wire table and limits from this brief.

**Interfaces:**

```csharp
public sealed class Ps3ShaderArtifactIdentity {
    public Ps3ShaderArtifactIdentity(string shaderAssetId, string vertexProgramName,
        string pixelProgramName, string variantName);
    public string ShaderAssetId { get; }
    public string VertexProgramName { get; }
    public string PixelProgramName { get; }
    public string VariantName { get; }
    public string RelativePath { get; }
}
public static class Ps3ShaderArtifactWriter {
    public static byte[] Write(Ps3ShaderArtifactIdentity identity, string sourceHash,
        string vertexEntryPoint, string pixelEntryPoint, Ps3ShaderProgramPair pair);
}
```

Existing Ps3ShaderProgramPair supplies Vertex, Pixel, VertexTranslation, PixelTranslation and Bindings. Existing program objects supply copied Bytecode, ToolchainIdentity and Parameters. Uniform mappings have Stage, SourceMember and RsxParameter. Attribute mappings have SourceVariable, RsxParameter and HardwareSlot. Sampler entries have SourceSampler, RsxParameter and TextureUnit. Do not change these types or create a parallel pair representation.

- [ ] Write failing tests first. Construct synthetic immutable pair objects through test-visible internal constructors solely to test serialization; arbitrary nonempty byte sequences here test opaque preservation, not SDK validity. Production pairs are returned by the existing validated compiler. Do not call that compiler in pure serialization tests.

```csharp
Ps3ShaderArtifactIdentity a = new("a|b", "c", "d", "v");
Ps3ShaderArtifactIdentity b = new("a", "b|c", "d", "v");
Assert.NotEqual(a.RelativePath, b.RelativePath);
byte[] bytes = Ps3ShaderArtifactWriter.Write(identity, "source-hash", "VS", "PS", pair);
Assert.Equal(new byte[] { 0x50, 0x33, 0x53, 0x50 }, bytes[..4]);
Assert.Equal(1u, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(4, 4)));
Assert.Equal((uint)bytes.Length, BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(8, 4)));
Assert.Equal(bytes, Ps3ShaderArtifactWriter.Write(identity, "source-hash", "VS", "PS", pair));
```

- [ ] Run focused test with `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/artifact/dotnet --filter FullyQualifiedName~Ps3ShaderArtifactTests --verbosity quiet`; capture expected RED. Another worker may build the same solution with a different --artifacts-path: do not touch its output root or product files.

- [ ] Implement strict text validation (required strings nonblank, no NUL, valid UTF-8, at most 65536 bytes). Calculate RelativePath as `shaders/` plus lowercase SHA-256 hex plus `.ps3sp`. Hash input is the four identity strings in constructor order, each preceded by its unsigned 32-bit big-endian UTF-8 byte length. Source hash and toolchain identity do not enter the path key.

```csharp
byte[] encoded = new UTF8Encoding(false, true).GetBytes(value);
Span<byte> length = stackalloc byte[4];
BinaryPrimitives.WriteUInt32BigEndian(length, checked((uint)encoded.Length));
// Feed length then encoded bytes to SHA-256, once per identity component.
```

- [ ] Implement the writer using checked arithmetic and BinaryPrimitives. A bounded MemoryStream is sufficient if every write checks the resulting length against 16 MiB before growing. No filesystem writes. Primitive U32 is big-endian; String is U32 byte count followed by strict UTF-8 bytes, no NUL/padding. Blob is U32 byte count followed by bytes. List is U32 count followed by records. Header is raw `P3SP`, U32 version=1, U32 exact total size (patch this container field only after writing; never patch stage blobs).

Exact record order:

| Record | Fields, in exact order |
| --- | --- |
| Pair body | String shaderAssetId, String vertexProgramName, String pixelProgramName, String variantName, String sourceHash, String toolchainIdentity, Vertex Stage record, Pixel Stage record, List Attribute, List PixelSampler |
| Stage | U32 stage (0 vertex, 1 pixel; explicit format values, not enum casts), String entryPoint, Blob SDK program, List Buffer, List Uniform |
| Buffer | String bufferName, U32 authoredBinding, U32 declaredByteSize |
| Uniform | String bufferName, U32 authoredBinding, String memberPath, U32 byteOffset, U32 byteSize, U32 arrayStride, U32 matrixStride, U32 rowMajor (0 or 1), String scalarType, U32 vectorWidth, U32 columns, U32 arrayLength, String generatedName, U32 SDK dataType, U32 SDK elementCount |
| Attribute | String fullAuthoredSemantic, String generatedName, String scalarType, U32 vectorWidth, U32 columns, U32 arrayLength, U32 hardwareSlot, U32 SDK dataType |
| PixelSampler | String textureName, U32 textureBinding, String samplerName, U32 samplerBinding, String generatedName, String dimension, U32 textureUnit, U32 SDK dataType |

Buffers come from the stage's Translation.UniformBuffers; uniforms come from pair.Bindings.Uniforms filtered by stage; attributes/samplers come from the pair binding map. Preserve their existing order, which the compiler defines deterministically. Do not infer buffer sizes from members. Preserve raw scalar/dimension names, offsets, strides, hardware slots and SDK type values exactly. No translated GLSL, assembly dump or duplicate relocation tables are serialized.

- [ ] Validate: nonnull arguments; required strings; both programs have same nonblank ToolchainIdentity; matching vertex/pixel translation and program stages; each stage blob is nonempty and at most 1 MiB; every list has at most 4096 records; total at most 16 MiB. Required buffer byte sizes are nonzero; reject duplicate buffer name or binding per stage. Every mapped uniform must reference exactly one corresponding declared buffer by both name and binding, have nonzero size/width/columns/arrayLength, and satisfy checked byteOffset+byteSize <= declared size. Reject duplicate uniform member paths within the same buffer/stage. Uniform generatedName must equal RsxParameter.Name and SDK category must be Constant. Attribute SDK category must be Attribute and generated name/index must match the map. Sampler SDK category must be Sampler and generated name/unit must match the map. Reuse existing enum names after a bounded source read.

Version 1 runtime subset: reject any vertex translation Samplers; accept pixel dimensions `Dim2D` and `DimCube` only. The real ForwardStandardShader dump contains both, even in the ForwardStandard variant. Reject other dimensions, duplicate physical attribute slots and pixel texture units. Fail visibly, with identity/resource context. No identity, field or binding defaults.

- [ ] Extend tests with an independent bounded test decoder using BinaryPrimitives (test helpers can be private methods, not local functions). Walk the full body to exact EOF and assert every record field, not just a round-trip through production code. Cover Unicode identity, embedded NUL/unpaired surrogate, delimiter ambiguity, missing required text, differing pair toolchains, vertex sampler rejection, positive DimCube and negative Dim3D pixel samplers, duplicate buffer/slot, out-of-range member, metadata limits, and byte-exact stage copies. Include a non-square row-major matrix with nonzero ArrayStride/MatrixStride in the synthetic mapping to prove preservation. Assert writing leaves all input DTO properties and Bytecode unchanged.

- [ ] Run focused GREEN and the pure suite once (`--filter Category!=Docker`) with a separate artifacts root. Report any unrelated concurrent backend failure accurately rather than editing another worker's files. Write the format documentation, self-review, and return exact files/test evidence. Do not commit; parent creates the scoped review package and commits named files only after validation.

## Self-review

Only one task touches these files. The existing immutable pair is the sole input interface; there is no dependency on the in-flight backend files. The native decoder is a separate required integration task and is not claimed delivered here. Header patching affects only the new container length, not generated SDK bytes. Tests independently inspect byte order and full records. Default policies never alter physical metadata.
