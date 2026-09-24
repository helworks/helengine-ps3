# PS3 fragment relocation identity implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Parent designs, Luna implements, existing checkout only.

**Goal:** Accept repeated SDK relocation entries for the same constant element while continuing to reject collisions between distinct elements/uniforms.

**Architecture:** Keep SDK bytes and parsed relocation sequences immutable. Collision validation tracks unique offsets within each element before adding them to the pair-wide ownership set. Duplicate offsets within that one element are idempotent writes of the same constant; offsets owned by different elements remain errors.

**Tech Stack:** Existing C# binding-map builder and xUnit synthetic DTO fixtures.

**Spec:** docs/superpowers/specs/2026-09-23-ps3-cooked-shader-contract.md; preserve original SDK data and reject genuinely ambiguous bindings.

## Global constraints

- No shader/SDK output rewriting, deduplication of stored metadata, shared HLSL changes, or new compiler patch.
- Own only builder/shaders/Ps3ShaderBindingMapBuilder.cs, builder.tests/Ps3ShaderBindingMapTests.cs and task report.
- No worktrees, nested agents, commits/push/screenshots; parent reviews/commits.
- Test outputs only C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/relocation. Redirect TEMP/TMP to fixture-storage and restore.
- Preserve concurrent cooker/registration/SDK files and unrelated dirty build scripts.

## Root cause evidence

Parent actual HLSL acceptance after SDK capacity fix: both stages compile and Ps3RsxProgramReader accepts; map fails at duplicate offset3072 in ValidateNonOverlappingBindings. TRX backend/results/parent-standard-capacity.trx. New fragment artifact: backend/sdk-capacity-acceptance/backend-forward-standard/ps3-shader-77b827c3d23c4373a2b50a4ae4001d5b/program.bin under the standard integration build root.

Independent read-only header/table inspection found offset3072 repeated at entries0 and1 of constant record3, same exact ShadowBuffer_member_1. Sixteen further duplicates likewise all occur within a single constant record, not between records. Pinned PSL1GHT compilerfp.cpp NVFXSR_CONST handling allocates one constant slot per instruction under have_const, then appends fragment_program_data for each source operand without deduplication; repeated same-constant operands therefore record the same microcode address twice. This is not a collision between values.

## Review focus

1. Same element repeated offset must pass without altering its original list or SDK bytes.
2. Distinct uniforms sharing an offset must still fail.
3. Distinct elements of the same array/matrix sharing an offset must still fail.
4. Repeated vertex constant register remains invalid; no change to vertex validation.
5. A duplicate intra-element followed by another-element collision cannot hide that collision.

### Task 1: Element-local duplicate handling

**Files:** builder/shaders/Ps3ShaderBindingMapBuilder.cs ValidateNonOverlappingBindings; builder.tests/Ps3ShaderBindingMapTests.cs existing synthetic map helpers.

**Interfaces:** No API changes, no extra types. Build(vertexTranslation,vertexProgram,pixelTranslation,pixelProgram) continues returning the existing immutable map.

- [ ] Add failing pure fixture: pixel float4 constant with one Ps3RsxConstantElement containing offsets `[16,16,32]`, matching exact source/dump metadata and one declared buffer. Build must succeed; assert same parameter reference, same offsets `[16,16,32]`, same SDK bytes. Use existing Uniform/Translation/Program/Map helpers and valid active `#var float4 ... : : c[...] : ... : 1` format as existing uniform tests.

```csharp
Ps3RsxConstantElement element = new(0, null, new uint[] {16,16,32}, new uint[4]);
// Construct matching source/SDK parameter using existing test helpers.
Assert.Equal(new uint[] {16,16,32}, Assert.Single(map.Uniforms).RsxParameter.Elements[0].FragmentRelocationOffsets);
```

- [ ] Run focused RED with --filter FullyQualifiedName~Ps3ShaderBindingMapTests --artifacts-path C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/relocation/dotnet. Record actual duplicate-offset failure. If concurrent cooker files prevent build, report that promptly; do not edit another task's files or call that a valid RED.

- [ ] Implement only pixel inner loop change:

```csharp
HashSet<uint> elementOffsets = new();
foreach (uint offset in element.FragmentRelocationOffsets) {
    if (!elementOffsets.Add(offset)) continue;
    string reference = "f:" + offset.ToString(CultureInfo.InvariantCulture);
    if (!constantReferences.Add(reference))
        throw new InvalidOperationException($"Active pixel uniforms overlap fragment relocation offset {offset}.");
}
```

Create elementOffsets inside each element iteration, never per uniform or per stage. Keep stored list untouched. Update method comment to distinguish duplicate identical writes from conflicting values and mention SDK repeated source operands.

- [ ] Add negative regressions for all four remaining review inputs, ensuring each failure reaches overlap logic (match message and offset/register). Two uniforms can use distinct buffer/member/generated names; one matrix/array with two elements must have valid count/type/dump shapes before overlap check. Do not weaken layout validation to make fixtures pass.

- [ ] Run focused GREEN, then parent will rerun actual ForwardStandard and small-pair Docker tests with freshly rebuilt test assembly once concurrent cooker compiles. Preserve genuine errors if another integration mismatch appears. Do not edit backend tests or SDK patch/lock. Report code/tests/RED/GREEN and unresolved Docker acceptance; parent handles review and named-file commit.

## Self-review

The algorithm uses equality of element ownership, not arbitrary tolerance of overlaps. It leaves all generated bytes/metadata unchanged and preserves cross-element and vertex rejection. The original real failure and pinned SDK source establish why repeated identical addresses are legitimate. All five focus conditions have tests.
