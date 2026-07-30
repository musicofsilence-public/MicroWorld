# Integration Sources

Inherits `../AGENTS.md`.

## Architecture

Integration currently has no production translation units because `TNetSystem`
is a caller-instantiated template in the public header. This directory remains
the explicit home for any future non-template Integration implementation,
without changing the package's role as the Messaging–Net composition boundary.

## Concepts

- `TNetSystem` stays header-only so its caller-selected capacities remain
  compile-time values without a separate allocation-owning runtime object.
- A future source file may depend on Core, Messaging, and Net only. Engine stays
  out, here as in the public header.

## Verification

Any future source here must compile with the package's strict portable options
and preserve the fixed-capacity, allocation-free composition model.
