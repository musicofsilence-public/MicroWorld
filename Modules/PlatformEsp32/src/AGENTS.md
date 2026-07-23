# ESP32 Platform Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` is the SOLE home of lwIP, ESP-IDF, `<driver/uart.h>`, and
`<driver/i2c_*.h>` headers in this package: each is confined to one private
`*PlatformImplementation.h` (`Esp32SocketPlatformImplementation.h` for the UDP
driver; `E32UartPlatformImplementation.h`, the shared UART open/read/write/close
toolkit, for both `Esp32E32LoraDriver.cpp` and `Esp32UartDriver.cpp`;
`I2cPlatformImplementation.h`, the `<driver/i2c_master.h>`/`<driver/i2c_slave.h>`
toolkit, for `Esp32I2cDriver.cpp`). A public header must never reach these files.

## Concepts

- Every lwIP/ESP-IDF divergence (handle width, non-blocking mode, error
  classification, the oversize-datagram size probe) is hidden behind helpers
  in the platform-implementation header so the driver `.cpp` reads one
  platform-free send/receive path mirroring the host driver.
- Exact runtime behavior at the lwIP/UART boundary (oversize-datagram sizing,
  short-write/would-block classification) is marked UNVERIFIED at runtime
  until exercised on real hardware; the header comments say so explicitly.
- The I2C slave receives only through an ESP-IDF `on_receive` ISR callback that
  copies bytes into the driver-owned `FI2cReceiveInbox`, while the master clocks
  whole-frame read windows; both paths are UNVERIFIED at runtime until example
  20's hardware checkpoint (§1.2).

## Verification

Build with PlatformIO for `esp32-s3-devkitc-1`, warnings as errors, exceptions
and RTTI disabled. Keep target-only claims out of compile-only proofs; record
any new hardware smoke-test evidence in `../benchmarks/Results/`.
