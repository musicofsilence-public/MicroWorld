# Engine Public Contracts

Inherits `../../AGENTS.md`.

## Architecture

`Engine/` owns the managed-runtime contracts above Object: `UWorld` traces
`AActor`, `AActor` traces `UActorComponent`, and `TEngineHost` wires the class
registry, object store, garbage collector, world root, and `TTimerManager`
behind one canonical per-tick frame order. `NetworkFrame.h` defines the
`INetworkFrame` seam so `TEngineHost` can drive an optional caller-owned
network host without this package depending on `microworld-net`.

## Concepts

- `TEngineHost::Tick` runs a fixed 7-step frame: (1) network TickDispatch,
  (2) `Timers.Advance`, (3) `World.Advance`, (4) `World.ApplyPending`,
  (5) `Store.ApplyPendingDestroy`, (6) one bounded GC slice, (7) network
  TickFlush. Steps 3-4 produce the authoritative per-frame result; every other
  step is bounded best-effort.
- Components dispatch before their owning Actor during Begin and Advance;
  End runs in the reverse of registration order.
- `TTimerManager` is a standalone caller-owned value with no reference to
  `UWorld`, `AActor`, or `UActorComponent`; `FTimerHandle` is a
  {slot index, generation} pair local to the issuing manager.
- `INetworkFrame` keeps the network seam optional: a null frame leaves both
  Tick steps inert, and `TNetHostFrame<TNet>` adapts a concrete net host
  without naming it in this package.

## Verification

Compile headers under C++17, strict warnings, exceptions disabled, and RTTI
disabled. Engine behavior tests exercise the frame order, registration
closure, weak-parent expiry, and timer bounded dispatch through these public
contracts.
