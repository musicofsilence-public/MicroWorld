# MicroWorld

MicroWorld is a small, embedded-suitable C++17 engine that brings familiar UE5
concepts — lifecycle, World/Actor/Component, GC, smart pointers, and a simple
`TNetManager`/`INetDriver` networking layer — to constrained microcontrollers
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

| Module | CMake target | PlatformIO package | Role |
| --- | --- | --- | --- |
| Core | `MicroWorld::Core` | `MicroWorld` | Lifecycle, tick, containers, delegates, smart pointers, timers, `IPlaySystem` |
| Object | `MicroWorld::Object` | `MicroWorldObject` | Object store, garbage collector, handles |
| Engine | `MicroWorld::Engine` | `MicroWorldEngine` | `UWorld` / `AActor` / `UActorComponent`, `TEngine`, `IEngine` |
| Messaging | `MicroWorld::Messaging` | `MicroWorldMessaging` | Message router, channel bindings (header-only) |
| Net | `MicroWorld::Net` | `MicroWorldNet` | Byte I/O, frame codec, `TNetHost` |
| RadioE32 | `MicroWorld::RadioE32` | `MicroWorldRadioE32` | Optional portable E32 framing and `FRadioE32Driver` over Core's `IUartByteStream` interface |
| Application | `MicroWorld::Application` | `MicroWorldApplication` | `FApplication` — owns one engine and its frame loop |
| Integration | `MicroWorld::Integration` | `MicroWorldIntegration` | `TNetSystem` — the only Engine + Net joiner |
| PlatformHost | `MicroWorld::PlatformHost` | — | Host UDP transport (non-portable) |
| PlatformEsp32 | — | `MicroWorldPlatformEsp32` | ESP32 UDP + wired transports, UART SDK bindings, and optional E32 facade (PlatformIO/ESP-IDF only) |
| PlatformPico | `MicroWorld::PlatformPico` | — | RP2040 UART SDK binding and optional E32 facade (native Pico SDK only) |

Memory is folded into Core; no Memory package remains. Dependencies point
inward:

```text
Core <- Object <- Engine <- Application
Core <- Messaging
Core <- Net
Core <- Net <- RadioE32
Core, Object, Messaging, Engine, Net <- Integration
```

Net is an independent overlay above Core: it never pulls Object or Engine, so an
application can use byte I/O without the managed runtime, and Integration is the
only package permitted to see both. PlatformHost, PlatformEsp32, and PlatformPico are the
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
`Modules/Engine` or `Modules/PlatformHost`. The CMake `-Werror` gate and the
`tools/Check*.py` scripts enforce dependency direction, profile-map evidence,
folder-agent coverage, class-documentation contracts, and clang-format
conformance (`microworld_format_check` runs with every `ctest`).
