# Public Include Boundary

Inherits `../AGENTS.md`.

## Architecture

`include/` is the only supported compile-time dependency surface for consumers.
Headers expose value types, lifecycle/tick primitives, ownership boundaries,
and bounded templates. Implementation, tests, examples, benchmarks, and
platform shells depend inward on this tree; public headers never depend back.

## Concepts

- Every header is self-sufficient under C++17.
- Templates hold fixed-capacity non-owning registrations because capacity is a
  consumer compile-time decision.
- Small virtual interfaces allow heterogeneous lifecycle dispatch while state
  ownership stays explicit.
- No public declaration may require platform headers, dynamic allocation,
  exceptions, RTTI, threads, clocks, or logging.

## Documentation and verification

Every exported function and data member requires an adjacent intent-focused
Doxygen contract. Keep inline/template hot paths bounded and allocation-free.
Compile each public header independently and build the `microworld` target with
warnings treated as errors.
