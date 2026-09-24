# PS3 HLSL compiler acceptance

This repository contains native PS3 compiler infrastructure for translating a vertex and pixel HLSL request pair into RSX programs. It is not an enabled authored-shader rendering path: no material selection, cooker staging, native runtime shader selection, or platform feature enablement is included.

## Host and preparation requirements

Run the managed frontend on Windows x64 with the .NET 9 SDK installed. The frontend loads the packaged `runtimes/win-x64/native/spirv-cross.dll`; running the final SDK conversion in Linux Docker does not make this frontend portable to Linux. The managed builder projects also require the sibling `helengine` checkout at `C:\dev\helworks\helengine`, or an explicit `HelengineRoot` MSBuild property / `HELENGINE_ROOT` environment override.

Docker Desktop must be running Linux containers. The pinned PS3 SDK image and the verified NVIDIA Cg 3.1 files must already be prepared. A developer prepares them explicitly from this repository root with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/prepare_toolchain.ps1
```

That preparation command verifies/downloads the locked Cg archive when needed, extracts its `usr` tree under `.ps3\dependencies\cg`, verifies `libCg.so`, builds the pinned image, runs the SDK probe, and records its immutable image identity in `toolchain-lock.json`. Compiler acceptance does not invoke preparation, download a .NET SDK, or pull an SDK image. It validates the existing image lock and Cg library before testing. RPCS3 and PS3 firmware are not compiler-test prerequisites.

## Acceptance

Pass an explicit absolute output directory strictly below the sibling `builds\helengine-ps3` directory. For example:

```powershell
./scripts/tests/ps3-hlsl-compiler.tests.ps1 -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/acceptance
```

The script resolves the repository from its own location, checks the shared engine checkout, validates the immutable Docker image ID and the Cg library SHA-256, then runs the builder suite in two reported groups: all tests without `Category=Docker`, followed by the Docker integration category. Each invocation gets a unique run ID and writes fresh TRX reports under its own `runs/<run-id>` directory, preserving reports from earlier invocations. Each run must exit successfully and produce a nonempty passing report containing the expected PS3 shader, RSX reader, Docker compiler, and pair-compiler classes. For each required class, the report validator follows `UnitTestResult.testId` to `UnitTest.id` and verifies that linked test result passed; an unused class definition cannot satisfy the requirement. Missing output-root input fails immediately without prompting. A process exit failure, absent/empty report, zero tests, failed test, or missing required executed class fails acceptance. The script prints the pinned compiler identity and report/log locations only after both reports pass.

The output directory contains the .NET artifacts under `dotnet`, invocation-specific TRX files and full process logs under `runs\<run-id>`, and Docker shader-job/test artifacts under `shader-artifacts`. The contract tests use an isolated `contract-fixtures` child. Generated outputs remain within the selected build ID. Acceptance paths must be strict descendants of the sibling build root; the build root itself is not a valid output directory.

## Translation and supported coverage

For each stage, the managed frontend uses shaderc's HLSL compiler and preprocessor, captures SPIR-V reflection, and uses the packaged SPIRV-Cross library to emit GLSL 1.20. Active `#include` directives are resolved through the shared include resolver and shaderc callbacks, including nested relative includes and conditional branches. Inactive includes are not resolved. Request defines are passed to shaderc before preprocessing; the `HEL_API_PS3` descriptor name is tested through a conditional include. The target descriptor exposes `ps3` / `HEL_API_PS3`, while callers supply request defines explicitly.

The accepted native pair path then converts generated vertex/pixel GLSL through `cgcomp` in the pinned Docker SDK. It reads the resulting SDK program binaries and compiler dumps, checks active vertex attributes, uniforms and samplers against SDK records, and reconciles authored inter-stage links by canonical Cg/RSX channel identity. The stages use their own SDK slots, which need not be numerically equal. Invalid or unsupported active routes fail explicitly rather than receiving invented defaults.

