# Runtime Implementations

Inherits `../AGENTS.md`.

## Architecture

Source files implement non-template state machines declared by the public API.
`TickFunction.cpp` owns schedule transitions, Actor/Component sources own
hierarchical dispatch, `Network.cpp` owns the independent subsystem lifecycle,
and `Application.cpp` owns the outer composition guard. Templates remain in
headers because capacity is a compile-time consumer choice.

## Concepts and boundaries

- Source files depend only on public MicroWorld headers and approved C++17
  facilities.
- Lifecycle wrappers validate state before consumer hooks run.
- Rollback and end paths preserve deterministic ordering and retain the first
  reported error without skipping cleanup.
- Tick code uses saturated arithmetic and at most one execution per caller
  update.
- Tests, examples, benchmarks, consumers, platform APIs, and product policy
  must not enter this dependency boundary.

## Documentation and verification

Public declarations own primary function/state documentation; add source
comments only for non-obvious rationale, edge cases, or ordering constraints.
Format all sources with the repository policy, build the `microworld` target,
and run behavior tests.
