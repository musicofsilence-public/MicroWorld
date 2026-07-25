# 22-ActorMessages

**Feature:** local actor messaging through `TMessageRouter` — one board, one
world, no wire. Two actors talk only through `IMessageRouter&`, injected at
construction (D9): a broadcast reading and a targeted calibrate reply.

> Status: not yet verified on hardware.

## What it does

1. `FThermometerActor` owns one `FReadingSensorComponent`. Every 500 ms the
   sensor produces a synthetic reading (a named base plus a bounded ramp
   derived from its own tick count — no peripheral, ADR 0003 keeps device
   buses out of engine-first examples). The actor then **broadcasts**
   `TemperatureReadingMessageId` with the reading packed as a 2-byte
   little-endian payload.
2. `FDisplayActor` subscribes to `TemperatureReadingMessageId` in `BeginPlay`
   and logs every reading it receives. After it has logged 5 readings it sends
   one **targeted** `CalibrateMessageId` to `ThermometerActorId` via
   `SendMessageToActor` — sending from inside a message handler is legal (D5):
   it just appends to the outbound queue like any other send.
3. The thermometer subscribes to `CalibrateMessageId` (targeted to its own id)
   in `BeginPlay`; its handler logs receipt and resets the sensor's reading
   counter, so the displayed value cycles back to its starting point.
4. The run is bounded and deterministic: it stops once the display has logged
   7 readings (5 to trigger calibrate, 2 more to show the counter has reset).

## The one-frame latency teaching point

A "frame" here means one call to `TEngineHost::Tick`, not one 500 ms sensor
cadence — the two are independent. Within a single `Tick` call the canonical
order is fixed: **(1) `TickDispatch`** delivers whatever is already queued,
**(3) the world advances** (components tick, then actors tick — this is when
the thermometer broadcasts), and **(7) `TickFlush`** moves that new
`LocalChannelId` send into the inbound queue for next time. So:

- A reading broadcast during the world-advance step of frame **F** is flushed
  to the inbound queue at the end of that same frame **F**, and is only
  handed to the display's handler by `TickDispatch` at frame **F+1** — the
  very next `Tick` call (about 10 ms later at this example's poll pace, well
  before the sensor's next 500 ms tick). This is decision **D5**: sends are
  queued, never dispatched inline.
- The same rule applies to the calibrate reply: sent from inside the
  display's handler during frame **F+1**'s `TickDispatch`, it is flushed at
  the end of frame **F+1** and reaches the thermometer's handler at frame
  **F+2**.

The expected trace below shows this exactly: each `broadcast` line is followed
one poll later by its matching `received` line, and `sent calibrate` is
followed one poll later by `calibrated`.

## MicroWorld APIs used

- `TMessageRouter`, `IMessageRouter` (`AddMessageHandler` / `BroadcastMessage`
  / `SendMessageToActor`)
- `FMessageView`, `FMessageHandlerBinding`
- `LocalChannelId`, `BroadcastActorId`
- `TInlineActor`, `UActorComponent`
- `TEngineHost` (network-frame constructor, `RegisterClass` / `CreateWorld` /
  `CreateObject` / `RegisterComponent` / `BeginPlay` / `Tick` / `EndPlay`)
- `FEsp32TimeSource`, `Esp32OutputDevice`, `SleepMilliseconds`

## Hardware required

One ESP32-S3-DevKitC-1, one USB cable. No wiring — both actors run in the
same world on the same board.

## Build

```sh
pio run -d examples/22-ActorMessages
```

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2):

```sh
pio run -d examples/22-ActorMessages -t upload --upload-port <COM-port>
pio device monitor -d examples/22-ActorMessages
```

## Expected output

Not yet verified on hardware; derived from the fixed 500 ms sensor cadence,
10 ms poll pace, and the deterministic reading formula:

```text
I (nnnn) ex22: microworld 0.3.0
I (nnnn) ex22: thermometer broadcast reading N=1 value=201
I (nnnn) ex22: display received reading value=201 (count=1)
I (nnnn) ex22: thermometer broadcast reading N=2 value=202
I (nnnn) ex22: display received reading value=202 (count=2)
I (nnnn) ex22: thermometer broadcast reading N=3 value=203
I (nnnn) ex22: display received reading value=203 (count=3)
I (nnnn) ex22: thermometer broadcast reading N=4 value=204
I (nnnn) ex22: display received reading value=204 (count=4)
I (nnnn) ex22: thermometer broadcast reading N=5 value=205
I (nnnn) ex22: display received reading value=205 (count=5)
I (nnnn) ex22: display sent calibrate to thermometer
I (nnnn) ex22: thermometer calibrated (reset reading counter)
I (nnnn) ex22: thermometer broadcast reading N=1 value=201
I (nnnn) ex22: display received reading value=201 (count=6)
I (nnnn) ex22: thermometer broadcast reading N=2 value=202
I (nnnn) ex22: display received reading value=202 (count=7)
I (nnnn) ex22: done readings=7
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1):

```text
RAM:   10.3% (used 33772 bytes from 327680 bytes)
Flash:  5.0% (used 211037 bytes from 4194304 bytes)
```
