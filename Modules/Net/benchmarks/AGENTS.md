# Net Benchmark and Evidence Sources

Inherits `../AGENTS.md`.

## Architecture

`benchmarks/` hosts evidence records for the Net package. Host evidence
records the CMake/CTest run, the strict consumer compile, and the strict
single-translation-unit compiles under the available GCC and Clang toolchains.
ESP32-S3 evidence records the compile-only PlatformIO build of the
Core+Memory+Net consumer.

## Concepts and boundaries

- Evidence records distinguish host behavior, target compile evidence, and
  hardware runtime evidence; a compile success is never turned into a runtime
  or hardware claim.
- Records capture the exact toolchain, commands, byte counts, and pass/fail
  counts that reproduce the recorded result.
- No target upload, radio transmit, or physical-hardware execution is
  authorized for Net evidence.
