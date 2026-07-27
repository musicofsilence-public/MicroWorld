# 17-TwoBoardLora

**Feature:** the same ping-pong counter volley as example 18, over an E32 LoRa
radio link instead of a wire — a `FEsp32E32LoraDriver` swapped in for
`FEsp32UartDriver` on the `INetDriver` seam, with the volley loop, frame codec,
and address helpers unchanged. This is the wireless twin of example 18.

> Status: hardware-verified 2026-07-24 (EBYTE E32-433T20D, 433 MHz).

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
- `FEsp32TimeSource::Now`, `SleepMilliseconds`, `WriteEsp32LogRecord`, `MW_LOG`

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

## Verified output

Captured 2026-07-24 on two ESP32-S3-DevKitC-1 boards, each with an EBYTE
E32-433T20D (433 MHz, FCC ID 2ALPH-E32) in transparent mode, antennas
attached. The two consoles show independent board uptimes (node 2 booted
first, so it is already listening when node 1 seeds `n=1`); the counter climbs
alternately at the 1 s cadence with `result=Success` on every send, and
continues indefinitely.

Board A (node 1), COM5:

```text
I (572) ex17: node=1 open=1
I (1582) ex17: tx n=1 result=Success
I (3192) ex17: rx n=2 from=2
I (4192) ex17: tx n=3 result=Success
I (5802) ex17: rx n=4 from=2
I (6802) ex17: tx n=5 result=Success
I (8412) ex17: rx n=6 from=2
I (9412) ex17: tx n=7 result=Success
```

Board B (node 2), COM7:

```text
I (536) ex17: node=2 open=1
I (5936) ex17: rx n=1 from=1
I (6936) ex17: tx n=2 result=Success
I (8546) ex17: rx n=3 from=1
I (9546) ex17: tx n=4 result=Success
I (11156) ex17: rx n=5 from=1
I (12156) ex17: tx n=6 result=Success
```

Unlike a wire, an occasional LoRa gap is radio weather (distance,
interference) rather than necessarily a defect.

## Pico H pairing (prototype hardware-verified 2026-07-26)

The captured run below used the private prototype that was promoted into
`Modules/PlatformPico`. The current Pico-native `lora` consumer acts as node 1
through `FPicoE32LoraDriver`; this ESP32 source remains unchanged as node 2.
Wire the Pico's E32 as GP4 TX → RXD, GP5 RX ← TXD, 3V3 → VCC, GND → GND/M0/M1,
and leave AUX open. Attach antennas before power.

```bat
mw flash 17 esp32-s3-node-b COMx
mw log COMx
Modules\Core\tests\consumer\pico-freertos\pico.bat build lora
Modules\Core\tests\consumer\pico-freertos\pico.bat upload lora --drive E:
```

Use the ESP32 trace as the Pico runtime observation. A fresh node-B boot should
show `node=2 open=1`, followed by `rx n=1 from=1`,
`tx n=2 result=Success`, `rx n=3 from=1`, and `tx n=4 result=Success`.

Captured on 2026-07-26 after uploading the Pico `lora` image to the wired Pico
H and flashing ESP32 node B on COM5. The ESP32 log proved the initial exchange
and continued past Pico counter 49, exercising the Pico task's stack-headroom
assertion throughout sustained traffic:

```text
I (445166) ex17: rx n=1 from=1
I (446166) ex17: tx n=2 result=Success
I (447936) ex17: rx n=3 from=1
I (448936) ex17: tx n=4 result=Success
I (450706) ex17: rx n=5 from=1
I (451706) ex17: tx n=6 result=Success
...
I (503336) ex17: rx n=45 from=1
I (504336) ex17: tx n=46 result=Success
I (506106) ex17: rx n=47 from=1
I (507106) ex17: tx n=48 result=Success
I (508876) ex17: rx n=49 from=1
```

### PlatformPico package checkpoint (hardware-verified 2026-07-27)

Commit `45f6488` promoted the prototype into `Modules/PlatformPico`. The LoRa
image built from that commit had UF2 SHA-256
`c39174625595225b514b550eb0a15bccaf64c8ec2df4fd784a7b39bb47837811`.
It was uploaded to the Pico H through the detected `D:\` BOOTSEL volume; the
unchanged ESP32 example-17 node-B firmware was observed on COM5.

```bat
Modules\Core\tests\consumer\pico-freertos\pico.bat build lora
certutil -hashfile Modules\Core\tests\consumer\pico-freertos\build\microworld_pico_lora_interop.uf2 SHA256
Modules\Core\tests\consumer\pico-freertos\pico.bat upload lora
examples\tools\mw.bat log COM5
```

The Pico ran as node 1 on UART1, GP4 TX, GP5 RX, 9600 baud, 8N1. Both E32
modules used transparent mode with M0/M1 low and antennas attached. This
contiguous ESP32 trace proves repeated Pico receives and successful replies
through counter 40:

```text
I (36417636) ex17: rx n=19 from=1
I (36418636) ex17: tx n=20 result=Success
I (36420406) ex17: rx n=21 from=1
I (36421406) ex17: tx n=22 result=Success
I (36423176) ex17: rx n=23 from=1
I (36424176) ex17: tx n=24 result=Success
I (36425946) ex17: rx n=25 from=1
I (36426946) ex17: tx n=26 result=Success
I (36428716) ex17: rx n=27 from=1
I (36429716) ex17: tx n=28 result=Success
I (36431486) ex17: rx n=29 from=1
I (36432486) ex17: tx n=30 result=Success
I (36434256) ex17: rx n=31 from=1
I (36435256) ex17: tx n=32 result=Success
I (36437026) ex17: rx n=33 from=1
I (36438026) ex17: tx n=34 result=Success
I (36439796) ex17: rx n=35 from=1
I (36440796) ex17: tx n=36 result=Success
I (36442566) ex17: rx n=37 from=1
I (36443566) ex17: tx n=38 result=Success
I (36445336) ex17: rx n=39 from=1
I (36446336) ex17: tx n=40 result=Success
```

The same hardware was rechecked after moving UART SDK calls behind the
SDK-free driver seam and wiring transmit progress into `INetDriver`. The rebuilt
UF2 SHA-256 was
`9d85c3cc2ce14cd099730779d0323f1bfd135b4cc61fdbf6aa50a717bf91e288`.
After upload through `D:\`, COM5 showed an uninterrupted node-1/node-2 volley:

```text
I (57350126) ex17: tx n=10 result=Success
I (57351896) ex17: rx n=11 from=1
I (57352896) ex17: tx n=12 result=Success
I (57354666) ex17: rx n=13 from=1
...
I (57376826) ex17: rx n=29 from=1
I (57377826) ex17: tx n=30 result=Success
```

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). Both role environments
produce the same image — they differ only by a compile-time node-id constant:

```text
RAM:   6.3% (used 20604 bytes from 327680 bytes)
Flash: 5.2% (used 216637 bytes from 4194304 bytes)
```
