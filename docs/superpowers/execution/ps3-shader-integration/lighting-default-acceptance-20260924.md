# Standard material lighting-default correction

## Diagnosis

The earlier inference that CubeTestSolid was authored Unlit was incorrect. The
common material has base-color=#ffffffff and casts-shadow=false; no lighting-mode
is authored. Its Windows override uses standard-shader/ForwardStandardShader.
MaterialAssetSettingsService clones common settings then
MaterialAssetSchemaSettingsService fills missing fields from the platform schema.
The PS3 schema and cooker supplied unlit, causing cooked flags0. Fix both PS3
defaults to lit; preserve explicit unlit. No authored or shared source changes.

Original common asset SHA256 before correction:
`11C3B96C4325A4EC9BC16B75C2C5E6A768F0AF7D6680DBB65D62E49E2E6B69D9`.

## Pixel oracle recorded before corrected run

The previous standard05 staged cube (`engine:model:cube`) was decoded read-only
according to ModelAssetPayloadSerializer:24 positions and normals, front vertices
4..7 at z=0.5, x/y=+-0.5, normal=(0,0,1). World and normal matrices are identity;
camera=(0,0,5), viewport1280x720, WVP x/y scales1.35799503/2.41421342.
Require these inputs to remain the same in the corrected run before using this oracle.

Provider surface constants: roughness0.4, metallic0, specular0.5, emissive0.
Base/diffuse white, scene ambient0, directional color1 and direction
(0.399412841,-0.751280427,-0.52540189). At the center sample, the front face receives
normalized light direction=(-0.3994128323,0.7512804106,0.5254018786).
View direction is approximately(0,0,1), including half-pixel offsets changes the
result by less than one byte. The unchanged ForwardStandardShader equations give:

- Fresnel approximately0.0400311; GGX distribution0.1232332, geometry0.8187931.
- Diffuse0.16054579 plus specular0.00100981 =0.16155560 per RGB channel.
- Linear framebuffer byte=41.19668, expected center **292929**, tolerance2 per channel
  for shader precision/raster sample rounding. Clear corner remains **6495ED**.

The oracle is calculated from source equations and submitted inputs, not fitted to
the corrected output. Require P3MT flags1, Lit1/HasDir1, matching input diagnostics,
real authored program, first-frame sample, normal duration exit and GPU cleanup.
This does not establish parity for platform-specific Windows material overrides,
nor support for general surface properties beyond the current PS3 standard schema.

## Validation

Focused host tests: RED4 failed/25 passed, then GREEN29/29, exit0.
Logs: material-lighting/logs/material-lighting-red.log and
material-lighting/logs/material-lighting-green.log under the owned integration root.
Ordinary scoped review approved the three-file change with no findings. Parent
confirmed the unchanged unknown-lighting rejection test remains in that suite.
Builderb4 compiled0errors/41preexisting shared-engine warnings and was registered
only in the isolated standard-scene/state user-settings.

Standard06 completed with exit0 through the normal platform driver. Artifact:
`standard-scene/output/core-caec30cac7114bd3ae472ea5481ed848/helengine_ps3.self`,
SHA256 `6FB97340F666D4B0C30869667F57FBB395DA0B2266D352EAD611130D6539548D`.
RPCS3 run `emulator-runs/20260924-125923/TTY.log` completed launcher/application0.
Its submitted matrices, material, camera and light inputs match the oracle above.
P3MT flags1, Lit1/HasDir1; first-frame center292929, final2A2A2A (within the
pre-recorded tolerance2), clear probes6495ED. The actual HLSL source hash remains
BF2EE7E830BE15CF6F91793C8ACC0C1C8067A0D4A7D79F527A2E09AA39480C77.
896frames/896draws, normal duration exit, Core disposal, successful drain,
authored cache ownedGpuBytes0, shutdown0. The authored-log checker passes with
explicit ExpectedCenterRgb292929, ExpectedLit1, ChannelTolerance2.
User visually confirmed the result. Original material SHA256 remains unchanged.
This closes the default-lighting regression, not the separate nonempty texture
content gate or global shader feature enablement (shaders remains forced-disabled).
