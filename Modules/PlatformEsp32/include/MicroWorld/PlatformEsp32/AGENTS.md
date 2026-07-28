# ESP32 Public Driver Headers

Inherits `../../AGENTS.md`.

## Architecture

This directory declares the nine ESP32 adapters: `FEsp32TimeSource` (clock
seam); `FEsp32UdpDriver`, `FEsp32E32LoraDriver`, `FEsp32UartDriver`,
`FEsp32I2cMasterDriver`, `FEsp32I2cSlaveDriver`, `FEsp32SpiMasterDriver`, and
`FEsp32SpiSlaveDriver` (`INetDriver` transport seams — the optional LoRa facade
delegates framing to RadioE32; wired UART, I2C, and SPI drivers use the portable
`Net/FrameCodec.h` CRC-16/CCITT-FALSE framing, over the 1-byte broadcast `LoraAddress` and the
1-byte point-to-point `UartAddress`, `I2cAddress`, and `SpiAddress` respectively);
and `WriteEsp32LogRecord` (the log output device). Their declarations depend only on
Net/Object/Core and optional RadioE32 public headers and stay free of ESP-IDF, lwIP, and vendor
headers; those live only in the matching `src/*PlatformImplementation.h`.

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
- `FEsp32SpiMasterConfig` and `FEsp32SpiSlaveConfig` carry the SPI host and
  MOSI/MISO/SCLK/CS GPIOs as plain integers; the master defaults `ClockHz` to
  1000000 (1 MHz). SPI is full-duplex, so both master ops feed the received
  window to the decoder; the slave keeps one persistent transaction queued.
- `WriteEsp32LogRecord` maps `ELogLevel` to `ESP_LOGE`/`ESP_LOGW`/`ESP_LOGI`/`ESP_LOGV`
  and is installed once via `SetOutputDevice`.

## Verification

Compile each header standalone under the ESP-IDF C++17 toolchain with
warnings as errors, exceptions disabled, and RTTI disabled. Document every
exported declaration with the real-hardware behavior it wraps.
