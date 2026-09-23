# SDD ledger — plan: docs/superpowers/plans/2026-09-23-ps3-shader-backend-foundations.md

Helena approved with "vai man". Bases PS3 96ca68d, shared b806163d. Existing checkouts; no worktrees. Parent commits only named validated changes. No cleanup or independent whole-branch review without permission. Bookkeeping retained here per established user workflow.

| Check | Evidence | Result |
| --- | --- | --- |
| Task 1 API/tests | Optional policy branches before key/cache; tests include seeded key and legacy behavior | Consistent; key comparisons use ToString, not object reference |
| Task 2 API/tests | Required buffer collection; direct compiler-declared size query; fixtures updated explicitly | Consistent; size is compiled SPIR-V, not unoptimized source extent |
| Tasks 1/2 interface | No shared edited files; Task 2 final pure acceptance includes cache tests | Sequential reviews; no competing writes |
| Global constraints | Baseline unexpectedly already contains shared cache policy, before Luna dispatch; other native-cache/layout dirt excluded | Preserve existing UseSharedCache API; no runtime/editor/backend registration expansion |

Graph: list_projects failed Transport closed again. Coverage/freshness unknown; verified exact source evidence in approved plan used instead.

- Ruling: Preserve pre-existing IShaderBackendCachePolicy.UseSharedCache rather than rename to planned UseSharedCompileCache; observed service branch already implements cache bypass. New tests will use existing API, with baseline GREEN honestly reported if behavior already passes. Do not manufacture RED by removing another contributor's code. Same specified semantics; cost if wrong is later API reconciliation. Helena asked asynchronously whether these shared files may be included in commit; do not commit them pending response.
- Task 1: Luna/high 01a0d07b-a150-7d91-9095-aa9b2e4230f4, test implementation proceeds over preserved existing shared code. Review pending.
- Task 1: complete, uncommitted. Terra/high 01a0d085-376b-76d3-990c-b89724dc57a1: spec compliant, quality approved, no Critical/Important findings. Controller read final 18/18 log and confirmed shared source hashes unchanged from baseline.
- Task 1 minor (deferred): test backend Failure documentation says "next" although property affects every subsequent request until changed; no behavioral defect. Existing 41 core warnings on initial build are outside task; final incremental run had zero warnings.
- Task 2: dispatching Luna/high after clean named-file preflight. Task 1 accepted UseSharedCache API must remain unchanged.

Helena answered "Pode incorporar nesta etapa": existing shared cache policy/service files may be validated and committed with this task. Preserve existing UseSharedCache API and attribute baseline accurately.

Shared cache commit: `helengine 526e967d` contains only the two reviewed policy/service files. Named-file hashes matched preserved baseline immediately before commit. Task 1 report now includes exact process-local TEMP/TMP restoration wrappers. PS3 tests/docs will be committed with Task 2 after final acceptance; no push.

Task 2 checkpoint: expected missing Ps3ShaderUniformBuffer RED recorded. First focused run 61/62 failed existing negative export-root test because Path.GetTempPath() is now inside the permitted build root under required process TEMP redirection. Ruling: replace only that negative fixture with repositoryRoot/path-probe (no file creation); production guard unchanged. This preserves the test's outside-root intent. Cost if wrong: fixture might cease testing the intended boundary; existing base-root/traversal/prefix-sibling negatives and valid child tests remain.

Task 2: complete, final scoped review approved (Terra/high 01a0d091-c925-7b00-9192-2827a62e3168), spec compliant and quality approved, no findings. Luna/high implementer 01a0d087-0ff4-78b1-8088-3c2fe9b1ce9c. Controller read actual diff, focused log 62/62 and acceptance 125/125 pure + 7/7 Docker, exit0 run108367ae8a814e8f84795f548e4b8889. TRX linked results confirm execution of 18 cache, 6 buffer DTO and 16 reflection tests. No warnings at task source/test locations in final acceptance. Report contains exact process-local TEMP/TMP wrappers; parent verified finally restoration and separate acceptance process.

Both planned tasks complete. Only deferred minor is Task1 test-helper wording, plus existing out-of-scope core warnings. No independent whole-branch review performed. Shared cache policy committed526e967d; PS3 named product/test/docs changes ready for authorized commit. Compiler feature remains disabled; backend/cooker registration, artifact encoding and native runtime are not implemented here.
