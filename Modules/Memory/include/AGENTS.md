# Memory Public Include Boundary

Inherits `../AGENTS.md`.

## Architecture

`include/` is the only supported compile-time surface of the Memory package.
Higher packages and applications depend inward on these headers; public headers
never include implementation, tests, platform code, or product code.

## Concepts and boundaries

- Every header is self-contained under C++17.
- Allocation and ownership remain explicit through caller-selected resources.
- Templates retain their implementation here so compile-time capacity and
  alignment remain consumer decisions.
- Public contracts require no platform heap, exceptions, RTTI, threads, SDK,
  or hidden global state.

## Verification

Compile each public header independently with strict warnings and with
exceptions and RTTI disabled. Document every exported function and persistent
state member with the ownership or invariant it protects.
