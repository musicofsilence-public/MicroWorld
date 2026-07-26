# Raspberry Pi Pico H Native SDK + FreeRTOS Consumer

## Problem

The current Pico H compile probe uses Arduino Mbed because that is the only
framework exposed by PlatformIO's official Raspberry Pi RP2040 platform. That
proves Core portability, but it does not resemble MicroWorld's ESP32 execution
environment: ESP32 uses the vendor SDK, CMake, and FreeRTOS directly.

MicroWorld needs a Pico proof that crosses the native Raspberry Pi Pico SDK and
the official RP2040 FreeRTOS port without introducing RTOS or hardware
dependencies into Core.

## Proposed Approach

Replace the Arduino Pico probe with a dedicated native CMake consumer under the
existing Core consumer fixture:

- Pin official Raspberry Pi Pico SDK and FreeRTOS-Kernel revisions and resolve
  them through the Pico SDK's supported CMake integration.
- Build for `PICO_BOARD=pico` with C++17, strict warnings, exceptions disabled,
  and RTTI disabled.
- Provide a conventional `main()` that creates one statically allocated
  FreeRTOS task, starts the scheduler, runs `RunCoreConsumerProbe()` once, and
  then suspends the task.
- Start with the single-core RP2040 FreeRTOS configuration. It matches the API
  and scheduling model used by ESP-IDF while avoiding unnecessary SMP behavior
  in a compile/runtime smoke probe.
- Generate ELF, BIN, and UF2 artifacts with the Pico SDK.
- Add a consumer-local `pico.bat` dispatcher backed by `pico.py` so
  `pico.bat build` performs the native build and `pico.bat upload` builds then
  copies the UF2 to a validated `RPI-RP2` BOOTSEL drive.
- Make `pico.bat build` compile three Pico firmware artifacts by default: the
  native Core probe, a Pico port of `examples/01-CoreTick`, and the existing
  Core behavioral test suite behind a Pico/FreeRTOS test entry point.
- Accept `probe`, `example`, or `tests` selectors for focused builds and require
  the selector for upload, preventing the wrong UF2 from being copied.
- Keep ESP32 hardware examples 15–26 out of the Pico baseline until matching
  Pico UDP/UART/I2C/SPI/LoRa platform drivers exist; the build must not pretend
  those hardware integrations are portable.
- Keep tool discovery, subprocess execution, drive validation, and diagnostics
  in Python; the batch file only locates Python and forwards arguments.
- Keep PlatformIO and ESP-IDF unchanged for ESP32. Remove the Arduino Pico
  PlatformIO environment only after the native replacement passes.
- Keep Core platform-neutral; a future `PlatformPico` module can own GPIO,
  clocks, USB/UART, or other Pico SDK adapters when real hardware features are
  requested.

This gives both targets the same architectural shape:

```text
ESP32: PlatformIO -> ESP-IDF SDK -> FreeRTOS -> MicroWorld
Pico:  CMake      -> Pico SDK    -> FreeRTOS -> MicroWorld
```

An unofficial/custom PlatformIO Pico-SDK platform is deliberately rejected. It
would preserve the `pio run` command but make the build depend on a
community-maintained framework integration instead of the supported Pico SDK
CMake contract.

## Open Questions

None.

## Decisions Log

- 2026-07-26: Use native Pico SDK and FreeRTOS instead of Arduino Mbed - this
  matches the ESP32 vendor-SDK/RTOS architecture.
- 2026-07-26: Keep RTOS and Pico SDK dependencies outside Core - Core remains
  portable and owns no hardware policy.
- 2026-07-26: Prefer official CMake integration over unofficial PlatformIO
  frameworks - reproducibility and upstream support outweigh one-command
  symmetry.
- 2026-07-26: Recommend one FreeRTOS core first - smallest deterministic proof
  with a clean path to SMP later.
- 2026-07-26: Concept approved - direct CMake and single-core FreeRTOS are the
  implementation baseline.
- 2026-07-26: Add BAT + Python build/upload automation - the user rejected
  PowerShell; one dispatcher and one shared implementation avoid duplication.
- 2026-07-26: Upload through a validated `RPI-RP2` BOOTSEL volume - it needs no
  debug probe or unofficial PlatformIO integration and never guesses a drive.
- 2026-07-26: Put canonical build/upload commands in scoped `AGENTS.md` files -
  future agents and contributors must discover the supported workflow without
  reconstructing it from CMake.
- 2026-07-26: Build the native probe, `01-CoreTick`, and the Core behavioral
  tests through the Pico scripts - this verifies a portable example and its
  behavior suite without misrepresenting ESP32 hardware integrations as Pico
  support.
- 2026-07-26: Disable Pico SDK FreeRTOS interoperability - the bounded
  baseline uses direct Pico monotonic time and no SDK lock/time bridge, avoiding
  unused timer and event-group APIs in the static-only kernel configuration.
- 2026-07-26: Use PlatformIO's cached `elf2uf2` only as a host converter - Pico
  SDK 2.2.0's optional `picotool` host build is incompatible with the installed
  Visual Studio Clang/STL combination, while the official CMake SDK build and
  generated ELF/BIN artifacts remain unchanged.
