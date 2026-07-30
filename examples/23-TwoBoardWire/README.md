# 23-TwoBoardWire

**Feature:** the vision demo — actor messaging over the cheapest possible link.
A client board's `FSwitchActor` drives a server board's `FLampActor` through
`TMessageChannelBinding` + `TMessageRouter` over `FEsp32UartDriver`; only the
driver construction line would change to run the same application protocol
over WiFi.

> Status: not yet verified on hardware.

## What it does

1. The **client** board (`node=2`) composes a world with one `FSwitchActor`.
   Every 2 s it toggles a boolean lamp state and sends a **targeted**
   `SetLampStateMessageId` (1-byte payload) to `LampActorId`, then increments
   a heartbeat counter and **broadcasts** it as `HeartbeatCountMessageId`.
2. The **server** board (`node=1`) composes a world with `FLampActor`
   (subscribed to `SetLampStateMessageId` targeted at its own id — logs
   `lamp ON` / `lamp OFF`) and `FDisplayActor` (subscribed to the broadcast
   `HeartbeatCountMessageId` — logs `heartbeat=<n>`).
3. Both actors reach messaging only through `IMessageRouter&`, injected at
   construction (D9); neither actor ever sees `TNetHost` or the UART driver
   directly — that boundary is `TMessageChannelBinding`.
4. The run is **unbounded** (matching 18-TwoBoardUart and 19-UartMessaging's
   server): this is a continuous two-board demo, not a self-terminating trace.

## MicroWorld APIs used

- `TMessageRouter`, `IMessageRouter` (`AddMessageHandler` / `SendMessageToActor`
  / `BroadcastMessage` / `PreAdvance` / `PostAdvance`)
- `TMessageChannelBinding`, `EChannelSendTarget` (`Server` on the client,
  `AllPeers` on the server)
- `TNetHost` (`Configure` / `Start`), `TNetHostSystem`, `ENetMode`
- `FEsp32UartDriver`, `FEsp32UartConfig`, `MakeUartAddress`
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` /
  `BeginPlay` / `Tick`), `AActor`, `UWorld::RegisterActor`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `WriteEsp32LogRecord`, `MW_LOG`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, and three jumper wires.

## Wiring

Both boards use UART1 with TX on GPIO 17 and RX on GPIO 18, wired crossover:

| Board A | Board B | Why |
| --- | --- | --- |
| GND | GND | common ground first |
| GPIO 17 (TX) | GPIO 18 (RX) | A talks to B |
| GPIO 18 (RX) | GPIO 17 (TX) | B talks to A |

Wiring safety:

- ESP32-S3 GPIO is **3.3 V logic** — never feed 5 V into a data pin.
- Always connect **GND↔GND first**; two boards without a common ground do not
  have a signal.
- Rewire only with **both boards unpowered**.

## Build

```sh
pio run -d examples/23-TwoBoardWire
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which
differ only by `-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Human-gated (see `../AGENTS.md`). Flash the server to board A
and the client to board B, then open both monitors:

```sh
pio run -d examples/23-TwoBoardWire -e esp32-s3-server -t upload --upload-port <COM-A>
pio run -d examples/23-TwoBoardWire -e esp32-s3-client -t upload --upload-port <COM-B>
pio device monitor -d examples/23-TwoBoardWire -e esp32-s3-server
pio device monitor -d examples/23-TwoBoardWire -e esp32-s3-client
```

## Expected output

Server board (not yet verified on hardware; alternates `lamp ON`/`lamp OFF`
every 2 s and the heartbeat counter climbs):

```text
I (nnnn) ex23: server node=1 open=1
I (nnnn) ex23: server listening (no WiFi -- UART only)
I (nnnn) ex23: lamp ON
I (nnnn) ex23: heartbeat=1
I (nnnn) ex23: lamp OFF
I (nnnn) ex23: heartbeat=2
I (nnnn) ex23: lamp ON
I (nnnn) ex23: heartbeat=3
```

Client board (not yet verified on hardware; one pair of `switch` lines every 2 s):

```text
I (nnnn) ex23: client node=2 open=1
I (nnnn) ex23: client connecting (no WiFi -- UART only)
I (nnnn) ex23: switch -> lamp ON
I (nnnn) ex23: switch broadcast heartbeat=1
I (nnnn) ex23: switch -> lamp OFF
I (nnnn) ex23: switch broadcast heartbeat=2
I (nnnn) ex23: switch -> lamp ON
I (nnnn) ex23: switch broadcast heartbeat=3
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). Both role environments
carry the same engine/object/GC stack (each board runs a full `TEngineHost`),
so the two images are nearly identical:

```text
server  RAM:   10.8% (used 35548 bytes from 327680 bytes)
        Flash:  5.8% (used 241501 bytes from 4194304 bytes)
client  RAM:   10.8% (used 35548 bytes from 327680 bytes)
        Flash:  5.7% (used 240329 bytes from 4194304 bytes)
```
