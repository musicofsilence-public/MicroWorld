# 22-ActorMessages

**Feature:** local actor messaging through engine-owned `FMessagingSystem` — one
board, one world, no wire. Two actors talk only through injected Messaging: a
named reading and a named calibrate reply on one device-free channel.

> Status: not yet verified on hardware.

## What it does

1. `FThermometerActor` owns one `FReadingSensorComponent`. Every 500 ms the
   sensor produces a synthetic reading (a named base plus a bounded ramp
   derived from its own tick count — no peripheral, ADR 0003 keeps device
   buses out of engine-first examples). The actor then sends
   `TemperatureReading` with the reading packed as a 2-byte
   little-endian payload.
2. `FDisplayActor` subscribes to `TemperatureReading` in `BeginPlay`
   and logs every reading it receives. After it has logged 5 readings it sends
   one `Calibrate` message through the same local channel. Sending from inside
   a message handler is legal because Messaging prevents the active display
   subscription from re-entering itself.
3. The thermometer subscribes to `Calibrate` in `BeginPlay`; its weak owner
   makes the registration inert if the garbage collector reclaims the actor.
   Its handler logs receipt and resets the sensor's reading counter, so the
   displayed value cycles back to its starting point.
4. The run is bounded and deterministic: it stops once the display has logged
   7 readings (5 to trigger calibrate, 2 more to show the counter has reset).

## Synchronous local delivery

A "frame" here means one call to `TEngine::Tick`, not one 500 ms sensor
cadence — the two are independent. The `Local` channel has a `nullptr` device,
so Messaging delivers to matching local subscribers directly inside
`SendMessageToChannel` and stops there: it neither frames nor sends anything.
Within a world-advance tick:

- The display's reading subscriber runs inside the thermometer's `Tick`. The
  thermometer logs its reading immediately before the send so the trace stays
  in causal order.
- The display's calibrate send invokes the thermometer subscriber nested in
  the same call stack. The sensor counter resets in that same frame, rather
  than on the next poll.

The expected trace below shows this: each `broadcast` line is immediately
followed by its matching `received` line, and the calibrate handler completes
before its sender returns.

## MicroWorld APIs used

- `FMessagingSystem` (`CreateChannel` / `SubscribeToChannel` /
  `SendMessageToChannel`)
- `FMessage`, `FNameId`, `MakeNameId`
- `MakeWeakOwner` — actor-owned subscriptions become inert when collection
  reclaims their owner
- `AActor`, `UActorComponent`
- `TEngine` (`CreateMessagingSystem`, `RegisterClass` / `CreateWorld` /
  `CreateObject` / `RegisterComponent` / `BeginPlay` / `Tick` / `EndPlay`)
- `FEsp32TimeSource`, `WriteEsp32LogRecord`, `SleepMilliseconds`

## Hardware required

One ESP32-S3-DevKitC-1, one USB cable. No wiring — both actors run in the
same world on the same board.

## Build

```sh
pio run -d examples/22-ActorMessages
```

## Flash and observe

Human-gated (see `../AGENTS.md`):

```sh
pio run -d examples/22-ActorMessages -t upload --upload-port <COM-port>
pio device monitor -d examples/22-ActorMessages
```

## Expected output

Not yet verified on hardware; derived from the fixed 500 ms sensor cadence,
10 ms poll pace, and the deterministic reading formula:

```text
I (nnnn) ex22: microworld 0.4.0
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

Historical output from `pio run` before the `FMessagingSystem` port (release
build, ESP32-S3-DevKitC-1). Fresh size evidence is required for the current
example:

```text
RAM:   10.3% (used 33772 bytes from 327680 bytes)
Flash:  5.0% (used 211037 bytes from 4194304 bytes)
```
