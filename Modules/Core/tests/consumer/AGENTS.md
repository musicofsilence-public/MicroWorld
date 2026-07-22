# Downstream Consumer Probes

Inherits `../AGENTS.md`.

## Architecture

This is a downstream CMake/PlatformIO project, not part of the MicroWorld
library. Standalone CMake adds selected adjacent packages as subdirectories and
links `MicroWorld::Core`, `MicroWorld::Memory`, `MicroWorld::Object`, or
`MicroWorld::Engine`.
PlatformIO resolves local packages through `symlink://`, then builds mutually
exclusive native, Core ESP32-S3, Memory ESP32-S3, Object ESP32-S3, Engine
ESP32-S3, and executable benchmark applications.
MicroWorld never depends on these fixtures.

## Concepts

- The native environment verifies ordinary host consumption with exceptions
  and RTTI disabled.
- The standalone CMake mode provides an independent MSVC Windows host probe
  alongside the PlatformIO Native GCC probe.
- The standalone Memory mode proves Core+Memory public APIs compile and link
  together with exceptions and RTTI disabled.
- The basic ESP32-S3 environment proves the public package crosses the
  ESP-IDF/toolchain boundary without platform headers entering MicroWorld.
- The Memory ESP32-S3 environment composes the Core and Memory manifests and
  records compile-only whole-image evidence.
- The Object ESP32-S3 environment composes Core, Memory, and Object manifests.
  Its public-API probe exercises fixed storage, explicit roots, weak expiry,
  and full collection without target hardware I/O.
- The Engine ESP32-S3 environment composes Core, Memory, Object, and Engine.
  Its probe exercises registration, lifecycle, tick, rooted retention,
  unrooted collection, and the bounded `TTimerManager` (schedule, caller-time
  Advance, one-shot completion, stale-handle rejection) without target
  hardware I/O.
- The benchmark environment adds target-only measurement code around the same
  public scheduling API.
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
