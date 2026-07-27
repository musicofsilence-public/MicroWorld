# PlatformPico Source

Inherits `../AGENTS.md`.

## Architecture

Source files split deterministic E32 state from the thin Pico SDK UART wrapper.
Only `PicoE32LoraDriver.cpp` may include Pico hardware headers.

## Concepts

- Validate UART index, RP2040 pin functions, and exact achieved baud before
  marking a driver open.
- Each call performs bounded work; no source path allocates or blocks.
- The composition root guarantees exclusive UART ownership.
