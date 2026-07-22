# Object Behavior Tests

Inherits `../AGENTS.md` and `../../Core/tests/AGENTS.md`.

## Architecture

Tests consume Object only through its public contracts and shared Core test
support. They own fresh fixed storage and must not depend on Object internals
or shared mutable state.

## Concepts

Each case constructs isolated storage and observes public results, resolution,
destruction, roots, and operation counts without timing or implementation
access.

## Verification

Compile with C++17, strict warnings, exceptions disabled, and RTTI disabled.
Exercise direct public postconditions for capacity, stale generations, roots,
cycles, bounded collection, and deferred destruction.
