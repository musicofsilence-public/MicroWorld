# MicroWorld ESP32 Examples

One small standalone project per engine feature, each runnable on a real
ESP32-S3 and readable in one sitting. The active plan and task tracker is
[`docs/EXAMPLES_ROADMAP.md`](../docs/EXAMPLES_ROADMAP.md); this file is the living
catalog students read to find an example. Examples climb the dependency ladder
(`Core → Memory → Object → Engine → Net`) and end with two boards talking over
WiFi UDP, E32 LoRa, and plain wires.

## Hardware shopping list

- 2 × ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1-N16R8), USB serial.
- A shared 2.4 GHz WiFi network (examples 15–16).
- 2 × E32 LoRa UART modules (example 17 only).
- Jumper wires for the wired board-to-board examples (18–21); example 20 (I2C)
  also wants two ~4.7 kΩ pull-up resistors.

## Build and flash

Every example is a standalone PlatformIO project. From the repository root:

```sh
# Compile (downloads the Espressif toolchain on first run — several minutes):
pio run -d examples/<NN-Name>

# Flash and watch the serial trace (human-gated — needs a connected board):
pio run -d examples/<NN-Name> -t upload --upload-port <COM-port>
pio device monitor -d examples/<NN-Name>
```

`pio run` ending with `[SUCCESS]` is a compile-only proof. A board is flashed
only under the human-gated hardware checkpoint (`docs/EXAMPLES_ROADMAP.md` §1.2);
until an example's real trace is captured, its README says so.

## Catalog

Status: ⬜ planned · 🟨 built (compiles) · ✅ hardware-verified

| # | Example | The one feature | Extra hardware | Status |
| --- | --- | --- | --- | --- |
| 01 | `01-CoreTick` | `FTickFunction` cadence from caller-supplied real time | — | 🟨 |
| 02 | `02-CoreLifecycle` | forward-only lifecycle: `FApplication` + `FLifecycleGuard` | — | ⬜ |
| 03 | `03-CoreLog` | `FLogSink` seam: `MW_LOG` through `Esp32LogSink` | — | ⬜ |
| 04 | `04-MemoryArena` | `TFixedArena` / `IMemoryResource` explicit allocation | — | ⬜ |
| 05 | `05-MemorySmartPointers` | `TUniquePtr` / `TSharedPtr` / `TWeakPtr` ownership | — | ⬜ |
| 06 | `06-MemoryContainers` | `TStaticVector` + `TSpan` bounded storage and views | — | ⬜ |
| 07 | `07-MemoryDelegates` | `TDelegate` + `TMulticastDelegate` fixed dispatch | — | ⬜ |
| 08 | `08-ObjectStore` | managed identity: store, handles, object pointers | — | ⬜ |
| 09 | `09-ObjectGarbageCollector` | rooted tracing + budgeted incremental collection | — | ⬜ |
| 10 | `10-EngineWorld` | `UWorld` / `AActor` / `UActorComponent` via `TEngineHost` | — | ⬜ |
| 11 | `11-EngineTimers` | `TTimerManager` one-shot / looping / cancel | — | ⬜ |
| 12 | `12-NetBytes` | `FByteWriter` / `FByteReader` transactional byte I/O | — | ⬜ |
| 13 | `13-NetLoopback` | `TNetManager` FIFO over `THostLoopback` | — | ⬜ |
| 14 | `14-NetFrameCodec` | `EncodeFrame` / `TFrameDecoder` CRC framing + resync | — | ⬜ |
| 15 | `15-UdpEcho` | `FEsp32UdpDriver` over real WiFi | PC on same WiFi | 🟨 |
| 16 | `16-TwoBoardUdp` | `TNetHost` client/server + `TNetHostFrame` engine binding | 2nd board, WiFi | ⬜ |
| 17 | `17-TwoBoardLora` | `FEsp32E32LoraDriver` framed radio link | 2nd board, 2 × E32 | ⬜ |
| 18 | `18-TwoBoardUart` | wired UART `INetDriver` link — example 17's volley over a plain wire | 2nd board, 3 wires | 🟨 |
| 19 | `19-UartMessaging` | full `TNetHost` client/server message design over UART, zero WiFi — example 16's protocol, only the driver changed | 2nd board, 3 wires | 🟨 |
| 20 | `20-TwoBoardI2c` | wired I2C master/slave `INetDriver` link — example 18's volley over a clocked bus | 2nd board, 3 wires + 2 pull-ups | 🟨 |
| 21 | `21-TwoBoardSpi` | wired SPI master/slave `INetDriver` link — example 20's volley over a clocked full-duplex bus | 2nd board, 5 wires | 🟨 |

Wired board-to-board transports (examples 18–21) are planned in
[`docs/WIRED_TRANSPORTS_ROADMAP.md`](../docs/WIRED_TRANSPORTS_ROADMAP.md) and are
appended to this catalog as they are built.
