# Messaging Sources

Inherits `../AGENTS.md`.

## Architecture

Messaging currently has no production translation units. Its portable router,
channel, frame, and reliability primitives are templates or inline codecs whose
caller-selected capacities must remain visible at instantiation.

## Concepts

- This directory is the explicit home for future non-template implementation
  only when it can preserve the package's Core-only, fixed-capacity boundary.
- A future source file may depend only on Messaging's public headers and Core;
  it must not introduce hidden transport, engine, clock, heap, or SDK coupling.

## Verification

Any future source here must compile with the package's strict portable options
and preserve deterministic, allocation-free steady-state message work.
