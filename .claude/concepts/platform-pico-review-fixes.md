# PlatformPico Review Fixes

## Problem

The promoted Pico E32 driver works in the proven interoperability firmware, but
five review findings limit its reuse: generic `INetDriver` consumers cannot
advance queued UART bytes, standalone CMake defaults fail, PlatformIO metadata
overstates framework support, the SDK wrapper lacks automated tests, and one
backpressure test cannot detect an overwrite.

## Proposed Approach

Add a bounded, default-no-op transport progress hook to `INetDriver`, invoke it
from the standard network pump, and implement it in `FPicoE32LoraDriver`.
Correct the PlatformPico build defaults and native-SDK metadata, introduce a
small fakeable Pico SDK seam for wrapper tests, and strengthen the transactional
transport-state tests with distinct frames and retry cases.

## Open Questions

None. The fixes preserve the existing one-byte-per-pump transmit bound and do
not add Arduino support.

## Decisions Log

- 2026-07-27: Keep transmit progress bounded and caller-driven through the
  generic driver lifecycle — this preserves deterministic MCU work while making
  PlatformPico usable through `TNetSystem`.
- 2026-07-27: Advertise only the verified native Pico SDK surface — Arduino
  compatibility remains explicitly out of scope.
- 2026-07-27: Test SDK orchestration through a narrow fake seam — hardware tests
  remain the authority for real electrical behavior.
