# Native Pico H + FreeRTOS build

Status: all three native Pico images compile and link; no image has been
uploaded or verified on hardware.

This is MicroWorld's native RP2040 path. It uses the Raspberry Pi Pico C/C++
SDK and FreeRTOS directly, not Arduino. The scripts keep third-party SDK
sources in `build/_deps/`, so the repository contains the pinned integration
metadata rather than a copy of either SDK.

## Build

From the repository root:

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat build
```

The first configure downloads the exact SDK and FreeRTOS commits. `pico.bat`
looks for CMake, Git, Ninja, GNU Arm, and `elf2uf2` on `PATH`, then in the
normal PlatformIO package cache. A normal PlatformIO installation therefore
supplies the host toolchain without making Arduino part of this firmware.

Build one image when iterating:

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat build probe
Modules\Core\tests\consumer\pico-freertos\pico.bat build example
Modules\Core\tests\consumer\pico-freertos\pico.bat build tests
```

| Selector | Firmware target | Meaning |
| --- | --- | --- |
| `probe` | `microworld_pico_freertos_consumer.uf2` | Links the public Core consumer probe in one static FreeRTOS task. |
| `example` | `microworld_pico_core_tick_example.uf2` | Runs the portable CoreTick behavior from Pico monotonic time. |
| `tests` | `microworld_pico_core_tests.uf2` | Compiles and links Core test translation units only; it does not run them on-device. |

Each target also produces an ELF, BIN, and `<target>.elf.map` in `build/`.

## Script verification

```bat
py -3 -m unittest Modules\Core\tests\consumer\pico-freertos\test_pico.py
```

The tests cover selector validation, artifact checks, and BOOTSEL drive safety;
they do not touch a physical drive.

## Direct CMake escape hatch

The scripts are the canonical Windows workflow. For toolchain diagnosis only,
configure the same project directly after resolving the host tool paths:

```bat
cmake -S Modules\Core\tests\consumer\pico-freertos -B Modules\Core\tests\consumer\pico-freertos\build -G Ninja ^
  -DCMAKE_MAKE_PROGRAM=<path-to-ninja> ^
  -DPICO_TOOLCHAIN_PATH=<arm-toolchain-root> ^
  -DMICROWORLD_ELF2UF2=<path-to-elf2uf2>
cmake --build Modules\Core\tests\consumer\pico-freertos\build --target microworld_pico_core_tick_example
```

The configured project verifies both pinned dependency revisions and writes the
same artifacts under its local `build/` directory.

## Upload (human-gated)

Do not run an upload as part of compile verification. With a single Pico held
in BOOTSEL while connecting USB, wait for the `RPI-RP2` drive, then run only
after explicit authorization:

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat upload probe --drive E:
Modules\Core\tests\consumer\pico-freertos\pico.bat upload example --drive E:
```

The optional drive must be its root. The script checks the `RPI-RP2` label and
`INFO_UF2.TXT` Board-ID before copying the selected UF2. `tests` intentionally
has no upload command.
