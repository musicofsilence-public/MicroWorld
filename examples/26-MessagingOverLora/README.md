# 26-MessagingOverLora

**Feature:** the full MicroWorld message design — a dedicated-server `TEngineHost`
bound to `TNetHost` through the `TNetHostSystem` interface, and a bare `TNetHost`
client — running over an E32 LoRa radio. **Same application protocol as
example 19 — only the driver construction and the D8 session profile differ.**

`TNetHost` already advances queued driver transmission after outbound FIFO
progress, so this example has no direct `AdvanceTransmit` call.

> Status: hardware-verified 2026-07-24 (EBYTE E32-433T20D, 433 MHz).

> Post-refactor status (2026-07-28): this example was not rerun because the
> second ESP32 has no E32 LoRa module. The owner accepted that unavailable
> rerun as a waiver, not a pass. Current cross-platform RadioE32 evidence is
> recorded in [example 17's payload-boundary regression](../17-TwoBoardLora/README.md#payload-boundary-regression-hardware-verified-2026-07-28);
> the verified output below remains historical pre-refactor evidence.

## What it does

1. The **server** board (`node=1`) composes a `TEngineHost` + `TNetHostSystem` +
   `TNetHost` in `DedicatedServer` mode over one `FEsp32E32LoraDriver`,
   registers a spawnable actor class, creates its world, and ticks forever.
   It broadcasts the world actor count on channel 2 **every 1 s** (not every
   tick — see below).
2. The **client** board (`node=2`) runs a bare `TNetHost` in `Client` mode over
   its own `FEsp32E32LoraDriver`, greeting `MakeLoraAddress(1)` as its server.
   Once connected it sends two channel-1 spawn requests one second apart and
   logs every channel-2 state broadcast it receives.
3. Each accepted request spawns one actor in the server world, so the broadcast
   count climbs 0 → 1 → 2. When the client observes count 2 it logs a
   done line; the server logs its own done line after spawning two actors
   and keeps running.

The application code above the driver is identical to example 19's — there is
no `WifiStation`, no `NetworkConfig`, no `esp_netif_init`. Only the driver
construction line and the session profile's timing constants change.

## Airtime and the relaxed profile

A full E32 frame at 9600 baud costs hundreds of milliseconds of airtime, so
this example uses the D8 LoRa session profile — heartbeat 3000 ms, peer
timeout 15000 ms — and paces the server's state broadcast at 1 s instead of
every tick. The wired example's 1000 ms / 5000 ms heartbeat and per-tick
broadcast would congest the channel and time peers out over the air.

## MicroWorld APIs used

- `TNetHost` (`Configure` / `Start` / `PumpReceive` / `PumpSend` / `SendTo` /
  `Broadcast` / `GetState` / `GetServerPeer`, message-handler multicast),
  `ENetMode`, `ENetHostState`, `FNetHostConfig`, `FPeerId`
- `TNetHostSystem` / `IPlaySystem` and the `TEngineHost` network-frame constructor
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` / `BeginPlay` /
  `Tick` / `GetWorld`), `UWorld::SpawnActor`, `AActor`, `FGarbageCollectionBudget`
- `FEsp32E32LoraDriver`, `FEsp32E32LoraConfig`, `MakeLoraAddress`
- `FEsp32TimeSource::Now`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, two E32 LoRa modules **each
with its antenna attached**, and jumper wires per board.

## Wiring

Both boards wire their E32 module identically to UART1 (TX on GPIO 17, RX on
GPIO 18); the two E32 modules then talk to each other over the air, so there
is no wiring between the boards themselves:

| ESP32-S3 | E32 module | Why |
| --- | --- | --- |
| GND | GND | common ground — connect first |
| 3V3 | VCC | module power (3.3 V) |
| GPIO 17 (TX) | RXD | ESP sends to the module |
| GPIO 18 (RX) | TXD | module sends to the ESP |
| GND | M0 | tie low → transparent mode (D7) |
| GND | M1 | tie low → transparent mode (D7) |
| — | AUX | leave unconnected |

M0 = M1 = GND selects the E32's transparent mode; AUX is unused in this
example. Connect the common ground before any other pin, and rewire only with
both boards unpowered — ESP32-S3 GPIO is 3.3 V logic.

## Safety

Never power an E32 module without its antenna attached — transmitting into no
load can damage the RF stage. Keep the two antennas ≥ 0.5 m apart on the
bench.

Bench tests run at the module's factory default power on its factory default
channel; regional regulations are the operator's responsibility.

## Build

```sh
pio run -d examples/26-MessagingOverLora
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which
differ only by `-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

The console is on the native USB port, so the port you flash is the port you
read (see [`../LOGGING.md`](../LOGGING.md)). Flash the server first:

```bat
mw flash 26 esp32-s3-server COM5     :: server
mw flash 26 esp32-s3-client COM7     :: client
mw log   COM5                        :: server trace (Ctrl-C to stop)
mw log   COM7                        :: client trace (second terminal)
```

`mw` is [`../tools/mw.bat`](../tools/mw.bat). Do **not** use `pio device monitor`
on these boards -- its reset-on-open can drop the native-USB port into the ROM
download loader; `mw log` holds the line steady and reconnects across resets.

## Verified output

Captured 2026-07-24 on two ESP32-S3-DevKitC-1 boards, each with an EBYTE
E32-433T20D (433 MHz, FCC ID 2ALPH-E32) in transparent mode, antennas
attached — the server on COM5, the client on COM7.

Server board (`node=1`):

```text
I (575) ex26: server node=1 open=1
I (575) ex26: server listening (no WiFi -- LoRa radio only)
I (5615) ex26: server spawned actor -> world actor count=1
I (6615) ex26: server spawned actor -> world actor count=2
I (6615) ex26: done (server spawned 2 actors)
```

Client board (`node=2`): it connects, sends its two spawn requests, then prints
the channel-2 state broadcast every second as it arrives (the actor count climbs
0 → 1 → 2). The broadcasts keep arriving indefinitely, which shows the session's
heartbeats holding the link open far past the 15 s peer timeout — tick 104 below
is ~100 s of uptime, still connected:

```text
I (537) ex26: client node=2 open=1
I (537) ex26: client connecting (no WiFi -- LoRa radio only)
I (1197) ex26: client connected
I (1197) ex26: client sent spawn request 1
I (1837) ex26: client rx state tick=6 actors=0
I (2197) ex26: client sent spawn request 2
I (2837) ex26: client rx state tick=7 actors=1
I (3837) ex26: client rx state tick=8 actors=2
I (3837) ex26: done (observed actor count 2)
I (4837) ex26: client rx state tick=9 actors=2
...
I (99837) ex26: client rx state tick=104 actors=2
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The server carries the
engine, object store, and GC, so its image is larger than the bare client:

```text
server  RAM:   7.7% (used 25084 bytes from 327680 bytes)
        Flash: 5.5% (used 232341 bytes from 4194304 bytes)
client  RAM:   6.5% (used 21388 bytes from 327680 bytes)
        Flash: 5.3% (used 221493 bytes from 4194304 bytes)
```
