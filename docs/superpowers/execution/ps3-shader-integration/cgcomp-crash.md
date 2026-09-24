# StandardShader cgcomp crash investigation

Historical diagnosis, preserved unchanged below. The source fix was subsequently committed as 6313a94 and the current pinned image differs from the investigation image. Current runtime evidence is in [acceptance-checkpoint-20260924.md](acceptance-checkpoint-20260924.md); the old pending-work statements below are not current blockers.

2026-09-23. Locked image: sha256:da929def76aacc0a5f499e1b8be143912062ab4a93b206277ead4de9530fe411.

Backend focused-green-2.trx reports 30/32. One failure is an invalid float2 negative-test expectation (worker correcting); the other is real standard fragment shader conversion, cgcomp exit 139.

Failing job: `C:/dev/helworks/builds/helengine-ps3/shader-integration-20260923/backend/shader-artifacts/backend-forward-standard/ps3-shader-462caeb9e7e043e6a5f550425ec50f37`.

source.glsl is 40672 bytes. program.bin.dump is 29992 bytes and ends with 1047 instructions, 15 R-regs, 5 H-regs. No program.bin was produced. Longest assembly line: 131 characters, below the parser's 255-character read limit.

Direct reproduction in the same Docker image with original job mounted read-only: `cgcomp -f -a /input/program.bin.dump /out/repro.bin` also exits 139. New output directory: `.../standard-shader-crash/assembly-repro`. No HLSL/GLSL/dump was edited.

## Pinned source evidence

- [parser.h](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/tools/cgcomp/include/parser.h): MAX_NV_FRAGMENT_PROGRAM_INSTRUCTIONS is 1024.
- [fpparser.cpp](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/tools/cgcomp/source/fpparser.cpp): CFPParser allocates that many nvfx_insn records, then Parse writes m_pInstructions[m_nInstructions] and increments without a capacity check. At most one instruction is parsed per line, including END.
- [parser.cpp](https://github.com/ps3dev/PSL1GHT/blob/f649a08fd536a9e27c08c7db2d93a2d7ee4c3bbe/tools/cgcomp/source/parser.cpp): base constructor sets option/count; destructor deletes the instruction array. The concrete parser must keep the pointer valid.

Hypothesis: 1047 instructions overrun the fixed 1024-record parser buffer. Cg frontend succeeded; assembler-only reproduction isolates the failure from shaderc/SPIRV-Cross and libCg compilation. A source patch plus successful same-input regression is still required to verify the correction.

Additional evidence: the standard fragment dump retains four sampler2D and four samplerCUBE even in ForwardStandard. Artifact plan updated to accept Dim2D and DimCube. Do not remove source branches or define WiiU-only switches to evade these requirements.

Next: repository-owned SDK patch under docker/patches, applied by existing Dockerfile loop and recorded by prepare_toolchain.ps1. Luna implements; all SDK compilation remains Docker. Rebuild/verify image and rerun unchanged failing input and compiler regressions. Do not claim fixed yet.
