# PS3 ABI facts for the generated Core codegen profile

The generated Core C++ profile must state the target's pointer size and byte order, and the
v0.2 design commits to enabling exceptions and RTTI against the transpiler's default. Every
value below was measured, none assumed. Reproduce with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/probe-core-abi.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/launch_in_emulator.ps1 -ArtifactPath .ps3\toolchain\core-abi-<utc>\abi.self -RpcS3Path C:\dev\helworks\emus\rpcs3-v0.0.42-20013-2d103407\rpcs3.exe -Visible
```

## Measurements

Measured 2026-09-17 against SDK image
`sha256:b19ffd3cfb54c04835a0e32989dfa59205764802469a2faa89aa2519734b8b3f`.

| Fact | Value | How it was established |
|---|---|---|
| Pointer size | 8 bytes | Compiler macro `__SIZEOF_POINTER__`, confirmed by `sizeof(void*)` on the console |
| Long size | 8 bytes | Compiler macro, confirmed at run time |
| `size_t` size | 8 bytes | Compiler macro, confirmed at run time |
| Byte order | big | Preprocessor comparison, the linked ELF header, and a run-time union probe, all agreeing |
| ELF class and machine | ELF64, PowerPC64 | `ppu-readelf -h` |
| Exceptions compile | yes | `-fexceptions` accepted |
| Unwinding linked | yes | `__cxa_throw` and `_Unwind_RaiseException` present in the linked ELF |
| Exceptions unwind at run time | yes | A thrown `std::runtime_error` was caught with its message intact |
| RTTI works at run time | yes | `dynamic_cast` down to the derived type resolved |
| `std::string` works | yes | The caught exception's message survived into a `std::string` |

The runtime evidence path above is from the historical external-root run. New probe
outputs and emulator logs are placed under `.ps3\toolchain\core-abi-<utc>\`.

## What the codegen profile consumes

- `pointer-size-bytes` is **8**. This is the single most important difference from the
  Dreamcast profile, which declares 4. PS3 GameOS code is 64-bit; PSL1GHT's 32-bit pointer
  attribute applies only to specific module-export fields, not to ordinary pointers.
- Serialization endianness is **big**.
- `codegen-use-exceptions` and `codegen-use-rtti` are both **true**, overriding the
  transpiler's `stl-lite` profile, which disables both. The runtime evidence above is what
  justifies the override; without it the correct choice would have been to accept the
  default.

## Notes

The first version of the probe reported little-endian from the macro dump while the ELF
header said big. The fault was in the probe: GCC defines `__BYTE_ORDER__` as the token
`__ORDER_BIG_ENDIAN__` rather than a number, so comparing the dumped values as text is
wrong. The check now lets the preprocessor resolve the comparison and additionally
cross-checks the linked ELF header, failing loudly if the two ever disagree.

The probe emits a manifest in the launcher's schema so it runs through the same
containment, identity and hash verification as a product build rather than being started
directly. Its mode is recorded as `CoreAbiProbe` so it is never mistaken for one.
