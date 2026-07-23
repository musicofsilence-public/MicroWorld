# 21-TwoBoardSpi

**Feature:** the same ping-pong volley as example 20, over a wired SPI bus
instead of I2C — proof that swapping only the driver construction line swaps the
transport, across every wired bus the plan builds.

> Status: compiled for ESP32-S3; not yet verified on hardware.

## What it does

1. Each board constructs a static SPI driver and prints
   `[ex21] <role> open=<0|1>`. If the bus fails to open it prints a halt line and
   stops (rather than looping on a dead link).
2. **Only the master clocks the bus**, so the master paces every volley: it
   sends a 5-byte payload (sender node id + a big-endian `std::uint32_t` counter)
   to the slave, then polls reads until the slave's reply arrives. SPI is
   full-duplex and the slave's reply is pipelined by a transaction, so the master
   may poll a few reads before the reply lands — that is normal, not a stall.
3. The **slave** is purely reactive: on each received counter it prints
   `[ex21] rx n=<counter> from=1` and stages `counter + 1` with `TrySend` for the
   master's next read. The master reads it, prints `[ex21] rx n=<counter> from=2`,
   and sends the next counter. The counter climbs across the two monitors.
4. There is **no WiFi and no radio** — the whole link is four bus wires plus
   ground.

## MicroWorld APIs used

- `FEsp32SpiMasterDriver` / `FEsp32SpiSlaveDriver`
  (`TrySend` / `TryReceive` / `IsOpen`), `FEsp32SpiMasterConfig` /
  `FEsp32SpiSlaveConfig`
- `SpiMaxPayloadBytes`
- `MakeSpiAddress`, `SpiAddressNodeId`
- `ENetResult`, `FNetReceiveResult`
- `FEsp32TimeSource::Now` (master pacing only)

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, and five jumper wires (GND, MOSI,
MISO, SCLK, CS).

## Wiring

Both boards use SPI2 (FSPI). SPI is a shared bus, so wire each signal
straight-through by name (ESP-IDF names the same line on both sides):

| Master (node 1) | Slave (node 2) | Why |
| --- | --- | --- |
| GND | GND | common ground first |
| GPIO 11 (MOSI) | GPIO 11 (MOSI) | master out, slave in |
| GPIO 13 (MISO) | GPIO 13 (MISO) | slave out, master in |
| GPIO 12 (SCLK) | GPIO 12 (SCLK) | clock, driven by the master |
| GPIO 10 (CS) | GPIO 10 (CS) | chip select, driven by the master |

Wiring safety:

- ESP32-S3 GPIO is **3.3 V logic** — never feed 5 V into a data pin.
- Always connect **GND↔GND first**; two boards without a common ground do not
  have a signal.
- Rewire only with **both boards unpowered**.

## Build

```sh
pio run -d examples/21-TwoBoardSpi
```

Builds both role environments (`esp32-s3-master`, `esp32-s3-slave`), which
differ only by `-DMICROWORLD_EXAMPLE_SPI_MASTER`.

## Flash and observe

Human-gated (see `docs/EXAMPLES_ROADMAP.md` §1.2). Flash the master to board A
and the slave to board B, then open both monitors:

```sh
pio run -d examples/21-TwoBoardSpi -e esp32-s3-master -t upload --upload-port <COM-A>
pio run -d examples/21-TwoBoardSpi -e esp32-s3-slave -t upload --upload-port <COM-B>
pio device monitor -d examples/21-TwoBoardSpi -e esp32-s3-master
pio device monitor -d examples/21-TwoBoardSpi -e esp32-s3-slave
```

## Expected output

Master board (node 1):

```text
[ex21] master open=1
[ex21] master clocks the bus; the slave only reacts
[ex21] tx n=1 result=Success
[ex21] rx n=2 from=2
[ex21] tx n=3 result=Success
[ex21] rx n=4 from=2
```

Slave board (node 2):

```text
[ex21] slave open=1
[ex21] rx n=1 from=1
[ex21] tx n=2 result=Success
[ex21] rx n=3 from=1
[ex21] tx n=4 result=Success
```

The counter climbs with no stalls. Because only the master clocks the bus, every
exchange is master-initiated — the slave never speaks unprompted.

## Verified output

Not yet verified on hardware. This example has been compiled for ESP32-S3 only;
the captured serial traces from both boards go here once the human-gated
checkpoint runs.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The two role environments
produce different images because each links a different driver class:

```text
master  RAM:   6.6% (used 21712 bytes from 327680 bytes)
        Flash: 5.6% (used 234149 bytes from 4194304 bytes)
slave   RAM:   6.6% (used 21628 bytes from 327680 bytes)
        Flash: 5.3% (used 220481 bytes from 4194304 bytes)
```
