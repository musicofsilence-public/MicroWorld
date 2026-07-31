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

Six portable systems. Dependencies point inward, and the graph is
machine-enforced by `tools/CheckDependencyBoundaries.py` — a violation fails
`ctest`, not review.

```text
              ┌────────────┐
              │    Core    │  (no dependencies)
              └─────┬──────┘
        ┌───────────┼───────────┐
        │           │           │
   ┌────▼───┐  ┌────▼─────┐ ┌───▼────────┐
   │ Engine │  │Messaging │ │ Transport  │
   └────┬───┘  └────┬─────┘ └────────────┘
        │           │           │
 ┌──────▼───────────▼───────────▼──────┐
 │            Networking               │
 └─────────────────────────────────────┘

 Application sits beside this on Engine alone:
   Core <- Engine <- Application
```

**The invariant the whole shape protects:** Engine and Transport never see each
other. An engine that knows about radios cannot be tested without one, and a
transport that knows about actors cannot be reused. No system joins them either:
`Networking` composes Messaging and Transport behind Core's `IPlaySystem`, and a
composition root is the only thing that hands the result to an engine.

`Platform/Host`, `Platform/Esp32`, and `Platform/Pico` sit outside this graph as
the non-portable edges. Only they may include OS or SDK headers.

---

## System responsibilities

| System | Depends on | Owns |
| --- | --- | --- |
| **Core** | — | Result codes, time, logging, `FLifecycleGuard`, tick scheduling, fixed-capacity containers, delegates, smart pointers, timers, `IPlaySystem` |
| **Engine** | Core | The managed runtime and identity: `UWorld`, `AActor`, `UActorComponent`, the `TEngine`/`IEngine` interface, timer manager, plus the folded Object store, garbage collector, and generation-checked handles |
| **Messaging** | Core | Actor messaging: message types, router, channel bindings, reliable channel. Header-only — no archive |
| **Transport** | Core | Byte I/O: `IDevice`, `TTransportHost`, protocol, framing, plus the optional portable E32 LoRa transport (`FE32LoraDevice`) over `IUartByteStream`. Link it only for LoRa builds — the RadioE32 sources are toggled by `MICROWORLD_TRANSPORT_LORA` |
| **Application** | Core, Engine | Program entry: `FApplication` holds one engine for its lifetime and owns the `Run` frame-loop template |
| **Networking** | Core, Messaging, Transport | `TNetworking` — transport hosts, one shared router, and channels composed behind Core's `IPlaySystem` |
| **Platform/Host** | non-portable | Host UDP over OS sockets, `steady_clock` time source |
| **Platform/Esp32** | non-portable | ESP32-S3 transports (lwIP UDP, E32 LoRa UART, wired UART/I2C/SPI), ESP timer and log |
| **Platform/Pico** | Transport, non-portable | RP2040 E32 LoRa UART over the native Pico SDK |

Six portable systems plus three platform edges. The folder tree under
`Modules/MicroWorld/` states these systems directly: one directory per system,
header and source side by side. Object folded into Engine, and Net + RadioE32
folded into Transport, so the build package count now equals the architecture
system count — the two are reconciled.

---

## Concepts worth knowing before reading code

**The engine interface.** `TEngine<TTraits>` is the concrete engine; its traits
struct carries the eight compile-time capacities. `IEngine` is the narrow
interface an application holds — `BeginPlay`, `Tick`, `EndPlay`, `GetWorld`,
`GetObjectStore`. Class registration and object creation are function templates on
`TEngine`, so they are reachable only from the composition root.

**Two ways an actor enters a world.** `UWorld::RegisterActor` takes an
already-constructed actor before play begins. `UWorld::SpawnActor<TActor>` is the
typed path: it registers the class on first use, queues a factory, and constructs
at the next safe barrier. It works both before play — draining when `BeginPlay`
runs — and during play, so composition and gameplay spawn the same way.

**Engine systems.** `IPlaySystem` (in Core, so Messaging and Transport can implement
it without depending on Engine) has four turns: `BeginPlay`, `PreAdvance`,
`PostAdvance`, `EndPlay`. `TEngine` holds one; `TPlaySystemSet` composes several
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
