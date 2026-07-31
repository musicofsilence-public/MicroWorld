# Networking Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Networking test executable owns `TNetworking` composition: device/transport host
ownership, the shared router, channel bindings, guaranteed channels, and the
direct lifecycle pumping that preserves frame order. The test executable links
Engine because `EngineNetHostTests` plays the composition root the system itself
must not.

## Concepts

Tests assert generation-checked handles, add-order host start, two-phase
guaranteed-channel wiring, and direct pump dispatch order.

## Verification

Run the `microworld_networking_tests` target via CTest.
