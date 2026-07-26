# 22-ActorMessages

Inherits `../AGENTS.md`.

## Architecture

One composition root (`app_main` in `src/Main.cpp`) owns one world with two
actors — `FThermometerActor` (one `FReadingSensorComponent`) and
`FDisplayActor` (no components) — and one `TMessageRouter` that doubles as the
`TEngineHost`'s network frame. Everything is static, sized at compile time,
and allocation-free; no wire, no second board.

## Concepts

- **Local messaging through `IMessageRouter`.** Both actors talk only to the
  router — a broadcast reading and a targeted calibrate reply — never to each
  other directly.
- **D9 constructor injection.** Both actors take `IMessageRouter&` (and the
  thermometer also takes its sensor) through their constructor; neither reads
  a global router, and neither `AActor` nor `UActorComponent` gained any
  messaging member for this.
- **D5 one-frame local latency.** A send queues; only the next `Tick` call's
  `PreAdvance` delivers it. See the README's teaching-point section for the
  exact frame-by-frame trace.
- **Component-before-actor tick order.** The thermometer's `Tick` runs after
  its own sensor component's `TickComponent` within the same `Advance` (see
  `Modules/Engine/include/MicroWorld/Engine/Actor.h`), and both are configured
  with the identical 500 ms cadence started from the same boot time — so the
  actor always broadcasts the reading its sensor just produced, never a stale
  one.

## Verification

Build Verify (`../AGENTS.md`): `pio run -d
examples/22-ActorMessages` then the root `cmake --build` / `ctest`. Hardware
checkpoint (`../AGENTS.md`, human-gated):

```sh
pio run -d examples/22-ActorMessages -t upload --upload-port <COM-port>
pio device monitor -d examples/22-ActorMessages
```

Expect the deterministic trace in the README: five broadcast/received reading
pairs, a calibrate send and receipt after the 5th, then two more reading pairs
showing the counter has reset, ending `done readings=7`.
