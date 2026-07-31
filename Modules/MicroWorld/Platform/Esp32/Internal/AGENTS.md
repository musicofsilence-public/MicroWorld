# ESP32 Internal Adapter Details

Inherits `../AGENTS.md`.

## Architecture

This directory holds unsupported implementation details used by PlatformEsp32
compatibility facades. `FEsp32UartByteStream` implements Core's narrow
`IUartByteStream` interface while owning ESP-IDF UART installation; dependency flow
remains inward from PlatformEsp32 to Core and optional RadioE32, never back.

## Concepts

- Publicly included Internal declarations use only plain integer configuration
  fields and Core contracts; ESP-IDF headers and types stay in `src/`.
- The stream owns one UART exclusively from successful `Open` through `Close`
  or destruction; configuration, device lifecycle policy, and vendor calls
  remain platform responsibilities.
- One method attempts one byte and maps temporary unavailability separately
  from hard UART errors. This is a byte-transfer interface, not a universal HAL.

## Verification

Compile PlatformEsp32 with PlatformIO under ESP-IDF, strict warnings,
exceptions disabled, and RTTI disabled. Check public Internal headers for vendor
includes and test portable RadioE32 behavior with a fake byte stream; target
UART lifecycle behavior requires the owning platform's hardware checks.
