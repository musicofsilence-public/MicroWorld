# Net Tests

Inherits `../AGENTS.md`.

## Architecture

Net host tests use the shared Core test harness from
`Modules/Core/tests/TestMain.cpp` and `TestSupport.h`. One test executable
links `NetAllocationCounters.cpp` plus one behavior file per public type and
aggregates results through `RunAllTests()`.

## Concepts and boundaries

- Each behavior test asserts one observable contract: byte boundaries,
  transactional failure, FIFO ordering, backpressure retention, capacity
  reuse, direct receive propagation, and zero steady-state allocation.
- `NetAllocationCounters.cpp` overrides every global `operator new`/`delete`
  so the executable observes any heap traffic from portable Net paths; the
  allocation test proves steady-state operations perform zero allocations.
- Tests never claim target runtime behavior; they prove host-side correctness
  of the bounded byte-I/O contract.
