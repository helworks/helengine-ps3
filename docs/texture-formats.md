# PS3 texture formats

The PS3 platform publishes 27 RSX pixel encodings for texture and font-atlas cooking. Texture settings use native string identifiers; existing `Rgba32` settings retain the historical RGBA-to-A8R8G8B8 upload path.

| Family | Encodings |
| --- | --- |
| Packed color | B8, A1R5G5B5, A4R4G4B4, R5G6B5, A8R8G8B8, G8B8, R6G5B5, R5G5B5A1, D1R5G5B5, D8R8G8B8 |
| Block and shared-channel storage | DXT1, DXT3, DXT5, B8R8_G8R8, R8B8_R8G8, HILO8, HILO_S8 |
| Normalized 16-bit data | X16, Y16_X16 |
| Floating-point data | W16_Z16_Y16_X16_FLOAT, W32_Z32_Y32_X32_FLOAT, X32_FLOAT, Y16_X16_FLOAT |
| Depth | DEPTH16, DEPTH16_FLOAT, DEPTH24_D8, DEPTH24_D8_FLOAT |

## Settings and layouts

Native identifiers have the form `Ps3.<encoding>.Linear` or `Ps3.<encoding>.Swizzled`. Append `.Mipmapped` to generate the complete chain down to 1x1. Uncompressed encodings publish both layouts. DXT, shared-channel compression and HILO publish linear conversion settings. Together these are 94 native settings, plus `Rgba32` and `Ps3.GtfSource`.

Linear pixel and block rows retain the base-level pitch at every mip level. Swizzled uncompressed textures require power-of-two dimensions; encodings wider than 32 bits interleave their 32-bit lanes rather than whole pixels. Native GTF DXT blocks with the swizzled flag remain row-major blocks, with packed mip levels. GTF input also preserves its original pitch, remap and partial mip chains.

Only formats with independent alpha expose Binary/A4/A8 conversion choices. Single-channel and channel-pair conversions use opaque output; the hardware replicates their channels. The X16 converter selects its data channel for grayscale output. Shared-green/shared-blue pair conversion averages the two source green/blue values and stores both red values. HILO_S8 converts normalized source R/G to positive signed samples. Source GTF signed values remain unchanged.

Float color conversion starts with the editor's RGBA8 image. DEPTH16_FLOAT uses RSX unsigned E4M12, not IEEE half; values below its minimum positive range saturate to its lowest encoding. DEPTH24_D8_FLOAT truncates the float32 representation to its native 24-bit depth field. Converting a color image to depth uses the red channel and clears D8. These are texture encodings; this change does not add render-target creation or shadow-comparison samplers.

## GTF import and preservation

The editor registers `.gtf` through the portable `PlayStation3GtfTextureImporter`. The standard big-endian GTF header has a 12-byte file header followed by 36-byte texture attributes, each containing a 24-byte RSX descriptor. Generated files contain one texture with payload starting at a 128-byte-aligned offset.

The normal importer previews the first texture's base level in RGBA8. Its API can select another attribute in a multi-texture file. Preview decoding applies the RSX channel remap, including the special 16-bit pair selectors and XXXY override. Float previews clamp to display range; they do not replace source bytes.

Choose `Ps3.GtfSource` to package a single-texture GTF byte-for-byte, preserving HDR/signed/depth values and authored mipmaps. This setting validates the requested maximum resolution but does not resize or quantize alpha. Its Opaque/A8 UI values do not change the source descriptor. Use a conversion setting when resizing or changing encoding is desired. The VRAM inspector cannot infer source-preserving native storage from dimensions alone and leaves that estimate unavailable.

PNG, DDS, TGA, WebP and the existing ordinary raster formats use the pinned Magick.NET decoder during platform cooking. Serialized engine texture assets, including generated font atlases, use the shared pixel decoder. TIM and Dreamcast PVR sources retain their portable native importers.

## Runtime and limits

Both the sprite/font texture owner and authored 3D material cache recognize GTF content by its header. They validate it before allocating RSX-local memory, copy native texels without RGBA expansion, retain mip/remap/pitch metadata and replace file offsets with the allocation's actual RSX offset. Existing explicit GPU drain/retirement and source-scoped cache ownership remain in force.

Sampling enables the stored mip range. 32-bit float color formats use nearest filtering. In-place RGBA updates require single-level linear A8R8G8B8 and reject other layouts before touching memory.

This pipeline handles normalized, borderless, non-cube 2D textures with dimensions 1..4096, up to 64 MiB of native storage and a 65 MiB input envelope. Cube maps, volumes, bordered GTF storage and unnormalized texture coordinates require separate engine resource/shader contracts and are rejected explicitly. Runtime source-preserving GTF must contain one texture. No hardware or emulator rendering was performed for this change.

## Validation

All 150 focused builder/staging tests and the editor registration test passed. Coverage includes every published native layout, literal GTF/ARGB/DXT/depth bytes, linear pixel and block mip pitch, wide-float Morton addresses, channel-pair remaps, invalid descriptors, source preservation, font-atlas cooking, ordinary raster sources, staging receipts and native build handoff. The native C++ parser accepted 94 cooked fixtures and rejected 13 malformed inputs per fixture. The parser, upload, 2D owner and 3D cache compiled for PPU in the pinned SDK image. The authored-texture and texture-update GPU fixture ELF binaries also compiled and linked successfully; neither binary was executed.

Visible artifacts: `C:\dev\helworks\builds\helengine-ps3\texture-formats-20261008`.

## Primary references

- [Pinned PSL1GHT RSX texture descriptor and constants](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/ppu/include/rsx/gcm_sys.h).
- [Newcastle University GCM texturing workshop: standard GTF header and texture attributes](https://research.ncl.ac.uk/game/mastersdegree/workshops/ps3introductiontogcm/GCM%20Texturing.pdf).
- [RPCS3 native texture layouts, endian conversion and wide-texel swizzling](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Common/TextureUtils.cpp).
- [RPCS3 special channel remapping](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/RSXTexture.cpp).
- [RPCS3 E4M12 depth conversion](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/GL/GLCompute.h).
