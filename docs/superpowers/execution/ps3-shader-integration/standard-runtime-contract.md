# Standard shader native handoff evidence

Historical implementation handoff. Prerequisites mentioned as pending below were subsequently implemented and validated; use [acceptance-checkpoint-20260924.md](acceptance-checkpoint-20260924.md) for current completion boundaries. Lighting and shader-feature enablement still require their distinct final gates.

Parent source inspection, 2026-09-24. This records implementation constraints for the remaining real-scene integration, not completed renderer functionality. Native reader, GPU owner and material reader tasks remain prerequisites. No shader feature may be enabled based on this note.

## Source and coverage

The codebase-memory `list_projects` retry returned `Transport closed`; graph generation/freshness and coverage are unknown. Bounded direct reads covered:

- Native `Ps3RenderManager3D.cpp/.hpp`, `Ps3RuntimeAssets.cpp/.hpp`, `Ps3RasterRenderer.cpp/.hpp`, `Ps3Vertex.hpp`, `Ps3BootHost.cpp`, `Ps3Video.hpp`, `Ps3ContentStreamSource.cpp`, `Ps3RenderManager2D.cpp`.
- Sibling `engine/helengine.editor/shaders/builtin/ForwardStandardShader.hlsl`, `engine/helengine.shader/shaders/StandardMeshShaderData.cs`, `engine/helengine.directx11/rendering/DirectX11ForwardLightShaderDataBuilder.cs`, `engine/helengine.core/model/LightType.cs`, `engine/helengine.core/assets/raw/TextureAsset.cs`, `TextureAssetColorFormat.cs`.
- Committed PS3 `Ps3MaterialCooker.cs` and P3MT/P3SP contracts.

## Uniform semantics from the actual HLSL

All active members must be supplied in each stage where present; optimized-away members need no upload. Resolve exact `BufferName` + `MemberPath` + authored binding and check shape before passing SDK-packed values to `Ps3AuthoredProgram::UploadUniform`. Unknown members are errors, not zero-filled uploads.

| Buffer / binding | Source members and intended runtime values |
| --- | --- |
| TransformBuffer / b0 | `world`, `worldViewProj`, `normalMatrix`: actual entity/camera matrices; `cameraPosition`: camera world position; `materialFlags`: x controls shadow reception, y selects emissive texture, not lighting or vertex color. |
| ForwardLightBuffer / b1 | `ambientLightColor`; `lightMetadata.x` active count; four slots each containing `lightNColorAndType`, `lightNDirectionAndShadow`, `lightNPositionAndRange`, `lightNSpotAngles`. |
| ShadowBuffer / b2 | `shadowMetadata`, four `shadowLightNAtlasRect`, `shadowLightNMetadata`, `shadowLightNWorldToShadowClip`. Explicit no-shadow state is required for this basic-lighting milestone; do not claim shadow rendering. |
| BaseColorBuffer / b3 | `baseColor` is normalized P3MT RGBA; it multiplies the sampled diffuse texture before lighting. |
| RoughnessBuffer / b4 | `roughnessValue.x`; current P3MT has no authored roughness field. Shared default is0.4, not an arbitrary missing-uniform fallback. |
| MetallicBuffer / b5 | `metallicValue.x`, shared default0. |
| SpecularBuffer / b6 | `specularValue.x`, shared default0.5. |
| EmissiveColorBuffer / b7 | `emissiveColorValue`: RGB times alpha strength when no emissive texture is selected. Current P3MT has no authored emissive field. |

The HLSL directly evaluates `mul(float4(input.pos,1), world)` and the corresponding WVP multiplication. The existing embedded-Cg `ToRowMajor` transposes Core M11..M44 into four SDK rows, but its comment alone is not proof for the translated HLSL path. Reuse the asymmetric GPU fixture result and inspect actual standard vertex code before choosing the final packing; do not double-transpose based only on the reflection RowMajor flag.

The shadow-free unlit material behavior can be represented by ambient RGB1 and activeLightCount0 because the actual PS begins with `surfaceColor * ambientLightColor.rgb`. This is an explicit authored `Lit=false` policy, not a fallback for missing light uniforms. Lit materials must receive scene radiance and camera position. No-light lit state must not become unlit silently.

