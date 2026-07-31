# 19-UartMessaging

**Feature:** the full MicroWorld message design — a dedicated-server `TEngineHost`
bound to `TTransportHost` through the `THostPlaySystem` interface, and a bare `TTransportHost`
client — running over a plain wire with **zero WiFi**. **Same application
protocol as example 16 — only the driver construction changed.**

> Status: not yet verified on hardware.

## What it does

1. The **server** board (`node=1`) composes a `TEngineHost` + `THostPlaySystem` +
   `TTransportHost` in `DedicatedServer` mode over one `FEsp32UartDriver`, registers a
   spawnable actor class, creates its world, and ticks forever. Each tick it
   broadcasts the world actor count on channel 2.
2. The **client** board (`node=2`) runs a bare `TTransportHost` in `Client` mode over
   its own `FEsp32UartDriver`, greeting `MakeUartAddress(1)` as its server. Once
   connected it sends two channel-1 spawn requests one second apart and logs
   every channel-2 state broadcast it receives.
3. Each accepted request spawns one actor in the server world, so the broadcast
   count climbs 0 → 1 → 2. When the client observes count 2 it logs a
   done line; the server logs its own done line after spawning two actors
   and keeps running.

The application code above the driver is identical to example 16's — there is no
`WifiStation`, no `NetworkConfig`, no `esp_netif_init`. That deletion is the
demonstration.

## MicroWorld APIs used

- `TTransportHost` (`Configure` / `Start` / `PumpReceive` / `PumpSend` / `SendTo` /
  `Broadcast` / `GetState` / `GetServerPeer`, message-handler multicast),
  `ENetworkMode`, `ETransportHostState`, `FTransportHostConfig`, `FPeerId`
- `THostPlaySystem` / `IPlaySystem` and the `TEngineHost` network-frame constructor
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` / `BeginPlay` /
  `Tick` / `GetWorld`), `UWorld::SpawnActor`, `AActor`, `FGarbageCollectionBudget`
- `FEsp32UartDriver`, `FEsp32UartConfig`, `MakeUartAddress`
- `FEsp32TimeSource::Now`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, and three jumper wires.

## Wiring

Both boards use UART1 with TX on GPIO 17 and RX on GPIO 18, wired crossover
(identical to example 18):

| Server (node 1) | Client (node 2) | Why |
| --- | --- | --- |
| GND | GND | common ground first |
| GPIO 17 (TX) | GPIO 18 (RX) | server → client bytes |
| GPIO 18 (RX) | GPIO 17 (TX) | client → server bytes |

Wiring safety:

- ESP32-S3 GPIO is **3.3 V logic** — never feed 5 V into a data pin.
- Always connect **GND↔GND first**; two boards without a common ground do not
  have a signal.
- Rewire only with **both boards unpowered**.

## Build

```sh
pio run -d examples/19-UartMessaging
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which
differ only by `-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Human-gated (see `../AGENTS.md`). Flash the server to board A
and the client to board B, then open both monitors:

```sh
pio run -d examples/19-UartMessaging -e esp32-s3-server -t upload --upload-port <COM-A>
pio run -d examples/19-UartMessaging -e esp32-s3-client -t upload --upload-port <COM-B>
pio device monitor -d examples/19-UartMessaging -e esp32-s3-server
pio device monitor -d examples/19-UartMessaging -e esp32-s3-client
```

## Expected output

Server board (not yet verified on hardware):

```text
I (nnnn) ex19: server node=1 open=1
I (nnnn) ex19: server listening (no WiFi -- UART only)
I (nnnn) ex19: server spawned actor -> world actor count=1
I (nnnn) ex19: server spawned actor -> world actor count=2
I (nnnn) ex19: done (server spawned 2 actors)
```

Client board (not yet verified on hardware; the `rx state` line repeats as
broadcasts arrive; the count climbs to 2):

```text
I (nnnn) ex19: client node=2 open=1
I (nnnn) ex19: client connecting (no WiFi -- UART only)
I (nnnn) ex19: client connected
I (nnnn) ex19: client sent spawn request 1
I (nnnn) ex19: client rx state tick=<n> actors=1
I (nnnn) ex19: client sent spawn request 2
I (nnnn) ex19: client rx state tick=<n> actors=2
I (nnnn) ex19: done (observed actor count 2)
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The server carries the
engine, object store, and GC, so its image is larger than the bare client:

```text
server  RAM:   7.9% (used 25820 bytes from 327680 bytes)
        Flash: 5.6% (used 235893 bytes from 4194304 bytes)
client  RAM:   6.7% (used 21980 bytes from 327680 bytes)
        Flash: 5.4% (used 225129 bytes from 4194304 bytes)
```
