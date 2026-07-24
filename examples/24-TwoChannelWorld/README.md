# 24-TwoChannelWorld

**Feature:** one world, two live channels on two different physical links at
once — a single `TMessageRouter` behind one `TNetworkFrameSet<3>` carries
telemetry over WiFi UDP and commands over a UART wire, simultaneously, between
the same two boards. This is the first example to use `TNetworkFrameSet`
(Phase 4.1), replacing 23-TwoBoardWire's manual per-frame router pump.

> Status: not yet verified on hardware.

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
   construction (D9); none of them ever sees `TNetHost`, a driver, UDP, or
   UART — swapping which channel rides which transport is a composition-root
   edit only.
6. The run is **unbounded** (matching 16-TwoBoardUdp and 23-TwoBoardWire):
   this is a continuous two-board, two-channel demo, not a self-terminating
   trace.

## MicroWorld APIs used

- `TMessageRouter`, `IMessageRouter` (`AddMessageHandler` / `SendMessageToActor`
  / `BroadcastMessage`)
- `TMessageChannelBinding`, `EChannelSendTarget` (`Server` on the client,
  `AllPeers` on the server, per channel)
- `TNetworkFrameSet` (`Add`, D3 dispatch/flush order) — the per-frame seam
  that pumps both nets and the router behind one `TEngineHost`
- `TNetHost` (`Configure` / `Start`), `TNetHostFrame`, `ENetMode`
- `FEsp32WifiLink` (`StartAccessPoint` / `JoinAccessPoint`), `FEsp32UdpDriver`,
  `MakeUdpAddress`
- `FEsp32UartDriver`, `FEsp32UartConfig`, `MakeUartAddress`
- `AActor::SetTickInterval` — the sensor re-times its own reporting cadence
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` /
  `BeginPlay` / `Tick`), `TInlineActor`, `UWorld::RegisterActor`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `Esp32LogSink`, `MW_LOG`

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

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash the server to board A
and the client to board B, then wire the UART crossover per the table above,
and open both monitors. Capture the **server** console: it is the one that
shows both the `rx telemetry reading=` (UDP) lines and the `tx command ->
sensor rate=` (UART) lines interleaved, proving both links are alive at once.

```sh
pio run -d examples/24-TwoChannelWorld -e esp32-s3-server -t upload --upload-port <COM-A>
pio run -d examples/24-TwoChannelWorld -e esp32-s3-client -t upload --upload-port <COM-B>
pio device monitor -d examples/24-TwoChannelWorld -e esp32-s3-server
pio device monitor -d examples/24-TwoChannelWorld -e esp32-s3-client
```

## Expected output

Server board (not yet verified on hardware; telemetry lines arrive roughly
once a second, a command line every 10 s):

```text
I (nnnn) ex24: wifi softap up, gateway 192.168.4.1
I (nnnn) ex24: telemetry open=1 udp_port=40404
I (nnnn) ex24: commands node=1 open=1
I (nnnn) ex24: server up (telemetry=UDP, commands=UART)
I (nnnn) ex24: rx telemetry reading=1
I (nnnn) ex24: rx telemetry reading=2
I (nnnn) ex24: tx command -> sensor rate=500 ms
I (nnnn) ex24: rx telemetry reading=3
I (nnnn) ex24: rx telemetry reading=4
```

Client board (not yet verified on hardware; the reporting interval visibly
halves/restores every 10 s):

```text
I (nnnn) ex24: wifi joined AP
I (nnnn) ex24: telemetry open=1
I (nnnn) ex24: commands node=2 open=1
I (nnnn) ex24: client up (telemetry=UDP, commands=UART)
I (nnnn) ex24: tx telemetry reading=1
I (nnnn) ex24: tx telemetry reading=2
I (nnnn) ex24: sensor reporting rate -> 500 ms
I (nnnn) ex24: tx telemetry reading=3
I (nnnn) ex24: tx telemetry reading=4
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). Both role environments
carry the full engine/object/GC stack plus both the WiFi/lwIP and UART
transports:

```text
server  RAM:   18.2% (used 59696 bytes from 327680 bytes)
        Flash: 20.0% (used 840457 bytes from 4194304 bytes)
client  RAM:   18.2% (used 59696 bytes from 327680 bytes)
        Flash: 20.1% (used 841077 bytes from 4194304 bytes)
```
