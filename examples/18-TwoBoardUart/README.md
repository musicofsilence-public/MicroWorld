# 18-TwoBoardUart

**Feature:** the same ping-pong volley as example 17, over a plain wire instead
of a radio — proof that swapping only the driver construction line swaps the
transport, with the byte I/O, frame codec, and address helpers unchanged.

> Status: not yet verified on hardware.

## What it does

1. Each board constructs a static `FEsp32UartDriver` and logs
   `node=<id> open=<0|1>`. If the UART fails to open it logs a halt
   line and stops (rather than looping on a dead link).
2. Node 1 seeds the volley: half a second after boot it sends a 5-byte payload
   (sender node id + a big-endian `std::uint32_t` counter) to the peer and
   logs `tx n=<counter> result=<text>`.
3. Both boards poll `TryReceive` on a 10 ms pace. On a received frame they log
   `rx n=<counter> from=<node-id>` (the sender id comes from the frame,
   read back through `UartAddressNodeId`), then reply with `counter + 1` half a
   second later. The counter climbs alternately across the two serial monitors.
4. There is **no WiFi and no radio** — the whole link is two GPIO wires.

## MicroWorld APIs used

- `FEsp32UartDriver` (`TrySend` / `TryReceive` / `IsOpen`), `FEsp32UartConfig`
- `UartMaxPayloadBytes`
- `MakeUartAddress`, `UartAddressNodeId`
- `ENetResult`, `FNetReceiveResult`
- `FEsp32TimeSource::Now`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, and three jumper wires.

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

## Build

```sh
pio run -d examples/18-TwoBoardUart
```

Builds both role environments (`esp32-s3-node-a` = node 1, `esp32-s3-node-b` =
node 2), which differ only by `-DMICROWORLD_EXAMPLE_NODE_ID`.

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash node 1 to board A and
node 2 to board B, then open both monitors:

```sh
pio run -d examples/18-TwoBoardUart -e esp32-s3-node-a -t upload --upload-port <COM-A>
pio run -d examples/18-TwoBoardUart -e esp32-s3-node-b -t upload --upload-port <COM-B>
pio device monitor -d examples/18-TwoBoardUart -e esp32-s3-node-a
pio device monitor -d examples/18-TwoBoardUart -e esp32-s3-node-b
```

## Expected output

Board A (node 1) (not yet verified on hardware):

```text
I (nnnn) ex18: node=1 open=1
I (nnnn) ex18: tx n=1 result=Success
I (nnnn) ex18: rx n=2 from=2
I (nnnn) ex18: tx n=3 result=Success
I (nnnn) ex18: rx n=4 from=2
```

Board B (node 2) (not yet verified on hardware):

```text
I (nnnn) ex18: node=2 open=1
I (nnnn) ex18: rx n=1 from=1
I (nnnn) ex18: tx n=2 result=Success
I (nnnn) ex18: rx n=3 from=1
I (nnnn) ex18: tx n=4 result=Success
```

The counter climbs alternately with no stalls. Unlike the LoRa example, a wired
link dropping frames at 30 cm is a defect, not weather — investigate rather than
accept it.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). Both role environments
produce the same image — they differ only by a compile-time node-id constant:

```text
RAM:   6.4% (used 20892 bytes from 327680 bytes)
Flash: 5.3% (used 220249 bytes from 4194304 bytes)
```