Light types are Directional0, Point1, Spot2, Ambient3. Directional HLSL normalizes the negative direction-of-travel; do not negate twice. Shared packing multiplies RGB by intensity, sums ambient radiance, and has four nonambient slots. Existing PS3 only reads the first ambient/directional light and invents0.2 ambient when absent. The integration must state its supported light budget and preserve real scene inputs rather than interpreting `materialFlags` as a lighting switch. Point/spot/shadow capability cannot be claimed from shader compilation alone.

## Texture and vertex contracts

The exact generated Core asset type is `::TextureAsset`, NOT `RawTextureAsset`. Its fields are `Colors`, `PaletteColors`, `Width`, `Height`, `ColorFormat`, `AlphaPrecision`, `IsEngineOwned`; Rgba32 enum0 is four RGBA8 bytes per texel. Rgba4444/Indexed4/Indexed8/GxRgb5A3 are distinct encodings, never reinterpret them as RGBA32. For the initial explicit RGBA32 path, require nonzero dimensions, supported dimension limit, and exactly width*height*4 bytes using checked arithmetic before RSX allocation.

Read texture assets with existing AssetSerializer through the `IContentStreamSource` supplied by Core. No new hardcoded host path or separate runtime content root. P3MT carries a validated content-relative diffuse path; absent/empty means the schema's white diffuse texture, but a nonempty missing/invalid asset must fail visibly.

The standard pair's sampler logical names are `DiffuseTexture`/`DiffuseTextureSampler`, `RoughnessTexture`/`RoughnessTextureSampler`, `EmissiveTexture`/`EmissiveTextureSampler`, `shadowAtlasTexture`/`shadowAtlasSampler`, and `pointShadowTexture0..3`/`pointShadowSampler`. Bind the metadata's physical unit, not t/s ordinals. Actual standard conversion retains DimCube resources, so provide validated solid-cube defaults even when shadows are disabled; do not substitute 2D descriptors or rewrite shared HLSL to strip the samplers.

Standard VS semantics are POSITION float3, NORMAL float3, TEXCOORD0 float2; no COLOR semantic. Current native vertex layout is40bytes with position0, normal12, color24, no UV. The integration needs real ModelAsset.TexCoords, not fake UVs for textured draws. Adding UV at40 yields48bytes while preserving diagnostic color offsets; update all stride/layout assertions and diagnostic vertex initializers together.

P3MT preserves UseVertexColor but the standard shader cannot consume it. This cannot silently become a working vertex-color feature. Current ModelAsset upload only creates white vertex color, so the runtime must either explicitly reject an unsupported modulate request or document/prove equivalent white-only behavior for the available model data. Do not change the shared shader without approval.

## Loading and lifetime boundaries

`BuildMaterialFromCooked(path, source)` currently calls generic AssetSerializer immediately. It must instead consume P3MT with the native reader, open the referenced P3SP through that same source, and require `MatchesProgram` before initialization. P3MT bytes must never be sent to the generic material serializer. A malformed/missing pair is a load error, never the diagnostic embedded pair.

The renderer must own cached `Ps3AuthoredProgram`/`Ps3AuthoredTexture` GPU resources. Materials hold references/keys, not independently freed program copies. Cache reuse must verify exact program identity; avoid sharing path-only entries across distinct content-source contexts. Partial initialization must not publish a cache entry.

Existing shutdown is Core disposal -> `video.Drain()` -> renderer shutdown only if drained -> video shutdown. New resources must be retained on drain failure and released only through the successful renderer shutdown branch. Material disposal happens before drain and must not free in-flight resources. `Ps3AuthoredProgram` and `Ps3AuthoredTexture` provide `ShutdownAfterDrain` for this contract.

Existing two-type `Ps3RuntimeAssets.hpp` and embedded `Ps3MeshBuffers` in `Ps3RasterRenderer.hpp` violate the requested one-type/file convention. When modifying these types for integration, split their definitions into dedicated files and keep an include-only compatibility header only if callers require it; do not create more multi-type containers.

