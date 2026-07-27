# PlatformPico Host Tests

Inherits `../AGENTS.md`.

## Architecture

These host tests compile the production E32 state source without the Pico SDK.
They use the shared Core test harness and link inward to Net.

## Concepts

- Tests cover observable acceptance, backpressure, byte progression,
  corruption recovery, held-frame retry, and transactional outputs.
- Hardware initialization and UART calls remain native-build and hardware-test
  responsibilities.
