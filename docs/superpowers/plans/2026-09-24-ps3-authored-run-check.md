# Authored scene log acceptance

Parent-authored Task2 checker slice; Luna implements, no worktrees.

## Scope

Modify only scripts/tests/ps3-runtime-visible-content.tests.ps1, add scripts/tests/ps3-runtime-authored-log.tests.ps1, and report under .superpowers/sdd/2026-09-24-ps3-authored-run-check/. Do not enable shaders yet or modify native sources, builder, docs owned by another worker, generated output or DemoDisc. Parent owns standard04 and emulator; no emulator/build by this worker.

## Contract

Keep the current default smoke mode unchanged. Add explicit -AuthoredShaders mode with required caller-supplied -ExpectedCenterRgb (six hex digits) and -ExpectedLit (0 or 1), plus bounded -ChannelTolerance default0 range0..8. Absence of either expected value in authored mode must fail, never infer expected color from observed pixels. The checker validates a TTY run, not artifact provenance or the independent CPU color oracle; parent verifies those separately.

In authored mode require cube_test scene loaded; exact ForwardStandardShader asset/vertex/pixel program READY and matching material variantForwardStandard; first authored DRAW Frame=0 with nonzeroindices and requestedLit; exactly one finite numeric record for each first-frame field World/WVP/Normal(16values), Camera/Base/Ambient/DirColor/DirDirection(4values). Parse numbers invariant-culture and reject NaN/Infinity/duplicates/malformed counts. Require first-frame pixelprobe readable1, corner6495ED, center distinct from corner and each channel within requested tolerance. Reject any PS3_*_ERROR or PS3_CONTENT_MISSING marker, PS3_EXIT other than run-duration-reached failed0 with positiveframes/draws, and absent PS3_SHUTDOWN exitCode0. Require CoreDisposed, DRAIN_COMPLETE, every authored cache SHUTDOWN ownedGpuBytes0 and at least one programCreated; ordering is CoreDisposed before drain before cache retire before finalshutdown. Reject contradictory or duplicate exit/shutdown/probe records rather than matching just one good line.

Actual log spelling is in existing native sources (read only): Ps3AuthoredProgram.cpp READY, Ps3AuthoredContentCache.cpp MATERIAL/CACHE_SHUTDOWN, Ps3RenderManager3D.cpp DRAW/VALUES, Ps3BootHost.cpp FRAME_PROBE/EXIT/SHUTDOWN, Ps3Video.cpp DRAIN_COMPLETE. Diagnostic embedded shader initialization messages may still appear at boot and are not alone evidence of fallback; successful authored selection/draw is required.

## Tests

Write a pure PowerShell5 test runner first, constructing synthetic TTY text only in a caller-specified fully qualified OutputRoot beneath the owned shader-integration build root. Do not write fixtures under system TEMP. Child invocation uses powershell -NoProfile -ExecutionPolicy Bypass. Include a valid unlit white record with explicit expectedFFFFFF/Lit0; valid lit example with independently specified expectedRGB; legacy smoke still passes; and mutations for missing/wrong shader, wrongLit, missing/duplicate/nonfinitevalues, unreadableprobe, clear-only/wrongcenter/wrongcorner, error marker, failed/duplicateexit, missingdrain, nonzeroGPUownership, wrongshutdown/order, missingexpectedparameters. Expected failure assertions must check meaningful diagnostic and nonzeroexit rather than accept any unrelated failure.

Observe RED against old checker, then implement and GREEN all named cases. Do not modify real TTY or loosen expectedcolor to observedpixels. All artifacts/logs/TEMP/TMP under shader-integration-20260923/authored-log-check, restored in finally. Preserve others' edits. No commits/nestedagents/screenshots. Return frozen source/test/report with commands and exact counts. This checker cannot certify lighting for an authored Unlit material; user decision on that scope remains pending.

## Bounded review fixes

Accept the three source-proven findings in task review.md. Require exactly one one-time CORE_AUTHORED_DRAW; add identical, contradictory and malformed duplicate tests. Retain material RGBA/flags and compare the selected draw Lit/Double against flag bits0/1; compare its Base vector with RGBA/255 using absolute tolerance1e-6, enough for float32 %.9g output but not visibly different values. Correct the synthetic lit-positive fixture's material flags as well as its draw. Add mismatch cases for Lit, Double and Base. Collect all CORE_STAGE records identifying stage=CoreDisposed before validating/counting, so malformed duplicates cannot be discarded. Add a malformed duplicate test. Preserve the real unlit run04 pass. Tests first RED for these cases, then GREEN all; no scope outside the same two scripts/report.
