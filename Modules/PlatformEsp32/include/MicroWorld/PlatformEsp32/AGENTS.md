# ESP32 Public Driver Headers

Inherits `../../AGENTS.md`.

## Architecture

This directory declares the five ESP32 adapters: `FEsp32TimeSource` (clock
seam), `FEsp32UdpDriver`, `FEsp32E32LoraDriver`, and `FEsp32UartDriver`
(`INetDriver` transport seams — the LoRa and wired UART drivers both built on
the portable `Net/FrameCodec.h` CRC-16/CCITT-FALSE framing, over the 1-byte
broadcast `LoraAddress` and the 1-byte point-to-point `UartAddress`
respectively), and `Esp32LogSink` (log sink). Their declarations depend only on
Net/Object/Memory/Core public headers and stay free of ESP-IDF, lwIP, and
vendor headers; those live only in the matching `src/*PlatformImplementation.h`.

## Concepts

- `FEsp32UdpDriver` and `FHostUdpDriver` (PlatformHost) share the same 6-byte
  UDP address encoding via `Net/UdpAddressCodec.h`, so wire framing matches
  across platforms.
- `FEsp32E32LoraConfig` and `FEsp32UartConfig` carry UART port and GPIO numbers
  as plain integers so these headers name ESP-IDF hardware without including its
  enum types; `FEsp32UartConfig` defaults `BaudRate` to 115200 (a wire is fast),
  the LoRa config to 9600 (the module's airtime).
- `Esp32LogSink` maps `ELogLevel` to `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`/`ESP_LOGV`
  and is installed once via `SetLogSink`.

## Verification

Compile each header standalone under the ESP-IDF C++17 toolchain with
warnings as errors, exceptions disabled, and RTTI disabled. Document every
exported declaration with the real-hardware behavior it wraps.
