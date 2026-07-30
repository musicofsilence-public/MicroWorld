# Engine Host Behavior Tests

Inherits `../../AGENTS.md`.

## Architecture

The Engine test executable consumes the merged Engine system (the folded Object
store and GC plus the World/Actor/Component runtime) only through public
contracts and shared Core test support. Object-store tests own capacity, stale
generations, roots, cycles, and bounded collection; Engine tests own
registration, lifecycle order, deferred spawn, and host composition.

## Concepts

Each case constructs isolated storage and observes public results, resolution,
destruction, roots, and operation counts without timing or implementation
access. Engine allocation counters are shared with the Messaging tests.

## Verification

Compile with C++17, strict warnings, exceptions disabled, and RTTI disabled.
Run the `microworld_engine_tests` target via CTest.
