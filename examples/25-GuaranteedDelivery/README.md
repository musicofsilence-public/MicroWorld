# 25-GuaranteedDelivery

**Feature:** best-effort versus reliable delivery on one WiFi-UDP link. The
client injects deterministic send loss through `FPacketDropDevice`; reliable
Messaging retries until the server acknowledges every counter value.

> Status: requires hardware verification after the Messaging port.

## What it does

1. The **server** board (`esp32-s3-server`) hosts the WiFi SoftAP and runs
   `FLedgerActor`. It subscribes to the same `Counter` message on both named
   channels, logs each arrival, and records the distinct values received on
   each channel.
2. The **client** board (`esp32-s3-client`) joins the SoftAP, wraps only its
   UDP send path in `FPacketDropDevice{DropEveryNthSend = 3}`, and runs
   `FCounterActor`. Every 500 ms it sends the next value in 1..30 on both
   channels: `BestEffort` and reliable `Guaranteed`.
3. `FMessagingSystem` owns reliable framing, sequence numbers, pending frames,
   retry timing, the bounded attempt budget, and acknowledgements. The example
   composes that behavior only by setting `bIsReliable` on `Guaranteed`.
4. The client uses fixed UDP port `40405`; the server's channels name
   `192.168.4.2:40405`, so acknowledgements have a return route. This demo
   assumes the SoftAP gives its first and only station `192.168.4.2`.
5. The run is **unbounded** (matching 16-TwoBoardUdp and 24-TwoChannelWorld):
   after counter 30 the actor idles while the boards remain observable.

## Proof of delivery

The trace proves outcomes instead of reporting retry internals:

1. The client logs `drop injector dropped sends=<n>` whenever deterministic
   loss occurs, proving packets really were removed on the client send path.
2. The server logs `rx best-effort n=<n>` and `rx guaranteed n=<n>` for each
   delivery. The best-effort values show gaps; the guaranteed values cover the
   full range.
3. Once all distinct guaranteed values arrive, the server logs
   `guaranteed complete 30/30; best-effort <m>/30`. That is the completion
   proof for the run.
4. The client remains quiet about `GetAbandonedReliableMessageCount()` while it
   is zero. An error `guaranteed abandoned=<n>` means the bounded retry budget
   gave up and the run failed its guarantee.

The injector is deliberately client-side and send-only. Dropping a server
acknowledgement would make the client resend a value that the server currently
has no duplicate suppression for, corrupting the comparison.

## MicroWorld APIs used

- `FMessagingSystem` (`CreateChannel`, `SubscribeToChannel`,
  `SendMessageToChannel`, `GetAbandonedReliableMessageCount`)
- `FChannelInformation` (`bIsReliable`, the client/server fixed peer address)
- `FMessage`, `FNameId`, `MakeWeakOwner`
- `FPacketDropDevice` (`DroppedSendCount`), client-side only
- `FEsp32WifiLink` (`StartAccessPoint` / `JoinAccessPoint`),
  `FEsp32WifiDevice`, `MakeUdpAddress`
- `TEngine` (`CreateMessagingSystem`, `RegisterClass`, `CreateWorld`,
  `CreateObject`, `BeginPlay`, `Tick`), `AActor`, `UWorld::RegisterActor`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `WriteEsp32LogRecord`,
  `MW_LOG`

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
mw log   COM7                        :: client TX + drop evidence (second terminal)
```

`mw` is [`../tools/mw.bat`](../tools/mw.bat). Do **not** use `pio device monitor`
on these boards -- its reset-on-open can drop the native-USB port into the ROM
download loader; `mw log` holds the line steady and reconnects across resets.

## Hardware verification

After flashing both boards, retain the server's closing
`guaranteed complete 30/30; best-effort <m>/30` line and the matching client
drop-injector lines as the hardware evidence for this port. The former trace
and its image-size measurements applied to the retired composition, so they
are intentionally not retained here.
