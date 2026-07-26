# MicroWorld — Architecture Overview

MicroWorld is a mini engine for microcontrollers: UE5's familiar runtime concepts
— World, Actor, Component, lifecycle, garbage collection, networking — rebuilt
small enough for an ESP32-S3. It exists so a UE5 developer can write firmware
without first learning every hardware detail.

Everything here is bounded on purpose: C++17, fixed capacities chosen at compile
time, no heap in steady state, no exceptions, no RTTI, no global mutable state.
Time is always supplied by the caller, never read from a hidden clock.

---

## The dependency graph

Nine packages. Dependencies point inward, and the graph is machine-enforced by
`tools/CheckDependencyBoundaries.py` — a violation fails `ctest`, not review.

```text
                    ┌────────────┐
                    │    Core    │  (no dependencies)
                    └─────┬──────┘
            ┌─────────────┼─────────────┬──────────────┐
            │             │             │              │
        ┌───▼────┐   ┌────▼─────┐  ┌────▼───┐          │
        │ Object │   │ Messaging│  │  Net   │          │
        └───┬────┘   └────┬─────┘  └────┬───┘          │
            │             │             │              │
        ┌───▼────┐        │             │              │
        │ Engine │        │             │              │
        └───┬────┘        │             │              │
            ├─────────────┴─────────────┘              │
            │                                          │
   ┌────────▼──────┐                        ┌──────────▼──────────┐
   │  Integration  │                        │     Application     │
   └───────────────┘                        └─────────────────────┘
```

**The invariant the whole shape protects:** Engine and Net never see each other.
An engine that knows about radios cannot be tested without one, and a transport
that knows about actors cannot be reused. `Integration` is the single package
permitted to join them.

`PlatformHost` and `PlatformEsp32` sit outside this graph as the non-portable
edges. Only they may include OS or SDK headers.

---

## Module responsibilities

| Module | Depends on | Owns |
| --- | --- | --- |
| **Core** | — | Result codes, time, logging, `FLifecycleGuard`, tick scheduling, fixed-capacity containers, delegates, smart pointers, timers, `IEngineSystem` |
| **Object** | Core | Managed identity: class descriptors, object store, garbage collector, generation-checked handles |
| **Engine** | Core, Object | The managed runtime: `UWorld`, `AActor`, `UActorComponent`, the `TEngine`/`IEngine` front door, timer manager |
| **Messaging** | Core | Actor messaging: message types, router, channel bindings, reliable channel. Header-only — no archive |
| **Net** | Core | Byte I/O: `INetDriver`, `TNetHost`, protocol, framing |
| **Application** | Core, Object, Engine | Program entry: `FApplication` holds one engine for its lifetime, `TApplicationRunner` drives the frame loop |
| **Integration** | Core, Object, Messaging, Engine, Net | `TNetSystem` — the one place Engine and Net meet |
| **PlatformHost** | non-portable | Host UDP over OS sockets, `steady_clock` time source |
| **PlatformEsp32** | non-portable | ESP32-S3 transports (lwIP UDP, E32 LoRa UART, wired UART/I2C/SPI), ESP timer and log |

---

## Concepts worth knowing before reading code

**The engine front door.** `TEngine<TTraits>` is the concrete engine; its traits
struct carries the eight compile-time capacities. `IEngine` is the narrow
interface an application holds — `BeginPlay`, `Tick`, `EndPlay`, `GetWorld`,
`GetObjectStore`. Class registration and object creation are function templates on
`TEngine`, so they are reachable only from the composition root.

**Two ways an actor enters a world.** `UWorld::RegisterActor` takes an
already-constructed actor before play begins. `UWorld::SpawnActor<TActor>` is the
typed path: it registers the class on first use, queues a factory, and constructs
at the next safe barrier. It works both before play — draining when `BeginPlay`
runs — and during play, so composition and gameplay spawn the same way.

**Engine systems.** `IEngineSystem` (in Core, so Messaging and Net can implement
it without depending on Engine) has four turns: `BeginPlay`, `PreAdvance`,
`PostAdvance`, `EndPlay`. `TEngine` holds one; `TEngineSystemSet` composes several
with add-order start and reverse-order shutdown.

**Composition roots own everything.** Objects are constructed by the entry point
and passed by reference inward. On ESP32 targets they must be `static` — the main
task stack cannot hold an engine composition as locals.

---

## Verification

`AGENTS.md` at the repository root owns the contributor workflow: build commands,
the five gate scripts, the formatting policy, and the frozen-identity rules.
Each module directory carries its own `AGENTS.md` describing what that directory
owns. Read those before changing build files or public headers.

The short version: `ctest` covers formatting, dependency boundaries, and the
profile map, so a green `ctest` means the architecture rules above still hold.
