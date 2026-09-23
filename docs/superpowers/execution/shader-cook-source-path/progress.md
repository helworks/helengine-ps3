# Source-path prerequisite execution

Approved by Helena. Existing checkouts only; no worktrees, commits, cleanup or independent whole-branch review.

Preflight: Task 1 additive DTO preserves legacy constructor; Task 2 consumes its new overload in the existing service. All four named product/test paths clean before execution. Test artifacts and process-local TEMP/TMP use the approved source-path build root. Prior codebase-memory attempt failed with Transport closed; exact source evidence in approved plan, graph coverage unknown.

- Baselines: PS3 `ef27238ed85fe3218618a752c11d9eeede0a75a2`; shared `72a4fcbfd6b2fa6cc2c59a618c09eb4a49bf9637`.
- Task 1: complete, uncommitted. Luna/high `01a0d009-7bb6-7e51-a31c-18202988e29b`; Terra/high reviewer `01a0d00f-f2ae-7a83-ab85-2ee2e925acfb`: spec compliant, quality approved, no findings. Controller checked diff and log evidence: intended missing-API RED, 12/12 GREEN, no changed-file warning locations. Extra editor baseline 15/17: two cases lack installed windows descriptor; outside DTO scope.
- Task 2: implementation complete, uncommitted. Luna/high `01a0d011-7856-7c91-a811-6a349476d5f1`; Terra/high reviewer `01a0d01a-cb05-7d90-8b21-05456e79eaf6`: spec compliant, quality approved, no findings. Controller checked actual diff and logs: RED 2 expected HasSourcePath failures; GREEN 2/2; final class 16/18, same two missing Windows descriptor failures as baseline. Existing warnings in unchanged service lines remain. Initial testhost aborted for missing CoreUtilities; invocation-only CopyLocalLockFileAssemblies=true resolved it, no tooling source edits. No Docker rerun for metadata-only change.

This prerequisite now preserves real source paths for includes; it does not implement backend/cooker registration, dependency-complete caching, artifact encoding, or runtime shader loading. Include-aware consumers must reject explicit legacy absence. No deferred code findings; no commits or broad independent review.