The task reports and tests establish vertex and pixel stages. Task 4's real Docker integration fixture uses a modern HLSL source with a `Texture2D`, two sampler identities, a constant-buffer member, `COLOR0` and `TEXCOORD0`; it compiles both stages and links those varyings through distinct Cg channels. Its active attributes, sampler units and fragment constant relocation are validated. This fixture is evidence for that concrete pair, not general HLSL interface support.

The existing pair binding mapper recognizes synthetic sampler dimension tokens for 1D, 2D, 3D, cube and rectangle resources, but only `Texture2D`/`Dim2D` has real HLSL-to-SDK integration coverage. The other dimension mappings are unit-level format mappings, not proof that modern HLSL declarations of those dimensions compile in the pinned SDK. Active user varyings are limited to one-column, non-array FP3232 scalar/vector values of width 1–4 with matching generated Cg declaration and cross-stage shape/interpolation. Vertex attributes must be non-array and map to a matching SDK attribute record. Active float scalar/vector constants and the documented Cg matrix forms (`float3x3`, `float3x4`, `float4x3`, `float4x4`) are decoded only when source reflection and SDK metadata agree. Optimized-out resources are not synthesized.

Integer/non-FP3232 inter-stage values, varying matrices and arrays, unverified active Cg routes, and untested sampler dimensions remain unsupported. A successful compile of an isolated program does not extend these limits.

## Declared constant-buffer metadata

Each active constant buffer carries its SPIRV-Cross declared compiled-structure size and binding. The size comes directly from the compiled SPIR-V struct query; it describes that compiled structure, which may differ from the full authored declaration after optimization, and is not an RSX register count. `UniformMembers` separately records physical SPIR-V member offsets, sizes, array and matrix strides, and major decoration. These records do not define runtime constant packing or prove that constants are uploaded; runtime packing and upload remain pending. Members removed by optimization are not recovered or synthesized.

The shared compile service exposes `IShaderBackendCachePolicy.UseSharedCache` for backends that can safely use the shared cache. `Ps3ShaderBackend` opts out because identity also depends on resolved include contents and compiler options/toolchain identity; the backend is not registered here, and this opt-out seam does not provide a complete cache key or register it.

## Shared PS3 backend slice

`Ps3ShaderBackend` adapts the shared compile service to the PS3 HLSL translator and an explicitly injected RSX compiler. Callers provide a fully qualified SDK working root; the backend creates no implicit temporary path. It accepts PS3 vertex/pixel requests, HLSL source models 3.0 through 5.0, and descriptor space 0. The shared result retains the source translation and validated SDK program, and its `ShaderCompiledBinary` carries the exact SDK program bytes. Cancellation is checked before translation, before SDK compilation, and before returning the result.

Shared logical metadata is derived only from the translation's source reflection. Constant-buffer sizes and leaf-member offsets/sizes come from declared SPIR-V structure reflection, with physical vectors and matrix columns represented as float types. Interface metadata is limited to scalar/vector FP32 values with one physical column and no arrays; trailing decimal semantic suffixes become semantic indices. The backend maps reflected `Dim2D` and `DimCube` textures and deduplicates logical texture/sampler bindings without dropping physical combined sampler records from the translation. This metadata is not an RSX upload layout or runtime registration.

The focused Docker fixture verifies a small authored 2D-texture vertex/pixel pair through the backend and existing pair reconciler. The sibling editor's authored `ForwardStandardShader.hlsl` smoke is a separate acceptance check; its current `cgcomp` exit-139 failure is recorded as an integration limitation and does not widen the backend's supported-feature claims.

## Pipeline boundary

```text
HLSL request + request defines + include resolver
                    |
                    v
        shaderc preprocessing and SPIR-V
                    |
                    v
     packaged SPIRV-Cross -> GLSL 1.20
                    |
                    v
      pinned Docker cgcomp / RSX SDK
                    |
                    v
     vertex/pixel binaries + Cg dumps
                    |
                    v
       validated pair and binding map
```

The generated SDK binaries and headers are consumed as produced and are never rewritten after compilation. This milestone stops at the validated compiler pair and binding map; runtime selection and rendering remain disabled.
