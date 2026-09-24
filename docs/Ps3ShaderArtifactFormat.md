# PS3 shader artifact format

P3SP is a deterministic, little-policy container for a validated PS3 vertex and pixel program pair. Version 1 stores the copied SDK bytecode and the source metadata needed by the native runtime. The serializer does not write files or alter the supplied pair.

All integer values are unsigned 32-bit big-endian values. Strings are strict UTF-8 with a U32 byte count and no terminator or padding. Blobs use a U32 byte count followed by the exact bytes. Lists use a U32 record count followed by records in the order supplied by the validated compiler. Boolean metadata is encoded as U32 `0` or `1`.

The 12-byte header is raw ASCII `P3SP`, U32 version `1`, and U32 exact total artifact size. The remaining bytes are the pair body. The writer patches only the total-size header field after completing the bounded body; SDK stage blobs are copied verbatim.

## Version 1 body

| Record | Fields, in exact order |
| --- | --- |
| Pair body | String shaderAssetId, String vertexProgramName, String pixelProgramName, String variantName, String sourceHash, String toolchainIdentity, Vertex Stage, Pixel Stage, List Attribute, List PixelSampler |
| Stage | U32 stage (`0` vertex, `1` pixel), String entryPoint, Blob SDK program, List Buffer, List Uniform |
| Buffer | String bufferName, U32 authoredBinding, U32 declaredByteSize |
| Uniform | String bufferName, U32 authoredBinding, String memberPath, U32 byteOffset, U32 byteSize, U32 arrayStride, U32 matrixStride, U32 rowMajor, String scalarType, U32 vectorWidth, U32 columns, U32 arrayLength, String generatedName, U32 SDK dataType, U32 SDK elementCount |
| Attribute | String fullAuthoredSemantic, String generatedName, String scalarType, U32 vectorWidth, U32 columns, U32 arrayLength, U32 hardwareSlot, U32 SDK dataType |
| PixelSampler | String textureName, U32 textureBinding, String samplerName, U32 samplerBinding, String generatedName, String dimension, U32 textureUnit, U32 SDK dataType |

Buffers come from the corresponding stage translation. Uniforms come from the binding map filtered to that stage. Attributes and pixel samplers come from their binding-map lists. Their order is preserved. Buffer sizes, offsets, strides, slots, dimensions, and SDK type values are source values; they are not inferred or renumbered. Translated GLSL, assembly dumps, and relocation tables are not part of this format.

## Limits and validation

| Value | Version 1 limit or rule |
| --- | --- |
| Encoded string | Required, nonblank, no NUL, valid Unicode/UTF-8, at most 65,536 bytes |
| SDK stage blob | Nonempty, at most 1 MiB per stage |
| Any serialized list | At most 4,096 records |
| Complete artifact | At most 16 MiB |
| Toolchain | Vertex and pixel identities must be the same nonblank string |
| Uniform buffer | Declared size must be nonzero; buffer name and binding are unique within each stage |
| Uniform mapping | Must identify exactly one declared buffer by both name and binding; size and shape are nonzero; checked byte range must fit; member path is unique within that buffer and stage; SDK category is Constant and generated names agree |
| Attribute mapping | SDK category is Attribute; generated name and physical slot agree with the map; physical slots are unique |
| Pixel sampler mapping | SDK category is Sampler; generated name and physical texture unit agree with the map; texture units are unique; dimension is `Dim2D` or `DimCube`, preserved exactly |
| Runtime subset | Vertex-stage source samplers are rejected |

Identity paths are `shaders/<lowercase SHA-256>.ps3sp`. The hash input is each of shaderAssetId, vertexProgramName, pixelProgramName, and variantName in constructor order, encoded as strict UTF-8 and preceded by its U32 big-endian byte length. Source hash and toolchain identity do not participate in that key.
