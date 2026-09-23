# Pinned SDK format evidence for Tasks 3/4

Read-only verification by controller, 2026-09-23. SDK image is the ImageId in toolchain-lock.json; do not hardcode it in production.

Primary sources:
- Installed Docker header: `/opt/ps3dev/ppu/include/rsx/rsx_program.h`.
- [Pinned cgcomp writer](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/tools/cgcomp/source/main.cpp).

Header fields (both stages have a 36-byte header): magic u16, padding u16, registers/attributes/constants/instructions as four u16s, then attribute/constant/microcode offsets as three u32s. Vertex tail: input/output mask u32s and constant/instruction start u16s. Fragment tail: control u32, texcoords/texcoord2D/texcoord3D/padding u16s.

The writer uses big-endian VP (`0x5650`) and FP (`0x4650`) magic, big-endian header/table integer fields, 16-byte microcode alignment and 16 bytes per instruction slot. Fragment microcode also uses endian_fp word transformation: preserve it as bytes rather than re-encoding it.

Attribute record: name offset u32, index u32, type u8, three padding bytes (12 bytes).

Installed header type values (read again from pinned image): BOOL=0, BOOL1=1, BOOL2=2, BOOL3=3, BOOL4=4; FLOAT=5, FLOAT1=6, FLOAT2=7, FLOAT3=8, FLOAT4=9; FLOAT3x4=10, FLOAT4x4=11, FLOAT3x3=12, FLOAT4x3=13; SAMPLER1D=14, SAMPLER2D=15, SAMPLER3D=16, SAMPLERCUBE=17, SAMPLERRECT=18; UNKNOWN=0xff. Preserve the distinction between scalar FLOAT and FLOAT1. These are SDK constants, not ordinal guesses from fixture bytes.
Constant record: name offset u32, index u32, type/internal/count/padding u8s, four 32-bit values (28 bytes).

Important non-obvious cases from the writer:

- A matrix/array parameter consumes multiple constant records. Count/type/name identify the first record; continuation records may have no name. Do not reject every zero name offset or treat each record as a new authored member.
- Vertex internal constants may legitimately have no names.
- Fragment `index` is a file offset to a relocation table, NOT the constant register number. It can be `0xffffffff` when there are no relocations for an element.
- A relocation table holds a big-endian u32 count followed by u32 offsets relative to microcode. Each reference must fit the microcode region, not merely the whole file.
- Do not fabricate a hardware constant register for fragment bytecode from `index`. Assembly and authored reflection supply additional identity; runtime patching uses validated relocation offsets.
- Shader output varyings need compiler dump evidence; binary attribute tables alone do not provide a complete vertex-output/fragment-input interface.
- Pinned `tools/cgcomp/source/parser.cpp` parses Cg `#var` bindings via `$vin`, `texunit`, or constant register `c[...]`; `$vout` does not enter that binary parameter list. Samplers share the attribute table but are distinguished by parameter type and texture-unit index. Do not assume every attribute record is a vertex input or treat absent vertex outputs as missing shader data.

Task 3 must encode the distinction between vertex registers and fragment relocations explicitly in its DTO/parser. Task 4 must not mistake the relocation-table file offset for a hardware slot. This refines the plan's generic `hardware index` terminology; it does not permit postprocessing generated output.

## Inter-stage register spaces

The pinned vpparser.cpp maps result.texcoord[0]/TEX0 to vertex output index 7; fpparser.cpp maps fragment.texcoord[0]/TEX0 to fragment input index 4. These are the same routing channel despite different stage-local indices. Link validation must compare canonical Cg/RSX channels and retain raw indices separately. The plan/Task 4 brief were corrected before implementation.

Full named-channel subset re-read from that exact commit's [vertex parser](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/tools/cgcomp/source/vpparser.cpp) and [fragment parser](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/tools/cgcomp/source/fpparser.cpp):

- Vertex outputs: HPOS/POSITION=0; COL0=1; COL1=2; BFC0=3; BFC1=4; FOGC=5; PSIZ/PSZ=6; TEX0..TEX7=7..14; TEMP=15 is not a user varying channel.
- Fragment inputs: WPOS=0; COL0=1; COL1=2; FOGC=3; TEX0..TEX7=4..11.
- Legacy named vertex inputs are a different table: POSITION=0, WHGT=1, NRML=2, COL0=3, COL1=4, FOGC=5, TEX0..TEX7=8..15. Do not confuse this table with vertex outputs or generic ATTR indices.
- Do not extrapolate additional named TEX channels beyond the pinned tables without evidence. Built-in vertex HPOS and fragment WPOS are separate fixed-function roles, not a direct user-varying link.

The web cache returned cache miss for these files on the latest read; a read-only HTTPS fetch of the pinned raw files supplied the table evidence. No SDK source/output files were edited.

## Task 2 complex fixture probe

Parent ran the exported ReflectionSource pair through the pinned Docker SDK using `cgcomp -v/-f -d -Wcg,-oglsl`. Both succeeded: VS.vpo 1296 bytes, PS.fpo 272 bytes under `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/task-2-probe`. Repeated successfully at 13:33 local after the final 29/29 managed test run reexported GLSL.

The dumps preserve full generated nested uniform paths, vertex inputs ATTR0/1/2, COLOR0 routed through TEX0 and TEXCOORD0 through TEX1 on both stages. Combined samplers t0/s0 and t0/s1 become distinct texunit 0 and 1. Matrix constants use `c[0], 4` and `c[4], 3`. Fragment dump also declares unused uniform members with empty bindings and a final active flag of 0; these are not active missing-binding errors. Active records end in 1. Do not infer activity merely from a #var line being present.

Task 3 fixture coverage: the complex PS has no active constants (`num_const=0`) and cannot prove relocation parsing. The earlier read-only `.../shader-integration-20260923/probe/PS.fpo` (160 bytes) has one active tint constant, with `num_const=1`, `num_insn=3`, constant table at 0x44 and microcode at 0x70. Its dump has `_34._m1 : c[0]` and texture multiplication. Use an equivalent newly generated constant-bearing fragment fixture for Docker acceptance; do not mistake the zero-constant complex PS for relocation coverage.

Complex VS binary observation (36-byte header, 3 attributes, 12 constant records, constant table 0x48, microcode 0x410, 16 instruction slots): first four constants are authored leaves at registers 7/8/9/10. Matrix record 4 has type11/count4/register0 and owns continuation records5..7 at registers1..3 with name/type/count zero. Matrix record8 has type10/count3/register4 and owns continuations9..10 at registers5..6. Record11 is internal float4 at register11, type9/internal1/count1/name0, values `[0x3f800000,0,0,0]`. Matrix default-value words contain identity components, not uniformly zero. This specimen supports precise positive parser assertions in addition to synthetic corruption cases.
