# PS3 Native Material Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Decode actual P3MT material output on the PPU and verify its identity against the real cooked ForwardStandardShader P3SP artifact.

**Architecture:** Extract the already-tested bounded BE/UTF-8 primitives from the shader reader into a header-only utility shared by both artifact readers. A material DTO preserves all fields and a reader validates the fixed standard-schema contract. The standalone test uses real C# producer output and the unchanged native shader validator; Core/GPU attachment is subsequent work.

**Tech Stack:** C++17, C# net9 fixture producer, pinned Docker PPU compiler and shader converter, existing RPCS3 launcher.

**Spec:** `docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md` and committed `docs/Ps3MaterialArtifactFormat.md`.

## Global Constraints

- New material-only files and tests may be prepared now. Do not modify Ps3ShaderArtifactReader.cpp until the parent confirms its task is committed and ownership released. Parent writes plan; Luna implements in this checkout, no worktrees/nested agents.
- No generated Core, editor, shared engine, authored scene, production renderer, shader binary/header/dump changes. Keep shaders disabled.
- Preserve unrelated Makefile/build scripts and GPU worker changes. Header-only shared utility avoids changes to the GPU worker's link source list.
- All output/logs/TEMP/TMP under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/native-material`.
- All SDK/native compilation and conversion only current immutable Docker image. No host/WSL fallback, no screenshots.
- One production type per file, substantive documentation, no fabricated valid defaults. Only parent commits named validated files, no push.
- Existing P3SP wire format and public reader behavior remain unchanged. Source helpers move mechanically; don't redesign shader validation during extraction.

## Review Focus

- Cross-language UTF-8 semantics remain unchanged after extraction: rerun the existing native shader-reader suite.
- Optional empty texture is valid; whitespace/null-equivalent encoded NUL and invalid relative paths are not defaulted.
- Material identity/path is tied to the exact fixed standard pair; changing only the path or one identity field must fail.
- Caller output must remain unchanged on any malformed material, including errors in the final texture field.
- A well-formed material paired with a different well-formed shader must fail MatchesProgram; parser success alone is insufficient.

## Task 1: Shared primitives, P3MT reader and real-pair PPU gate

**Files:**
- Create `src/platform/ps3/Ps3ArtifactReadUtilities.hpp` (header-only, one static utility class).
- Modify `src/platform/ps3/Ps3ShaderArtifactReader.cpp` only to consume extracted primitive functions.
- Create `src/platform/ps3/Ps3CookedMaterial.hpp`.
- Create `src/platform/ps3/Ps3MaterialArtifactReader.hpp` and `.cpp`.
- Create `builder.tests/Ps3MaterialNativeFixtureTests.cs`.
- Create `tests/native/Ps3MaterialArtifactReaderTests.cpp`.
- Create `tests/native/material-reader.Makefile`.
- Create `scripts/tests/ps3-material-reader.tests.ps1`.
- Create `scripts/tests/ps3-material-reader-paths.tests.ps1` for the runner's pure fully-qualified/strict-child path regression (review refinement).
- Create `docs/Ps3NativeMaterialReader.md`.

**Interfaces:**

```cpp
struct Ps3CookedMaterial {
    std::string MaterialAssetId;
    std::string RendererFamilyId;
    std::string ShaderAssetId;
    std::string VertexProgramName;
    std::string PixelProgramName;
    std::string VariantName;
    std::string ShaderRelativePath;
    std::uint8_t BaseColorR;
    std::uint8_t BaseColorG;
    std::uint8_t BaseColorB;
    std::uint8_t BaseColorA;
    std::uint32_t SurfaceFlags;
    std::string DiffuseTextureRelativePath;
};
class Ps3MaterialArtifactReader {
public:
    static bool Read(const std::uint8_t* data, std::size_t size,
                     Ps3CookedMaterial& result, std::string& error);
    static bool MatchesProgram(const Ps3CookedMaterial& material,
                               const Ps3CookedShaderProgram& program, std::string& error);
};
```

Utilities expose static ReadU32(data,size,cursor,resource), ReadString(data,size,cursor,resource,bool allowEmpty=false), ReadUtf8CodePoint(data,length,cursor,codePoint), IsWhitespace(codePoint), matching current function signatures/semantics. ReadString's only extension is accepting length0 when explicitly allowEmpty; nonempty blank still fails. Keep all definitions inline in the header. The final state moves rather than duplicates ReadU32/UTF8/whitespace/string-validation logic; P3SP call sites use default allowEmpty=false. While shader-reader ownership is still active, create the utility for the new material reader but leave the existing shader source untouched; record the pending extraction and ask parent for its release before that last edit. Do not change list/blob limits or validator functions.

- [ ] **Step 1: Generate actual producer fixture and write RED tests.**

Add Docker-category test `ProducesNativeMaterialFixture`; require explicit fully-qualified HELENGINE_PS3_REPOSITORY_ROOT and HELENGINE_PS3_MATERIAL_NATIVE_FIXTURE_ROOT, latter a strict child of native-material root. Redirect temporary storage as usual. Cook an actual PS3 standard material with ID `Materials/Native-\u00e9`, base color `#C0201040`, lit=true, double-sided=true, vertex-color=modulate, texture-relative-path=`cooked/textures/fixture.hasset`. Serialize through the production Ps3MaterialCooker, never write envelope bytes by hand. Assert one shader dependency.

Read the actual shared `engine/helengine.editor/shaders/builtin/ForwardStandardShader.hlsl` from the explicitly resolved sibling HELENGINE_ROOT. Use the production shader artifact cooker with its real SourcePath/source snapshot/hash and exactly the material's returned dependency. Existing cooker tests provide request-constructor examples; no alternate compiler path or builtin specialization. Put its real artifact under the hash path in the fixture directory, and write the material producer's bytes as `material.p3mt`. A texture file isn't needed for this CPU parsing test: do not claim a content load was proven.

