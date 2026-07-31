# 24-TwoChannelWorld

**Feature:** one world, two live channels on two different physical links at
once — one `TNetworking` owns the shared `TMessageRouter`, host bindings, and
engine-system ordering for telemetry over WiFi UDP and commands over a UART
wire, simultaneously, between the same two boards.

> Status: not yet hardware-verified after the `TNetworking` conversion.

## What it does

1. The **server** board (`esp32-s3-server`) hosts the WiFi SoftAP and runs
   `FTelemetrySinkActor`, which subscribes to the broadcast telemetry reading
   and logs `rx telemetry reading=<n>` for every one that arrives over **UDP**.
2. The server also runs `FCommanderActor`, which every 10 s sends a
   **targeted** `SetReportingRateMessageId` to the client's sensor actor over
   **UART**, alternating its reporting interval between 1000 ms and 500 ms
   (halve, then restore, then halve…).
3. The **client** board (`esp32-s3-client`) joins the SoftAP and runs
   `FSensorActor`, which every reporting interval (starting at 1000 ms)
   broadcasts a 2-byte synthetic reading (ADR 0003 — no GPIO/sensor
   peripheral) on the telemetry (**UDP**) channel, and subscribes to
   `SetReportingRateMessageId` targeted at its own id — on receipt it calls
   `SetTickInterval` so its reporting cadence visibly halves/restores.
4. Both links are alive **at the same time**: the server console interleaves
   telemetry-in lines (UDP) and command-out lines (UART).
5. Every actor reaches messaging only through `IMessageRouter&`, injected at
   construction (D9); none of them ever sees `TTransportHost`, a device, UDP, or
   UART — swapping which channel rides which transport is a composition-root
   edit only.
6. The run is **unbounded** (matching 16-TwoBoardUdp and 23-TwoBoardWire):
   this is a continuous two-board, two-channel demo, not a self-terminating
   trace.

## MicroWorld APIs used

- `TNetworking` (`AddDevice` / `AddChannel` / `GetRouter`) — configures
  both devices, derives wire channels and peer targets, and starts hosts at
  engine `BeginPlay`
- `IMessageRouter` (`AddMessageHandler` / `SendMessageToActor` /
  `BroadcastMessage`)
- `ENetworkMode` and `FTransportHostConfig` — explicit client/server session policy
- `FEsp32WifiLink` (`StartAccessPoint` / `JoinAccessPoint`), `FEsp32WifiDevice`,
  `MakeUdpAddress`
- `FEsp32UartDevice`, `FEsp32UartConfig`, `MakeUartAddress`
- `AActor::SetTickInterval` — the sensor re-times its own reporting cadence
- `TEngine` (`RegisterClass` / `CreateWorld` / `CreateObject` /
  `BeginPlay` / `Tick`), `AActor`, `UWorld::RegisterActor`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `WriteEsp32LogRecord`, `MW_LOG`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, and three jumper wires. WiFi
needs no router or credentials — the server hosts its own SoftAP.

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

WiFi needs no wiring at all: the server board hosts a SoftAP named
`microworld-ex24` at the fixed gateway `192.168.4.1`, and the client joins it
directly — no home router, no credentials to configure.

## Build

```sh
pio run -d examples/24-TwoChannelWorld
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which
differ only by `-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Flash both roles, wire the UART crossover per the table above, and read either
console (both are on the native USB port — see [`../LOGGING.md`](../LOGGING.md)):

```bat
mw flash 24 esp32-s3-server COM5     :: server hosts the SoftAP + drives commands
mw flash 24 esp32-s3-client COM7     :: client joins + reports telemetry
mw log   COM5                        :: server: rx telemetry (UDP) + tx command (UART)
mw log   COM7                        :: client: tx telemetry (UDP) + rate change (UART)
```

`mw` is [`../tools/mw.bat`](../tools/mw.bat). Do **not** use `pio device monitor`
on these boards -- its reset-on-open can drop the native-USB port into the ROM
download loader; `mw log` holds the line steady and reconnects across resets.

## Historical hardware verification

The following trace was captured on two ESP32-S3-DevKitC-1 boards on
**2026-07-24**, before this example was converted to `TNetworking`. It documents
the physical setup only; the current composition still needs a fresh hardware
run.

**Server** — UDP telemetry in and UART commands out, interleaved:

```text
I (119438) ex24: rx telemetry reading=62
I (120458) ex24: rx telemetry reading=63
I (121278) ex24: tx command -> sensor rate=500 ms
I (121298) ex24: rx telemetry reading=64
...
I (131298) ex24: tx command -> sensor rate=1000 ms
```

**Client** — same readings out over UDP, and the rate command applied from UART:

```text
I (45041) ex24: tx telemetry reading=62
I (46061) ex24: tx telemetry reading=63
I (46901) ex24: sensor reporting rate -> 500 ms
I (46901) ex24: tx telemetry reading=64
...
I (56921) ex24: sensor reporting rate -> 1000 ms
```

The reading values match end to end (client `tx telemetry reading=62` →
server `rx telemetry reading=62`), and each server `tx command -> sensor
rate=N` lands on the client as `sensor reporting rate -> N`, alternating
500/1000 ms every 10 s across both transports.

## Image size

Historical output from `pio run` before the `TNetworking` conversion (release
build, ESP32-S3-DevKitC-1). Fresh size evidence is required for the current
example:

```text
server  RAM:   18.2% (used 59696 bytes from 327680 bytes)
        Flash: 20.0% (used 840457 bytes from 4194304 bytes)
client  RAM:   18.2% (used 59696 bytes from 327680 bytes)
        Flash: 20.1% (used 841077 bytes from 4194304 bytes)
```
