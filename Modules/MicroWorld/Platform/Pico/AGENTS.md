# MicroWorld Platform/Pico

Inherits `../../../AGENTS.md`.

## Architecture

`Platform/Pico` is the non-portable native Pico SDK edge for RP2040. Its public
`MicroWorld::Platform::Pico::FPicoLoraDevice` is a compatibility facade over optional RadioE32 (now inside
Transport); Pico SDK UART/GPIO headers remain behind the `Internal/` implementation
headers. It is excluded from `CheckDependencyBoundaries.py`.

## Concepts

- Construction is inert; the application entry point initializes one exclusively
  owned UART after entering `main`.
- `TrySend(Success)` queues a frame; the composition owner invokes
  `PreAdvance` exactly once after outbound FIFO progress.
- FreeRTOS, SDK fetching, the complete `pico_stdlib` runtime, task scheduling,
  and upload policy belong to the consuming firmware, not this family.
- Host tests cover UART validation, ownership, lifecycle, and facade delegation;
  Transport tests own portable protocol behavior. Runtime readiness still
  requires the Pico-to-ESP32 hardware checkpoint.

## Verification

The host compatibility facade is built by the superbuild as the
`microworld_platform_pico_compatibility_tests` target. The real device target
requires an initialized Pico SDK and is built by the consumer harness at
`../../../tests/Core/consumer/pico-freertos/`.