Parent follow-up exact-source check: `engine/helengine.core/assets/RuntimeModel.cs` initializes its own empty submesh array in the base constructor. `SetSubmeshes` allocates a new array container, copies the non-null RuntimeSubmesh pointers into it and releases the prior owned items/container. The caller may therefore release the temporary input container after transferring the item ownership; it must not delete the transferred items. `assets/raw/ModelSubmeshResolver.cs` supplies validated authored ranges or its documented whole-model range. The new native model does not need to recreate the old redundant empty array. A concern raised during the split was resolved by this source check, not by changing generated output. The graph service retry still returned Transport closed, so graph coverage remains unknown.

## Remaining authoritative acceptance

Actual project read-only preflight: `C:/dev/helprojs/demodisc/project.heproj` names DemoDisc, includes ps3 in supportedPlatforms and has sceneRouting=null. Existing `assets/scenes/rendering/cube_test.helen` is a50679-byte HELE binary; `assets/materials/rendering/cube_test/CubeTestSolid.hasset` is a189-byte HELE binary, not JSON. Material siblings observed are windows/ps2/psvita overrides, with no ps3 override in that directory. Do not infer its decoded PS3 lighting/color state from the filename or old factory; inspect the actual new P3MT produced by the standard packager. `settings/platform.ps3.json` exists and selects1280x720/vSync; profile IDs are empty (builder defaults apply). These files were not changed.

The exact source runtime Stream header is sibling `engine/vendor/csharpcodegen/cs2.cpp/.net.cpp/system/io/stream.hpp`, read directly: `size_t Read(uint8_t*,size_t offset,size_t count)`, `size_t Length() const`, `int ReadByte()`, virtual destructor and `Dispose()`. Short-read loops and disposal in the runtime integration plan use this actual API, not guessed .NET method spellings. The header is source input, not an agent-created generated build product.

Build the actual demodisc project via sibling `scripts/build-platform.ps1 -Project C:/dev/helprojs/demodisc/project.heproj -Platform ps3`, isolated builder registration, explicit disjoint output/cache roots under the approved visible build root. Do not patch generated Core, cooked output or authored project assets.

Launch the resulting SELF using the existing visible RPCS3 launcher and a serialized emulator slot. Require real startup `cube_test`, P3MT/P3SP identity logs, no fallback/loader errors, material/light/camera inputs, geometry and meaningful pixel probes. Existing center-not-corner alone is insufficient to prove HLSL lighting. The separate four-color texture GPU fixture proves UV/sampler behavior, not the real scene or material content path. Only the complete acceptance gates permit shader-feature enablement and goal completion.

## Shader-feature reachability preflight

Parent rechecked the graph service on2026-09-24; list_projects still returns Transport closed, so generation/coverage remain unknown. Exact-source fallback inspected `helengine.shader/shaders/IShaderRenderManager3D.cs`, `helengine.shader/assets/ShaderRuntimeMaterialLoader.cs`, `ShaderRenderManager3DExtensions.cs`, and Core's `content/RuntimeContentManagerConfiguration.cs`, `scene/runtime/RuntimeSceneAssetReferenceResolver.cs`, `managers/rendering/RenderManager3D.cs`.

The raw shader-aware interface adds BuildMaterialFromRaw(material,shader), shader invalidation, and compile-target metadata. It is distinct from platform-owned cooked material loading. Under `HELENGINE_RUNTIME_MATERIAL_RESOLUTION_COOKED_PLATFORM_OWNED`, both generated and file-backed material references in RuntimeSceneAssetReferenceResolver call `BuildMaterialFromCooked(path, ContentStreamSource)` directly. This supports the planned native P3MT entry point without routing those scene references through ShaderRuntimeMaterialLoader's raw ShaderMaterialAsset package loader. It does not prove every shader-enabled generated call is covered: still require a fresh standard build/run after enabling the feature, and inspect actual compiler/reachability output rather than adding unused raw/hot-reload interfaces speculatively.
