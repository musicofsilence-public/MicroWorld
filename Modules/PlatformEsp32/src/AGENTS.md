# ESP32 Platform Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` is the SOLE home of lwIP, ESP-IDF, and `<driver/uart.h>` headers in
this package: each is confined to one private `*PlatformImplementation.h`
(`Esp32SocketPlatformImplementation.h` for the UDP driver;
`E32UartPlatformImplementation.h`, the shared UART open/read/write/close
toolkit, for both `Esp32E32LoraDriver.cpp` and `Esp32UartDriver.cpp`). A public
header must never reach these files.

## Concepts

- Every lwIP/ESP-IDF divergence (handle width, non-blocking mode, error
  classification, the oversize-datagram size probe) is hidden behind helpers
  in the platform-implementation header so the driver `.cpp` reads one
  platform-free send/receive path mirroring the host driver.
- Exact runtime behavior at the lwIP/UART boundary (oversize-datagram sizing,
  short-write/would-block classification) is marked UNVERIFIED at runtime
  until exercised on real hardware; the header comments say so explicitly.

## Verification

Build with PlatformIO for `esp32-s3-devkitc-1`, warnings as errors, exceptions
and RTTI disabled. Keep target-only claims out of compile-only proofs; record
any new hardware smoke-test evidence in `../benchmarks/Results/`.
