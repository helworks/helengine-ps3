# PS3 compiler native library in an external builder

Parent plan; Luna implements in the existing checkout. No worktrees.

## Evidence and goal

Standard DemoDisc build attempt02 terminated session42138 exit1 while cooking the standard HLSL. `Ps3ShaderSourceCompiler.EmitGlsl` combines AppContext.BaseDirectory with runtimes/win-x64/native/spirv-cross.dll. In the editor host this resolves beneath the editor publish directory, not the externally registered builder. The packaged DLL exists beside the builder under standard-scene/b1/bin/helengine.ps3.builder/release/runtimes/win-x64/native (2778624bytes). Fix the source's package-relative resolution; never copy a DLL into generated editor output to hide the problem.

## Exact ownership

- Modify builder/shaders/Ps3ShaderSourceCompiler.cs.
- Add builder.tests/Ps3ShaderCompilerLibraryPathTests.cs.
- Add docs/Ps3ShaderCompilerNativeDependencies.md.
- Task report only under .superpowers/sdd/2026-09-24-ps3-compiler-plugin-library/.

No shared/editor/build-script/native-C++ changes. Keep existing Windows-x64 library layout; adding other host platforms is not this fix. No fallback to cwd, editor base directory, environment search or system-installed binaries.

## Implementation

1. Add focused tests first for a pure internal static ResolveSpirvCrossLibraryPath(string compilerAssemblyLocation) helper on Ps3ShaderSourceCompiler. Given an absolute external plugin assembly file path, return its own directory plus runtimes/win-x64/native/spirv-cross.dll, irrespective of AppContext.BaseDirectory. Cover a path with spaces, null/empty/whitespace, relative or drive-relative paths, and a directory-only path. Invalid required location must throw explicitly. The pure helper computes/validates a path without creating files or loading the library.
2. Run the focused expected missing-helper RED. Production bug RED is independently preserved in standard-scene/standard-build-02.stderr.log.
3. Implement the helper with fully-qualified validation, Path.GetFullPath and Path.GetDirectoryName; reject missing file/directory components. In EmitGlsl call it with typeof(Ps3ShaderSourceCompiler).Assembly.Location. Keep the existing File.Exists failure and Cross.CreateDefaultContext with the exact returned path. Empty assembly location (such as a non-file-backed load) is an explicit error, not a default directory.
4. Run focused GREEN and existing Ps3ShaderSourceCompilerTests if that exact test class exists (otherwise report the actual bounded compiler test names before selecting). No Docker/SDK/emulator is needed for the host frontend translation tests. Parent will rebuild the Release builder, update isolated registration and repeat the same standard build to prove the external-host boundary.
5. Document the package-relative layout, why host AppContext.BaseDirectory is not authoritative, and explicit missing-package/non-file-backed failures. Report exact files, commands, results and remaining real-host gate; no commit.

## Constraints and outputs

Use apply_patch. One type per file, substantive XML documentation, no tuples/local functions/nullable reference annotations. All artifacts/logs/TEMP/TMP under C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/compiler-host with short run IDs, restoring prior TEMP/TMP in finally. Byte-cap command output. Preserve other workers' edits; own only the three named files. Do not spawn agents, take screenshots, launch an emulator, commit or push. Parent owns review and commits.
