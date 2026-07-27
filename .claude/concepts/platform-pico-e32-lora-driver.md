# PlatformPico E32 LoRa Driver

## Problem

The Pico-to-ESP32 E32 LoRa test proved the transport on hardware, but its Pico
driver still lives inside one consumer executable. Other Pico applications
cannot reuse it without copying the driver, address encoding, and UART rules.

The promotion must preserve the verified non-blocking, fixed-capacity behavior
without making the portable engine depend on the Pico SDK or FreeRTOS.

## Proposed Approach

Add `Modules/PlatformPico` as a native Pico SDK CMake package. Its first public
feature is `FPicoE32LoraDriver`, an `INetDriver` implementation that owns:

- validated UART and GPIO configuration;
- one fixed-capacity transmit frame slot;
- bounded receive pumping through the existing Net frame decoder;
- an explicit `AdvanceTransmit()` call that advances at most one UART byte.

Move the platform-neutral E32 address helpers and maximum payload constant into
`Modules/Net`. Keep the existing PlatformEsp32 address header as a forwarding
compatibility header so current ESP32 consumers do not break.

Refactor the proven Pico LoRa consumer to construct the packaged driver. The
consumer remains responsible for FreeRTOS task scheduling, while
`PlatformPico` depends only on Net/Core and the Pico UART SDK.

The package will be verified by:

- host tests for the shared E32 wire helpers;
- native Pico cross-compilation and the existing map/profile checks;
- both ESP32 LoRa example builds;
- the Pico-to-ESP32 hardware volley after the refactor.

An interrupt- or DMA-driven transmitter is deliberately deferred. It would
reduce polling but adds concurrency, lifecycle, and buffering complexity that
the proven first package does not need.

## Open Questions

None. The proposed first boundary is intentionally narrow: reusable E32 LoRa
transport only, not a generic Pico hardware abstraction layer.

## Decisions Log

- 2026-07-26: Promote only the hardware-proven E32 UART transport.
- 2026-07-26: Keep FreeRTOS ownership in the composition root, outside the
  reusable driver.
- 2026-07-26: Put shared E32 wire identity in Net and preserve existing ESP32
  include compatibility.
- 2026-07-26: Use explicit bounded transmit advancement instead of IRQ or DMA.
