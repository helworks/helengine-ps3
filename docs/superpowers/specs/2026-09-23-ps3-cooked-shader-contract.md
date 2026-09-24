# PS3 cooked shader/runtime contract

This is the parent design for the next integration slice, not an implementation or acceptance report. It extends the approved HLSL integration design without requiring generated Core or shared material-schema changes.

## Decisions

Use a PS3-owned binary pair artifact, not two anonymous raw stage blobs. One artifact identifies the authored shader asset, both program names and the variant; it carries unmodified RSX binaries plus the source-to-RSX binding contract. Materials name that artifact explicitly. Missing or invalid content is a visible load failure, never a request to select the embedded diagnostic shader.

The native loader will use the content stream supplied by Core. Do not hardcode host paths or bypass it with a second content-root convention. A loaded program owns its CPU binary storage and fragment microcode allocation. The renderer owns the program cache and releases GPU allocations only after the existing successful video drain. Material disposal must not free a program still in use by other materials or queued GPU work.

Native shader diagnostics remain the existing no-authored-scene cube path. Authored content takes the cooked program path. A legacy material may remain readable while the migration is under construction; the completed shader-enabled material path must not silently fall back on missing program identity.

## Artifact identity

The lookup key consists of four exact ordinal UTF-8 strings: shader asset ID, vertex program name, pixel program name, variant. Path-safe identifiers use lowercase SHA-256 over each length-prefixed UTF-8 component in that order; no delimiter concatenation or lossy sanitization. Relative path is `shaders/<64-hex-digest>.ps3sp`.

Source hash and toolchain identity are recorded for provenance but are not part of the lookup path: a material should not need compiler state to resolve its pair. Re-cooking the same lookup key replaces only that owned artifact. Distinct assets/program pairs sharing a variant cannot collide by construction of the path key; duplicate identical dependency requests are compiled once per cooking invocation.

## Binary encoding proposal

All container numeric fields are unsigned 32-bit big-endian, except four raw magic bytes. Strings use a byte length followed by strict UTF-8 bytes, without a terminator or implicit alignment. Length-delimited RSX blobs are copied byte-for-byte; their internal SDK encoding remains untouched. The native loader copies them into suitably aligned owned storage before calling any SDK pointer-returning function.

Header: magic `P3SP`, version `1`, exact total byte length. Identity strings follow in the order above, then source hash and toolchain identity. Next are two stage records in fixed vertex-then-pixel order, followed by pair attributes and pixel sampler mappings. Exact record tables must be frozen in the implementation plan before writing the serializer.

Stage records need stage identity, entry point, raw RSX byte length/blob, declared buffer descriptors and active uniform mappings. Uniform records preserve buffer identity/binding/declared size, member path, source byte offset/size, scalar type, vector width, matrix columns, array length/stride, matrix stride, row-major flag and generated SDK name. The SDK binary already carries constant register/fragment relocation records; do not create a second independently editable relocation table. Resolve the generated name against the validated program and cross-check type/count. Physical source layout remains distinct from the shared engine's logical register shifts.

Attribute records preserve full authored semantic, actual hardware slot and scalar/vector shape. The runtime matches semantic to its owned vertex layout; it does not treat the reflected source location or array ordinal as a hardware index. Unknown semantics or unavailable vertex data are explicit failures.

Pixel sampler records preserve authored texture and sampler names/bindings, combined generated name, dimension and actual texture unit. Version 1 accepts pixel Texture2D and TextureCube: the actual ForwardStandardShader retains both dimensions. Vertex textures and other dimensions are rejected explicitly. Runtime support must provide both dimension contracts before enabling authored shaders. The present pair map does not attach a stage to sampler entries, so cooker validation checks vertex translation samplers before serialization rather than guessing ownership from names.

