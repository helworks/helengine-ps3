# Native P3SP shader artifact reader

`helengine::ps3::Ps3ShaderArtifactReader` turns one version-1 P3SP artifact into an owning CPU snapshot and checks its metadata against the exact vertex and pixel SDK program blobs. The reader performs no GPU allocation, command submission, shader selection, fallback, or drawing. The original stage bytecode is copied byte-for-byte into the snapshot.

The serialized wire layout is specified in [Ps3ShaderArtifactFormat.md](Ps3ShaderArtifactFormat.md). The reader deliberately accepts that bounded format and the pinned SDK's raw RSX program layout; it is not a general-purpose SDK binary parser.

## Calling the reader

```cpp
helengine::ps3::Ps3CookedShaderProgram program;
std::string error;
if (!helengine::ps3::Ps3ShaderArtifactReader::Read(data, size, program, error)) {
    // Keep and report the diagnostic; program is unchanged on failure.
}
```

`Read` returns `true` only after the complete artifact, both stage blobs, all SDK structures, and all artifact-to-SDK mappings validate. On success it clears `error` and replaces `program` with the fully checked candidate. On failure it reports a diagnostic through `error` and leaves the caller's existing `program` untouched. A null input, malformed field, unsupported format/type, inconsistent mapping, or standard allocation/parsing exception is a failure rather than a request to choose a default resource.

The decoder enforces these resource bounds before reserving or copying data:

- Complete artifact: 12 bytes through 16 MiB.
- Each stage SDK blob: 1 byte through 1 MiB.
- Serialized strings: at most 65,536 bytes and strict UTF-8 (no embedded NUL, overlong encoding, surrogate, or truncated sequence).
- Each serialized record list: at most 4,096 entries.

All integer fields are read as bounded big-endian bytes. Range checks use checked-width arithmetic; the parser does not use unaligned typed reads or SDK pointer casts.

## Owning snapshot and stable field names

The DTOs are CPU-side values in `helengine::ps3`; their strings and bytecode own their storage and do not alias the input artifact. These exact public names are the handoff for later runtime work:

| Record | Public fields |
| --- | --- |
| `Ps3CookedShaderProgram` | `ShaderAssetId`, `VertexProgramName`, `PixelProgramName`, `VariantName`, `SourceHash`, `ToolchainIdentity`, `Vertex`, `Pixel`, `Attributes`, `PixelSamplers` |
| `Ps3CookedShaderStage` | `Stage`, `EntryPoint`, `Bytecode`, `Buffers`, `Uniforms` |
| `Ps3CookedShaderBuffer` | `BufferName`, `AuthoredBinding`, `DeclaredByteSize` |
| `Ps3CookedShaderUniform` | `BufferName`, `AuthoredBinding`, `MemberPath`, `ByteOffset`, `ByteSize`, `ArrayStride`, `MatrixStride`, `RowMajor`, `ScalarType`, `VectorWidth`, `Columns`, `ArrayLength`, `GeneratedName`, `SdkDataType`, `SdkElementCount` |
| `Ps3CookedShaderAttribute` | `FullAuthoredSemantic`, `GeneratedName`, `ScalarType`, `VectorWidth`, `Columns`, `ArrayLength`, `HardwareSlot`, `SdkDataType` |
| `Ps3CookedShaderSampler` | `TextureName`, `TextureBinding`, `SamplerName`, `SamplerBinding`, `GeneratedName`, `Dimension`, `TextureUnit`, `SdkDataType` |

The stage `Bytecode` vectors preserve the SDK program exactly. Parsed SDK records used during validation are private implementation details and do not replace or normalize those bytes.

## Validation performed

The reader checks the artifact's exact magic, version, declared total length, stage order, field bounds, and absence of trailing data. It validates required identities and stage entry points, string encodings, list limits, and each DTO's structural constraints before asking `Ps3RsxProgramValidator` to inspect the raw SDK programs.

The SDK validator checks vertex/pixel magic and header ranges; ordered, non-overlapping attribute, constant, and microcode regions; 16-byte microcode alignment; bounded, terminated SDK names; attribute/constant table record structure; and the 512-register tested vertex bound. Fragment relocation tables must be in bounds, disjoint from SDK tables and microcode, and contain aligned complete writes for the head record's SDK data type. Repeated relocations within one constant element are preserved and allowed; overlapping writes owned by distinct active mapped pixel uniform elements are rejected.

Source metadata is cross-checked rather than used as a fallback policy:

- Buffers have nonempty unique names, unique authored bindings, and nonzero declared sizes.
- Uniforms identify exactly one buffer by name and authored binding, fit its declared byte range, have valid shape/row-major metadata, and match a uniquely named SDK constant group's type and element count.
- Vertex attributes match their generated SDK parameter names, data types, and actual hardware slots.
- Pixel samplers match the SDK sampler parameter name, type, and actual texture unit.

The accepted producer scalar label is the exact `FP3232` spelling. Matrix/array stride and row-major metadata remain authored values; the reader does not transpose or synthesize layout. Unsupported scalar labels, flags, sampler dimensions, SDK types, duplicate identities, missing mappings, and mismatched indices/types fail visibly.

## Scope boundary

This component provides validated CPU metadata and untouched SDK program bytes only. It does not create textures or buffers, allocate GPU program state, patch microcode, issue graphics commands, choose a built-in shader, or establish that authored shader rendering works. Those are later runtime/GPU acceptance tasks.
