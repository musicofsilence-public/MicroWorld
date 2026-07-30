# ADR 0003: Wired Board-to-Board Links Are Network Transports

- **Status:** Accepted
- **Date:** 2026-07-23
- **Decision owner:** Project owner

## Context

Two ESP32-S3 boards are on hand, and wired board-to-board links — UART, I2C,
SPI — are wanted to be as easy to use as the WiFi UDP and E32 LoRa links
MicroWorld already ships. The engine already has a complete peer-messaging
design *above* the `INetDriver` interface: transactional byte I/O, the `FrameCodec` wire
framing, the `TNetManager` outbound FIFO, and the `TNetHost` channel/peer/message
model. That whole stack runs over any `INetDriver` without change. The open
question is only how a plain wire should enter the engine.

`docs/Porting.md`'s Net-driver interface defines the contract: an `INetDriver` is two
non-blocking, transactional operations (`TrySend` / `TryReceive`) over an opaque
`FNetAddress` the adapter owns; `FEsp32UdpDriver` and `FEsp32E32LoraDriver` are
the shipped implementations.

## Decision

- **Wired board-to-board links are network transports.** Each one is a new
  `INetDriver` implementation inside `Modules/PlatformEsp32`, exactly like
  `FEsp32UdpDriver` and `FEsp32E32LoraDriver`. Nothing portable changes: the
  byte I/O, frame codec, `TNetManager` FIFO, and `TNetHost` channel/message
  design run over a wire unchanged — that is the entire payoff. Each driver
  implements the full Net-driver contract of `docs/Porting.md`.
- **Peripheral-bus device access is out of scope.** Reading a sensor over I2C
  or driving a display over SPI is a different problem — master-driven register
  traffic, not peer messaging. If MicroWorld ever needs it, it becomes its own
  clean system behind its own boundary, designed then, not now.

The boundary in one sentence a student can quote: **a wire between two boards is
just another `INetDriver`; talking to a chip on a bus is a different system we
have not built.**

## Consequences

- The portable packages (`Core`, `Memory`, `Object`, `Engine`, `Net`) and
  `PlatformHost` stay untouched; every new line lives in `Modules/PlatformEsp32`
  and `examples/`. ESP-IDF bus headers stay confined to private
  `src/*PlatformImplementation.h` files, as the E32 UART driver already does.
- Master/slave asymmetry (I2C and SPI have a clocking master and a responding
  slave) enters only at the platform edge, as separate master/slave driver
  classes. The peer-messaging layer above the `INetDriver` interface never learns which side
  clocks the bus.
- Each wired example needs two physical boards and jumper wires (I2C also two
  external pull-up resistors); hardware verification stays human-gated.
- The same ping-pong volley runs over LoRa, UART, I2C, and SPI with the driver
  construction line as the only difference — the demonstration the plan exists
  to produce.

## Alternatives considered

- **Model I2C/SPI as device-access APIs (register read/write).** Rejected: that
  solves the out-of-scope problem and would push master/slave asymmetry and bus
  addressing up into the portable messaging layer, which is exactly the coupling
  the `INetDriver` interface exists to prevent.
- **Add a portable "wired transport" abstraction above `INetDriver`.** Rejected
  (YAGNI): `INetDriver` already is that abstraction. Wired links need nothing the
  `INetDriver` interface does not already provide.

## Revisit triggers

- A real application needs device/peripheral access (a sensor or display) — that
  is the separate system this ADR defers; design it then, behind its own boundary.
- Multi-drop topology (one master, many slaves on one bus) becomes a
  requirement — that is a `TNetHost` capacity question, not a driver-interface change.

---

## Appendix A — I2C design spike (Task 2.1)

Deliverable of the Phase 2 spike: verified answers, taken from the **ESP-IDF
6.0.1** headers installed for this project (`driver/i2c_master.h`,
`driver/i2c_slave.h`, `driver/i2c_types.h`), before any driver code. No hardware
probe was run — hardware stays human-gated — so these answers are
header/documentation-derived, and the driver's platform-implementation carries
"UNVERIFIED at runtime" wording until example 20's checkpoint passes (§1.2).

