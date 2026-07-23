# ESP32 Public Driver Headers

Inherits `../../AGENTS.md`.

## Architecture

This directory declares the four ESP32 adapters: `FEsp32TimeSource` (clock
seam), `FEsp32UdpDriver` and `FEsp32E32LoraDriver` (`INetDriver` transport
seams, the second built on the portable `Net/FrameCodec.h` CRC-16/CCITT-FALSE
framing plus the 1-byte broadcast `LoraAddress`), and `Esp32LogSink` (log
sink). Their declarations depend only on Net/Object/Memory/Core public
headers and stay free of ESP-IDF, lwIP, and vendor headers; those live only in
the matching `src/*PlatformImplementation.h`.

## Concepts

- `FEsp32UdpDriver` and `FHostUdpDriver` (PlatformHost) share the same 6-byte
  UDP address encoding via `Net/UdpAddressCodec.h`, so wire framing matches
  across platforms.
- `FEsp32E32LoraConfig` carries UART port and GPIO numbers as plain integers
  so this header names ESP-IDF hardware without including its enum types.
- `Esp32LogSink` maps `ELogLevel` to `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`/`ESP_LOGV`
  and is installed once via `SetLogSink`.

## Verification

Compile each header standalone under the ESP-IDF C++17 toolchain with
warnings as errors, exceptions disabled, and RTTI disabled. Document every
exported declaration with the real-hardware behavior it wraps.
