# Engine Public Include Boundary

Inherits `../AGENTS.md`.

## Architecture

`include/` is the only supported compile-time surface of the Engine package.
Public headers may depend inward on Object, Memory, and Core public headers,
never on Engine implementation, tests, benchmarks, examples, platform code, or
product code.

## Concepts

Public contracts expose `UWorld`, `AActor`, `UActorComponent`, `TEngineHost`,
and `TTimerManager` as explicit ownership and bounded-work results without
leaking implementation storage, networking concrete types, or platform
dependencies.

## Verification

Each public header must compile independently under C++17, strict warnings,
exceptions disabled, and RTTI disabled. Document exported functions and state
with the ownership or bounded-work invariant they protect.
