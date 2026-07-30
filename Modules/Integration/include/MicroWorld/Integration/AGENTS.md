# Integration Networked-Engine Header

Inherits `../../AGENTS.md`.

## Architecture

The `Integration/` header defines the networked-engine composition:
`NetSystem.h` owns `TNetSystem<TTraits>`, the one object that composes net
hosts, a shared message router, per-channel bindings, guaranteed channels, and
direct lifecycle pumping behind the `IPlaySystem` interface a `TEngine`
drives.

## Concepts and boundaries

- `TNetSystem<TTraits>` derives `IPlaySystem` and owns every subsystem in
  fixed-capacity in-place storage sized by the traits, so it never allocates and
  every owned object's address is stable for its bound relationships.
- `AddNetDriver`/`AddChannel` return generation-checked handles and enforce the
  ordering rules (guaranteed `SetInnerChannel` before router `AddChannel`; hosts
  receive before router dispatch; router flushes before reliable channels and
  hosts) so a caller cannot observe them.
- `FDefaultNetSystemTraits` carries the ESP32-S3 starting-point capacities; a
  project overrides members in its own traits struct to grow or shrink.
