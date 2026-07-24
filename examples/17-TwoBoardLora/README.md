# 17-TwoBoardLora

**Feature:** the same ping-pong counter volley as example 18, over an E32 LoRa
radio link instead of a wire — a `FEsp32E32LoraDriver` swapped in for
`FEsp32UartDriver` on the `INetDriver` seam, with the volley loop, frame codec,
and address helpers unchanged. This is the wireless twin of example 18.

> Status: not yet verified on hardware.

## What it does

1. Each board constructs a static `FEsp32E32LoraDriver` over a local UART
   wired to an E32 module and logs `node=<id> open=<0|1>`. If the UART fails
   to open it logs a halt line and stops (rather than looping on a dead link).
2. Node 1 seeds the volley: one second after boot it sends a 5-byte payload
   (sender node id + a big-endian `std::uint32_t` counter) to the peer and
   logs `tx n=<counter> result=<text>`.
3. Both boards poll `TryReceive` on a 10 ms pace. On a received frame they log
   `rx n=<counter> from=<node-id>` (the sender id comes from the frame,
   read back through `LoraAddressNodeId`), then reply with `counter + 1` one
   second later. The counter climbs alternately across the two serial
   monitors. The volley is paced at 1 s instead of example 18's 500 ms because
   a full LoRa frame costs hundreds of milliseconds of airtime; a faster pace
   would congest the channel.
4. There is **no WiFi** and the two boards share **no wires** between them —
   each talks to its own E32 module over a local UART, and the two modules
   reach each other over the air.

## MicroWorld APIs used

- `FEsp32E32LoraDriver` (`TrySend` / `TryReceive` / `IsOpen`), `FEsp32E32LoraConfig`
- `E32MaxPayloadBytes`
- `MakeLoraAddress`, `LoraAddressNodeId`
- `ENetResult`, `FNetReceiveResult`
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `Esp32LogSink`, `MW_LOG`

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, two E32 LoRa UART modules
**each with its antenna attached**, and jumper wires per board.

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
pio run -d examples/17-TwoBoardLora
```

Builds both role environments (`esp32-s3-node-a` = node 1, `esp32-s3-node-b` =
node 2), which differ only by `-DMICROWORLD_EXAMPLE_NODE_ID`.

## Flash and observe

The console is on the native USB port, so the port you flash is the port you
read (see [`../LOGGING.md`](../LOGGING.md)):

```bat
mw flash 17 esp32-s3-node-a COM5     :: node 1
mw flash 17 esp32-s3-node-b COM7     :: node 2
mw log   COM5                        :: node 1 trace (Ctrl-C to stop)
mw log   COM7                        :: node 2 trace (second terminal)
```

`mw` is [`../tools/mw.bat`](../tools/mw.bat). Do **not** use `pio device monitor`
on these boards -- its reset-on-open can drop the native-USB port into the ROM
download loader; `mw log` holds the line steady and reconnects across resets.

## Expected output

Board A (node 1) (not yet verified on hardware):

```text
I (nnnn) ex17: node=1 open=1
I (nnnn) ex17: tx n=1 result=Success
I (nnnn) ex17: rx n=2 from=2
I (nnnn) ex17: tx n=3 result=Success
I (nnnn) ex17: rx n=4 from=2
```

Board B (node 2) (not yet verified on hardware):

```text
I (nnnn) ex17: node=2 open=1
I (nnnn) ex17: rx n=1 from=1
I (nnnn) ex17: tx n=2 result=Success
I (nnnn) ex17: rx n=3 from=1
I (nnnn) ex17: tx n=4 result=Success
```

Unlike a wire, an occasional LoRa gap is radio weather (distance,
interference) rather than necessarily a defect.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). Both role environments
produce the same image — they differ only by a compile-time node-id constant:

```text
RAM:   6.3% (used 20604 bytes from 327680 bytes)
Flash: 5.2% (used 216637 bytes from 4194304 bytes)
```
