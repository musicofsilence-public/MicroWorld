# 20-TwoBoardI2c

**Feature:** the same ping-pong volley as example 18, over a wired I2C bus
instead of a UART — proof that swapping only the device construction line swaps
the transport, even when the new transport has a clocking master and a
responding slave.

> Status: not yet verified on hardware.

## What it does

1. Each board constructs a static I2C device and logs
   `<role> open=<0|1>`. If the bus fails to open it logs a halt line and
   stops (rather than looping on a dead link).
2. **Only the master clocks the bus**, so the master paces every volley: it
   sends a 5-byte payload (sender node id + a big-endian `std::uint32_t` counter)
   to the slave, then polls reads until the slave's reply arrives.
3. The **slave** is purely reactive: on each received counter it logs
   `rx n=<counter> from=1` and stages `counter + 1` with `TrySend` for the
   master's next read. The master reads it, logs `rx n=<counter> from=2`,
   and sends the next odd counter. The counter climbs across the two monitors.
4. There is **no WiFi and no radio** — the whole link is two bus wires plus
   ground and two pull-up resistors.

## MicroWorld APIs used

- `FEsp32I2cMasterDevice` / `FEsp32I2cSlaveDevice`
  (`TrySend` / `TryReceive` / `IsOpen`), `FEsp32I2cMasterConfig` /
  `FEsp32I2cSlaveConfig`
- `I2cMaxPayloadBytes`
- `MakeI2cAddress`, `I2cAddressNodeId`
- `ETransportResult`, `FReceiveResult`
- `FEsp32TimeSource::Now` (master pacing only)

## Hardware required

Two ESP32-S3-DevKitC-1 boards, two USB cables, three jumper wires (GND, SDA,
SCL), and two external ~4.7 kΩ pull-up resistors (one on SDA, one on SCL, each
to 3V3).

## Wiring

Both boards use I2C on SDA GPIO 8 and SCL GPIO 9, wired straight through (an I2C
bus is shared, not crossover), with the two pull-ups to 3V3:

| Master (node 1) | Slave (node 2) | Why |
| --- | --- | --- |
| GND | GND | common ground first |
| GPIO 8 (SDA) | GPIO 8 (SDA) | shared data line (pull-up to 3V3) |
| GPIO 9 (SCL) | GPIO 9 (SCL) | shared clock line (pull-up to 3V3) |

Wiring safety:

- ESP32-S3 GPIO is **3.3 V logic** — never feed 5 V into a data pin, and pull
  the bus up to 3V3, never 5 V.
- Always connect **GND↔GND first**; two boards without a common ground do not
  have a signal.
- Rewire only with **both boards unpowered**.

## Build

```sh
pio run -d examples/20-TwoBoardI2c
```

Builds both role environments (`esp32-s3-master`, `esp32-s3-slave`), which
differ only by `-DMICROWORLD_EXAMPLE_I2C_MASTER`.

## Flash and observe

Human-gated (see `../AGENTS.md`). Flash the master to board A
and the slave to board B, then open both monitors:

```sh
pio run -d examples/20-TwoBoardI2c -e esp32-s3-master -t upload --upload-port <COM-A>
pio run -d examples/20-TwoBoardI2c -e esp32-s3-slave -t upload --upload-port <COM-B>
pio device monitor -d examples/20-TwoBoardI2c -e esp32-s3-master
pio device monitor -d examples/20-TwoBoardI2c -e esp32-s3-slave
```

## Expected output

Master board (node 1) (not yet verified on hardware):

```text
I (nnnn) ex20: master open=1
I (nnnn) ex20: master clocks the bus; the slave only reacts
I (nnnn) ex20: tx n=1 result=Success
I (nnnn) ex20: rx n=2 from=2
I (nnnn) ex20: tx n=3 result=Success
I (nnnn) ex20: rx n=4 from=2
```

Slave board (node 2) (not yet verified on hardware):

```text
I (nnnn) ex20: slave open=1
I (nnnn) ex20: rx n=1 from=1
I (nnnn) ex20: tx n=2 result=Success
I (nnnn) ex20: rx n=3 from=1
I (nnnn) ex20: tx n=4 result=Success
```

The counter climbs with no stalls. Because only the master clocks the bus, every
exchange is master-initiated — the slave never speaks unprompted.

## Image size

From `pio run` (release build, ESP32-S3-DevKitC-1). The two role environments
produce different images because each links a different device class:

```text
master  RAM:   6.4% (used 20924 bytes from 327680 bytes)
        Flash: 5.2% (used 218837 bytes from 4194304 bytes)
slave   RAM:   6.5% (used 21164 bytes from 327680 bytes)
        Flash: 5.1% (used 213409 bytes from 4194304 bytes)
```
