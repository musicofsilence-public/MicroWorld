# Native Pico H + FreeRTOS build

Status: all five native Pico images compile and link. The Pico-to-ESP32 LoRa
path was hardware-verified on 2026-07-27, and its payload-boundary regression
was hardware-verified on 2026-07-28; evidence is recorded with the ESP32 peer
in example 17's README.

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
Modules\Core\tests\consumer\pico-freertos\pico.bat build lora
Modules\Core\tests\consumer\pico-freertos\pico.bat build lora-regression
```

| Selector | Firmware target | Meaning |
| --- | --- | --- |
| `probe` | `microworld_pico_freertos_consumer.uf2` | Links the public Core consumer probe in one static FreeRTOS task. |
| `example` | `microworld_pico_core_tick_example.uf2` | Runs the portable CoreTick behavior from Pico monotonic time. |
| `tests` | `microworld_pico_core_tests.uf2` | Compiles and links Core test translation units only; it does not run them on-device. |
| `lora` | `microworld_pico_lora_interop.uf2` | Pico node 1 RadioE32 interoperability image for ESP32 example 17 node B; its direct task advances queued TX. |
| `lora-regression` | `microworld_pico_lora_payload_regression.uf2` | Exercises empty, typical, and maximum payloads against the ESP32 example-17 regression peer. |

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
Modules\Core\tests\consumer\pico-freertos\pico.bat upload lora --drive E:
Modules\Core\tests\consumer\pico-freertos\pico.bat upload lora-regression --drive E:
```

The optional drive must be its root. The script checks the `RPI-RP2` label and
`INFO_UF2.TXT` Board-ID before copying the selected UF2. `tests` intentionally
has no upload command.

## Pico + ESP32 LoRa pairing (hardware-gated)

The `lora` image consumes the reusable `FPicoLoraDevice` from
`Modules/MicroWorld/Platform/Pico`. It is Pico node 1 and sends the same five-byte counter
payload and MicroWorld frame format as ESP32 example 17. The device uses UART1
at 9600 baud, 8N1; its direct FreeRTOS task invokes the generic device hook
once each iteration, so UART progress remains non-blocking.

| Pico H | E32-433T20D | Purpose |
| --- | --- | --- |
| GND | GND | common ground |
| 3V3 | VCC | 3.3 V power |
| GP4 / pin 6 (TX) | RXD | Pico sends radio UART bytes |
| GP5 / pin 7 (RX) | TXD | Pico receives radio UART bytes |
| GND | M0 | transparent mode |
| GND | M1 | transparent mode |
| — | AUX | unused |

Attach both antennas before power. Flash `examples/17-TwoBoardLora` environment
`esp32-s3-node-b`, begin `mw log COMx`, then BOOTSEL-upload `lora`. A successful
Pico exchange is proved by a fresh ESP32 trace containing `node=2 open=1`,
`rx n=1 from=1`, `tx n=2 result=Success`, `rx n=3 from=1`, and
`tx n=4 result=Success`. Repeat this volley after any rebuild that changes the
Pico SDK binding because host tests do not exercise physical UART behavior.

The observed 2026-07-28 `lora-regression` artifact, device, and exchange
evidence is recorded in example 17's README.
