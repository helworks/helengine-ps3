# PS3 v0.2: generated Core compiled into the native host

Status: approved for planning on 2026-09-16. No implementation has started.

**Goal.** Produce a PS3 executable that contains the generated Core C++ and completes a full
Core lifecycle on the console: construct, initialize with native adapters, run bounded
update and draw cycles, dispose, exit zero. The v0.1 cube keeps drawing underneath so a
video regression is visible immediately.

**Confirmed scope.** This milestone is the Core lifecycle gate only. Authored scene
loading and rendering is the next milestone and is not attempted here.

## Why this gate exists separately

The risky, unknown part of Core integration on PS3 is not the renderer. It is whether a
large generated C++ tree compiles and links for the Cell PPU with the pinned toolchain,
whether its allocation and exception behaviour survive on GameOS, and whether the codegen
profile describes PS3 correctly. Proving that against an empty scene isolates those
failures from renderer and content work. The Dreamcast handler draws the same line, with
`runDreamcastRuntimeInitProbe` separate from `runDreamcastAuthoredScene`.

## Reference intake

Verified directly during design:

- `C:\dev\helworks\helengine` contains no platform-specific code. A search for a `ps3`
  identifier across its C#, PowerShell and JSON returns nothing, and the existing handlers
  live in their own sibling repositories. Platform support is therefore added in this
  repository, not in the shared engine.
- `csharpcodegen/cs2.cpp/model/CPPRuntimeProfile.cs`: the `stl-lite` profile, which is the
  default the codegen CLI selects, sets `UseExceptions = false` and `UseRtti = false`,
  while keeping the standard string, vector and hash containers enabled.
  `CPPConversionRules.UseExceptions` separately defaults to true. Both statements are true
  about different objects, and the profile is what reaches generated output.
- `C:\dev\helprojs\demodisc` is the real reference project. Its name is `city`, it pins
  `requiredEngineVersion` to `1.0.0+ec2d7ff8a5b2624bbc51784bd9eb0c9babed1843`, and its
  supported platform list does not include PS3. `C:\dev\helworks\helprojs\city` is an empty
  stub and is not used.
- `helengine-dc/builder/`: twelve C# files. The definition factory is small; the bulk is
  material cooking, content staging, cooked-content validation and the startup catalog
  writer, all of which are authored-scene concerns this milestone does not need.
- `helengine-dc/Makefile`: the generated tree is compiled as the finalized unity
  translation unit plus every generated `.cpp` the unity file does not include, with an
  assertion that the two sets are disjoint.
- `helengine-dc/src/platform/dreamcast/DreamcastRuntimeHost.*`: the probe and authored
  entry points, the staged snapshot structs, and the adapter set Core is initialized with.

Reported by survey and to be re-verified against the pinned engine version before use: the
exact platform discovery chain through a user-settings `platforms.json`, the codegen CLI
flag spelling, the regeneration service's output layout, and the precise abstract member
lists of the render managers and input backend. The generated headers at the pinned engine
version are the authority, not this document.

## Architecture

Two halves that meet at one directory.

**The managed half** produces generated Core C++ for the `ps3` platform id and hands the
native build a generated-core root. It is a builder assembly in this repository, loaded by
the editor through reflection, plus the registration that makes the editor aware of it.

**The native half** compiles that root into the existing host and owns the Core lifetime
behind the same boot host that already owns video and the cube renderer.

The Core path is selected by an explicit build mode. With that mode off, the sources
compile to exactly today's executable and the v0.1 behaviour is unchanged; with it on, the
boot host gains a Core stage between renderer startup and the frame loop.

## Managed half

New files in this repository, modelled on the Dreamcast builder and reduced to the gate:

| File | Responsibility |
|---|---|
| `builder/helengine.ps3.builder.csproj` | Targets the shared engine by project reference, resolved through an overridable root property. |
| `builder/Ps3PlatformDefinitionFactory.cs` | Declares the `ps3` platform: display name, build profiles, graphics profile, codegen profile, runtime generation contract. |
| `builder/Ps3PlatformAssetBuilder.cs` | Implements the platform asset builder interface. For this milestone it regenerates Core and invokes the native build. Material cooking is required by the interface, so it is implemented as an explicit refusal naming the milestone rather than a silent no-op. |
| `builder/Ps3NativeBuildExecutor.cs` | Invokes the existing containerized native build with the generated-core root. |
| `builder/Program.cs` | Entry point so the assembly can be exercised standalone. |
| `platform-plugin.json` | Declares the platform id, display name and builder assembly path. |

Registration is a `platforms.json` entry under a user-settings root naming the platform id,
an engine version, and either the builder assembly or the plugin manifest. The project side
adds PS3 to demodisc's supported platform list, its per-platform settings file, and a build
configuration entry.

**The demodisc build entry carries no prebuild scene generation.** A prebuild step of that
kind destroyed authored scenes on PS2 and PSP previously, and this milestone must not
repeat it.

