# Engine Behavior Tests

Inherits `../AGENTS.md` and `../../Core/tests/AGENTS.md`.

## Architecture

Tests consume Engine only through its public contracts and shared Core test
support. They own fresh fixed `TEngineHost` storage and must not depend on
Engine internals or shared mutable state.

## Concepts

Each case constructs an isolated host and observes public results: frame
order, registration closure, weak-parent expiry, spawn/destroy barrier
timing, and bounded timer dispatch, without timing assumptions or
implementation access.

## Verification

Compile with C++17, strict warnings, exceptions disabled, and RTTI disabled.
Exercise direct public postconditions for the 7-step frame order, Begin/End
ordering, capacity exhaustion, and stale timer handles.