The PPU test opens `/app_home/USRDIR/content/material.p3mt`, decodes it, then opens the exact decoded ShaderRelativePath, reads its real P3SP through Ps3ShaderArtifactReader and calls MatchesProgram. Assert all four raw color bytes, flags7, exact UTF-8 identity, texture spelling, all four shader identities and path.

Write malformed cases by changing fresh in-memory copies only: null input; short header; wrong magic/version/total; trailing byte; truncated each string/color/flags/final texture region; string length>65536; total>1MiB; NUL/overlongUTF8/surrogate/truncatedUTF8/out-of-range Unicode; blank required IDs; foreign renderer; wrong shader asset/program/variant; different valid-looking 64-hex shader path; unknown flag bit8; absolute/drive-relative/backslash/colon/dot/dotdot/empty-segment/whitespace/control texture paths. Assert unchanged sentinel output on every failed Read. Optional empty texture must pass. Full-pair matcher test changes each shader identity independently on fresh owning DTO copies and rejects it. At least25 native cases execute on PPU.

Run the missing-material-reader compile or explicit stub behavioral RED; retain its real diagnostic. An unrelated upstream compile error is not valid RED.

- [ ] **Step 2: Extract shared primitives and implement exact material decoding.**

Enforce the committed P3MT table verbatim: 12-byte header; seven required strings in order; four raw RGBA bytes; U32 flags; one optional texture string; exact end. Total1MiB, text65536, strict UTF-8 and no NUL. Read into a complete local candidate, publish only after all checks. Catch errors to return false with nonempty field-specific diagnostics, preserving caller output.

The version1 PS3 standard material producer has exactly one supported pair. Require renderer `ps3-standard`, shader `ForwardStandardShader`, vertex `ForwardStandardShader.vs`, pixel `ForwardStandardShader.ps`, variant `ForwardStandard`. Its canonical path is:

```text
shaders/76a43ab5bef0cb1c1deca6d3a596d9a5e6f2ff86ce65b03d009a0d63b207b615.ps3sp
```

This constant was independently computed from four BE-length-prefixed UTF-8 identity strings and must be asserted against the production Ps3ShaderArtifactIdentity in the fixture producer test. No generic SHA implementation is needed to validate this fixed schema. Don't accept arbitrary identity/path pairs because they look syntactically valid. Future custom-material support requires an explicit extension, not a fallback here.

Flags may use only bits0..2 and must be preserved. Texture validation exactly matches producer: empty allowed; otherwise no leading/trailing slash, backslash, colon, NUL/control, empty or whitespace-only segment, `.`/`..`; preserve valid original UTF-8 bytes/case. Use the shared scalar decoder to identify Unicode whitespace and controls U+0000..001F and U+007F..009F, not only ASCII spaces. Do not normalize paths.

MatchesProgram compares all four identity strings with exact byte equality and validates the fixed material contract before acceptance. It does not substitute a program or do file I/O/GPU work.

- [ ] **Step 3: Build/run the PPU tests and unchanged shader-reader regression.**

New standalone Makefile links material reader, shader reader/validator and new test main with current pinned SDK rules; source read-only /src, output writable /out. Follow the existing validated shader-reader test Makefile/runner, not production build-script edits.

Runner requires explicit -FixtureRoot and -OutputRoot strict descendant of native-material build root, optional -RpcS3Path. Validate locked image/input hashes, use fresh build-id, stage producer files preserving relative shader path, write actual schema1 manifest/current lock hash, invoke existing launcher headless for CPU tests only. Require this run's terminal `PS3_MATERIAL_READER_TESTS passed=<n> failed=0`, at least25 cases, no stale logs. Refuse concurrent RPCS3 use and coordinate parent before launch; GPU worker uses the same emulator.

Also run existing `scripts/tests/ps3-shader-reader.tests.ps1` with its accepted arguments and a fresh native-reader output to prove helper extraction preserves the P3SP contract. Do not change its test expectations to achieve GREEN. Hash all producer fixtures before/after and reject mutations.

Review refinement: Windows PowerShell5 is the existing runner environment and pwsh is absent. Enforce fully qualified drive-absolute or complete UNC inputs before GetFullPath using compatible APIs; do not use unavailable Path.IsPathFullyQualified or accept root-relative/drive-relative values. The pure path test extracts only the validation function definitions through PowerShell AST, never executes the Docker/build body or writes test directories. Cover absolute child success, root equality with both trailing separators, ancestor, sibling-prefix, relative, drive-relative, and root-relative paths that would otherwise normalize inside the permitted root. Test the chosen complete-UNC behavior explicitly. Preserve strict descendant checks after normalization. The malformed trailing-byte case must repair its declared total length after appending, then require the exact-end/trailing-bytes error; keep a separate wrong-total-length case.

- [ ] **Step 4: Document and report.**

Document exact APIs, preserved flags, fixed standard identity/path, strict text/path behavior, producer-fixture commands and native test evidence. State clearly that the test doesn't yet load texture content or draw a Core scene. Report RED/GREEN, case counts, image/input hashes, command handles/results and changed files in the plan workspace. No commit by worker; parent verifies/reviews/commits.

## Parent preflight

The header-only extraction has one deliberate shared file modification and requires released shader-reader ownership. GPU Makefile needs no added link source because the utility is header-only. Material wire/API matches committed8180b3d; P3SP API/format unchanged. Reader output is the later Core material loader's exact handoff, not a second material format.
