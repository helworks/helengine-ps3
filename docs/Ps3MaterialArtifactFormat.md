# PS3 Material Artifact Format

The PlayStation 3 standard material cooker writes a PS3-owned `P3MT` version 1 envelope. It carries the material identifier, the fixed standard shader pair identity, authored base color and surface choices, and an optional cooked diffuse texture path. The cooker also reports one shader dependency with the same four identity strings serialized in the envelope.

The native reader validates this envelope; full scene rendering acceptance remains separate and the PS3 shader feature stays disabled.

## Wire format

All integers are unsigned 32-bit big-endian values. Strings are a U32 UTF-8 byte length followed immediately by those bytes, with no terminator, BOM or padding. The magic and color channels are raw bytes.

| Order | Field | Encoding and value |
|---:|---|---|
| 1 | Magic | Four bytes: ASCII `P3MT` |
| 2 | Version | U32: `1` |
| 3 | TotalByteLength | U32: exact complete envelope size, including the 12-byte header |
| 4 | MaterialAssetId | String: exact material request identifier |
| 5 | RendererFamilyId | String: `ps3-standard` |
| 6 | ShaderAssetId | String: `ForwardStandardShader` |
| 7 | VertexProgramName | String: `ForwardStandardShader.vs` |
| 8 | PixelProgramName | String: `ForwardStandardShader.ps` |
| 9 | VariantName | String: `ForwardStandard` |
| 10 | ShaderRelativePath | String: packaged path from `Ps3ShaderArtifactIdentity.RuntimeRelativePath` for fields 6–9 |
| 11 | BaseColor | Four raw bytes in R, G, B, A order |
| 12 | SurfaceFlags | U32: bit 0 is Lit, bit 1 is DoubleSided, bit 2 is UseVertexColor; all other bits are zero |
| 13 | DiffuseTextureRelativePath | String: validated content-relative path, or a zero-byte empty string |

The identity path is the lowercase SHA-256 lookup path derived by `Ps3ShaderArtifactIdentity` from the exact four identity strings using length-prefixed UTF-8 components. The material dependency and payload use those same values. Compiler source hashes and toolchain identity are not part of material lookup identity.

There are two explicit path domains. RelativePath remains shaders/<identity-hash>.ps3sp
relative to CookRootPath and remains the declaration's LogicalArtifactId.
RuntimeRelativePath is cooked/ + RelativePath. Declaration RelativePath and the
P3MT reference use that runtime path, matching normal packaged content lookup.
The physical shader still goes to CookRootPath/shaders; neither identity hashes nor
P3SP/SDK bytes change. The fixed standard runtime reference is
cooked/shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp.
All materials must be recooked; the old unprefixed reference is rejected, without
cache rewriting or compatibility fallback.

## Bounds and validation

The complete envelope is limited to 1 MiB (`1,048,576` bytes); any single string is limited to `65,536` UTF-8 bytes. Length arithmetic is checked. Required strings must be nonempty and non-whitespace, must not contain NUL, and must encode as strict UTF-8. Invalid UTF-16, including an unpaired surrogate, is rejected rather than replaced. The optional texture string may be empty.

A nonempty texture path is relative to the supplied content stream and uses forward slashes. The writer rejects leading or trailing slashes, backslashes, colons, NUL, control characters, empty or whitespace-only components, and `.` or `..` components. It preserves valid case and spelling and does not normalize an invalid path. An empty texture path means the intentional default white diffuse texture. A nonempty path names required content; failure to load it must remain visible and must not trigger a fallback.

## Selection and authored defaults

The PS3 standard schema owns the fixed `ForwardStandardShader` pair and always selects `ForwardStandard`. A general editor `variant=Mesh` field is unrelated to PS3 shader selection and is ignored. An explicit `use-custom-shader=true` request is unsupported and fails with an actionable error; absent or false leaves the standard pair selected, while malformed nonblank boolean text fails validation.

The schema and cooker defaults are `#FFFFFFFF` base color, empty texture path, lit lighting, double-sided false, and vertex-color mode `ignore`. Thus a default envelope has opaque white RGBA and surface flags `1`. An explicit `lighting-mode=unlit` still clears bit0; the binary layout is unchanged. This matches the standard material's scene-lighting behavior when no platform override is authored. Materials cooked with the previous Unlit default must be recooked. Roughness, metallic and specular values are not encoded by this schema; the runtime uses the shared standard shader's documented defaults for fields the schema does not expose. Adding authored controls requires a separate explicit contract.

## Native migration boundary

The native reader decodes this field order and retains every surface flag. It verifies
the fixed P3MT shader identity and runtime-relative canonical path; MatchesProgram
compares the referenced P3SP identity. Content loading uses IContentStreamSource and
reports required texture/shader failures without fallback. Shaders remain disabled
until the separate standard build/rendering acceptance gates pass.
