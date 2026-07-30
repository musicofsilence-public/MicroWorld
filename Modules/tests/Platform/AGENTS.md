# Platform Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

`Platform/` holds the host-testable platform edges: `Host` (real OS socket UDP)
and `Pico` (the SDK-free compatibility facade). Esp32 has no host tests because
it is PlatformIO/ESP-IDF only.

## Concepts

Host tests cover real-socket round-trips, time-source monotonicity, and the
Pico facade's UART validation, ownership, and lifecycle delegation. Runtime
readiness for the Pico facade still requires the hardware checkpoint.

## Verification

Run the `microworld_platform_host_tests` and
`microworld_platform_pico_compatibility_tests` targets via CTest.
