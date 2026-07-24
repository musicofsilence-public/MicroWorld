# 25-GuaranteedDelivery

**Feature:** best-effort vs guaranteed delivery on ONE WiFi-UDP link, with the
client injecting deterministic loss via `FPacketDropDriver`; the guaranteed
channel (`TReliableChannel`) recovers every dropped packet.

> Status: not yet verified on hardware.

## What it does

1. The **server** board (`esp32-s3-server`) hosts the WiFi SoftAP and runs
   `FLedgerActor`, which registers two handlers -- one per channel -- and logs
   one column per channel: `rx best-effort n=<n>` and `rx guaranteed n=<n>`.
2. The **client** board (`esp32-s3-client`) joins the SoftAP, wraps its UDP
   driver in `FPacketDropDriver{DropEveryNthSend = 3}`, and runs
   `FCounterActor`, which every 500 ms sends the next value in 1..30 to the
   server's `FLedgerActor` on **both** channels: `BestEffortChannelId` (a
   plain `TMessageChannelBinding`) and `GuaranteedChannelId` (the same kind of
   binding, wrapped in `TReliableChannel`).
3. Every third packet the client sends -- of any kind, data or ack or
   heartbeat -- is silently dropped at the driver seam. As a result the
   server's best-effort column has gaps, while the guaranteed column is
   complete: `TReliableChannel` resends any unacknowledged value until the
   server acknowledges it.
4. Every actor reaches messaging only through `IMessageRouter&`, injected at
   construction (D9); neither actor ever sees `TNetHost`, a driver, UDP, or
   the drop injector.
5. The run is **unbounded** (matching 16-TwoBoardUdp and 24-TwoChannelWorld):
   this is a continuous two-board demo, not a self-terminating trace.

The best-effort column drops roughly every third value and never recovers it;
the guaranteed column shows all 30 because `TReliableChannel` retries each
unacked value until the server acknowledges it. Which exact values go missing
depends on how the dropped-every-third counter interleaves all outgoing
packets (data, acks, heartbeats) through the one shared driver, so treat the
gap positions as illustrative, not fixed.

## MicroWorld APIs used

- `TReliableChannel` (`SetInnerChannel`, `PendingCount` / `ResentCount` /
  `DuplicateDroppedCount`)
- `FPacketDropDriver` -- the client's deterministic loss injector, wrapping
  the real UDP driver
- `TMessageChannelBinding`, `EChannelSendTarget` (`Server` on the client,
  `AllPeers` on the server, per channel)
- `TMessageRouter`, `IMessageRouter` (`AddMessageHandler` /
  `SendMessageToActor`)
- `TNetworkFrameSet` (`Add`, D3 dispatch/flush order over the net frame, the
  reliable channel, and the router)
- `TNetHost` (`Configure` / `Start`), `TNetHostFrame`, `ENetMode`
- `FEsp32WifiLink` (`StartAccessPoint` / `JoinAccessPoint`), `FEsp32UdpDriver`,
  `MakeUdpAddress`
- `TEngineHost` (`RegisterClass` / `CreateWorld` / `CreateObject` /
  `BeginPlay` / `Tick`), `TInlineActor`, `UWorld::RegisterActor`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `Esp32LogSink`, `MW_LOG`

## Hardware required

Two ESP32-S3-DevKitC-1 boards and two USB cables. **WiFi only -- no wires**:
there is no UART in this example, so there is nothing to wire between the
boards.

## Build

```sh
pio run -d examples/25-GuaranteedDelivery
```

Builds both role environments (`esp32-s3-server`, `esp32-s3-client`), which
differ only by `-DMICROWORLD_EXAMPLE_SERVER`.

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash the server to one
board and the client to the other, then open both monitors. Capture the
**server** console: it shows the two columns side by side, with the
best-effort column visibly missing values the guaranteed column still shows.
The **client** console shows the `tx n=` lines and occasional `guaranteed
resent=`/`pending=` lines as the reliable channel retries. Per the two-board
rig, put the role you want to read on the CH343-USB board (it is the one with
a visible console/DTR reset).

```sh
pio run -d examples/25-GuaranteedDelivery -e esp32-s3-server -t upload --upload-port <COM-A>
pio run -d examples/25-GuaranteedDelivery -e esp32-s3-client -t upload --upload-port <COM-B>
pio device monitor -d examples/25-GuaranteedDelivery -e esp32-s3-server
pio device monitor -d examples/25-GuaranteedDelivery -e esp32-s3-client
```

## Expected output

Server board (not yet verified on hardware; illustrative gap positions only):

```text
I (nnnn) ex25: wifi softap up, gateway 192.168.4.1
I (nnnn) ex25: udp open=1 udp_port=40404
I (nnnn) ex25: server up (best-effort + guaranteed over one UDP link)
I (nnnn) ex25: rx best-effort n=1
I (nnnn) ex25: rx guaranteed n=1
I (nnnn) ex25: rx best-effort n=2
I (nnnn) ex25: rx guaranteed n=2
I (nnnn) ex25: rx guaranteed n=3
I (nnnn) ex25: guaranteed dedup dropped=1
I (nnnn) ex25: rx best-effort n=4
I (nnnn) ex25: rx guaranteed n=4
```

Client board (not yet verified on hardware; illustrative gap positions only):

```text
I (nnnn) ex25: wifi joined AP
I (nnnn) ex25: udp open=1
I (nnnn) ex25: client up (best-effort + guaranteed over one UDP link, dropping every 3-th send)
I (nnnn) ex25: tx n=1 (best-effort + guaranteed)
I (nnnn) ex25: tx n=2 (best-effort + guaranteed)
I (nnnn) ex25: tx n=3 (best-effort + guaranteed)
I (nnnn) ex25: guaranteed resent=1 pending=1
I (nnnn) ex25: tx n=4 (best-effort + guaranteed)
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1):

```text
server  RAM:   18.0% (used 58992 bytes from 327680 bytes)
        Flash: 19.4% (used 811741 bytes from 4194304 bytes)
client  RAM:   18.0% (used 59040 bytes from 327680 bytes)
        Flash: 19.4% (used 812981 bytes from 4194304 bytes)
```
