# 16-TwoBoardUdp

**Feature:** the full Networking boundary across two real boards — a server
`TEngine` owning Messaging and Network, and a client Messaging + Network composition,
exchanging channel messages over **WiFi UDP with no router**. The server board
hosts its own SoftAP; the client joins it. This is the host `TwoNodeDemo` split across two
boards, and the **WiFi twin of example 19** (same protocol, UART swapped for UDP).

> Status: hardware-verified on 2026-08-05 with two ESP32-S3-DevKitC-1 boards
> (server COM5, MAC `e0:72:a1:d5:56:9c`; client COM7, MAC
> `e0:72:a1:d6:c1:68`).

## What it does

Two roles talk directly over the server's SoftAP — **no home WiFi, no real credentials**
(the network name/password are fixed demo values), and the server's IP is always the fixed
SoftAP gateway `192.168.4.1`, so there is no IP to look up or copy:

1. The **server** (`esp32-s3-server`) hosts the SoftAP `microworld-ex16`, then composes a
   `TEngine` + Messaging + server `FNetworkSystem` over one
   `FEsp32WifiDevice` bound to `ServerPort`, registers a spawnable actor class, creates its
   world, and ticks forever. Each tick it broadcasts the world actor count on channel 2.
2. The **client** (`esp32-s3-client`) joins the AP and composes Messaging + client
   `FNetworkSystem` over its own `FEsp32WifiDevice`, then connects to
   `MakeUdpAddress(192.168.4.1, ServerPort)`. Once
   connected it sends two channel-1 spawn requests one second apart and prints every
   channel-2 broadcast.
3. Each accepted request spawns one actor, so the broadcast count climbs 0 → 1 → 2. When
   the client observes count 2 it logs `done`.

The application code above the device is identical to example 19's; only the device and the
pre-device WiFi bring-up changed. The same Messaging + Networking design rides UART,
LoRa, or WiFi UDP unchanged.

## MicroWorld APIs used

- `FNetworkSystem` (`ConnectToServer` / `SendToServer` / `SendTo` / `Broadcast` /
  `ResolveSenderPeer`), `ENetworkRole`, `EConnectionState`, `FPeerId`
- `FMessagingSystem` local application channels, registered device link, and explicit route
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

## Verified output

The following milestone lines were captured with `examples/tools/mwlog.py` on
2026-08-05. Intermediate periodic client state broadcasts are omitted only
between the shown `actors=1` and `actors=2` lines.

Server capture (COM5):

```text
I (49939) wifi:station: e0:72:a1:d6:c1:68 join, AID=1, bgn, 40U
I (49969) esp_netif_lwip: DHCP server assigned IP to a client, IP is: 192.168.4.2
I (51019) ex16: server spawned actor -> world actor count=1
I (52019) ex16: server spawned actor -> world actor count=2
I (52019) ex16: done (server spawned 2 actors)
```

Client capture (COM7):

```text
I (2440) wifi:connected with microworld-ex16, aid = 1, channel 1, 40U, bssid = e0:72:a1:d5:56:9d
I (3460) esp_netif_handlers: sta ip: 192.168.4.2, mask: 255.255.255.0, gw: 192.168.4.1
I (3490) ex16: client rx state tick=208 actors=0
I (3490) ex16: client connected
I (3490) ex16: client sent spawn request 1
I (3530) ex16: client rx state tick=209 actors=1
I (4490) ex16: client sent spawn request 2
I (4510) ex16: client rx state tick=3 actors=2
I (4510) ex16: done (observed actor count 2)
```

## Image size

Measured during the verified 2026-08-05 PlatformIO upload (release build,
ESP32-S3-DevKitC-1). The server carries the engine, object store, and GC on
top of the shared WiFi/lwIP stack, so its image is larger than the client:

```text
server  RAM:   15.1% (used 49520 bytes from 327680 bytes)
        Flash: 19.4% (used 814725 bytes from 4194304 bytes)
client  RAM:   13.9% (used 45408 bytes from 327680 bytes)
        Flash: 19.0% (used 798241 bytes from 4194304 bytes)
```
