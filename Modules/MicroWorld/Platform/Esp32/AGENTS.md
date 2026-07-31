# MicroWorld Platform/Esp32

Inherits `../../../AGENTS.md`.

## Architecture

`MicroWorldPlatformEsp32` is the non-portable ESP32-S3 platform adapter. It
supplies real transports (lwIP UDP, an optional E32 compatibility facade over
UART, a wired point-to-point UART, a wired point-to-point I2C master/slave pair,
and a wired point-to-point SPI master/slave pair), a time source (`esp_timer`),
and an output device (`ESP_LOG*`) behind the portable `IDevice` /
`TimePointMilliseconds` / `FOutputDeviceFunction` interfaces described in
`../../../docs/Porting.md`. It depends inward on Core, Engine, Transport, and
optional RadioE32 as needed and never the reverse, and it is excluded from
`CheckDependencyBoundaries.py` — it has no system key in that tool's portable
table.

## Concepts

- The adapter interfaces are `FEsp32TimeSource` (clock), the `IDevice`
  transports (`FEsp32UdpDriver`, `FEsp32E32LoraDriver`, `FEsp32UartDriver`,
  `FEsp32I2cMasterDriver`/`FEsp32I2cSlaveDriver`, `FEsp32SpiMasterDriver`/
  `FEsp32SpiSlaveDriver`), and `WriteEsp32LogRecord` (the log output device); the
  E32 facade delegates portable framing to RadioE32 (now inside Transport) while
  portable code never reaches ESP-IDF, lwIP, or vendor headers directly.
- All lwIP, ESP-IDF, `<driver/uart.h>`, `<driver/i2c_*.h>`, and `<driver/spi_*.h>`
  headers are confined to the `Detail/*PlatformImplementation.h` private headers;
  public declarations stay platform-neutral.
- `FEsp32WifiLink` is the one-per-firmware SoftAP/station bring-up facade, with
  every `esp_wifi`/`esp_netif`/`nvs`/event-loop include confined to one private
  implementation TU.
- `SleepMilliseconds` (`Esp32Sleep.h`) is the `vTaskDelay`-backed cooperative
  yield examples call in their run loops.
- Compile success on this family is a compile-only proof, never a runtime,
  timing, heap, stack, radio, or wired-link claim; see `benchmarks/Results/` for
  the measured evidence that closes that gap.
- Each driver's `Detail/*PlatformImplementation.h` header opens with a comment
  stating exactly which branches real hardware has exercised and which stay
  unverified. A new driver says "UNVERIFIED at runtime" there until its example's
  hardware checkpoint passes.

## Verification

Build with PlatformIO for `esp32-s3-devkitc-1` (`espidf` framework); keep
`-fno-exceptions -fno-rtti -Wall -Wextra -Wpedantic -Werror`. A newly changed
adapter must be smoke-run on the real target before any runtime-readiness claim,
per `../../../docs/Porting.md`.
