# Locked SDK runtime evidence

Historical SDK inspection against the image identified below. The current pinned image and actual runtime gates are recorded in [acceptance-checkpoint-20260924.md](acceptance-checkpoint-20260924.md); these declarations alone are not runtime acceptance.

Inspected 2026-09-23 using read-only, network-disabled containers from image `sha256:da929def76aacc0a5f499e1b8be143912062ab4a93b206277ead4de9530fe411`. These are SDK declarations, not runtime test results.

SDK headers live at `/opt/ps3dev/ppu/include/rsx/`, not `/opt/ps3dev/psl1ght/ppu/include/rsx/`. `commands.h` expands `commands_inc.h` through RSX function macros; the latter is the source of the declarations below.

## Uniform/program API

`commands_inc.h`:

```cpp
void rsxLoadVertexProgram(gcmContextData*, const rsxVertexProgram*, const void* ucode);
void rsxLoadFragmentProgramLocation(gcmContextData*, const rsxFragmentProgram*, u32 offset, u32 location);
void rsxSetVertexProgramParameter(gcmContextData*, const rsxVertexProgram*, const rsxProgramConst*, const f32* value);
void rsxSetFragmentProgramParameter(gcmContextData*, const rsxFragmentProgram*, const rsxProgramConst*, const f32* value, u32 offset, u32 location);
void rsxUpdateFragmentProgramLocation(gcmContextData*, u32 offset, u32 location);
```

Use an owned runtime copy for mutable fragment constants. Do not modify the cooked artifact or generated compiler outputs. Before relying on these calls for repeated draws, verify update ordering/cache behavior through actual SDK implementation/disassembly or runtime probes.

`rsx_program.h` confirms both stage headers are 36 bytes, with stage magic, u16 counts and u32 table/microcode offsets. Constant records have a name offset, index, u8 type/internal/count, padding and four 32-bit values. Fragment relocation tables contain a u32 count and u32 offsets. Attribute records contain name offset, index, u8 type and three padding bytes. The native reader must validate these before calling SDK helpers; helpers accept no blob length.

## Texture API

```cpp
void rsxLoadTexture(gcmContextData*, u8 index, const gcmTexture*);
void rsxTextureControl(gcmContextData*, u8 index, u32 enable, u16 minlod, u16 maxlod, u8 maxaniso);
void rsxTextureFilter(gcmContextData*, u8 index, u16 bias, u8 min, u8 mag, u8 conv);
void rsxTextureWrapMode(gcmContextData*, u8 index, u8 wraps, u8 wrapt, u8 wrapr, u8 unsignedRemap, u8 zfunc, u8 gamma);
void rsxInvalidateTextureCache(gcmContextData*, u32 type);
```

`gcmTexture` fields in declaration order: u8 format/mipmap/dimension/cubemap; u32 remap; u16 width/height/depth; u8 location/pad; u32 pitch/offset. Pitch is bytes per line. Location uses GCM_LOCATION_RSX or GCM_LOCATION_CELL. Dimensions must fit u16 plus actual hardware limits; narrowing a larger source dimension is not validation.

Header constants: GCM_TEXTURE_FORMAT_A8R8G8B8=5, LIN=0x20, UNRM=0x40; DIMS_2D=2; NEAREST=1, LINEAR=2; CLAMP_TO_EDGE=3. Use symbolic constants in code. Normalized UVs must not accidentally set UNRM. Color remapping has explicit A/R/G/B source indices and remap types; use GCM_TEXTURE_REMAP_MODE, not an unexplained magic word. RGBA fixture storage must be converted intentionally to the selected byte/channel convention and verified with distinct colored texels.

The public comments alone do not establish required base alignment, actual maximum texture dimensions, the mipmap count convention, or per-draw fragment mutation safety. Resolve these before implementation using the pinned SDK behavior, not guessed defaults.

## Fragment constant implementation inspected

### Producer matrix fixture evidence

Parent ran `ProducesNativeReaderFixture` with the existing freshly rebuilt test assembly and locked Docker: 1/1 pass, no skips, exit0, TRX `relocation/results/parent-native-fixture.trx`. Immutable files under `native-reader/parent-fixture` are producer artifacts, not handwritten data. Read-only independent P3SP decoding found Frame binding0 declared80 bytes; Transform offset0 size64 MatrixStride16 RowMajor1 ScalarType `FP3232`, VectorWidth4 Columns4 SDK type11/count4; Tint offset64 size16 ScalarType `FP3232`, SDK type9/count1. Exact scalar spelling comes from current producer and must be honored by the native reader.

