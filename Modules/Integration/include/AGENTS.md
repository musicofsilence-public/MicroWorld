# Integration Public Headers

Inherits `../AGENTS.md`.

## Architecture

The public header under `include/MicroWorld/Integration/` exposes
`TNetSystem<TTraits>` (the one object that turns net drivers into a working
networked engine) plus its `EChannelReliability` enum, `FDefaultNetSystemTraits`
capacities, and generation-checked `FNetDriverHandle`/`FChannelHandle`. The
package is header-only — `TNetSystem` is a template instantiated by the caller,
so there is no production translation unit.

## Concepts and boundaries

- Every header uses `#pragma once`, the flat `MicroWorld` namespace, and the
  repository doc-comment style: each declaration explains why it exists, the
  invariant it makes observable, or the ownership boundary it protects.
- Headers may include Core, Object, Engine, and Net public headers: this is the
  only package permitted to reach both Engine and Net.
