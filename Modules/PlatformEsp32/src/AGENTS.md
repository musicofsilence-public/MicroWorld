# ESP32 Platform Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` is the SOLE home of lwIP, ESP-IDF, `<driver/uart.h>`, `<driver/i2c_*.h>`,
and `<driver/spi_*.h>` headers in this package: each is confined to one private
`*PlatformImplementation.h` (`Esp32SocketPlatformImplementation.h` for the UDP
driver; `UartPlatformImplementation.h`, the shared UART open/read/write/close
toolkit for `Esp32UartDriver.cpp` and `Esp32UartByteStream.cpp`; the byte stream
is the internal Core seam used by the optional E32 compatibility facade;
`I2cPlatformImplementation.h`, the `<driver/i2c_master.h>`/`<driver/i2c_slave.h>`
toolkit, for `Esp32I2cDriver.cpp`; `SpiPlatformImplementation.h`, the
`<driver/spi_master.h>`/`<driver/spi_slave.h>` toolkit, for `Esp32SpiDriver.cpp`).
A public header must never reach these files.

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
- SPI is full-duplex and DMA-backed: every master transaction sends and receives,
  so both master ops feed the received window to the decoder; the slave keeps one
  persistent queued transaction (its descriptor lives in opaque driver storage).
  DMA buffers require the driver to be static/global; UNVERIFIED at runtime until
  example 21's hardware checkpoint (§1.2).

## Verification

Build with PlatformIO for `esp32-s3-devkitc-1`, warnings as errors, exceptions
and RTTI disabled. Keep target-only claims out of compile-only proofs; record
any new hardware smoke-test evidence in `../benchmarks/Results/`.
