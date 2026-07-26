# MicroWorld Engine

MicroWorld Engine is the managed-runtime layer above Object. It provides
`UWorld`, `AActor`, and `UActorComponent` for bounded embedded applications.
The runtime is built and host-tested, and its tick / GC-slice / net-pump
margins were measured on physical ESP32-S3 hardware in roadmap Phase 6.2 (see
[../PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md](../PlatformEsp32/benchmarks/Results/Esp32S3N16R8.md)).

Engine's headers and tests define current behavior; measured margins are indexed
by [ResourceBudgets.md](../../docs/ResourceBudgets.md).

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
- `TTimerManager<MaxTimers, InlineTimerCallbackBytes>` schedules fixed-capacity
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
  1. `EngineSystem::PreAdvance` — drain inbound traffic, dispatch messages,
     age peers (omitted when no network frame is bound);
  2. `Timers.Advance` — fire due timer callbacks;
  3. `World.Advance` — tick every component, then every actor;
  4. `World.ApplyPending` — begin pending spawns; end and unregister pending
     destroys;
  5. `Store.ApplyPendingDestroy` — bounded reclamation of the slots step 4
     marked (the GC sweep skips pending-destroy slots, so destroyed actors are
     reclaimed here, not by mark/sweep);
  6. GC slice — start a cycle when idle, then advance one bounded slice;
  7. `EngineSystem::PostAdvance` — flush outbound traffic and heartbeats (omitted
     when no network frame is bound).
- Lifecycle methods return `ERuntimeResult`; registration methods return
  `EEngineResult`; timer methods return `ETimerResult`.

The application owns the object store, root table, GC worklist, caller-owned
registration storage, and one `TStrongObjectPtr<UWorld>` root.

## Why registration storage sits outside the objects

The object store hands out `MaxObjects` fixed slots of `SlotSizeBytes` each, and
every slot is the same width because any slot must be able to hold any managed
type. The width is therefore set by the largest type — so a byte added to
`UWorld` is charged once per slot, for a world there is exactly one of.

The world's registration storage is not small. The live, pending-spawn, and
pending-destroy arrays plus `MaxActors × InlineDeferredSpawnFactoryBytes` of
deferred factory space come to several hundred bytes at default traits. Inside
`UWorld` they would widen every slot; as `TEngineHost` members they are paid for
once. That is the only benefit `FWorldActorRegistry` and
`FWorldActorRegistryReference` deliver, and the indirection is worth it only
because the target's RAM is measured in hundreds of kilobytes.

`AActor` does the opposite, for the mirror-image reason: its components are a
plain member array, because those bytes already sat inside the actor's own slot
and moving them out would have bought nothing.

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

## Actor messaging

- `Message.h` is the header-only vocabulary: id/result types
  (`FMessageTypeId`, `FMessageActorId`, `FMessageChannelId`, `EMessageResult`),
  the little-endian `EncodeActorMessage`/`DecodeActorMessage` codec,
  `FMessageView`, and the
  `IEncodedMessageSink`/`IMessageChannel`/`IMessageRouter` interfaces.
- `TMessageRouter` is the local bus: fixed handler table, broadcast
  (`BroadcastActorId`) + targeted send, one-frame latency via
  `PreAdvance`/`PostAdvance`; it is also an `IEngineSystem`.
- `TMessageChannelBinding<TNet>` carries one channel over a `TNetHost` and is
  **duck-typed** on the net type, so **Engine keeps zero dependency on Net**
  — that seam is the whole point.
- `TEngineSystemSet<MaxFrames>` pumps several frames (nets + router + reliable
  channels) behind one engine `IEngineSystem` slot, dispatch in add order and
  flush in reverse (D3).
- `TReliableChannel<MaxPendingMessages, MaxMessageBytes>` wraps a binding to
  add acknowledged, de-duplicated point-to-point delivery: sequence + ack +
  retry (bounded by `MaxSendAttempts`, then counted `LostCount`) plus a
  32-wide duplicate window, so a delivered message arrives exactly once.

Where an actor message sits inside the existing wire stack (§4.2):

```
driver frame:   [magic][node][len][ TNetHost message ][crc]        (FrameCodec — wired transports)
TNetHost msg:   [u8 WireChannel][u8 Flags=0][u16 PayloadBytes][ payload ]   (NetProtocol)
payload:        EncodedActorMessage                                (best-effort channel)
payload:        [u8 Kind][u16 Sequence][EncodedActorMessage]       (guaranteed channel, Kind=1 Data)
payload:        [u8 Kind][u16 Sequence]                            (guaranteed channel, Kind=2 Ack)
EncodedActorMessage = [u16 MessageTypeId][u16 TargetActorId][u16 SenderActorId][Payload...]
ActorMessageHeaderBytes = 6   ReliableHeaderBytes = 3
```

Engine does not provide networking, subsystems, serialization, replication,
platform abstraction, or hardware APIs. (Networking is reachable through an
engine-owned `IEngineSystem` seam bound into `TEngineHost`; the net host itself
lives in the Net package so Engine stays net-free.)
