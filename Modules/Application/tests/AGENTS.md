# Application Tests

Inherits `../AGENTS.md`.

## Architecture

Application host tests use the shared Core test harness from
`Modules/Core/tests/TestMain.cpp` and `TestSupport.h`. One test executable
links the behavior files and aggregates results through `RunAllTests()`.
Because `FApplication` drives an `IEngine`, the tests supply a minimal `IEngine`
double that records and scripts `BeginPlay`/`Tick`/`EndPlay` results rather
than building a real `TEngine`.

## Concepts and boundaries

- Each behavior test asserts one observable contract: `OnConfigure` runs once
  during `BeginPlay` before the engine begins; a failed configure latches the
  lifecycle terminal and fires `OnBeginPlayFailed`; backward time is rejected
  before the engine sees it; the runner drives one engine `Tick` per frame and
  ends play exactly once after a stopping frame.
- The `IEngine` double records call counts and scripts return values so the
  sealed forwarders are observed behaviourally without a real world or store.
- Tests never claim target runtime behavior; they prove host-side correctness
  of the program-entry contract.
