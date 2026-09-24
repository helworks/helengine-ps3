# PS3 standard build duration implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans. Parent plans; Luna implements.

**Goal:** Let a standard PS3 project build explicitly select the existing native run-duration diagnostic, so real-scene acceptance can exit normally and verify drain/shutdown.

**Architecture:** Ps3NativeBuildExecutor forwards one optional environment value to the existing `scripts/build.ps1 -RunSeconds` argument through ProcessStartInfo.ArgumentList. Unconfigured builds remain unchanged. No alternate build pipeline, generated-output editing, runtime file protocol or emulator termination mechanism.

**Tech Stack:** C# net9, existing ProcessStartInfo executor, xUnit pure argument tests, Windows PowerShell native wrapper.

**Spec:** `docs/superpowers/plans/2026-09-24-ps3-standard-scene-runtime.md` Task2 acceptance; existing `scripts/build.ps1` declares `[ValidateRange(0,3600)][int]$RunSeconds=0`, and Ps3BootHost uses that compile-time value for normal loop exit and ordered drain.

## Global constraints

- Existing checkout only, no worktrees/nested agents/commits/pushes/screenshots. Parent reviews and commits.
- Own only three named files below. Preserve other workers and dirty production Makefile/build scripts. Do not change scripts/build.ps1, shared code, project settings, generated Core or native runtime.
- All .NET outputs/logs/TEMP/TMP under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/run-duration`, use short run IDs. Restore TEMP/TMP in finally. apply_patch for source edits.
- No Docker or RPCS3 required for pure tests; future real standard build proves the forwarding end to end. Do not claim runtime shutdown from these unit tests.
- One type per file, substantive /// documentation, no nullable reference annotations, tuples or local functions. Existing InternalsVisibleTo in the builder already enables internal-helper tests.

## Review focus

1. Unset/empty option leaves the existing argument list untouched.
2. Whitespace, signs, fractions, overflow and out-of-range input fail before mutation/process launch.
3. Accepted input becomes two argument tokens, never command text or shell interpolation.
4. Build actually calls the tested helper with the named environment value before process.Start.
5. Normal Core-mode path and existing validation/cancellation/output checks remain unchanged.

## Task 1: Optional duration forwarding

**Modify:** `builder/Ps3NativeBuildExecutor.cs`.
**Create:** `builder.tests/Ps3NativeBuildExecutorRunDurationTests.cs`, `docs/Ps3NativeRunDuration.md`.

Add documented constant:
```csharp
public const string RunSecondsVariable = "HELENGINE_PS3_NATIVE_RUN_SECONDS";
```

Add a documented pure internal helper on the existing class (one type/file):
```csharp
internal static void AppendRunDurationArgument(ProcessStartInfo startInfo, string configuredValue) {
    ArgumentNullException.ThrowIfNull(startInfo);
    if (string.IsNullOrEmpty(configuredValue)) {
        return;
    }
    if (!uint.TryParse(configuredValue, NumberStyles.None, CultureInfo.InvariantCulture, out uint seconds)
        || seconds > 3600u) {
        throw new InvalidOperationException($"{RunSecondsVariable} must be an unsigned decimal integer from 0 through 3600.");
    }
    startInfo.ArgumentList.Add("-RunSeconds");
    startInfo.ArgumentList.Add(seconds.ToString(CultureInfo.InvariantCulture));
}
```
Use `System.Globalization`. Call this exact helper after existing argument construction and before constructing/starting Process:
```csharp
AppendRunDurationArgument(startInfo, Environment.GetEnvironmentVariable(RunSecondsVariable));
```

- [ ] Write pure tests first using a ProcessStartInfo with sentinel existing arguments. Test null/empty unchanged, explicit0,1,3600 and leading-zero input canonicalized; invalid whitespace-only, leading/trailing spaces, negative, plus sign,3601, overflow,fraction,exponent,letters and command-like string. On rejection assert the original argument list unchanged. Null startInfo must throw ArgumentNullException. Do not mutate process environment in unit tests.
- [ ] Run focused test compilation before helper exists; preserve expected missing-member RED in an owned log, not an unrelated compile error. Test command: `dotnet test builder.tests/helengine.ps3.builder.tests.csproj --filter FullyQualifiedName~Ps3NativeBuildExecutorRunDurationTests --artifacts-path <run-duration>/r1 --results-directory <run-duration>/results -p:HelengineRoot=C:/dev/helworks/helengine`, with owned TEMP/TMP and byte-capped console output.
- [ ] Implement the helper and actual call, preserving all existing behavior outside the option. No public settings schema, additional subprocess or global environment writes.
- [ ] Run focused GREEN tests; inspect the actual callsite and exact token count/order. Parent repeats focused test and requests bounded review.
- [ ] Document optional environment value0..3600, unset behavior,0 indefinite, compile-time effect, and example setting15 only around the standard `build-platform.ps1` invocation with finally restoring the prior value. The existing native loop starts the duration after initialization. Rebuilding is required; this does not forcibly terminate a hung startup or modify an already-running game. Reference normal drain/shutdown logs as the subsequent real-scene gate, not unit-test evidence.
- [ ] Report RED/GREEN, exact files, tests/commands and any live handles to `.superpowers/sdd/2026-09-24-ps3-standard-build-duration/task-1-report.md`. No commit; concise final status.

## Parent self-review

## Validation checkpoint (2026-09-24)

Implemented by Luna. Parent focused validation passed 18/18 with exit 0, using `run-duration/p1` and `run-duration/results/parent-duration.trx`. The scoped task review approved the three deliverables with no findings. Runtime exit/drain remains a real-scene acceptance gate, not a claim of these argument tests.

The existing native wrapper already supports this exact range and behavior; the sole missing handoff is in the PS3-owned builder. The option is explicitly opt-in and restored after validation, so normal clone/build behavior and the user's standard build path remain unchanged. Ownership is disjoint from material and uniform tasks. Unit coverage proves argument validation only; final real-scene acceptance must still observe normal loop exit, successful drain and resource retirement.
