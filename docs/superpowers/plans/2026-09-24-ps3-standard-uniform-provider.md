# PS3 standard uniform provider implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Parent plans; Luna implements.

**Goal:** Resolve every active member of the actual cooked ForwardStandard shader to explicit per-draw SDK-packed values before real-scene integration.

**Architecture:** A Core-independent draw DTO and pure resolver consume validated P3SP uniform metadata. A standalone PPU suite loads the real standard shader artifact and checks all active mappings. No GPU allocation, material loading or renderer modification.

**Tech Stack:** C++17, committed P3SP reader, pinned Docker PPU SDK, existing verified RPCS3 launcher.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md`; source-backed member semantics in `docs/superpowers/execution/ps3-shader-integration/standard-runtime-contract.md`.

## Global constraints

- Existing checkout only, no worktrees, nested agents, commits, pushes or screenshots. Parent owns review/commit.
- SDK/native compilation only locked Docker image `sha256:63a28f67dacb0a6d80c165645af126c7d6760e6a3b02e03b6e08aa5aeb96c40f`; no host/WSL SDK.
- All outputs/logs under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/standard-uniforms`. No system TEMP. Scripts edit through apply_patch only.
- Namespace helengine::ps3, one production type per file, substantive /// comments, PascalCase fields. No fabricated valid scene data in constructors.
- Reader sources belong to material worker for mechanical helper extraction; consume read-only, coordinate stable-source builds. Preserve all unrelated dirty files.
- Do not alter HLSL, generated Core, authored project, shader artifacts, material/GPU sources, production Makefile/build scripts or feature flags.
- No emulator launch until parent grants the serialized slot. BuildOnly preparation is allowed; report live handles immediately. Poll no longer than50 seconds per call.

## Review focus

1. Every actual active uniform is covered, not merely a synthetic subset.
2. Unknown names/bindings and incompatible shapes fail rather than receive zeros.
3. Output remains byte-identical on error, with written=0 and a useful diagnostic.
4. Lit/unlit and consecutive draw values cannot inherit previous state.
5. Matrix vectors preserve the measured GPU convention without an extra transpose.

## Task 1: Pure provider and real-artifact PPU tests

**Create only:**
- `src/platform/ps3/Ps3StandardDrawData.hpp`
- `src/platform/ps3/Ps3StandardUniformProvider.hpp`
- `src/platform/ps3/Ps3StandardUniformProvider.cpp`
- `tests/native/Ps3StandardUniformProviderTests.cpp`
- `tests/native/standard-uniforms.Makefile`
- `scripts/tests/ps3-standard-uniforms.tests.ps1`
- `docs/Ps3StandardUniformProvider.md`

**Consumes:** committed Ps3CookedShaderUniform, Ps3ShaderArtifactReader::Read, Ps3Matrix4. Actual immutable standard fixture:
`C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/native-material/fixtures/producer-002/shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp` (37501 bytes; SHA256233615d7dc7cce7ccd3f2d57bfd1b67a969437c22be35e37033da1312a7f7cb3).

**Produces:**
```cpp
struct Ps3StandardDrawData {
    Ps3Matrix4 World;
    Ps3Matrix4 WorldViewProjection;
    Ps3Matrix4 NormalMatrix;
    float CameraPosition[4];
    float BaseColor[4];
    float AmbientLight[4];
    float DirectionalColor[4];
    float DirectionalDirection[4];
    bool Lit;
    bool HasDirectionalLight;
    bool DoubleSided;
};
class Ps3StandardUniformProvider {
public:
    static bool Resolve(const Ps3CookedShaderUniform& uniform,
        const Ps3StandardDrawData& draw, float* values,
        std::size_t capacity, std::size_t& written, std::string& error);
};
```

Matrix fields are already SDK-packed four dot-product vectors. Resolver copies their16 elements in order, never transposes. CameraPosition/base/light float4s are supplied explicitly by the caller. DoubleSided belongs to draw raster state and does not become an invented shader uniform.

- [ ] Add real-artifact tests before provider implementation; preserve expected missing-header/API RED from locked Docker compilation.
- [ ] Load the immutable P3SP with the production reader and iterate every Vertex/Pixel Uniform. Print exact BufferName/MemberPath/binding/type/shape for coverage attribution. Decode no material here. Use printf, not iostream, and include `<sys/process.h>` with `SYS_PROCESS_PARAM(1001,0x100000)` as the established working PPU harness.
- [ ] Read exact HLSL `C:/dev/helworks/helengine/engine/helengine.editor/shaders/builtin/ForwardStandardShader.hlsl` and the source-backed handoff. Implement the following explicit table, never a catch-all zero fallback:

