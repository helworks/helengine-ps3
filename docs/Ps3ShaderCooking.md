# PS3 shader cooking

`Ps3PlatformAssetBuilder` exposes two optional shared capabilities: shader artifact cooking and shader backend registration. Builder discovery registers a lightweight PS3 backend descriptor without reading the SDK lock, requiring a configured repository, or launching Docker. The production backend is created on its first compile.

For a nonempty cook request, each complete shader dependency must have a matching source snapshot with a source hash, nonempty source text, and a fully qualified `SourcePath` whose parent directory exists. The cooker compiles the request's source text; it uses `SourcePath` only to resolve relative includes. The source file itself may be absent. Pair identity is the exact shader asset, vertex program, pixel program, and variant, so repeated identical dependencies compile once and distinct assets or program pairs produce distinct `shaders/<sha256>.ps3sp` files.

The exact `ForwardStandardShader` asset uses entry points and defines from `StandardShaderVariants.All`. Other assets support only the exact `default` variant, with `VS` and `PS` entry points. The cooker compiles both stages through `ShaderCompileService`, reconciles their PS3 bindings once, and serializes the returned SDK blobs into the existing P3SP format. It prepares every pair in memory before replacing final files; each individual replacement uses a temporary sibling file and an atomic move.

Set `HELENGINE_PS3_REPOSITORY_ROOT` to the fully qualified PS3 checkout containing `toolchain-lock.json`. This configuration is read only when compilation starts. Shared shader-service callers also set `HELENGINE_PS3_SHADER_WORK_ROOT` to a fully qualified compiler-job root. The cooker does not use that setting: it derives its job root from the request as `<CookRootPath>/.ps3-shader-work`. Both roots are explicit build-owned locations; neither is derived from the current directory or system temporary storage.

The platform's `shaders` feature remains forced disabled until the native runtime and material acceptance gates pass. Registering the cooker does not imply runtime shader support, and the existing material cooker remains unchanged in this slice.

The builder-registration helper accepts `-BuilderAssemblyPath <fully-qualified-path>` to register an external builder DLL while continuing to honor `--artifacts-path`. When `-BuilderAssemblyPath` is omitted, it preserves the prior repository-configured assembly path.
