# Native Pico + FreeRTOS Consumer

Inherits `../AGENTS.md`.

## Architecture

This is the native RP2040 proof for MicroWorld Core. It is a standalone CMake
consumer: it adds only `Modules/Core`, links the official Pico SDK and the
FreeRTOS RP2040 static kernel, and keeps Pico/FreeRTOS headers out of Core.
The Pico SDK is pinned to commit `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`
(2.2.0); FreeRTOS-Kernel is pinned to
`9b777ae5c5b8e9e456065a00294d1e5f5f9facf5` (V11.3.0). CMake verifies both
resolved revisions at configure time.

`FreeRTOSConfig.h` permits static tasks only: dynamic allocation, timers,
event groups, stream buffers, and Pico SDK interoperability are disabled.
Every firmware entry point owns a `StaticTask_t` and `StackType_t` array for
the full image lifetime. The test image deliberately links the existing Core
test translation units but never runs them, because several host tests have
stack requirements that have not been measured for RP2040.

The SDK's optional host `picotool` build is disabled with `PICO_NO_PICOTOOL`.
The local `elf2uf2` executable discovered from PlatformIO converts each ELF to
UF2 instead. This keeps the native CMake build independent of the incompatible
host-picotool toolchain while preserving normal Pico UF2 output.

## Concepts

- `probe` is the public Core API/link image, not a platform feature test.
- `example` is the Pico adapter for the platform-neutral five-tick CoreTick
  behavior; it has no logging or peripheral policy.
- `tests` links existing Core test translation units only; it never runs or
  uploads their stack-heavy behavior on RP2040.
- PlatformIO supplies cached host tools when present, but the firmware build
  remains the official Pico SDK CMake flow rather than an Arduino framework.

## Commands

Run from the repository root:

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat build
Modules\Core\tests\consumer\pico-freertos\pico.bat build probe
Modules\Core\tests\consumer\pico-freertos\pico.bat build example
Modules\Core\tests\consumer\pico-freertos\pico.bat build tests
py -3 -m unittest Modules\Core\tests\consumer\pico-freertos\test_pico.py
```

`pico.bat` resolves CMake, Git, Ninja, GNU Arm, and `elf2uf2` from `PATH`
first, then from the normal PlatformIO package cache. Its `build` default
creates the Core probe, the portable CoreTick example image, and the
compile/link-only Core test image under `build/`.

Upload is human-gated. Only after explicit authorization, put one Pico into
BOOTSEL mode and run one of these commands:

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat upload probe [--drive E:]
Modules\Core\tests\consumer\pico-freertos\pico.bat upload example [--drive E:]
```

The script rejects the test selector, a non-root drive argument, an unexpected
volume label, an invalid `INFO_UF2.TXT`, or ambiguous automatic drive detection
before copying a UF2. It does not monitor serial output or claim runtime
validation.

## Verification

Use `pico.bat build` for all three images. Validate every generated map with
`python tools/CheckProfileMap.py --map <image>.elf.map --profile Core`; check
that `pvPortMalloc`, `vPortFree`, and `heap_[1-5].c` are absent. Build output,
SDK source, and FreeRTOS source live under ignored `build/` only; do not vendor
either dependency into MicroWorld.