**A1 — Slave-side API; can a slave pre-queue TX without ISR callbacks?**
Yes. 6.0.1 ships the v2 new-generation slave driver. A slave stages TX bytes with
`i2c_slave_write(handle, data, len, &written, timeout_ms)` — no callback needed to
send; the bytes wait in the hardware FIFO plus a software ring (`send_buf_depth`)
for the master's next read. Receive is delivered **only** through the `on_receive`
ISR callback (`i2c_slave_received_callback_t`, event `{uint8_t* buffer, uint32_t
length}`); there is **no** polling `i2c_slave_receive` in 6.0.1. The `on_request`
callback (master reads while the FIFO is empty) is optional; the driver does not
register it — pre-queued TX plus hardware filler covers the point-to-point case.

**A2 — Master read of N bytes when the slave has nothing queued.**
The master generates the clock, so `i2c_master_receive(N)` returns `ESP_OK` with N
bytes regardless of slave state; unqueued positions read as bus-idle filler (SDA
released). It does not NACK or block for lack of data — a NACK only occurs in the
address phase if the slave is absent. "Nothing to receive" is therefore never
signalled by the API; it is detected by the `FrameCodec` decoder finding no valid
frame in the window, which the master driver maps to `Unavailable`.

**A3 — Bus speed and pull-ups.**
100 kHz standard mode. Over ~20 cm jumper wires the internal pull-ups are, by the
header's own warning, not strong enough to guarantee; two external ~4.7 kΩ
resistors to 3V3 on SDA and SCL are the reliable choice and are **mandatory** in
the example. The driver also sets `flags.enable_internal_pullup = true` as
harmless insurance.

**A4 — `I2cAddress.h` layout.**
One byte carrying the `FrameCodec` node id, identical in shape to `UartAddress.h`
and `LoraAddress.h` (`MakeI2cAddress` / `IsI2cAddress` / `I2cAddressNodeId`). The
bus-level 7-bit slave address is a separate config field (`SlaveAddress`), not
folded into the address helper, so the node-id concept stays uniform across every
transport. Default slave bus address `0x28`, outside the reserved `0x00`–`0x07`
and `0x78`–`0x7F` ranges.

**A5 — Frame-in-transaction shape.**
Fixed-size transaction window fed to the byte-pump decoder — **not**
length-prefixed. The master `TryReceive` reads one whole-frame window
(`I2cMaxPayloadBytes + FrameOverheadBytes`) and pushes every byte through the same
`TFrameDecoder` the UART driver uses; the codec's magic/length/CRC resync discards
the filler bytes a partially-filled read injects. This reuses the shipped codec
instead of inventing an I2C-specific length protocol.

**A6 — `I2cMaxPayloadBytes`.**
120 bytes — the same cap as UART, keeping every transport uniform (the acceptance
criterion wants only the driver construction to differ). The slave's
`send_buf_depth` and `receive_buf_depth` are sized to 256 bytes, comfortably larger
than one 126-byte frame, so a single frame always fits one transaction and the
slave inbox never truncates a frame at the example's pacing.

**Resulting driver shape (fills tasks 2.2/2.3):** `FEsp32I2cMasterConfig` = {port,
SDA GPIO, SCL GPIO, `SclSpeedHz` (100000), `SlaveAddress` (0x28), `LocalNodeId`};
`FEsp32I2cSlaveConfig` = {port, SDA GPIO, SCL GPIO, `SlaveAddress`, `LocalNodeId`}.
Master `TrySend` = one `i2c_master_transmit` of one encoded frame; master
`TryReceive` = one `i2c_master_receive` of a whole-frame window pumped through the
decoder (Unavailable when the window yields no frame — answer A2). Slave `TrySend`
= one `i2c_slave_write` of one encoded frame (`Full` when the ring cannot take it);
slave `TryReceive` = drain the ISR-filled inbox through the decoder (answer A1).
Pins on the DevKitC-1: **SDA = GPIO 8, SCL = GPIO 9** — both ordinary GPIOs, clear
of the S3 strapping pins (GPIO 0, 3, 45, 46).

---

## Appendix B — SPI design spike (Task 3.1)

Deliverable of the Phase 3 spike: verified answers from the same **ESP-IDF
6.0.1** headers (`driver/spi_master.h`, `driver/spi_slave.h`,
`driver/spi_common.h`, `hal/spi_types.h`). No hardware probe was run; the
answers are header/documentation-derived, and the driver carries
"UNVERIFIED at runtime" wording until example 21's checkpoint passes (§1.2).

