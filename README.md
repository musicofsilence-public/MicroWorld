# MicroWorld

MicroWorld is a small, embedded-suitable C++17 engine that brings familiar UE5
concepts — lifecycle, World/Actor/Component, GC, smart pointers, and a simple
`TNetManager`/`INetDriver` networking layer — to constrained microcontrollers
such as ESP32, STM32, and RP2040. It keeps only essential, bounded features so
UE5 developers can build small applications and games for these devices without
first learning every hardware detail. Platform support is verified one target at
a time, not claimed for every board.

Current development status is in [PROGRESS.md](PROGRESS.md); the active
improvement plan is in [docs/SIMPLICITY_ROADMAP.md](docs/SIMPLICITY_ROADMAP.md)
(the completed implementation plan remains in [docs/ROADMAP.md](docs/ROADMAP.md)
as a historical record). The active examples plan is in
[docs/EXAMPLES_ROADMAP.md](docs/EXAMPLES_ROADMAP.md), building one runnable
ESP32-S3 example per engine feature under [examples/](examples/README.md).

## Modules

| Module | CMake target | PlatformIO package | Role |
| --- | --- | --- | --- |
| Core | `MicroWorld::Core` | `MicroWorld` | Lifecycle, tick, application primitives |
| Memory | `MicroWorld::Memory` | `MicroWorldMemory` | Memory resource, arena, smart pointers |
| Object | `MicroWorld::Object` | `MicroWorldObject` | Object store, garbage collector |
| Engine | `MicroWorld::Engine` | `MicroWorldEngine` | `UWorld` / `AActor` / `UActorComponent`, timers |
| Net | `MicroWorld::Net` | `MicroWorldNet` | Byte I/O, frame codec, `TNetManager` |
| PlatformHost | `MicroWorld::PlatformHost` | — | Host UDP transport (non-portable) |
| PlatformEsp32 | — | `MicroWorldPlatformEsp32` | ESP32 UDP + E32 LoRa UART (PlatformIO/ESP-IDF only) |

Dependencies point inward:

```text
Core <- Memory <- Object <- Engine
Core <- Memory <- Net
```

Net is an independent overlay above Memory: it never pulls Object or Engine, so
an application can use byte I/O without the managed runtime. PlatformHost and
PlatformEsp32 are the non-portable edges that supply real transports. See
[docs/ModulePackaging.md](docs/ModulePackaging.md) for the full layout.

## Verify gate

Build and test the whole engine from the repository root:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Each module also configures standalone with the same command shape, for example
`Modules/Engine` or `Modules/PlatformHost`. The CMake `-Werror` gate and the
`tools/Check*.py` scripts enforce dependency direction, profile-map evidence,
folder-agent coverage, class-documentation contracts, and clang-format
conformance (`microworld_format_check` runs with every `ctest`).
