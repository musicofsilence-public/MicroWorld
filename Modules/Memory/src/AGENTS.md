# Memory Runtime Implementations

Inherits `../AGENTS.md`.

## Architecture

`src/` provides non-template implementation owned by the Memory package.
Sources depend only on public Memory/Core headers and approved C++17
facilities. Compile-time-capacity resource logic remains in public templates.

## Concepts and boundaries

- Implementations preserve explicit resource attribution and typed failures.
- No source may call a platform heap, SDK, logger, clock, thread primitive, or
  product policy.
- Tests, benchmarks, ports, managed objects, GC, and higher modules never enter
  this dependency boundary.

## Verification

Build the `MicroWorld::Memory` target with warnings as errors and verify
compatibility with exceptions and RTTI disabled. Source comments explain only
non-obvious invariants or safety rationale.
