#include <cstdio>

#include <sys/process.h>

/// Declares the PPU process parameters read by GameOS: normal priority (1001) and a 1 MiB main stack.
SYS_PROCESS_PARAM(1001, 0x100000)

/// Native PS3 entry point for the v0.1 boot route: proves the pinned toolchain produces a bootable executable.
int main() {
    std::printf("PS3_BOOT\n");
    return 0;
}
