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

Five portable systems. Dependencies point inward, and the graph is
machine-enforced by `tools/CheckDependencyBoundaries.py` — a violation fails
`ctest`, not review.

```text
              ┌────────────┐
              │    Core    │  (no dependencies)
              └─────┬──────┘
        ┌───────────┼───────────┐
        │           │           │
   ┌────▼─────┐     │      ┌────▼──────┐
   │Messaging │     │      │ Transport │
   └────┬─────┘     │      └───────────┘
        │           │
   ┌────▼───────────▼────┐
   │       Engine        │
   └─────────────────────┘

 Application sits beside this on Engine alone:
   Core <- Engine <- Application
```

**The invariant the whole shape protects:** Engine and Transport never see each
other, and neither do Messaging and Transport. An engine that knows about radios
cannot be tested without one, and a transport that knows about messages cannot be
reused. No system joins them: they meet only at `ITransportDevice` in Core — a
messaging channel sends through the interface, each medium realises it, and a
concrete device is named only at the application entry point.

`Platform/Host`, `Platform/Esp32`, and `Platform/Pico` sit outside this graph as
the non-portable edges. Only they may include OS or SDK headers.

---

## System responsibilities

| System | Depends on | Owns |
| --- | --- | --- |
| **Core** | — | Result codes, time, logging, `FLifecycleGuard`, tick scheduling, fixed-capacity containers, delegates, smart pointers, timers, `IPlaySystem`, `ITransportDevice` + `FDeviceAddress` |
| **Engine** | Core, Messaging | The managed runtime and identity: `UWorld`, `AActor`, `UActorComponent`, `TEngine`, the `IEngineRuntime` lifecycle contract, timer manager, the folded Object store, garbage collector, and generation-checked handles — plus creating and handing out `FMessagingSystem` |
| **Messaging** | Core | `FMessagingSystem` (engine-created): `FMessage` (name id + opaque payload), named channels from `FChannelInformation` (reliability, optional device + address), subscriptions with optional message-name filter. Compiled static module. |
| **Transport** | Core | Byte I/O: medium devices realising Core's `ITransportDevice`, wire framing, plus the optional portable E32 LoRa transport (`FE32LoraDevice`) over `IUartByteStream`. Link it only for LoRa builds — the RadioE32 sources are toggled by `MICROWORLD_TRANSPORT_LORA` |
| **Application** | Core, Engine | Program entry: `FApplication` holds one `IEngineRuntime` for its lifetime and owns the `Run` frame-loop template |
| **Platform/Host** | non-portable | Host UDP over OS sockets, `steady_clock` time source |
| **Platform/Esp32** | non-portable | ESP32-S3 transports (lwIP UDP, E32 LoRa UART, wired UART/I2C/SPI), ESP timer and log |
| **Platform/Pico** | Transport, non-portable | RP2040 E32 LoRa UART over the native Pico SDK |

Five portable systems plus three platform edges. The folder tree under
`Modules/MicroWorld/` states these systems directly: one directory per system,
header and source side by side. Object folded into Engine, Net + RadioE32 folded
into Transport, and Networking dissolved into Messaging, so the build package
count equals the architecture system count — the two are reconciled.

---

## Concepts worth knowing before reading code

**The engine runtime interface.** `TEngine<TTraits>` is the concrete engine; its
traits struct carries the eight compile-time capacities. `IEngineRuntime` is the
three-turn interface an application holds — `BeginPlay`, `Tick`, and `EndPlay`.
World, store, messaging, timers, class registration, and object creation remain
concrete `TEngine` APIs. An application that configures one retains that typed
dependency explicitly before runtime begin.

**Two ways an actor enters a world.** `UWorld::RegisterActor` takes an
already-constructed actor before play begins. `UWorld::SpawnActor<TActor>` is the
typed path: it registers the class on first use, queues a factory, and constructs
at the next safe barrier. It works both before play — draining when `BeginPlay`
runs — and during play, so composition and gameplay spawn the same way.

**Engine systems.** `IPlaySystem` (in Core, so Messaging and Transport can implement
it without depending on Engine) has four turns: `BeginPlay`, `PreAdvance`,
`PostAdvance`, `EndPlay`. `TEngine` holds one; `TPlaySystemSet` composes several
with add-order start and reverse-order shutdown.

**The application entry point owns everything.** Objects are constructed by the entry point
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

<!-- code-review-graph MCP tools -->
## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph. ALWAYS use the
code-review-graph MCP tools BEFORE using Grep/Glob/Read to explore
the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file
scanning cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes_tool` or `query_graph_tool` instead of Grep
- **Understanding impact**: `get_impact_radius_tool` instead of manually tracing imports
- **Code review**: `detect_changes_tool` + `get_review_context_tool` instead of reading entire files
- **Finding relationships**: `query_graph_tool` with callers_of/callees_of/imports_of/tests_for
- **Architecture questions**: `get_architecture_overview_tool` + `list_communities_tool`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes_tool` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context_tool` | Need source snippets for review — token-efficient |
| `get_impact_radius_tool` | Understanding blast radius of a change |
| `get_affected_flows_tool` | Finding which execution paths are impacted |
| `query_graph_tool` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes_tool` | Finding functions/classes by name or keyword |
| `get_architecture_overview_tool` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes_tool` for code review.
3. Use `get_affected_flows_tool` to understand impact.
4. Use `query_graph_tool` pattern="tests_for" to check coverage.
