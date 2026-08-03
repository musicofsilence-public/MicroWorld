# MicroWorld Package Layout

MicroWorld ships the portable systems as one package and each non-portable
platform edge as its own. Boundaries are enforced by the dependency gate and by
per-system CMake targets, not by splitting the shipped package — so a small
application still keeps unreferenced object code out of firmware through static
archive linking.

## Architecture view

![MicroWorld C4 container architecture](diagrams/microworld-c4-architecture.svg)

[Open the high-resolution PNG](diagrams/microworld-c4-architecture.png) or
inspect the
[editable Mermaid source](diagrams/microworld-c4-architecture.mmd).

| System | CMake target | PlatformIO package |
| --- | --- | --- |
| Core | `MicroWorld::Core` | `MicroWorld` |
| Engine | `MicroWorld::Engine` | `MicroWorld` |
| Messaging | `MicroWorld::Messaging` | `MicroWorld` |
| Transport | `MicroWorld::Transport` | `MicroWorld` |
| Application | `MicroWorld::Application` | `MicroWorld` |
| Platform/Host | `MicroWorld::Platform::Host` | `MicroWorldPlatformHost` |
| Platform/Esp32 | — (ESP-IDF only) | `MicroWorldPlatformEsp32` |
| Platform/Pico | `MicroWorld::Platform::Pico` | `MicroWorldPlatformPico` |

PlatformIO selects a library's source set through its manifest, and one manifest
cannot offer different source sets to different consumers. The portable systems
therefore share a single manifest whose `srcFilter` lists them and excludes
`Platform/`; the three platform edges keep their own manifests because a board
build must not see another board's SDK code.

CMake keeps one target per system, so link granularity survives the single
package: a consumer links `MicroWorld::Transport` without pulling Engine, and
`libArchive` plus static linking means unreferenced objects never reach the
firmware. Two options trim Transport further —
`MICROWORLD_TRANSPORT_LORA` and `MICROWORLD_TRANSPORT_IP` — so an RP2040 build
omits IP and protocol code entirely.

Dependencies point inward, and `tools/CheckDependencyBoundaries.py` fails
`ctest` on any violation:

```text
Core <- Messaging, Transport
Core + Messaging <- Engine
Core + Engine <- Application
```

Transport never sees Engine and Engine never sees Transport. Messaging and
Transport never see each other either: they meet at `Core::ITransportDevice`, so
a channel sends through the interface while each medium realises it, and the
application entry point is the only place that names a concrete device. CMake
links the named targets; local PlatformIO development uses one
`symlink://../../Modules` dependency plus one per platform edge in use.

## Verification

Package changes require an independent consumer build and a dependency/profile
map check. Exact recorded package, map, and ESP32-S3 compile facts are in the
[Core](../Modules/benchmarks/Core/Results/Esp32S3N16R8.md),
[Memory](../Modules/Memory/benchmarks/Results/Esp32S3N16R8.md), and
[Object](../Modules/benchmarks/Engine/Results/Esp32S3N16R8.md) evidence
records.

Engine behavior is defined by its headers and tests; measured margins are
indexed by [ResourceBudgets.md](ResourceBudgets.md).
