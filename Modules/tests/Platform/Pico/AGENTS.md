# Platform/Pico Host Behavior Tests

Inherits `../../../AGENTS.md`.

## Architecture

SDK-free compatibility-facade tests for `FPicoE32LoraDriver`: UART validation,
ownership, lifecycle, and facade delegation to the portable RadioE32 transport
(now inside Transport) without SDK access.

## Concepts

A fake platform records UART lifetime and byte-progress; protocol decoding
belongs to the Transport tests.

## Verification

Run the `microworld_platform_pico_compatibility_tests` target via CTest.
