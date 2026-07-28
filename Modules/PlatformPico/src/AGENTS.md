# PlatformPico Source

Inherits `../AGENTS.md`.

## Architecture

Source files implement generic SDK-free byte-stream policy, the thin Pico SDK
UART backend, and compatibility-facade delegation to RadioE32. Only
`PicoUartPlatform.cpp` may include Pico hardware headers; byte-stream policy
remains SDK-free in `PicoUartByteStream.cpp`.

## Concepts

- Validate UART index, RP2040 pin functions, and exact achieved baud before
  marking a byte stream open.
- Each call performs bounded work; no source path allocates or blocks.
- The composition root guarantees exclusive UART ownership.
