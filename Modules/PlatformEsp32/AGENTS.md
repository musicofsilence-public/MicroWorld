# MicroWorld PlatformEsp32 Package

Inherits `../AGENTS.md`.

## Architecture

`microworld-esp32` is the non-portable ESP32-S3 platform adapter. It supplies
real transports (lwIP UDP, E32 LoRa over UART), a time source (`esp_timer`),
and a log sink (`ESP_LOG*`) behind the portable `INetDriver` /
`TimePointMilliseconds` / `FLogSink` seams described in `docs/Porting.md`. It
depends inward on Core, Memory, Object, Engine, and Net as needed and never
the reverse, and it is **excluded from `CheckDependencyBoundaries.py`** — it
has no module key in that tool's portable table.

## Concepts

- The three adapter seams are `FEsp32TimeSource` (clock), `FEsp32UdpDriver` /
  `FEsp32E32LoraDriver` (`INetDriver` transports), and `Esp32LogSink` (log
  sink); portable code never reaches ESP-IDF, lwIP, or vendor headers
  directly.
- All lwIP, ESP-IDF, and `<driver/uart.h>` headers are confined to private
  `src/*PlatformImplementation.h` headers; public declarations stay
  platform-neutral.
- Compile success on this package is a compile-only proof, never a runtime,
  timing, heap, stack, or radio claim; see `benchmarks/Results/` for the
  measured evidence that closes that gap.

## Verification

Build with PlatformIO for `esp32-s3-devkitc-1` (`espidf` framework); keep
`-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror`. A newly changed
adapter must be smoke-run on the real target before any runtime-readiness
claim, per `docs/Porting.md`.
