# MicroWorld PlatformPico Package

Inherits `../AGENTS.md`.

## Architecture

`PlatformPico` is the non-portable native Pico SDK edge for RP2040. Its public
`FPicoE32LoraDriver` implements Net's `INetDriver`; dependencies point inward
to Net and Core, while Pico SDK UART/GPIO headers remain in `src/`.

## Concepts

- Construction is inert; the composition root initializes one exclusively
  owned UART after entering `main`.
- The driver is fixed-capacity and caller-polled: `TrySend` accepts one frame,
  and the generic `INetDriver::AdvanceTransmit` hook advances at most one byte.
- FreeRTOS, SDK fetching, the complete `pico_stdlib` runtime, task scheduling,
  and upload policy belong to the consuming firmware, not this package.
- Host tests exercise SDK-free E32 state and driver policy through the narrow
  internal UART binding; runtime readiness still requires the Pico-to-ESP32
  hardware checkpoint.
