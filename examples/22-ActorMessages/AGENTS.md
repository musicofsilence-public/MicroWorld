# 22-ActorMessages

Inherits `../AGENTS.md`.

## Architecture

One application entry point (`app_main` in `src/Main.cpp`) owns one world with two
actors — `FThermometerActor` (one `FReadingSensorComponent`) and
`FDisplayActor` (no components) — and an engine-owned `FMessagingSystem` with
one local-only channel. Everything is static, sized at compile time, and
allocation-free; no wire, no second board.

## Concepts

- **Local messaging through `FMessagingSystem`.** Both actors talk only to
  injected Messaging — a named reading and a named calibrate reply — never to
  each other directly. The `Local` channel has a null device, so delivery stops
  after local subscribers and no frame reaches a wire.
- **D9 constructor injection.** Both actors take `FMessagingSystem&` (and the
  thermometer also takes its sensor) through their constructor; neither reads
  a global Messaging system, and neither `AActor` nor `UActorComponent` gained any
  messaging member for this.
- **Weak actor subscriptions.** Both actors register their subscribers with
  `MakeWeakOwner`, so collection prevents callbacks into reclaimed actor slots
  and Messaging reclaims the registration.
- **Synchronous local delivery.** A local send dispatches inside
  `SendMessageToChannel`; the display runs in the thermometer's `Tick`, and
  its calibrate subscriber resets the sensor in that same frame. See the
  README for the trace order.
- **Component-before-actor tick order.** The thermometer's `Tick` runs after
  its own sensor component's `TickComponent` within the same `Advance` (see
  `Modules/MicroWorld/Engine/Actor.h`), and both are configured
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