**B1 — Slave transaction queuing; empty-queue behavior.**
`driver/spi_slave.h` is queue-based: `spi_slave_queue_trans(host, &trans, ticks)`
pre-queues one full-duplex transaction (`tx_buffer` + `rx_buffer`, `length` in
bits) that executes when the master pulls CS and clocks; `spi_slave_get_trans_result(host, &desc, 0)`
harvests a completed one without blocking (a timeout means none is done).
`queue_size` bounds in-flight transactions. If the master clocks while the slave
queue is empty, MISO carries undefined bytes and the slave misses the master's
bytes — so the driver keeps at least one transaction queued, and (as the
question anticipated) the `FrameCodec` CRC rejects any garbage a momentary gap
produces, so CRC suffices as the validation strategy.

**B2 — Transaction size; `SpiMaxPayloadBytes`.**
Fixed-size window fed to the byte-pump decoder, **not** exact-frame transfers —
the same shape as UART and I2C. `SpiMaxPayloadBytes = 120` (uniform), so one
whole frame is `120 + FrameOverheadBytes = 126` bytes, padded up to
`SpiTransactionWindowBytes = 128` so the DMA constraint (rx buffer word-aligned,
length a multiple of 4) is met; the pad bytes are filler the decoder discards.
Every transaction is full-duplex `128 * 8 = 1024` bits in both directions.

**B3 — Bus speed.**
Start low: **1 MHz** (`SpiClockHz = 1000000`), comfortably within jumper-wire
tolerance; SPI mode 0 (CPOL 0, CPHA 0).

**B4 — Pin set.**
`SPI2_HOST` (FSPI), DMA via `SPI_DMA_CH_AUTO` (a 128-byte transfer exceeds the
no-DMA limit). **MOSI = GPIO 11, MISO = GPIO 13, SCLK = GPIO 12, CS = GPIO 10** —
the S3's default FSPI pins, all ordinary GPIOs clear of the strapping pins
(0/3/45/46) and the octal flash/PSRAM range (26–37) on the WROOM-1-N16R8.

**B5 — `SpiAddress.h` layout.**
One byte carrying the `FrameCodec` node id, identical in shape to
`UartAddress.h`/`I2cAddress.h`/`LoraAddress.h` (`MakeSpiAddress` / `IsSpiAddress` /
`SpiAddressNodeId`). The CS pin selects the device in hardware; the node id is
only frame identity.

**Resulting driver shape (fills tasks 3.2/3.3):** `FEsp32SpiMasterConfig` = {host,
MOSI/MISO/SCLK/CS GPIOs, `ClockHz` (1000000), `LocalNodeId`}; `FEsp32SpiSlaveConfig`
= {host, MOSI/MISO/SCLK/CS GPIOs, `LocalNodeId`}. **Master** (`spi_master.h`):
full-duplex fixed-window transactions via `spi_device_transmit` (blocking but
bounded — 128 B at 1 MHz ≈ 1 ms). Because SPI is full-duplex, *every* transaction
moves both directions, so both `TrySend` (tx = encoded frame) and `TryReceive`
(tx = idle) feed the received window into the driver's `TFrameDecoder` and
`TryReceive` delivers completed frames — a documented CQS deviation: a send
unavoidably also clocks in the slave's bytes, and the driver keeps them rather
than discarding them. **Slave** (`spi_slave.h`): keeps one fixed-window
transaction queued (`tx` = staged frame or idle, `rx` = window); `TrySend` stages
one encoded frame for the next queued transaction (`Full` when a staged frame is
still pending); `TryReceive` harvests a completed transaction with
`spi_slave_get_trans_result(0)`, pumps its rx window through the decoder, and
re-queues so a transaction is always ready. The slave reply is pipelined by one
transaction (standard for SPI slaves); the master's poll-until-reply loop and the
codec's resync absorb the intervening idle bytes. Wiring is straight-through by
signal name (ESP-IDF names the line `mosi_io_num` on both sides): GND↔GND,
MOSI↔MOSI, MISO↔MISO, SCLK↔SCLK, CS↔CS.