Emitted vertex GLSL computes `vec4(position,1) * Frame.matrix`. Its SDK dump assigns the matrix c[0..3] and computes output.x/y/z/w as DP4(input,c[0/1/2/3]). Thus the runtime's SDK register packing must supply the four dot-product vectors in that order. Reflection RowMajor describes the SPIR-V physical source layout; it is not permission to blindly upload Core's row-array or reuse the embedded shader adapter. A non-symmetric transform test with translation and nonuniform scale must pin the source-layout-to-SDK-register mapping before renderer integration.

Pinned PSL1GHT `ppu/librsx/commands_impl.h` SetFragmentProgramParameter uses FIFO InlineTransfer, not CPU memcpy into live microcode. Scalar/float2/float3/float4 transfer respectively1/2/3/4 words after swapF32_16; matrix3x3/4x3 walks param.count elements and3 floats per element, matrix3x4/4x4 walks count and4 floats per element. InlineTransfer requires word addressing, aligns its destination surface base down to64 bytes and accounts for the word offset within that surface. Native relocation bounds must cover the entire per-type write width, not merely4 bytes as the cooker reader currently checks. Repeated same-element offsets are safe identical FIFO writes; different elements must not overlap.

UpdateFragmentProgramLocation emits NV40TCL_FP_ADDRESS with location and masked offset. For multiple draws, use FIFO parameter writes and program-location update before each draw, preserving ordered commands; do not mutate one live CPU fragment copy concurrently with queued rendering. Runtime verification is still required.

Important array nuance: the SDK vector cases return after one parameter and ignore count, unlike matrix cases. The runtime uniform uploader must explicitly iterate vector arrays rather than assuming the SDK uploads all elements automatically. No generated binary edits are needed; validated record indices and caller-owned packed values can drive the per-element upload.

## Native validation constraint

## Texture layout and authored-input follow-up

Pinned RPCS3 source `https://raw.githubusercontent.com/RPCS3/rpcs3/2d103407/rpcs3/Emu/RSX/Common/TextureUtils.cpp`, inspected read-only, supplies a concrete emulator layout contract in `get_subresources_layout_impl` (around lines 614-719). Linear mip levels use the supplied row pitch; swizzled levels derive packed pitch from width. Layers contain their mip chain, and only the swizzled branch aligns the next layer to 128 bytes. Do not generalize the header comment about cubemap alignment to linear storage: the actual conditional is `!padded_row`. For a first constant-white cube default, six swizzled 1x1 faces at 128-byte intervals with a 128-aligned base give a simple explicit layout; this is a design candidate, not GPU acceptance. Nonuniform 2D fixture should use linear normalized-UV RGBA-to-ARGB conversion with explicit pitch and one mip level. Physical hardware limits and runtime validation remain required before general support claims.

Current shared `TextureAsset` stores Width/Height as ushort, Colors as byte[], and separate ColorFormat/AlphaPrecision enums. `TextureAssetColorFormat.Rgba32=0` is four RGBA bytes per texel; other current formats are Rgba4444, Indexed4, Indexed8, GxRgb5A3. Native first integration must validate Rgba32 and exact checked width*height*4 bytes, not interpret every Colors array as RGBA. Missing required authored texture content is an error. Shared ModelAsset has TexCoords float2[] and no vertex-color array in its current schema; native mesh realization presently drops TexCoords and sets all vertex colors white.

Read-only authored-source evidence: demodisc CubeTestSceneFactory positions its camera at (0,0,5), clear RGBA (100/255,149/255,237/255,1), one white directional sun with yaw -0.65/pitch -0.85 and intensity1, ShadowsEnabled=false, origin unit-scale cube rotating PI/2 radians per second. These are factory-source facts, not proof of the current staged scene bytes. Final acceptance must inspect the newly built actual scene/logs and not replace it with a synthetic approximation.

`command -v g++` found no host C++ compiler in the runtime SDK image. The PPU toolchain is present per lock. Do not fall back to a host SDK/compiler or install an unpinned compiler to make a test appear green. Pure native decoder tests can use a PPU test executable inside the same SDK and run through the authorized RPCS3 launcher; all outputs remain in the standard visible build root.
