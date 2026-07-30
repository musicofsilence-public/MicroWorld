# Integration Tests

Inherits `../AGENTS.md`.

## Architecture

Integration host tests use the shared Core test harness from
`Modules/Core/tests/TestMain.cpp` and `TestSupport.h`. One test executable
links Engine and Net and aggregates results through `RunAllTests()`. Engine is
linked by the test target, not by the package: `EngineNetHostTests.cpp` plays the
composition root that binds a net host to an engine, which is exactly the role
Integration itself must not take. `NetSystemTests.cpp` owns the
`TNetSystem` contract: the handle, channel, capacity, and ordering rules.

## Concepts and boundaries

- Each behavior test asserts one observable contract: two drivers on one
  system, a best-effort and a guaranteed channel on the same driver, a stale
  handle rejected, capacity exhausted on both `AddNetDriver` and `AddChannel`,
  and `PreAdvance`/`PostAdvance` pumping in the required order.
- Tests compose a `TNetSystem` over `THostLoopback` drivers (the deterministic
  host loopback) so they prove the composition without real transports.
- Tests never claim target runtime behavior; they prove host-side correctness
  of the networked-engine composition.