| Buffer/binding | Known values |
|---|---|
| TransformBuffer/0 | world=World, worldViewProj=WorldViewProjection, normalMatrix=NormalMatrix, cameraPosition=CameraPosition, materialFlags=(0,0,0,0) (no shadows/emissive texture) |
| ForwardLightBuffer/1 | ambientLightColor=AmbientLight when Lit; (1,1,1,1) when unlit. lightMetadata=(Lit&&HasDirectionalLight?1:0,0,0,0). When active, light0ColorAndType=(DirectionalColor.rgb,0), light0DirectionAndShadow=(DirectionalDirection.xyz,0); position/range and spotangles zero. All four named slots inactive fields zero; slots1..3 entirely zero. |
| ShadowBuffer/2 | shadowMetadata and each shadowLight0..3AtlasRect/Metadata zero, each shadowLight0..3WorldToShadowClip identity (explicit no-shadow policy). |
| BaseColorBuffer/3 | baseColor=BaseColor |
| RoughnessBuffer/4 | roughnessValue=(0.4,0,0,0) |
| MetallicBuffer/5 | metallicValue=(0,0,0,0) |
| SpecularBuffer/6 | specularValue=(0.5,0,0,0) |
| EmissiveColorBuffer/7 | emissiveColorValue=(0,0,0,0) |

All names/case must match actual source and reflected metadata. Only these exact finite sets are accepted (slots0..3, not arbitrary parsed suffixes). Check expected binding, FP3232 scalar label, vector/matrix shape, ArrayLength1, SDK type/element count and ByteSize sufficient for that known value. Actual float4 is four floats, matrix4x4 sixteen; if actual metadata differs from the source-backed expectation report it rather than accepting an unexplained shape. Authored RowMajor is not a request to transpose already-packed vectors.

- [ ] Reject null destination, insufficient capacity, unknown buffer/member, wrong binding/type/shape/count, and nonfinite required draw values before any output writes. Set written=0 on entry; build values in local16-float storage, validate, then copy and publish written only on success. Error includes buffer/member. Do not validate semantic material defaults by silently clamping caller colors or directions.

```cpp
float output[16];
std::fill(output, output + 16, -91.0f);
std::size_t written = 77;
auto unknown = activeUniform;
unknown.MemberPath = "unprovidedMember";
Require(!Ps3StandardUniformProvider::Resolve(unknown, draw, output, 16, written, error));
Require(written == 0 && !error.empty());
Require(std::all_of(output, output + 16, [](float value) { return value == -91.0f; }));
```

- [ ] Test at least15 independently named behaviors plus every active mapping: all8 buffers; distinctive nonidentity matrices; camera and nonwhite material; actual ambient; directional count/type/direction; no-directional lit; unlit; two successive differing draws; each malformed input class; unchanged output after errors. Verify exact expected floats from explicit test draws, not only success/count.
- [ ] Test-only Makefile links reader+validator+provider+test with the same working SDK flags/libraries as shader-reader.Makefile. Source/fixtures readonly, fresh output writable, network disabled. Runner uses -FixturePath, -OutputRoot, -BuildOnly and optional -RpcS3Path; validate fully-qualified input and strict task-root output before writes. Stage exact fixture bytes under USRDIR/content/standard.ps3sp. Hash immutable fixture/source include closure before/after build; reject a mixed-source build. Write honest schema1 manifest and use unchanged launch_in_emulator.ps1.
- [ ] After parent grants slot, require fresh TTY with unique named PASS lines and `PS3_STANDARD_UNIFORMS_TESTS passed=N failed=0` (N>=15), fail on any missing/duplicate/failing assertions. Record exact commands, image, source/fixture hashes and attributed run. Parent repeats final gate and requests scoped review before committing named files.
- [ ] Document explicit supported lighting/default policies and that this resolver does not prove real-scene rendering. Report to `.superpowers/sdd/2026-09-24-ps3-standard-uniform-provider/task-1-report.md`; return brief status and live handles, no commit.

## Execution evidence (2026-09-24)

Luna implemented the seven files. Final locked-Docker plus RPCS3 run `standard-uniforms/p2-r27/emulator-runs/20260924-045749` exited0,26PASS/0FAIL and41 active uniform rows; the two additional cases reject duplicate-replaces-member and missing-identity mutations without changing fixture bytes. Parent independently verified22 source/include hashes, three artifacts and the attributed TTY, and exercised early relative-RPCS3-path rejection under Windows PowerShell5. Original missing-header RED, later fixture-staging RED, its runner-only correction, and intermediate unregistered-test-function compile failure are recorded separately in the task report. None substitutes for the final passing run. Production provider source was unchanged by scoped review fixes; real scene integration remains outside this CPU gate.

## Parent self-review

This extraction changes scheduling, not feature scope. The seven new files have no write overlap with material/GPU tasks. The real artifact and committed reader provide the complete metadata input; measured asymmetric GPU proof establishes vector packing. The remaining renderer plan consumes exactly this DTO and signature after review. No full-scene or shader-feature acceptance is inferred from this CPU gate.
