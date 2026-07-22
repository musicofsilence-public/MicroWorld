# MicroWorld Engine

MicroWorld Engine is the managed-runtime layer above Object. It provides
`UWorld`, `AActor`, and `UActorComponent` for bounded embedded applications.
The runtime is built and host-tested, and its tick / GC-slice / net-pump
margins were measured on physical ESP32-S3 hardware in roadmap Phase 6.2 (see
[../PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md](../PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md)).

Current status and recorded evidence live in
[PROGRESS.md](../../PROGRESS.md).

## What Engine provides

- `UWorld` registers and dispatches `AActor` instances.
- `AActor` registers and dispatches `UActorComponent` instances.
- Registration is fixed-capacity and closes at `BeginPlay`.
- Begin and tick use registration order; shutdown uses reverse registration
  order. Components begin and tick before their Actor; Actors end before their
  Components.
- **Runtime `SpawnActor` / `DestroyActor`** queue at the call site and apply at
  a single deferred `ApplyPending(now)` barrier once per frame (destroys first,
  then spawns, under a fresh lifecycle guard). Capacity counts live + pending
  actors; every rejection (capacity full, locked lifecycle, invalid handle) is
  transactional — no half-applied mutation survives the barrier.
- `UWorld` traces Actors; `AActor` traces Components; parent links are weak and
  expire when the parent is reclaimed.
- Caller-supplied monotonic milliseconds drive scheduling.
- `TTimerManager<MaxTimers, InlineCallbackBytes>` schedules fixed-capacity
  one-shot and looping timers from caller-supplied time. Every other
  `ETimerMode` value (including `None` and arbitrary casts) is rejected
  transactionally as `InvalidMode`. The application owns the manager value,
  supplies every clock reading, and decides when Advance is called relative to
  World dispatch. `FTimerHandle` is a {slot index, generation} pair local to
  the issuing manager; completed one-shots are cleared in place and removed in
  a single stable post-dispatch compaction pass.
- **`TEngineHost<...>`** is the composition root. It owns the class registry,
  object store, garbage collector, world actor registry, and timer manager, and
  drives one fixed per-frame order:
  1. `NetworkFrame::TickDispatch` — drain inbound traffic, dispatch messages,
     age peers (omitted when no network frame is bound);
  2. `Timers.Advance` — fire due timer callbacks;
  3. `World.Advance` — tick every component, then every actor;
  4. `World.ApplyPending` — begin pending spawns; end and unregister pending
     destroys;
  5. `Store.ApplyPendingDestroy` — bounded reclamation of the slots step 4
     marked (the GC sweep skips pending-destroy slots, so destroyed actors are
     reclaimed here, not by mark/sweep);
  6. GC slice — start a cycle when idle, then advance one bounded slice;
  7. `NetworkFrame::TickFlush` — flush outbound traffic and heartbeats (omitted
     when no network frame is bound).
- Lifecycle methods return `ERuntimeResult`; registration methods return
  `EEngineResult`; timer methods return `ETimerResult`.

The application owns the object store, root table, GC worklist, caller-owned
registration storage, and one `TStrongObjectPtr<UWorld>` root.

## Build

```sh
cmake -S Modules/Engine -B <build-directory>
cmake --build <build-directory>
ctest --test-dir <build-directory> --output-on-failure
```

CMake consumers link `MicroWorld::Engine`. A successful compile or host test
does not establish target runtime margins or hardware behavior.

## Example

[`examples/HostLifecycle/Main.cpp`](examples/HostLifecycle/Main.cpp) is the
canonical "hello MicroWorld": it builds a managed composition through
`TEngineHost`, registers one actor and one ticking component, and drives
`BeginPlay` / `Tick` / `EndPlay` so the deterministic lifecycle order is visible
on stdout. Build it with the CMake target `microworld_engine_host_lifecycle`
(included by the default `host-eng` configuration above). It is the smallest
demonstration of the composition root added in roadmap Phase 3.

Engine does not provide networking, subsystems, serialization, replication,
platform abstraction, or hardware APIs. (Networking is reachable through an
engine-owned `INetworkFrame` seam bound into `TEngineHost`; the net host itself
lives in the Net package so Engine stays net-free.)