Bounds for the implementation plan: at most 16 MiB per pair artifact; strings at most 64 KiB; each record count at most 4096; each stage program at most 1 MiB. Readers reject unknown versions, truncation, invalid UTF-8, embedded NULs in names, trailing bytes, invalid stages/enums/flags, arithmetic overflow, out-of-bounds source members and malformed SDK headers/tables before GPU allocation. Empty required identity and zero-sized required buffers/programs are errors. These bounds constrain this format version, not all hypothetical RSX content.

## Cooker and registration

Implement IPlatformShaderArtifactBuilder on the existing PS3 builder. Resolve sources solely from the supplied request and use SourcePath with the shared filesystem include resolver; the source text in the request remains authoritative. Legacy source DTOs without provenance fail explicitly when cooking nonempty shader dependencies.

The backend contributor must be discoverable without starting Docker or requiring a present SDK lock merely to list platforms. Resolve the existing explicit HELENGINE_PS3_REPOSITORY_ROOT and compiler configuration only when compilation is requested. A shared-service invocation needs an explicit working-root configuration; a cooker invocation has the build's explicit CookRootPath and must keep compiler jobs in an owned build location, not system temp. Do not derive a source checkout or build root from the current process directory.

Compile requested authored source, stage entry points and variants through the shared backend; take Translation/Program from its typed result, reconcile with Ps3ShaderBindingMapBuilder, and serialize once. Do not invoke a second pair compiler after the service already compiled both stages.

StandardShader variant entry points/defines come from StandardShaderVariants.All. Nonstandard source convention must be explicit and narrow (VS/PS with a declared default variant), with unsupported selections rejected rather than inventing entry points. Actual standard-source conversion is an acceptance gate before finalizing variant support.

## Material and runtime integration

A PS3-owned material envelope must carry shader selection and surface state without editing PlatformMaterialAsset or generated serializers. Its reader is native-only at BuildMaterialFromCooked(path, source), before the current generic deserialization branch. It preserves material identity, base color, lighting and double-sided/vertex-color choices, and explicit texture references. The cooker reports the same shader dependency that the envelope names.

The initial renderer upload contract should follow the shared StandardShader's authored names rather than current embedded `mvp`/`lit` names. World, world-view-projection, normal transform, camera position, material flags, light buffers and surface buffers need an explicit provider. Unknown active buffers/members must fail unless the material supplies their data; do not zero-fill unknown required uniforms. Optimized-away resources need no upload.

Texture validation needs real UV data in Ps3Vertex (currently absent), material texture selection, and owned RSX texture storage. A nonuniform RGBA8 fixture avoids adding an image decoder just to establish sampling correctness. Verify SDK texture calls, format, pitch and alignment in the locked Docker image before implementation. No claim of general texture import support follows from this fixture.

## Acceptance gates

1. Cooker tests preserve identity, provenance, include behavior, variant definitions, binding metadata and byte-exact SDK blobs. Duplicate requests and multiple assets sharing a variant have deterministic distinct outputs; malformed requests fail before published declarations.
2. Native decoder tests read producer-generated fixtures and reject malformed lengths/layouts before allocation. Run native compilation/testing only in the Docker image.
3. Real cube_test uses the authored HLSL-derived pair, selected material color, scene lighting, and camera clear color. Log the artifact identity and active program path. Deterministic pixel probes must distinguish material/lighting behavior from a clear screen, not merely count draw calls.
4. A nonuniform textured fixture verifies UV orientation and sampler mapping independently.
5. Enable the platform shader feature only when these gates pass; current compiler tests and draw-count probes alone are insufficient.

## Scope still requiring implementation-plan resolution

Actual standard-source conversion may expose RSX instruction/resource limits. Do not specialize away user-selected shader behavior or change shared builtins without the necessary authorization. The serializer tables, material envelope fields, normal/camera/lighting upload convention, exact texture SDK calls and concrete standard-build invocation must be made precise from source/toolchain evidence in the next plan. This document is deliberately not a worker task brief.
