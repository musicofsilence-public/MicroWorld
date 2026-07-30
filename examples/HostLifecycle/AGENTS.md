# Engine Host Lifecycle Example

Inherits `../AGENTS.md`.

## Architecture

`Main.cpp` is one executable composition root built on `TEngineHost`. A
100 ms-cadence sensor `UActorComponent` belongs to a tick-disabled device
`AActor`, and both live inside the host's fixed-capacity storage so the
lifecycle order is observable without a hidden allocator.

## Concepts

- Component hooks (`sensor begin`/`sensor tick`/`sensor end`) run relative to
  their Actor's hooks in the order `TEngineHost::Tick` and `EndPlay` define.
- Disabling the Actor's own tick (`bStartWithTickEnabled = false`) does not
  suppress the Component's independent 100 ms schedule, proving component and
  actor ticks are scheduled independently.
- Driving five tick times against the 100 ms cadence prints a deterministic
  7-line begin/tick/end trace, byte-identical across runs.
- Printed output is an observation aid, not a logging dependency of MicroWorld.

## Documentation and verification

Document hook overrides, trace storage, and counters by the lifecycle fact
they make observable. Keep the trace deterministic and exclude product,
hardware, and tutorial policy. Build and run the target, then verify the
7-line trace and tick counts against the documented cadence.
