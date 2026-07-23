# 16-TwoBoardUdp

**Feature:** the full networked engine across two real boards — a dedicated-server
`TEngineHost` bound to `TNetHost` through the `TNetHostFrame` seam, and a bare `TNetHost`
client, exchanging channel messages over **WiFi UDP with no router**. The server board
hosts its own SoftAP; the client joins it. This is the host `TwoNodeDemo` split across two
boards, and the **WiFi twin of example 19** (same protocol, UART swapped for UDP).

> Status: hardware-verified on two ESP32-S3 boards (2026-07-23) — the client connected over
> the server's SoftAP and drove the server's world actor count to 2, no router.

## What it does

Two roles talk directly over the server's SoftAP — **no home WiFi, no real credentials**
(the network name/password are fixed demo values), and the server's IP is always the fixed
SoftAP gateway `192.168.4.1`, so there is no IP to look up or copy:

1. The **server** (`esp32-s3-server`) hosts the SoftAP `microworld-ex16`, then composes a
   `TEngineHost` + `TNetHostFrame` + `TNetHost` (`DedicatedServer`) over one
   `FEsp32UdpDriver` bound to `ServerPort`, registers a spawnable actor class, creates its
   world, and ticks forever. Each tick it broadcasts the world actor count on channel 2.
2. The **client** (`esp32-s3-client`) joins the AP and runs a bare `TNetHost` (`Client`)
   over its own `FEsp32UdpDriver`, greeting `MakeUdpAddress(192.168.4.1, ServerPort)`. Once
   connected it sends two channel-1 spawn requests one second apart and prints every
   channel-2 broadcast.
3. Each accepted request spawns one actor, so the broadcast count climbs 0 → 1 → 2. When
   the client observes count 2 it prints `[ex16] done`.

The application code above the driver is identical to example 19's; only the driver and the
pre-driver WiFi bring-up changed. The same `TNetHost` + `TEngineHost` design rides UART, a
bare wire, or WiFi UDP unchanged.

## MicroWorld APIs used

- `TNetHost` (`Configure` / `Start` / `PumpReceive` / `PumpSend` / `SendTo` / `Broadcast` /
  `GetState` / `GetServerPeer`, message-handler multicast), `ENetMode`, `ENetHostState`,
  `FNetHostConfig`, `FPeerId`
- `TNetHostFrame` / `INetworkFrame` and the `TEngineHost` network-frame constructor
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` / `BeginPlay` / `Tick`),
  `UWorld::SpawnActor`, `AActor`, `FGarbageCollectionBudget`
- `FEsp32UdpDriver`, `MakeUdpAddress`, `FEsp32TimeSource::Now`

## Hardware required

Two ESP32-S3-DevKitC-1 boards and two USB cables. **No router, no wiring, no credentials.**

## Build

```sh
pio run -d examples/16-TwoBoardUdp
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which differ only by
`-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash each role to a board and capture
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

Server board:

```text
[ex16] wifi ip=192.168.4.1
[ex16] server open=1 udp_port=40404
[ex16] server listening (udp)
[ex16] server spawned actor -> world actor count=1
[ex16] server spawned actor -> world actor count=2
[ex16] done (server spawned 2 actors)
```

Client board:

```text
[ex16] wifi ip=192.168.4.2
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

Hardware-verified 2026-07-23 on two ESP32-S3 boards, no router. The server board (SoftAP
host) console — the client joined its AP and drove the whole exchange over WiFi UDP:

```text
[ex16] wifi ip=192.168.4.1
[ex16] server open=1 udp_port=40404
[ex16] server listening (udp)
I (29538) wifi:station: e0:72:a1:d5:56:9c join, AID=1, bgn, 40U
I (29608) esp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.4.2
[ex16] server spawned actor -> world actor count=1
[ex16] server spawned actor -> world actor count=2
[ex16] done (server spawned 2 actors)
```

The `station join` + DHCP lease is the client associating with the board-hosted SoftAP; the
two `spawned actor` lines are the client's channel-1 spawn requests — carried over `TNetHost`
Hello/Welcome admission and WiFi UDP — decoded server-side and spawning actors in the engine
world. The count reaching 2 proves the entire `TNetHost` + `TEngineHost` message design ran
across two boards over WiFi with zero router, exactly as example 19 does over a bare UART.

Only the server board's console was captured (the client board was on its native-USB port,
whose UART0 console is not routed to that connector); the server-side spawns are the stronger
proof anyway — they only occur when the remote client's requests actually arrive.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The server carries the engine, object
store, and GC on top of the shared WiFi/lwIP stack, so its image is larger than the client:

```text
server  RAM:   14.7% (used 48144 bytes from 327680 bytes)
        Flash: 19.2% (used 804705 bytes from 4194304 bytes)
client  RAM:   13.4% (used 44032 bytes from 327680 bytes)
        Flash: 18.9% (used 794457 bytes from 4194304 bytes)
```
