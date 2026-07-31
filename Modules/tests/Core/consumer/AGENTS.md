# Downstream Consumer Probes

Inherits `../AGENTS.md`.

## Architecture

This is a downstream CMake/PlatformIO project, not part of the MicroWorld
library. Standalone CMake adds selected adjacent packages as subdirectories and
links `MicroWorld::Core`, `MicroWorld::Object`, or `MicroWorld::Engine`.
PlatformIO resolves local packages through `symlink://`, then builds mutually
exclusive native, Core ESP32-S3, Memory ESP32-S3, Object ESP32-S3, Engine
ESP32-S3, and executable benchmark applications. The sibling
`pico-freertos/` folder is the separate native RP2040 CMake consumer: it links
Core with the Pico C/C++ SDK and static-allocation FreeRTOS, never Arduino.
MicroWorld never depends on these fixtures.

## Concepts

- The native environment verifies ordinary host consumption with exceptions
  and RTTI disabled.
- The standalone CMake mode provides an independent MSVC Windows host probe
  alongside the PlatformIO Native GCC probe.
- The standalone memory-API mode proves Core's memory public APIs compile and
  link together with exceptions and RTTI disabled.
- The basic ESP32-S3 environment proves the public package crosses the
  ESP-IDF/toolchain boundary without platform headers entering MicroWorld.
- The memory-API ESP32-S3 environment composes the Core manifest and records
  compile-only whole-image evidence.
- The Object ESP32-S3 environment composes Core and Object manifests.
  Its public-API probe exercises fixed storage, explicit roots, weak expiry,
  and full collection without target hardware I/O.
- The Engine ESP32-S3 environment composes Core, Object, and Engine.
  Its probe exercises registration, lifecycle, tick, rooted retention,
  unrooted collection, and the bounded `TTimerManager` (schedule, caller-time
  Advance, one-shot completion, stale-handle rejection) without target
  hardware I/O.
- The benchmark environment adds target-only measurement code around the same
  public scheduling API.
- The native Pico consumer pins and verifies the Pico SDK plus FreeRTOS-Kernel
  at configure time, then emits ELF, BIN, UF2, and linker-map evidence for its
  Core probe, portable CoreTick example, compile/link-only Core test image, and
  consumer-local E32 LoRa interoperability image.
- PlatformIO source filtering selects the native entry point; the ESP-IDF
  component CMake file selects exactly one `app_main` from the environment's
  isolated build directory.
- All ESP32 environments inherit repository N16R8 flash, PSRAM, and partition
  defaults but never upload unless separately authorized.
- Each selected package has its own `lib_deps` entry; it does not change the
  Core manifest's source filter.

## Documentation and verification

Document each environment-specific function and persistent measurement value by
its probe purpose. Verify compilation with explicit `-e` environments; a native
probe on Windows requires GNU `g++` on `PATH` and currently uses WinLibs GCC
16.1.0. Verify standalone CMake with `-DMICROWORLD_STANDALONE_CONSUMER=ON` or
`-DMICROWORLD_STANDALONE_MEMORY_CONSUMER=ON`.
Use `-DMICROWORLD_STANDALONE_OBJECT_CONSUMER=ON` for the Object profile.
Use `-DMICROWORLD_STANDALONE_ENGINE_CONSUMER=ON` for the Engine profile.
For native Pico commands, use
`pico-freertos\\pico.bat build [probe|example|tests|lora]`; the default builds
all four. `pico-freertos\\pico.bat upload probe|example|lora` is human-gated
and validates the RPI-RP2 BOOTSEL volume before copying a UF2. The `lora`
image is a downstream hardware proof and does not make Pico headers or a device
part of a released MicroWorld package.
