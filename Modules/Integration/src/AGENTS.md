# Integration Sources

Inherits `../AGENTS.md`.

## Architecture

Integration currently has no production translation units because `TNetSystem`
is a caller-instantiated template in the public header. This directory remains
the explicit home for any future non-template Integration implementation,
without changing the package's role as the sole Engine–Net composition boundary.

## Concepts

- `TNetSystem` stays header-only so its caller-selected capacities remain
  compile-time values without a separate allocation-owning runtime object.
- A future source file may depend on Core, Object, Messaging, Net, and Engine
  only through Integration's composition boundary.

## Verification

Any future source here must compile with the package's strict portable options
and preserve the fixed-capacity, allocation-free composition model.
