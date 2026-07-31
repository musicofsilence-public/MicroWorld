# 16-TwoBoardUdp

**Feature:** the full networked engine across two real boards — a dedicated-server
`TEngine` bound to `TTransportHost` through the `THostPlaySystem` interface, and a bare `TTransportHost`
client, exchanging channel messages over **WiFi UDP with no router**. The server board
hosts its own SoftAP; the client joins it. This is the host `TwoNodeDemo` split across two
boards, and the **WiFi twin of example 19** (same protocol, UART swapped for UDP).

> Status: not yet verified on hardware.

## What it does

Two roles talk directly over the server's SoftAP — **no home WiFi, no real credentials**
(the network name/password are fixed demo values), and the server's IP is always the fixed
SoftAP gateway `192.168.4.1`, so there is no IP to look up or copy:

1. The **server** (`esp32-s3-server`) hosts the SoftAP `microworld-ex16`, then composes a
   `TEngine` + `THostPlaySystem` + `TTransportHost` (`DedicatedServer`) over one
   `FEsp32WifiDevice` bound to `ServerPort`, registers a spawnable actor class, creates its
   world, and ticks forever. Each tick it broadcasts the world actor count on channel 2.
2. The **client** (`esp32-s3-client`) joins the AP and runs a bare `TTransportHost` (`Client`)
   over its own `FEsp32WifiDevice`, greeting `MakeUdpAddress(192.168.4.1, ServerPort)`. Once
   connected it sends two channel-1 spawn requests one second apart and prints every
   channel-2 broadcast.
3. Each accepted request spawns one actor, so the broadcast count climbs 0 → 1 → 2. When
   the client observes count 2 it logs `done`.

The application code above the device is identical to example 19's; only the device and the
pre-device WiFi bring-up changed. The same `TTransportHost` + `TEngine` design rides UART, a
bare wire, or WiFi UDP unchanged.

## MicroWorld APIs used

- `TTransportHost` (`Configure` / `Start` / `PumpReceive` / `PumpSend` / `SendTo` / `Broadcast` /
  `GetState` / `GetServerPeer`, message-handler multicast), `ENetworkMode`, `ETransportHostState`,
  `FTransportHostConfig`, `FPeerId`
- `THostPlaySystem` / `IPlaySystem` and the `TEngine` network-frame constructor
- `TEngine` (`RegisterClass` / `CreateWorld` / `CreateObject` / `BeginPlay` / `Tick`),
  `UWorld::SpawnActor`, `AActor`, `FGarbageCollectionBudget`
- `FEsp32WifiDevice`, `MakeUdpAddress`, `FEsp32TimeSource::Now`
- `FEsp32WifiLink` (`StartAccessPoint` / `JoinAccessPoint`), `SleepMilliseconds`,
  `MW_LOG` / `WriteEsp32LogRecord`

## Hardware required

Two ESP32-S3-DevKitC-1 boards and two USB cables. **No router, no wiring, no credentials.**

## Build

```sh
pio run -d examples/16-TwoBoardUdp
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which differ only by
`-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Human-gated (see `../AGENTS.md`). Flash each role to a board and capture
the **server** — its console shows the `spawned actor → count` proof that the remote
client's requests arrived (both consoles work; the server side needs no IP lookup):

```sh
pio run -d examples/16-TwoBoardUdp -e esp32-s3-server -t upload --upload-port <COM-A>
pio run -d examples/16-TwoBoardUdp -e esp32-s3-client -t upload --upload-port <COM-B>
pio device monitor -d examples/16-TwoBoardUdp -e esp32-s3-server
```

If the client connects but the count never climbs while the server shows no spawns, a stray
oversize datagram may have wedged the server socket (see AGENTS.md) — reboot the server.

## Expected output

Server board (not yet verified on hardware):

```text
I (nnnn) ex16: wifi softap up, gateway 192.168.4.1
I (nnnn) ex16: server open=1 udp_port=40404
I (nnnn) ex16: server listening (udp)
I (nnnn) ex16: server spawned actor -> world actor count=1
I (nnnn) ex16: server spawned actor -> world actor count=2
I (nnnn) ex16: done (server spawned 2 actors)
```

Client board (not yet verified on hardware):

```text
I (nnnn) ex16: wifi joined AP
I (nnnn) ex16: client open=1
I (nnnn) ex16: client connecting (udp)
I (nnnn) ex16: client connected
I (nnnn) ex16: client sent spawn request 1
I (nnnn) ex16: client rx state tick=<n> actors=1
I (nnnn) ex16: client sent spawn request 2
I (nnnn) ex16: client rx state tick=<n> actors=2
I (nnnn) ex16: done (observed actor count 2)
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The server carries the engine, object
store, and GC on top of the shared WiFi/lwIP stack, so its image is larger than the client:

```text
server  RAM:   14.7% (used 48144 bytes from 327680 bytes)
        Flash: 19.2% (used 804705 bytes from 4194304 bytes)
client  RAM:   13.4% (used 44032 bytes from 327680 bytes)
        Flash: 18.9% (used 794457 bytes from 4194304 bytes)
```
