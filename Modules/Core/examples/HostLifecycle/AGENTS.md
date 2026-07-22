# Host Lifecycle Example

Inherits `../AGENTS.md`.

## Architecture

`Main.cpp` is one executable composition root. A recording Component belongs
to a recording Actor, the Actor belongs to a fixed-capacity World, and the
stack-owned objects are declared so reverse destruction cannot invalidate a
registered pointer.

## Concepts

- Component hooks run before their Actor during begin and advance.
- Actor end runs before Component end.
- Explicit timestamps demonstrate that disabling the Actor tick does not
  suppress the Component's independent schedule.
- Printed output is an observation aid, not a logging dependency of MicroWorld.

## Documentation and verification

Document hook overrides, trace storage, and counters by the lifecycle fact they
make observable. Keep the trace deterministic and exclude product, hardware,
and tutorial policy. Build and run the target, then verify the expected order
and tick counts.
