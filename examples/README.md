# MicroWorld ESP32 Examples

One small standalone project per engine feature, each runnable on a real
ESP32-S3 and readable in one sitting. The active plan and task tracker is
[`docs/EXAMPLES_ROADMAP.md`](../docs/EXAMPLES_ROADMAP.md); this file is the living
catalog students read to find an example. Examples climb the dependency ladder
(`Core → Memory → Object → Engine → Net`) and end with two boards talking over
WiFi UDP, E32 LoRa, and plain wires.

## Hardware shopping list

- 2 × ESP32-S3-DevKitC-1 (ESP32-S3-WROOM-1-N16R8), USB serial.
- No router needed for examples 15–16: one board hosts a SoftAP the other joins.
- 2 × E32 LoRa UART modules (example 17 only).
- Jumper wires for the wired board-to-board examples (18–21); example 20 (I2C)
  also wants two ~4.7 kΩ pull-up resistors.

## Build and flash

Every example is a standalone PlatformIO project. The quickest path is the
[`tools/mw.bat`](tools/mw.bat) helper — one dispatcher for every example:

```bat
mw build 25                       :: compile all role envs of example 25
mw flash 25 esp32-s3-server COM5  :: build & flash one env to a COM port
mw log   COM5                     :: watch MW_LOG on that port (Ctrl-C to stop)
```

Or drive PlatformIO directly from the repository root:

```sh
pio run -d examples/<NN-Name>                                        # compile
pio run -d examples/<NN-Name> -e <env> -t upload --upload-port <COM> # flash one role
```

To read logs use `mw log <COM>` (or `python tools/mwlog.py <COM>`), **not**
`pio device monitor`: on these native-USB boards a monitor's reset-on-open can
drop the port into the ROM download loader. [`LOGGING.md`](LOGGING.md) explains how
the console is routed and how to write your own `MW_LOG` lines.

`pio run` ending with `[SUCCESS]` is a compile-only proof. Flashing a board is
human-gated (`docs/EXAMPLES_ROADMAP.md` §1.2); until an example's real trace is
captured, its README says so.

## Catalog

Status: ⬜ planned · 🟨 built (compiles) · ✅ hardware-verified

| # | Example | The one feature | Extra hardware | Status |
| --- | --- | --- | --- | --- |
| 01 | `01-CoreTick` | `FTickFunction` cadence from caller-supplied real time | — | 🟨 |
| 02 | `02-CoreLifecycle` | forward-only lifecycle: `FApplication` + `FLifecycleGuard` | — | ⬜ |
| 03 | `03-CoreLog` | `FOutputDeviceFunction` seam: `MW_LOG` through `WriteEsp32LogRecord` | — | ⬜ |
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
| 15 | `15-UdpEcho` | `FEsp32UdpDriver` over WiFi, board-to-board (SoftAP, no router) | 2nd board | ✅ |
| 16 | `16-TwoBoardUdp` | `TNetHost` client/server + `TNetHostSystem` over WiFi (SoftAP, no router) | 2nd board | ✅ |
| 17 | `17-TwoBoardLora` | `FEsp32E32LoraDriver` framed radio link | 2nd board, 2 × E32 | ✅ |
| 18 | `18-TwoBoardUart` | wired UART `INetDriver` link — example 17's volley over a plain wire | 2nd board, 3 wires | 🟨 |
| 19 | `19-UartMessaging` | full `TNetHost` client/server message design over UART, zero WiFi — example 16's protocol, only the driver changed | 2nd board, 3 wires | 🟨 |
| 20 | `20-TwoBoardI2c` | wired I2C master/slave `INetDriver` link — example 18's volley over a clocked bus | 2nd board, 3 wires + 2 pull-ups | 🟨 |
| 21 | `21-TwoBoardSpi` | wired SPI master/slave `INetDriver` link — example 20's volley over a clocked full-duplex bus | 2nd board, 5 wires | 🟨 |
| 22 | `22-ActorMessages` | local actor messaging: `TMessageRouter` broadcast + targeted send, one board | — | 🟨 |
| 23 | `23-TwoBoardWire` | actor messaging over a UART wire: `TMessageChannelBinding` client/server, switch drives lamp | 2nd board, 3 wires | 🟨 |
| 24 | `24-TwoChannelWorld` | two channels, one world: telemetry over WiFi UDP + commands over a UART wire on one `TMessageRouter` via `TEngineSystemSet<3>` | 2nd board, 3 wires + WiFi | ✅ |
| 25 | `25-GuaranteedDelivery` | best-effort vs guaranteed delivery on one WiFi-UDP link: `TReliableChannel` recovers packets `FPacketDropDriver` drops | 2nd board + WiFi | ✅ |
| 26 | `26-MessagingOverLora` | full `TNetHost` client/server message design over E32 LoRa — example 19's protocol at the D8 airtime profile (heartbeat 3 s, state broadcast paced 1 s) | 2nd board, 2 × E32 | ✅ |

Wired board-to-board transports (examples 18–21) are planned in
[`docs/WIRED_TRANSPORTS_ROADMAP.md`](../docs/WIRED_TRANSPORTS_ROADMAP.md) and are
appended to this catalog as they are built.
