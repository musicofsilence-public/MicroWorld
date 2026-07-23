# ESP32 Public Driver Headers

Inherits `../../AGENTS.md`.

## Architecture

This directory declares the seven ESP32 adapters: `FEsp32TimeSource` (clock
seam); `FEsp32UdpDriver`, `FEsp32E32LoraDriver`, `FEsp32UartDriver`,
`FEsp32I2cMasterDriver`, and `FEsp32I2cSlaveDriver` (`INetDriver` transport
seams — the LoRa, wired UART, and wired I2C drivers all built on the portable
`Net/FrameCodec.h` CRC-16/CCITT-FALSE framing, over the 1-byte broadcast
`LoraAddress`, the 1-byte point-to-point `UartAddress`, and the 1-byte
point-to-point `I2cAddress` respectively); and `Esp32LogSink` (log sink). Their
declarations depend only on Net/Object/Memory/Core public headers and stay free
of ESP-IDF, lwIP, and vendor headers; those live only in the matching
`src/*PlatformImplementation.h`.

## Concepts

- `FEsp32UdpDriver` and `FHostUdpDriver` (PlatformHost) share the same 6-byte
  UDP address encoding via `Net/UdpAddressCodec.h`, so wire framing matches
  across platforms.
- `FEsp32E32LoraConfig` and `FEsp32UartConfig` carry UART port and GPIO numbers
  as plain integers so these headers name ESP-IDF hardware without including its
  enum types; `FEsp32UartConfig` defaults `BaudRate` to 115200 (a wire is fast),
  the LoRa config to 9600 (the module's airtime).
- `FEsp32I2cMasterConfig` and `FEsp32I2cSlaveConfig` likewise carry the I2C
  port, SDA/SCL GPIOs, and 7-bit `SlaveAddress` as plain integers; the master
  defaults `SclSpeedHz` to 100000 (100 kHz standard mode) and both default
  `SlaveAddress` to 0x28. The slave owns an `FI2cReceiveInbox` the platform ISR
  fills.
- `Esp32LogSink` maps `ELogLevel` to `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`/`ESP_LOGV`
  and is installed once via `SetLogSink`.

## Verification

Compile each header standalone under the ESP-IDF C++17 toolchain with
warnings as errors, exceptions disabled, and RTTI disabled. Document every
exported declaration with the real-hardware behavior it wraps.
