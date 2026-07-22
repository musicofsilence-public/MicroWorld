# Object Public Include Boundary

Inherits `../AGENTS.md`.

## Architecture

`include/` is the only supported compile-time surface of the Object package.
Public headers may depend inward on Memory and Core public headers, never on
Object implementation, tests, benchmarks, higher packages, platform code, or
product code.

## Concepts

Public contracts expose explicit ownership, capacity, and bounded-work results
without leaking implementation storage or platform dependencies.

## Verification

Each public header must compile independently under C++17, strict warnings,
exceptions disabled, and RTTI disabled. Document exported functions and state
with the ownership or bounded-work invariant they protect.
