# PlatformPico Host Tests

Inherits `../AGENTS.md`.

## Architecture

These host tests cover the Pico compatibility facade without the Pico SDK.
They use the shared Core test harness and link inward to RadioE32 and Net.

## Concepts

- Tests cover UART validation, ownership, lifecycle, alias compatibility, and
  facade delegation; RadioE32 tests own framing and transactional protocol cases.
- Hardware initialization and UART calls remain native-build and hardware-test
  responsibilities.
