# Native PS3 shader artifact reader implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Parent plans; Luna implements directly in the existing checkout, no worktrees.

**Goal:** Decode producer-generated P3SP shader pairs on the PPU and reject malformed containers/SDK references before any GPU allocation.

**Architecture:** A Core-independent native reader builds an owned CPU snapshot in local candidate storage and publishes it only after full container and SDK validation. It never invokes pointer-returning SDK helpers on unvalidated bytes. A standalone PPU regression executable consumes the actual C# writer output, then tests malformed in-memory copies through the same public API; all compilation is in the locked Docker image and execution in RPCS3.

**Tech Stack:** C++17, standard containers, pinned PPU compiler, existing C# writer/frontend/SDK fixture producer, xUnit, PowerShell, existing verified emulator launcher.

**Spec:** docs/Ps3ShaderArtifactFormat.md (frozen wire format); docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md (native reader slice); docs/superpowers/execution/ps3-shader-integration/sdk-runtime-contract.md (SDK bounds and ownership evidence).

## Global constraints

- Native types in helengine::ps3, one type per file, substantive /// documentation for types/functions/fields, PascalCase fields.
- New native parser is independent of generated Core and rendering. Do not enable shaders, change renderer/material/vertex types, or edit shared/generated/authoring code.
- Preserve existing dirty Makefile, scripts/build.ps1 and scripts/tests/build-contract.tests.ps1. New test-specific Makefile is allowed, existing production Makefile is not edited; it already discovers src/platform/ps3/*.cpp automatically.
- All native compilation/SDK conversion only locked Docker, no host/WSL compiler fallback or image package installation.
- All generated fixtures, ELF/SELF, manifests/logs under C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/native-reader. No system temporary directories. Set TEMP/TMP there for .NET fixture production.
- Never edit generated SDK files. Malformed tests operate on fresh in-memory test copies only and never write corrupted binaries to disk.
- No screenshots, nested agents, worktrees, commits or pushes. Emulator launch authorized, but do not stop an unrelated emulator process; report a concrete concurrent-use conflict.

## Review focus

1. Length/count arithmetic cannot wrap or read/allocate past the16MiB input cap, including deeply repeated records.
2. Strict UTF-8 rejects overlong encodings, surrogates, truncated sequences, codepoints above U+10FFFF, NUL and empty required fields.
3. SDK constants/continuations/relocations cannot escape their tables or microcode, even when the container itself is valid.
4. Duplicate same-element relocation addresses are valid, but different elements cannot write overlapping word ranges; bounds cover actual type width, not only one word.
5. Failure leaves caller output unchanged and returns useful stage/resource context; CPU-only decoding allocates no RSX resources.

## Task 1: CPU snapshot, bounded reader and real PPU regression

**Files (new):**
- src/platform/ps3/Ps3CookedShaderBuffer.hpp
- src/platform/ps3/Ps3CookedShaderUniform.hpp
- src/platform/ps3/Ps3CookedShaderAttribute.hpp
- src/platform/ps3/Ps3CookedShaderSampler.hpp
- src/platform/ps3/Ps3CookedShaderStage.hpp
- src/platform/ps3/Ps3CookedShaderProgram.hpp
- src/platform/ps3/Ps3ShaderArtifactReader.hpp/.cpp
- src/platform/ps3/Ps3RsxProgramValidator.hpp/.cpp
- src/platform/ps3/Ps3RsxAttributeEntry.hpp
- src/platform/ps3/Ps3RsxConstantElement.hpp
- src/platform/ps3/Ps3RsxConstantGroup.hpp
- src/platform/ps3/Ps3RsxProgramView.hpp
- src/platform/ps3/Ps3RsxActiveWriteRange.hpp
- tests/native/Ps3ShaderArtifactReaderTests.cpp
- tests/native/shader-reader.Makefile
- scripts/tests/ps3-shader-reader.tests.ps1
- builder.tests/Ps3ShaderNativeFixtureTests.cs
- builder.tests/Ps3ShaderNativeFixtureRootTests.cs
- builder.tests/Ps3NativeShaderFixtureRoot.cs
- docs/Ps3NativeShaderReader.md

**Interfaces:**

```cpp
class Ps3ShaderArtifactReader {
public:
    static bool Read(const std::uint8_t* data, std::size_t size,
                     Ps3CookedShaderProgram& result, std::string& error);
};
class Ps3RsxProgramValidator {
public:
    static bool Validate(const Ps3CookedShaderProgram& program, std::string& error);
};
```

DTOs are owning CPU records, not SDK pointer aliases. Buffer/Uniform/Attribute/Sampler public fields use exactly PascalCase versions of every field in the corresponding docs/Ps3ShaderArtifactFormat.md table, strings as std::string and U32 as std::uint32_t (RowMajor remains U32 to validate flag before interpretation). Stage fields Stage, EntryPoint, Bytecode vector<uint8_t>, Buffers vector<Ps3CookedShaderBuffer>, Uniforms vector<Ps3CookedShaderUniform>. Program fields ShaderAssetId, VertexProgramName, PixelProgramName, VariantName, SourceHash, ToolchainIdentity, Vertex, Pixel, Attributes, PixelSamplers. No constructors invent valid defaults; the reader fills all scalars before validation and exposes only a fully validated candidate. These names are the future renderer handoff, so do not rename casually.

Exact record field names to avoid acronym/word-boundary ambiguity:
- Buffer: BufferName, AuthoredBinding, DeclaredByteSize.
- Uniform: BufferName, AuthoredBinding, MemberPath, ByteOffset, ByteSize, ArrayStride, MatrixStride, RowMajor, ScalarType, VectorWidth, Columns, ArrayLength, GeneratedName, SdkDataType, SdkElementCount.
- Attribute: FullAuthoredSemantic, GeneratedName, ScalarType, VectorWidth, Columns, ArrayLength, HardwareSlot, SdkDataType.
- Sampler: TextureName, TextureBinding, SamplerName, SamplerBinding, GeneratedName, Dimension, TextureUnit, SdkDataType.

Exact record field names to avoid acronym/word-boundary ambiguity:
- Buffer: BufferName, AuthoredBinding, DeclaredByteSize.
- Uniform: BufferName, AuthoredBinding, MemberPath, ByteOffset, ByteSize, ArrayStride, MatrixStride, RowMajor, ScalarType, VectorWidth, Columns, ArrayLength, GeneratedName, SdkDataType, SdkElementCount.
- Attribute: FullAuthoredSemantic, GeneratedName, ScalarType, VectorWidth, Columns, ArrayLength, HardwareSlot, SdkDataType.
- Sampler: TextureName, TextureBinding, SamplerName, SamplerBinding, GeneratedName, Dimension, TextureUnit, SdkDataType.

- [ ] Write the producer fixture Docker-category test. Require HELENGINE_PS3_REPOSITORY_ROOT and explicit HELENGINE_PS3_SHADER_NATIVE_FIXTURE_ROOT under native-reader. Use Ps3ShaderPairCompiler with real source compiler/Ps3DockerShaderCompiler, real filesystem resolver rooted at fixture-storage, ShaderModel4.0 and VS/PS. Source:

```hlsl
cbuffer Frame : register(b0) { float4x4 Transform; float4 Tint; };
Texture2D Albedo : register(t0); SamplerState LinearSampler : register(s0);
struct VIn { float3 Position : POSITION; float2 Uv : TEXCOORD0; };
struct VOut { float4 Position : SV_Position; float2 Uv : TEXCOORD0; float4 Color : COLOR0; };
VOut VS(VIn v) { VOut o; o.Position=mul(float4(v.Position,1),Transform); o.Uv=v.Uv; o.Color=Tint; return o; }
float4 PS(VOut v) : SV_Target { return Albedo.Sample(LinearSampler,v.Uv)*v.Color; }
```

Serialize with identity (native-fixture-é, native.vs, native.ps, default), sourceHash=SHA256 UTF8 source, entrypoints VS/PS; write only fixture.ps3sp and exact raw vertex.bin/pixel.bin copies under supplied root. Assert reflection has Frame declared80bytes, Transform offset0, Tint offset64 before writing; unexpected layout is a visible test failure requiring parent examination, not adjusted expected values. Keep test named ProducesNativeReaderFixture and Trait Category=Docker. No opaque synthetic production blobs.

- [ ] Require a fully qualified fixture root before normalization and a strict descendant of the native-reader root afterward. Pure tests cover valid child, equal root, ancestor, sibling-prefix and relative paths; invalid paths must fail before creating directories/writing files. xUnit2.9 StartsWith takes expectedStartString first and actualString second; its existing argument order is correct, but equality/relative rejection needs explicit validation.

- [ ] Write native tests first calling the missing reader so initial PPU compile fails as expected. The executable loads only fixed test paths under /app_home/USRDIR/content: fixture.ps3sp, vertex.bin, pixel.bin. Bound input file length before allocation. Main uses SYS_PROCESS_PARAM(1001,0x100000), prints per-case PASS/FAIL and exact `PS3_SHADER_READER_TESTS passed=N failed=0`, returns nonzero on failure. No GPU initialization needed. Script preserves process exit and validates this summary plus at least20 named test cases; no pass based only on launch/draw count.

- [ ] New test Makefile includes $(PSL1GHT)/ppu_rules, requires SRC_ROOT=/src, BUILD_ROOT=/out, compiles only the2new production .cpp files and test main using $(CXX) -std=c++17 -Wall -Wextra -Werror -O2 -g -mcpu=cell $(MACHDEP) $(LIBPSL1GHT_INC) -I/src/src, links $(LIBPSL1GHT_LIB) -lrt -llv2. Keep all objects/ELF/SELF under/out. SELF recipe mirrors production: strip an owned staged ELF copy, sprxlinker, make_self, fself; never mutate the original ELF. No edits to existing production Makefile/build.ps1.

- [ ] Script takes required fully-qualified -FixtureRoot and -OutputRoot, optional -RpcS3Path (env default), -BuildOnly switch for RED compile, -BuildId default shader-reader-<UTC+GUID>. Validate strict descendant outputroot under standard PS3 build root, all3fixtures nonempty, lock immutable image and unchanged input hashes. Create builddir under OutputRoot, invoke network-disabled locked Docker with repo read-only/src, output/out, make -f /src/tests/native/shader-reader.Makefile. Capture logs bounded on console. No Cg mount needed because this native compile performs no shader conversion. Stage unmodified fixturefiles to builddir/USRDIR/content.

- [ ] On successful compilation create honest build-manifest.json from actual artifact hashes, current ToolchainLockSha256, SchemaVersion1, BuildId matching dirname, LaunchArtifact=ps3_shader_reader_tests.self, Artifacts records Path/Sha256. Include TestKind/SourceHashes provenance fields without claiming production Core build. Call existing scripts/launch_in_emulator.ps1 with -ArtifactPath, -OutputRoot and explicit RPCS3 path; default headless is enough for CPU tests. Find that invocation's new TTY.log and require every test summary. Do not replace or weaken launcher checks. Never rebuild because a polling timeout expired; retain process handle.

- [ ] Implement checked big-endian primitives from byte reads, no unaligned reinterpret_cast. Each read requires needed<=size-cursor; counts<=4096 before reserve, strings<=65536 and strict UTF8 before construction, blobs1..1MiB, input12..16MiB. Check header magic/version/exact total. Decode exact wire order, fixedstage0/1, no trailing bytes. Catch allocation/standard exceptions at public boundary into error and preserve result; assign via move only after all validation succeeds.

```cpp
// Use subtraction after checking cursor <= size; never cursor + needed <= size.
if (cursor > size || needed > size - cursor) return Fail("truncated field", error);
std::uint32_t value = (std::uint32_t(data[cursor]) << 24) |
    (std::uint32_t(data[cursor+1]) << 16) |
    (std::uint32_t(data[cursor+2]) << 8) | std::uint32_t(data[cursor+3]);
```

Reader helpers may be private static functions/methods, no undocumented nested model types. Validate buffer names/bindings unique, nonzero size; uniforms exactlymatch buffer name+binding, nonzero shape/range within declaredsize with uint64 arithmetic, unique memberpaths and generatednames perstage, flags0/1; scalar FP3232, vectorwidth1..4, columns1..4, arraylengthpositive. Preserve all stride values, no transpose/defaultbinding policy. Attributes columns1,arraylength1,width1..4, FP3232, unique slots0..15 and nonemptyfullsemantic. Pixel samplers Dim2D/sdk15 or DimCube/sdk17, uniqueunits0..15; retain logical names/bindings as authored, no default resources. Names acrosscategories in one SDKprogram cannot conflict.

Production implementation detail types must also follow one-type-per-file: the SDK attribute entry, constant element, constant group, program view and active write range belong in the five dedicated Ps3Rsx* headers above, namespace helengine::ps3, with existing substantive field/type documentation retained. The validator includes them and keeps algorithms unchanged. Do not hide multiple native model structs in its anonymous namespace. These are implementation records, not new renderer API promises.

- [ ] Implement raw SDK validation before SDK helper use (this reader/validator uses no SDK helper at all). Mirror structural logic from builder/shaders/Ps3RsxProgramReader.cs excluding dump checks because P3SP intentionally omits dumps: header36, magicVP0x5650/FP0x4650, countsU16 offsets6/8/10, table offsetsU32 at12/16, microcodeoffset20; attributes12bytes, constants28bytes; positiveinstructioncount; checked ordered/nonoverlapping tables and16alignedmicrocode bounds. Every referenced SDK name must terminate within blob and be strict UTF8; zero only allowed for internal/continuation constants, not attributes. Named parameters unique.

- [ ] Validate constant groups elementcount>0 and withinrecordcount, continuation names zero or identical, internalflag0/1; continuation count may be zero (it belongs to the head), so never interpret it as a new group. Preserve SDK record types; check head type/count against container mapping. Vertex register indices must be below512: the pinned RPCS3 revision2d103407 rsx_methods.h explicitly owns transform_constants as std::array<u32[4],512>. This is the tested runtime's addressing bound, not a claim that all hardware shader resource budgets are satisfied. Do not use legacy compiler parser.h MAX_NV_VERTEX_PROGRAM_PARAMS96 as the register bound. Fragment indexUINT32_MAX denotes no relocations; otherwise validate tablecount/range and no overlap with SDKtables/microcode, offsets4aligned and whole writewidth in microcode. Width comes from head type: float/bool1word, float2/bool2 2, float3/bool3/matrix3x3/4x3 3, float4/bool4/matrix3x4/4x4 4. Reject unknowntypes. Duplicate offsets within one element allowed; different active mapped elements overlapping written word ranges rejected. Do not delete/deduplicate stored SDK data.

- [ ] Cross-check each serialized uniform generatedname against unique named SDKconstant and exact DataType/ElementCount; each attribute against vertex SDKattribute name/type/index; each pixel sampler against pixel SDKattribute table sampler name/type/unit. Also iterate SDK constant groups: every group containing a non-internal element must have a serialized generated-name mapping. Container bytecode copiedverbatim. Missing/mismatched mapping is failure, never selection of builtin fallback. Unmapped SDKinternal constants are allowed only as validated SDK records; no invented runtime uniform values in this task. A negative test removes one actual fixture uniform mapping in memory without changing SDK bytecode and requires the missing-mapping diagnostic plus unchanged caller output.

- [ ] Native positive test asserts exact identity strings including UTF8, sourcehash length64, nonblanktoolchain, stageorder/entrypoints, raw byte-for-byte equality against separatelycopied vertex.bin/pixel.bin, Frame80/Transform0/Tint64, sourcePOSITION/TEXCOORD semantics, samplerAlbedo/LinearSampler Dim2D and SDKtype15. Check all listed metadata fields with explicit expectations or relationships to validated SDK records; do not merely roundtrip via another production encoder.

- [ ] Negative tests on fresh memorycopies: truncation at every structural region, wrong magic/version/total/stage, oversizedstring/blob/list, trailingbyte, NUL/overlongUTF8/surrogate/truncatedUTF8, unknownscalar/flags/dimension, duplicatebuffer/attribute/unit, uniformrangeoverflow, SDKbadmagic/tableoverlap/nameunterminated/constcontinuationcount/relocoutofbounds/fullwidthoverflow, missinggeneratedname/mismatchedSDKtype/index. A small test-only bounded walker can locate mutationoffsets; never modify filefixtures. Verify failed Read leaves sentinel calleroutput unchanged. At least20cases must execute on PPU.

- [ ] Run test script GREEN via existing launcher, inspect exactTTY PASSsummary and no failingcase. Run standard native production compile once through scripts/build.ps1 -Mode Cube -OutputRoot <standardroot> with distinct BuildId to ensure wildcardincludednewCPP compiles withoutCore; do not launchdiagnosticgame or modifybuildflags. `Cube` is the existing script's native non-Core build mode; `Native` is not a valid mode. Report actualcompiler/runtimeevidence and remaining native renderer/material work. Parent reviews and commits namedfiles only after verification.

## Self-review

This slice provides bounded CPU decoding and honest PPU interoperability tests, not authored shader rendering. Existing SDKsource validates actualwritewidth and FIFO semantics. The parser is separable fromCore, respecting native-only scope and avoiding generatedserializer changes. Every reviewfocus has negative/runtimecases. Register addressing bound is evidenced by pinned emulator implementation; hardware resource acceptance remains a separate rendering gate. No SDK bytes are rewritten and no GPUallocation occurs here.

Parent environment check: configured RPCS3 env variable is absent. Explicit verified executable location from docs/Validation.md is C:/dev/helworks/emus/rpcs3-v0.0.42-20013-2d103407/rpcs3.exe; pass it to the test script, do not change user/global settings. No running RPCS3 process was observed at plan preparation; recheck before launching.
