# Shared 2D batch native contract

Status: shared Task3 native entry gate accepted. Static layout, five writer cases, SDK-boundary projection, and two regenerated text cases passed. The array-adapter ownership correction is present in regenerated C++ and passed the executable rerun. Core preserves row-vector matrices; the SDK boundary packs columns. This establishes the shared batch contract, not native GPU rendering or actual-scene 2D acceptance.

## Generated surface captured

The generated root inspected was `C:/dev/helworks/builds/helengine-ps3/2d-batch-20260924/standard/cache/v2/d87d222cd166d9049967e97c50c8e301/b/ps3/release/release/generated-core`.

- `Batch2DWriter`: constructor `(Batch2DSink*, int32_t)`; `ConfigureCamera(float4x4, float4)`; `AppendQuad(Batch2DRun, Batch2DVertex, Batch2DVertex, Batch2DVertex, Batch2DVertex)`; `Flush()`; `Dispose()`.
- `Batch2DBuffer`: constructor `(int32_t)`; `Vertices` and `Indices` pointers; `VertexCount` and `IndexCount`; `Dispose()`.
- `Batch2DSink`: abstract `SetCamera(float4x4, float4)` and `Submit(Batch2DBuffer*, Batch2DRun)`.
- `Batch2DRun`: variant, borrowed `RuntimeTexture*`, then scissor X, Y, width, height; constructor takes those six values in that order.
- `Batch2DDrawableEmitter`: constructor `(Batch2DWriter*, RenderManager2D*)`; methods `EmitSprite`, `EmitText`, and `EmitRoundedRect`, each taking its drawable pointer and a `Batch2DRun` value.

The standard build `core-2c7f18d1a11343b3a21162969fa07513` compiled the corrected generated translation unit successfully and installed 76 content files, including both real SpriteBatchShader variants. The native adapter still skips 2D; these proofs do not establish rendered 2D content.

## Ownership observations

Captured generated code showed `Batch2DBuffer` allocating vertex and index arrays and its `Dispose()` deleting both arrays. `Batch2DWriter` allocates the buffer and its `Dispose()` calls the buffer's `Dispose()` and deletes the buffer. The writer's sink pointer is borrowed. `Batch2DRun::Texture` is a borrowed pointer; neither the run nor the emitter owns or deletes a texture. The emitter retains borrowed writer and render-manager pointers and has no generated cleanup deleting either.

The shared source annotations captured in the CPU handoff mark the buffer arrays and writer buffer as owned, and mark the writer's sink, run texture, and emitter's writer/render-manager references as retained borrows. Those annotations align with the generated pointer fields and cleanup seen in the captured tree.

The source compatibility fixes for native disposed exceptions and enum bitwise operations compiled successfully. Generated text cleanup uses scope guards for lineOffsets/effectPasses; effect lists own their pass elements. The initial array-to-IReadOnlyList call sites generated unowned adapters; shared commit `d98d0020` uses direct array indexing. Current generated lines139/148 have no adapters, and array/effect cleanup is retained. No generated output was edited. Exception cleanup was inspected in generated code; the successful native cases are not an injected-exception runtime test.

## Static vertex ABI

The pinned PPU compiler accepted compile-time assertions against the generated `Batch2DVertex.hpp`:

| Member | Offset |
| --- | ---: |
| `Position` | 0 |
| `TexLocal` | 16 |
| `Color` | 32 |
| `Shape` | 48 |
| `Corners` | 64 |
| `BorderColor` | 80 |

`sizeof(float4) == 16`; `sizeof(Batch2DVertex) == 96`. The assertions also require both types to be standard-layout. The executable writer probe separately passed copied vertices/indices, A/B/A ordering, capacity chunking, camera boundaries, and idempotent disposal with queued data.

The PS3 profile selects `generated-math-convention=native-column-vector`, but current codegen does not replace source-owned matrix bodies. Shared and generated projection translation remains M41/M42. Both actual SpriteBatch vertex variants compile to four DP4 instructions against c[0..3], matching the row-vector HLSL expression. Native upload must pack Core columns into those SDK vectors, as existing 3D does. The failed raw-column assertion and its artifacts remain preserved. The separately named SDK-packing case passed corners, center, asymmetric Z and W=1; wrong packing remains a negative control. Actual GPU upload/pixels remain a later native-renderer gate.

Regenerated text execution verified 24 quads/96 vertices/144 indices, actual atlas substitution, all glyph/effect positions, UV corners and colors, skipped missing glyph, zero Z, and normal lifecycle cleanup. A second case verified leading/consecutive/trailing empty lines with glyph starts(14,44)/(14,84),8vertices12indices. Both cases passed against the corrected standard objects. Managed Release regression:94/94; editor helper evidence:4/4. No broad heap-leak or GPU lifetime claim is made.

## Provenance

- Pinned Docker image: `sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f`.
- `toolchain-lock.json` SHA-256: `3f68b2d89a0aa3013a703d6f983bc27ebde0be97f21910b9704f0ae01ef4fb8f`.
- Production `Makefile` SHA-256 used to supply the Core generated compiler flags: `ca376a0288249d07d33b9e57d59839561c3d92b3444310155e1f74a9a1fbc90c`.
- Static probe source SHA-256: `cb6e20c13087548a49ea853b18b8da385790ec87845cc7659deed73fe61fc273`.
- Static probe object SHA-256: `049829c48fd8004075085b7246eae917362cc1ae03960211be7e6a3f936f7506`.
- Corrected standard generated-file aggregate SHA-256 (877 files): `bc7e043f374bb2f21de9dc57d019f6fb43e8726121e2fa919b1ab8be5efa04c3`. The preserved baseline aggregate was `258533d46798c33a5640e6889385a1bf3b604a18bc5664e9c68507278af42c33`.
- Executable writer baseline: `native-probe/writer-projection/output/writer-projection-20260924/emulator-runs/20260924-192218/TTY.log` below the task build root; terminal `passed=5 failed=1` (projection convention assumption). The later corrected-boundary run must be recorded separately, never substituted into this baseline.
- Corrected SDK-boundary run: `native-probe/writer-projection/output/sdk-packed-projection-20260924/emulator-runs/20260924-203652/TTY.log`; terminal `passed=6 failed=0`.
- Frozen text baseline: `native-probe/text/output/text-20260924/emulator-runs/20260924-203314/TTY.log`; terminal `passed=1 failed=0`, clean shutdown.
- Corrected text run: `native-probe/text/output/text-regenerated-20260924/emulator-runs/20260924-205146/TTY.log`; terminal `passed=2 failed=0`, clean shutdown; SELF SHA-256 `0c5379c98237c2465f9f4ddebefc3d36e3da0309d07650a755e9a5e6edde97d7`.
- Static ABI repeated against corrected generated tree: `native-probe/standard/output/array-fix-abi/compile.log`, compiler exit0; object SHA unchanged (`049829c48fd8004075085b7246eae917362cc1ae03960211be7e6a3f936f7506`).
