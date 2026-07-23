# 16-TwoBoardUdp

**Feature:** the full networked engine across two real boards — a dedicated-server
`TEngineHost` bound to `TNetHost` through the `TNetHostFrame` seam, and a bare `TNetHost`
client, exchanging channel messages over **WiFi UDP**. This is the host `TwoNodeDemo`
split across two physical boards, and the **WiFi twin of example 19** (same protocol, UART
swapped for UDP).

> Status: compiled for ESP32-S3; not yet verified on hardware.

## What it does

1. The **server** board composes a `TEngineHost` + `TNetHostFrame` + `TNetHost`
   (`DedicatedServer`) over one `FEsp32UdpDriver` bound to `kServerPort`, registers a
   spawnable actor class, creates its world, and ticks forever. Each tick it broadcasts the
   world actor count on channel 2. It joins WiFi first and prints its `wifi ip` — the
   address you copy into the client.
2. The **client** board runs a bare `TNetHost` (`Client`) over its own `FEsp32UdpDriver`
   (ephemeral local port), greeting `MakeUdpAddress(kServerIpv4…, kServerPort)`. Once
   connected it sends two channel-1 spawn requests one second apart and prints every
   channel-2 state broadcast.
3. Each accepted request spawns one actor in the server world, so the broadcast count
   climbs 0 → 1 → 2. When the client observes count 2 it prints `[ex16] done`.

The application code above the driver is identical to example 19's; only the driver
construction and the server-address form changed. That is the demonstration: the same
`TNetHost` + `TEngineHost` design rides UART, a bare wire, or WiFi UDP unchanged.

## MicroWorld APIs used

- `TNetHost` (`Configure` / `Start` / `PumpReceive` / `PumpSend` / `SendTo` / `Broadcast` /
  `GetState` / `GetServerPeer`, message-handler multicast), `ENetMode`, `ENetHostState`,
  `FNetHostConfig`, `FPeerId`
- `TNetHostFrame` / `INetworkFrame` and the `TEngineHost` network-frame constructor
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` / `BeginPlay` / `Tick`),
  `UWorld::SpawnActor`, `AActor`, `FGarbageCollectionBudget`
- `FEsp32UdpDriver`, `MakeUdpAddress`, `FEsp32TimeSource::Now`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, and a shared 2.4 GHz WiFi network. No wiring.

## Configuration (before building)

Copy the template to the git-ignored real config on each board's build:

```sh
cp examples/16-TwoBoardUdp/src/NetworkConfig.example.h examples/16-TwoBoardUdp/src/NetworkConfig.h
```

Fill in `kWifiSsid` / `kWifiPassword`. `kServerIpv4` is used by the **client** build only —
set it to the server board's IP (read from the server's `[ex16] wifi ip=…` boot line).

## Build

```sh
pio run -d examples/16-TwoBoardUdp
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which differ only by
`-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash the server first, read its IP into
the client's `NetworkConfig.h`, then flash the client:

```sh
pio run -d examples/16-TwoBoardUdp -e esp32-s3-server -t upload --upload-port <COM-A>
pio device monitor -d examples/16-TwoBoardUdp -e esp32-s3-server   # read [ex16] wifi ip=...
# put that ip in src/NetworkConfig.h kServerIpv4, then:
pio run -d examples/16-TwoBoardUdp -e esp32-s3-client -t upload --upload-port <COM-B>
pio device monitor -d examples/16-TwoBoardUdp -e esp32-s3-client
```

If the client connects but its `rx state` lines never progress while the server shows no
activity, a stray oversize datagram may have wedged the server socket — reboot the server
board and re-observe. (Do not point example 15's oversize probe at this server.)

## Expected output

Server board:

```text
[ex16] wifi ip=<a.b.c.d>
[ex16] server open=1 udp_port=40404
[ex16] server listening (udp)
[ex16] server spawned actor -> world actor count=1
[ex16] server spawned actor -> world actor count=2
[ex16] done (server spawned 2 actors)
```

Client board:

```text
[ex16] wifi ip=<a.b.c.d>
[ex16] client open=1
[ex16] client connecting (udp)
[ex16] client connected
[ex16] client sent spawn request 1
[ex16] client rx state tick=<n> actors=1
[ex16] client sent spawn request 2
[ex16] client rx state tick=<n> actors=2
[ex16] done (observed actor count 2)
```

## Verified output

Status: compiled for ESP32-S3; not yet verified on hardware.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The server carries the engine, object
store, and GC on top of the shared WiFi/lwIP stack, so its image is larger than the bare
client:

```text
server  RAM:   14.7% (used 48144 bytes from 327680 bytes)
        Flash: 19.2% (used 805569 bytes from 4194304 bytes)
client  RAM:   13.4% (used 44032 bytes from 327680 bytes)
        Flash: 18.9% (used 794445 bytes from 4194304 bytes)
```
