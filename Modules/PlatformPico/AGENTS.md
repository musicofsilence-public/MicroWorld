# MicroWorld PlatformPico Package

Inherits `../AGENTS.md`.

## Architecture

`PlatformPico` is the non-portable native Pico SDK edge for RP2040. Its public
`FPicoE32LoraDriver` is a compatibility facade over optional RadioE32; Pico SDK
UART/GPIO headers remain in `src/`.

## Concepts

- Construction is inert; the composition root initializes one exclusively
  owned UART after entering `main`.
- `TrySend(Success)` queues a frame; direct callers invoke `AdvanceTransmit`,
  while `TNetHost` already advances its driver after outbound FIFO progress.
- FreeRTOS, SDK fetching, the complete `pico_stdlib` runtime, task scheduling,
  and upload policy belong to the consuming firmware, not this package.
- Host tests cover UART validation, ownership, lifecycle, and facade delegation;
  RadioE32 tests own portable protocol behavior. Runtime readiness still
  requires the Pico-to-ESP32 hardware checkpoint.
