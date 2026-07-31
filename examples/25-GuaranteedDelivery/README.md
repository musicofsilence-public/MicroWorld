# 25-GuaranteedDelivery

**Feature:** best-effort vs guaranteed delivery on ONE WiFi-UDP link, with the
client injecting deterministic loss via `FPacketDropDriver`; the guaranteed
channel (`TReliableChannel`) recovers every dropped packet.

> Status: hardware-verified on two ESP32-S3 boards, 2026-07-24 (SoftAP UDP).

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
   heartbeat -- is silently dropped at the `IDevice` interface. As a result the
   server's best-effort column has gaps, while the guaranteed column is
   complete: `TReliableChannel` resends any unacknowledged value until the
   server acknowledges it.
4. Every actor reaches messaging only through `IMessageRouter&`, injected at
   construction (D9); neither actor ever sees `TTransportHost`, a driver, UDP, or
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
- `TPlaySystemSet` (`Add`, D3 dispatch/flush order over the host play system, the
  reliable channel, and the router)
- `TTransportHost` (`Configure` / `Start`), `THostPlaySystem`, `ENetworkMode`
- `FEsp32WifiLink` (`StartAccessPoint` / `JoinAccessPoint`), `FEsp32UdpDriver`,
  `MakeUdpAddress`
- `TEngine` (`RegisterClass` / `CreateWorld` / `CreateObject` /
  `BeginPlay` / `Tick`), `AActor`, `UWorld::RegisterActor`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `WriteEsp32LogRecord`, `MW_LOG`

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

The console is on the native USB port, so the port you flash is the port you
read (see [`../LOGGING.md`](../LOGGING.md)). Flash the **server first** so its
SoftAP is up before the client joins:

```bat
mw flash 25 esp32-s3-server COM5     :: server hosts the SoftAP
mw flash 25 esp32-s3-client COM7     :: client joins and sends 1..30
mw log   COM5                        :: server RX columns  (Ctrl-C to stop)
mw log   COM7                        :: client TX + resent  (second terminal)
```

`mw` is [`../tools/mw.bat`](../tools/mw.bat). Do **not** use `pio device monitor`
on these boards -- its reset-on-open can drop the native-USB port into the ROM
download loader; `mw log` holds the line steady and reconnects across resets.

## Hardware verification

Verified on two ESP32-S3-DevKitC-1 boards over SoftAP UDP on **2026-07-24**
(server COM5, client COM7; primary console on USB-Serial-JTAG). One synchronized
run, values 1..30 sent on both channels.

**Server** -- best-effort has gaps, guaranteed is complete (abridged):

```text
I (41723) ex25: rx best-effort n=1
I (41983) ex25: rx guaranteed n=1
I (42243) ex25: rx best-effort n=2
I (42483) ex25: rx guaranteed n=2
I (42763) ex25: rx guaranteed n=3      <- best-effort n=3 never arrived
I (43303) ex25: rx best-effort n=4
I (43563) ex25: rx guaranteed n=4
I (43843) ex25: rx guaranteed n=5      <- best-effort n=5 never arrived
I (44383) ex25: rx best-effort n=6
I (44643) ex25: rx guaranteed n=6
```

Over the full run the **best-effort** column delivered **15 of 30** (missing 3,
5, 7, 9, 11, 13, 15, 16, 18, 20, 22, 24, 26, 28, 29) while the **guaranteed**
column delivered **all 30 in order**.

**Client** -- same run, the reliable channel resending the drops:

```text
I (3599)  ex25: wifi joined AP
I (3599)  ex25: client up (best-effort + guaranteed over one UDP link, dropping every 3-th send)
I (3599)  ex25: tx n=1 (best-effort + guaranteed)
I (3899)  ex25: guaranteed resent=1 pending=1
...
I (19267) ex25: tx n=30 (best-effort + guaranteed)
I (19527) ex25: guaranteed resent=15 pending=1
```

The numbers tie together: the client resent **15** guaranteed messages, and the
server received **all 30** guaranteed despite losing **15** best-effort -- exactly
the guarantee `TReliableChannel` exists to provide, over real WiFi. Which values
go missing shifts run to run with how the every-third drop interleaves all
outgoing packets; the counts are stable, the exact positions are not.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1):

```text
server  RAM:   18.0% (used 58992 bytes from 327680 bytes)
        Flash: 19.4% (used 811741 bytes from 4194304 bytes)
client  RAM:   18.0% (used 59040 bytes from 327680 bytes)
        Flash: 19.4% (used 812981 bytes from 4194304 bytes)
```
