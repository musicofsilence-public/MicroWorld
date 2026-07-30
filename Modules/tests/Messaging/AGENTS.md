# Messaging Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Messaging test executable owns message codecs, routing, channel bindings,
and reliable delivery. It reuses the Core test harness and the Engine
allocation counters, and links Engine and Transport because the moved channel
tests retain deterministic Engine/Net host-loopback fixtures. These test-only
dependencies do not expand Messaging's public Core-only boundary.

## Concepts

One test executable links the shared harness plus Engine and Transport fixtures.
Tests assert queued, deterministic delivery with caller-supplied time.

## Verification

Run the `microworld_messaging_tests` target via CTest.
