# PS3 HLSL shader integration

## Intent and scope

Compile authored Helengine HLSL through the existing shared shader contracts into
PS3 RSX programs. Reuse the same authored sources as other platforms instead of
substituting prewritten shaders by program name. The user approved changes to the
PS3 C# builder and the shared shader library. Work stays in the existing checkouts,
without worktrees. Native compilation and final RSX conversion remain in the
repository's pinned Docker SDK.

This document proposes the runtime integration; it is not a completion report.

## Verified feasibility

The current experimental source compiler uses shaderc to compile HLSL to SPIR-V
and SPIRV-Cross to emit GLSL 1.20. Inside the pinned SDK, `cgcomp -Wcg,-oglsl`
accepts that source and emits vertex and fragment RSX programs.

A test shader uses a matrix constant buffer, a tint, Texture2D and SamplerState.
Its vertex assembly transforms positions and passes UVs; fragment assembly samples
the texture and multiplies by tint. Three frontend tests passed on 2026-09-23.
Artifacts are under `builds/helengine-ps3/shader-integration-20260923/probe` in the
Helworks root. This verifies compilation, not rendering or general compatibility.

The experiment currently lacks include resolution, stable resource metadata,
stage-pair validation, backend registration, cooking and runtime loading. Existing
runtime shaders remain unchanged. The platform's shader feature remains disabled.

## Architecture

1. **Shared target:** append `Ps3` to ShaderCompileTarget, preserving existing
   serialized enum values, and add the `ps3` / `HEL_API_PS3` descriptor.
2. **Source compiler:** process the requested HLSL entry point, defines and includes
   through shaderc. Use SPIRV-Cross reflection and naming APIs before emission.
   Never modify generated shader text, binaries or headers after compilation.
3. **Docker compiler:** invoke the pinned `cgcomp` using structured process
   arguments, the existing read-only Cg mount and an explicit build-owned working
   directory. Capture errors and reject unsuccessful or empty output. There is no
   host SDK fallback or implicit temporary-directory output.
4. **Backend:** implement the shared IShaderBackend contract for vertex and pixel
   stages. Return compiled program bytes with metadata sufficient to resolve
   authored bindings to RSX resources. Register through the existing platform
   backend contributor seam.
5. **Cooker:** implement IPlatformShaderArtifactBuilder using the build graph's
   resolved sources and dependencies. Compile requested program pairs/variants,
   validate compatibility and publish declared artifacts through normal staging.
6. **Native runtime:** load validated vertex/fragment program artifacts, allocate
   fragment microcode in RSX-accessible memory and bind resources using cooked
   metadata. Program lifetime owns allocations. Keep current embedded shaders as
   an explicit diagnostic path, not an automatic fallback for broken content.

## Binding and compatibility contract

- Authored binding identity is independent of optimizer-generated variable names.
  Preserve reflection names and record constant-buffer member offsets/types before
  conversion; connect them to final RSX reflected parameters at cook time.
- Vertex layout mappings must identify actual RSX attribute indices. Do not assume
  that independently compiled GLSL declarations preserve the engine layout.
- Validate vertex outputs against fragment inputs by semantic, type and actual
  hardware slot. RSX has no GLSL linker to repair independently assigned slots.
  Reject incompatible pairs with a diagnostic naming the mismatch.
- Map combined GLSL samplers back to both the HLSL texture and sampler bindings;
  do not collapse distinct texture/sampler combinations silently.
- Matrix layout, constant data endianness, clip-space/depth conventions and texture
  coordinate conventions require explicit tests against the native renderer.
- Support only features that can be represented by RSX vertex/fragment programs.
  Unsupported stages, resources or hardware limits must fail during cooking with
  useful diagnostics. Accepting modern HLSL syntax does not promise modern GPU
  capabilities.

## Error behavior and reproducibility

Missing includes, toolchain configuration, SDK image, Cg library, invalid source,
incompatible stage pairs and unsupported bindings are errors. No default shader or
default binding is synthesized. Use the shared include resolver and compiler
preprocessor rather than textual include replacement.

Cache identity must include source/dependencies, defines, entry point, stage,
conversion options and compiler/toolchain identity. Read the SDK identity from the
existing lock file. Keep generated sources and diagnostics in the standard visible
build root. Do not change other builders or unrelated native-cache work.

## Validation and enablement

Tests must cover compilation errors, includes, defines, resource reflection,
nontrivial stage interfaces, texture/sampler combinations and unsupported stages.
Run final SDK conversion tests in Docker and validate binary structure, not just
process exit status. Exercise cooker declarations/staging through shared contracts.

Runtime acceptance is a real HLSL-derived program rendering `cube_test`, including
transform, material color and basic lighting, with the authored camera clear color
unchanged. Add a textured fixture to verify sampler and UV mapping independently.
Use existing automated/runtime probes; no screenshots without permission.

Enable the platform shader feature only after the cooking-to-rendering path passes.
Document the supported subset and explicit limitations. Do not claim generic
shader integration based solely on the current two compiled probe programs.
