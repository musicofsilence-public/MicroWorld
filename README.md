# MicroWorld

MicroWorld is a small, embedded-suitable C++17 engine that brings familiar UE5
concepts — lifecycle, World/Actor/Component, GC, smart pointers, and a simple
`TTransportManager`/`IDevice` networking layer — to constrained microcontrollers
such as ESP32, STM32, and RP2040. It keeps only essential, bounded features so
UE5 developers can build small applications and games for these devices without
first learning every hardware detail. Platform support is verified one target at
a time, not claimed for every board.

The one active plan is
[docs/RADIO_TRANSPORTS_ROADMAP.md](docs/RADIO_TRANSPORTS_ROADMAP.md) (E32 LoRa
and Bluetooth LE radio transports); every other `docs/*_ROADMAP.md` is a
finished plan kept as a historical record. Current behavior is defined by the
headers and tests, hardware evidence by each example's `README.md`, and measured
margins by [docs/ResourceBudgets.md](docs/ResourceBudgets.md). The style
contract every module follows is in [docs/Style.md](docs/Style.md), and one
runnable ESP32-S3 example per engine feature lives under
[examples/](examples/README.md).

![MicroWorld implementation journey](docs/diagrams/microworld-implementation-roadmap.svg)

[Open the high-resolution PNG](docs/diagrams/microworld-implementation-roadmap.png)
or inspect the
[editable Mermaid source](docs/diagrams/microworld-implementation-roadmap.mmd).

## Modules

| System | CMake target | PlatformIO package | Role |
| --- | --- | --- | --- |
| Core | `MicroWorld::Core` | `MicroWorld` | Lifecycle, tick, containers, delegates, smart pointers, timers, `IPlaySystem` |
| Engine | `MicroWorld::Engine` | `MicroWorld` | `UWorld` / `AActor` / `UActorComponent`, `TEngine`, `IEngine`, plus the object store, garbage collector, and handles |
| Messaging | `MicroWorld::Messaging` | `MicroWorld` | Message router, channel bindings, reliable channel (header-only) |
| Transport | `MicroWorld::Transport` | `MicroWorld` | Byte I/O, frame codec, `TTransportHost`, and the optional portable E32 `FE32LoraDevice` |
| Networking | `MicroWorld::Networking` | `MicroWorld` | `TNetworking` — Messaging over Transport behind `IPlaySystem` |
| Application | `MicroWorld::Application` | `MicroWorld` | `FApplication` — owns one engine and its frame loop |
| Platform/Host | `MicroWorld::PlatformHost` | `MicroWorldPlatformHost` | Host UDP transport and `steady_clock` time source (non-portable) |
| Platform/Esp32 | — | `MicroWorldPlatformEsp32` | ESP32 UDP + wired transports, UART SDK bindings, E32 facade (PlatformIO/ESP-IDF only) |
| Platform/Pico | `MicroWorld::PlatformPico` | `MicroWorldPlatformPico` | RP2040 UART SDK binding and E32 facade (native Pico SDK only) |

One folder under `Modules/MicroWorld/` per system, one CMake target per system,
one shipped package for all six. Memory folds into Core, the object store folds
into Engine, and the E32 radio folds into Transport behind the
`MICROWORLD_TRANSPORT_LORA` option. Dependencies point inward:

```text
Core <- Engine <- Application
Core <- Messaging
Core <- Transport
Core, Messaging, Transport <- Networking
```

Transport is an independent overlay above Core: it never pulls Engine, so an
application can use byte I/O without the managed runtime, and the application entry
point is the only place permitted to see both. PlatformHost, PlatformEsp32, and PlatformPico are the
non-portable edges that supply real transports. `IUartByteStream` is a narrow
byte-transfer interface, not a universal HAL: RadioE32 owns portable E32 state and framing,
while ESP32 and Pico facades own UART SDK lifetime. See
[docs/ModulePackaging.md](docs/ModulePackaging.md) for the full layout.

## Verify gate

Build and test the whole engine from the repository root:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Each module also configures standalone with the same command shape, for example
`Modules/MicroWorld/Engine` or `Modules/MicroWorld/Platform/Host`. The CMake `-Werror` gate and the
`tools/Check*.py` scripts enforce dependency direction, profile-map evidence,
folder-agent coverage, class-documentation contracts, and clang-format
conformance (`microworld_format_check` runs with every `ctest`).
