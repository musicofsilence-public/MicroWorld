# MicroWorld Core 0.2.0

MicroWorld Core is a standalone C++17 lifecycle and tick runtime for bounded
embedded applications. It has no ESP32, RTOS, transport, or product-policy
dependency.

Current development status is in [PROGRESS.md](../../PROGRESS.md).

## What Core provides

- `FApplication` guards a consumer composition root.
- `FTickFunction` owns bounded per-object scheduling: each object carries its
  own tick configuration and enable flags.
- Caller-supplied monotonic milliseconds drive scheduling. Late ticks run once,
  never in a catch-up burst.
- `FLifecycleGuard` and the `FTickable` contract express a forward-only
  begin/tick/end lifecycle without scattered boolean flags.
- Lifecycle, capacity, and time failures return `ERuntimeResult`.
- `MW_LOG` / `MW_LOG_MSG` give a bounded logging facade every package can use.

Core is lifecycle and tick **primitives** only. The managed World / Actor /
Component model lives in the Engine package (`UWorld` / `AActor` /
`UActorComponent`); Core retired its own duplicate Actor model in the Phase 1
consolidation. Consumers still own their concrete objects.

## Public headers

- `MicroWorld/Application.h`
- `MicroWorld/Lifecycle.h`
- `MicroWorld/Log.h`
- `MicroWorld/TickFunction.h`
- `MicroWorld/Tickable.h`
- `MicroWorld/Time.h`
- `MicroWorld/Version.h`

## Logging

`MicroWorld/Log.h` is a bounded logging facade owned by Core and usable from
every package. It has four levels — `Error`, `Warning`, `Log`, `Verbose` — and a
single process-global sink:

```cpp
using FLogSink = void (*)(ELogLevel Level, const char* Category, const char* Message);
```

Install one sink at startup with `SetLogSink`; the default sink is null, which
disables logging. There are two call macros:

```cpp
MW_LOG(Warning, "Net", "peer %u timed out", Index); // printf-style
MW_LOG_MSG(Log, "Boot", "runtime ready");           // already-formed message
```

Use `MW_LOG_MSG` for a runtime string that may itself contain `%`. Formatting
happens in a fixed caller-stack buffer (`MW_LOG_MESSAGE_CAPACITY`, default 128
bytes) via `vsnprintf` — no heap, no exceptions, no clock.

A **compile-time floor** `MW_LOG_MIN_LEVEL` (default `Log`) strips less important
call sites entirely: they expand to nothing, emit no code, keep no format or
category strings in flash, and never evaluate their arguments. Lower the floor
for a build with `-DMW_LOG_MIN_LEVEL=MW_LOG_LEVEL_Verbose`. The facade is
single-threaded; install the sink before the first log call.

## Build

```sh
cmake -S Modules/Core -B <build-directory>
cmake --build <build-directory>
ctest --test-dir <build-directory> --output-on-failure
```

CMake consumers link `MicroWorld::Core`. PlatformIO probes are available with:

```sh
pio run -d Modules/Core/tests/consumer -e native
pio run -d Modules/Core/tests/consumer -e esp32-s3
```

These commands build only. Uploading or running target firmware needs separate
hardware authorization.

## Next scope

The 0.2.0 release has delivered the surrounding runtime that sits on Core:

- **Engine** — managed `UWorld` / `AActor` / `UActorComponent` with fixed
  registration, runtime `SpawnActor` / `DestroyActor` at a deferred
  `ApplyPending` barrier, and bounded `TTimerManager` timers.
- **`TEngineHost`** — the composition root wiring class registry, object
  store, garbage collector, world, timers, and (optionally) a network frame
  behind one fixed 7-step per-frame order.
- **Net** — simple messages with roles: `TNetHost<MaxPeers, MaxPacketBytes>`
  over `ENetMode` (Standalone / Client / ListenServer / DedicatedServer) with
  a bounded peer table, hello/heartbeat/timeout sessions, and channel-based
  send/receive.
- **Platform adapters** — `microworld-platform-host` (UDP) and
  `microworld-platform-esp32` (UDP + E32 LoRa UART) supply the real
  transports; the portable `Net/FrameCodec.h` provides CRC-16/CCITT-FALSE
  framing for the LoRa adapter.
- **Two-node demo** — `Modules/PlatformHost/examples/TwoNodeDemo` is
  the acceptance demo (dedicated server + bare client over real localhost UDP).
- **ESP32-S3 margins measured** — tick / GC-slice / net-pump / heap / stack
  captured on physical ESP32-S3 @ 160 MHz; see the platform-esp32 results
  file.

Core itself still provides only primitives — spawn/destroy, timers, and
networking live in Engine and Net, not Core. Live state and exact evidence are
recorded in [PROGRESS.md](../../PROGRESS.md).

Core does not provide runtime spawn/destroy, timers, networking policy,
reflection, hardware abstraction, or a real-time guarantee.

## Evidence and details

- [Current status and evidence](../../PROGRESS.md)
- [Package architecture](../../docs/ModulePackaging.md)
- [Concept map](../../docs/UE5ConceptMap.md)
- [Resource rules](../../docs/ResourceBudgets.md)
- [Porting to a new platform](../../docs/Porting.md)
- [Host benchmark](benchmarks/Results/Host.md)
- [ESP32-S3 compile evidence](benchmarks/Results/Esp32S3N16R8.md)
