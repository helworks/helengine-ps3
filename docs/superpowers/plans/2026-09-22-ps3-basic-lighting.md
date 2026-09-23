# PS3 Basic Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add basic native PS3 scene lighting for materials marked lit and clear the frame using the active camera's authored clear settings.

**Architecture:** Carry cooked model normals into the RSX vertex buffer. Feed scene ambient and directional light values into a lit shader path while preserving the existing unlit path for materials whose `Lit` flag is false. Move frame clearing to the point where the active camera is known, preserving the existing fallback clear for the diagnostic/no-camera path.

**Tech Stack:** C++ native PS3 adapter, Cg shaders compiled only in the repository's Docker SDK image, PowerShell contract tests, standard platform build and RPCS3 runtime validation.

**Spec:** Approved in-chat design: respect material `Lit`; add basic ambient/directional light support; use active camera clear color. Native-only, this repository only; no authored project edits.

## Global Constraints

- Keep the implementation under `src/platform/ps3/` and `shaders/`; do not modify generated shader output.
- Preserve material behavior when `Lit` is false.
- Use the standard platform build path and the repository Docker image for all SDK shader/compile work.
- Do not use worktrees or modify the authored project.
- Preserve existing unrelated changes in the working tree.

## Review Focus

- Missing or empty normals: lit geometry must not read past the cooked normal array or silently submit invalid geometry.
- No ambient/directional lights: lit material behavior must remain defined and visible rather than black.
- Disabled camera color clear: color-buffer behavior must honor the camera setting; depth clearing remains independent.
- Color channel conversion: authored float clear-color channels must be clamped and packed in the RSX's expected channel order.
- Unlit material: its existing output must remain unchanged by scene lighting.

---

### Task 1: Lit mesh and shader path

**Files:**
- Modify: `src/platform/ps3/Ps3Vertex.hpp`
- Modify: `src/platform/ps3/Ps3RasterRenderer.hpp`
- Modify: `src/platform/ps3/Ps3RasterRenderer.cpp`
- Modify: `src/platform/ps3/Ps3RenderManager3D.cpp`
- Modify: `src/platform/ps3/Ps3RuntimeAssets.hpp`
- Modify: `shaders/unlit.vcg`
- Modify: `shaders/unlit.fcg`
- Test: existing PS3 build-contract tests plus native build/runtime checks.

**Interfaces:** Renderer submissions consume vertex position, normal, base color and a lit/unlit selector; the renderer receives ambient and directional light inputs from the scene render manager. Mesh normals come from `ModelAsset::Normals`; `Ps3RuntimeMaterial::Lit` controls whether the lit path is selected.

- [x] Inspect material/submesh association and the generated light APIs, then write a focused build-contract test for `Lit` preservation and required normal/light shader inputs.
- [x] Run that test and confirm it fails against the current renderer/shaders.
- [x] Extend the PS3 vertex format and upload cooked normals; fail visibly when a lit model lacks usable normals.
- [x] Gather supported ambient/directional scene light values and submit them to a lit shader path; keep unlit output unchanged.
- [x] Compile the shader and native executable through the Docker-backed PS3 build; run the focused tests. The final standard platform rebuild was blocked before native compilation by an unrelated authored-project C# error, so the PS3 repository Docker-native build was run against Core/cooked outputs from successful standard build #04.

### Task 2: Camera-authored frame clear

**Files:**
- Modify: `src/platform/ps3/Ps3Video.hpp`
- Modify: `src/platform/ps3/Ps3Video.cpp`
- Modify: `src/platform/ps3/Ps3BootHost.cpp`
- Modify: `src/platform/ps3/Ps3RenderManager3D.cpp` if needed to expose the selected camera settings.
- Test: focused build-contract coverage and standard platform build.

**Interfaces:** The frame clear consumes `ICamera::get_ClearSettings()` when an active scene camera exists; the legacy fixed clear remains the diagnostic/no-camera fallback.

- [x] Write a failing build-contract test proving the frame clear uses active camera settings and handles disabled color clearing.
- [x] Run the test and confirm the expected failure.
- [x] Apply camera color/depth clear settings before scene draws, packing clamped `float4` channels into RSX A8R8G8B8 order; preserve fallback behavior.
- [x] Run focused tests and launch the standard-build scene in RPCS3; inspect runtime log evidence (no screenshots). Final native/shader refinement was rebuilt via the repo Docker script, with RPCS3 confirming `cube_test` loads, camera blue clear, lighting values, and repeated draw submissions.

## Verification

- `scripts/tests/build-contract.tests.ps1` passes.
- The standard `scripts/build-platform.ps1` build succeeds using the existing Docker image and `.ps3` workspace-owned build roots.
- RPCS3 logs show scene camera/light/material values and successful draw submissions; no generated shader output is edited by hand.