## Codegen profile

The decisions that describe PS3 to the transpiler:

- **Endianness: big.** The Cell PPU is big-endian. Dreamcast declares little-endian. This
  value is inert for the boot gate, which loads no cooked data, and decisive for every
  asset in the milestone after it, so it is set correctly now.
- **Exceptions and RTTI: enabled.** Overridden away from the profile default. PSL1GHT runs
  on newlib with a full C++ runtime, the native host already uses try/catch, and Core
  reports failures by raising. The gate must therefore prove that unwinding works through
  the generated tree, not merely that it links.
- **Pointer size: measured, not assumed.** The toolchain produces a 64-bit PowerPC ELF, but
  PSL1GHT declares an explicit 32-bit pointer attribute on some interfaces. The first
  implementation task measures the real value inside the SDK image and sets the profile
  from the measurement.
- **Native file system: bound to the PS3 content stream source.** The header and type
  settings point generated file access at the native adapter, as Dreamcast does.
- **Forced-disabled features: minimal.** Dreamcast disables a broad set including the host
  file system. PS3 has real storage and a programmable GPU, so the starting position is to
  disable only what the gate genuinely cannot serve, and to record each exclusion with its
  reason.

## Native half

New types under `src/platform/ps3/`, one per file, in the existing namespace:

| Type | Responsibility |
|---|---|
| `Ps3RuntimeHost` | Owns the Core lifetime: construct, initialize, update and draw cycles, dispose. Cycle count is bounded by the run duration the build wrapper already accepts, so no new duration mechanism is introduced. Reports every stage and failure. |
| `Ps3RenderManager3D` | Minimal 3D render manager adapter. Accepts an empty scene and fails loudly on any request the gate does not serve. |
| `Ps3RenderManager2D` | Minimal 2D render manager adapter on the same terms. |
| `Ps3InputBackend` | Returns a neutral input frame. No pad reading in this milestone. |
| `Ps3ContentStreamSource` | Opens content relative to the application's content root. Present because generated file access binds to it, exercised only lightly here. |

The existing boot host gains a Core stage. Video and the cube renderer initialize first and
keep running, then Core initializes against the adapters, and the frame loop drives both.
Ordered shutdown stays as it is today: drain, then release renderer resources, then video.

Evidence is primarily the TTY log, which v0.1 already proved is captured reliably from the
emulator. A small versioned snapshot struct records stage, error code, frame count and heap
figures so a future hardware or debugger run can read the same facts without a console.

## Build flow

`build.ps1` gains a Core mode taking a generated-core root. The Makefile compiles the
finalized unity translation unit plus the generated sources it does not include, asserting
the two sets are disjoint, and links them with the native host. The generated tree is never
edited after generation; a wrong output is fixed in the generator or the profile.

The end-to-end path is the shared platform build script with the PS3 platform id, which
regenerates Core and calls the native build through the builder. The direct path, where
`build.ps1` is handed an already-generated root, stays available for fast iteration and for
reproducing a failure without the managed half.

## Error handling

Every stage failure is visible and fatal. Core construction, initialization, each frame
call and disposal are checked, and a failure logs its stage and numeric cause and returns
nonzero. Exceptions escaping the generated tree are caught at the host boundary and
reported by type and message rather than terminating silently. The existing deliberate
failure points stay, and the Core stage gains one so partial teardown can be exercised.

## Validation gates

| Gate | Evidence required |
|---|---|
| Compiles | The generated tree builds for the PPU with the pinned image, and the unity and extra-source sets are provably disjoint. |
| Links | A single executable containing Core and the native host, with a link map and retained symbols. |
| Initializes | Core reaches initialized state with all four adapters bound, observed in the log. |
| Runs | A bounded number of update, draw and frame-boundary cycles complete with the cube still presenting. |
| Unwinds | A deliberately raised error from the generated tree is caught at the host boundary and reported. |
| Disposes | Core disposes and the process exits zero, with heap figures recorded before and after. |
| Regression | A build without the Core mode still produces the v0.1 executable and passes its existing checks. |

## Risks and unknowns

- **The engine version pin.** Demodisc requires a specific engine version and the shared
  engine has moved past it. If the builder cannot satisfy the pin, that is reported rather
  than resolved by quietly editing the project's pin.
- **Binary size and memory.** The generated tree is large. The executable may need a bigger
  process stack than the v0.1 declaration, and heap headroom must be measured, not assumed.
- **Unwinding on the PPU.** Enabling exceptions is a deliberate choice and could fail at
  link or run time. If it does, falling back to the disabled default is a scope decision to
  raise, not to take silently.
- **Adapter surface drift.** The abstract members the render managers must implement come
  from the generated headers at the pinned engine version and will not match Dreamcast's
  exactly.

## Out of scope

Authored scene loading, cooked content and materials, shaders, textures, physics, audio,
real input, SPU work, packaging beyond the existing executable, and hardware validation.
