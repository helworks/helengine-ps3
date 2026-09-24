# PS3 external builder registration implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Luna implements in the current checkout; no worktrees.

**Goal:** Register a builder compiled into the standard external build root without copying outputs into the repository or touching global engine registration.

**Architecture:** Extend the existing isolated registration script with one optional explicit assembly path. Keep current default behavior and all registry validation unchanged. Add behavioral tests using isolated test projects/settings and dummy assembly files, since the script records a path and does not load the assembly.

**Tech Stack:** Windows PowerShell, existing scripts/register-platform.ps1 and ps3-paths.ps1.

**Spec:** docs/superpowers/execution/ps3-shader-integration/progress.md, standard build input section. This is registration plumbing, not shader/runtime acceptance.

## Global constraints

- Parent plans; Luna implements. Only named files, no worktrees, screenshots, push, global registry or authored-project changes.
- Preserve dirty Makefile/scripts/build.ps1/scripts/tests/build-contract.tests.ps1.
- Test artifacts only C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/registration; no system temporary directories.
- Do not change the default registry location or project path for existing callers.

## Review focus

1. Explicit path containing spaces is preserved as one normalized path.
2. Relative explicit path is rejected rather than depending on caller cwd.
3. Missing/directory assembly paths fail before settings/generated-root creation.
4. Existing foreign-platform settings remain byte-for-byte untouched on refusal.
5. The target project requiredEngineVersion remains authoritative, not builder version or a hardcoded value.

### Task 1: Optional explicit assembly path with behavioral tests

**Files:** modify scripts/register-platform.ps1; create scripts/tests/register-platform.tests.ps1; update docs/Ps3ShaderCooking.md only after its cooker worker has finished writing it, otherwise return requested documentation text to parent.

**Interfaces:** new optional `[string]$BuilderAssemblyPath = ''` parameter. Empty/whitespace uses current `$repoRoot/builder/bin/$Configuration/net9.0/helengine.ps3.builder.dll`; nonblank requires fully qualified Windows path, normalizes with GetFullPath, then passes existing leaf check. Existing callers unchanged.

- [ ] Add behavioral RED tests with required explicit `-OutputRoot`. Create unique test directory there, authored project fixture with `requiredEngineVersion="test-registration-version"`, dummy .dll file with spaces in its path and isolated -StateRoot. Invoke script with -BuilderAssemblyPath, assert process exit0, parse resulting platforms.json and compare assembly path/project version/player root/plugin path. Assert no repo default registry changed by comparing its hash/existence before and after.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/register-platform.ps1 `
  -ProjectFile $fixtureProject -StateRoot $fixtureState -BuilderAssemblyPath $fixtureAssembly
if ($LASTEXITCODE -ne 0) { throw 'Explicit external registration failed.' }
```

- [ ] Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/tests/register-platform.tests.ps1 -OutputRoot C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/registration/red`; expected unknown parameter before implementation, retain bounded log.

- [ ] Implement path resolution before input checks and any writes:

```powershell
if ([string]::IsNullOrWhiteSpace($BuilderAssemblyPath)) {
    $builderAssembly = Join-Path $repoRoot "builder\bin\$Configuration\net9.0\helengine.ps3.builder.dll"
} else {
    # Windows PowerShell 5/.NET Framework may not have IsPathFullyQualified.
    # Require rooted drive+separator or UNC server/share, not drive-relative C:foo or root-relative \foo.
    if ($BuilderAssemblyPath -notmatch '^(?:[A-Za-z]:[\\/]|[\\/]{2}[^\\/]+[\\/][^\\/]+[\\/])') {
        throw 'BuilderAssemblyPath must be fully qualified.'
    }
    $builderAssembly = [IO.Path]::GetFullPath($BuilderAssemblyPath)
}
```

Retain existing Test-Path Leaf gate, normalize through GetFullPath, mention optional parameter in synopsis/example. No changes to manifest schema or source/generated-root semantics.

- [ ] Add all five focus regressions. For relative/missing/directory paths assert no user-settings or generated-core directory created. For foreign registry create fixture with another platform, assert nonzero and exact original bytes. Test omitted parameter via script AST/default-resolution check without building into repo; do not fabricate an existing repo-local assembly. Test explicit Debug invocation still uses specified external path rather than fallback Debug directory.

- [ ] Run GREEN command with new output root registration/green. Capture full logs to build root but cap console output. Preserve all fixture outputs for review, no recursive cleanup. Return report and changed paths, no commit. Parent runs fresh test and commits named files after scoped review.

## Self-review

No pipeline registration state is changed by implementation itself. Tests operate only synthetic project/settings roots. The optional argument is sufficient for --artifacts-path output DLL paths. Runtime feature enablement stays separate. Every review focus has a behavioral check; backward compatibility for default path is checked without writing repo build outputs.
