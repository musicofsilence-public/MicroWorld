# ADR 0003: Wired Board-to-Board Links Are Network Transports

- **Status:** Accepted
- **Date:** 2026-07-23
- **Decision owner:** Project owner

## Context

Two ESP32-S3 boards are on hand, and wired board-to-board links — UART, I2C,
SPI — are wanted to be as easy to use as the WiFi UDP and E32 LoRa links
MicroWorld already ships. The engine already has a complete peer-messaging
design *above* the driver seam: transactional byte I/O, the `FrameCodec` wire
framing, the `TNetManager` outbound FIFO, and the `TNetHost` channel/peer/message
model. That whole stack runs over any `INetDriver` without change. The open
question is only how a plain wire should enter the engine.

`docs/Porting.md` seam 2 defines the contract: an `INetDriver` is two
non-blocking, transactional operations (`TrySend` / `TryReceive`) over an opaque
`FNetAddress` the adapter owns; `FEsp32UdpDriver` and `FEsp32E32LoraDriver` are
the shipped implementations.

## Decision

- **Wired board-to-board links are network transports.** Each one is a new
  `INetDriver` implementation inside `Modules/PlatformEsp32`, exactly like
  `FEsp32UdpDriver` and `FEsp32E32LoraDriver`. Nothing portable changes: the
  byte I/O, frame codec, `TNetManager` FIFO, and `TNetHost` channel/message
  design run over a wire unchanged — that is the entire payoff. Each driver
  implements the full seam-2 contract of `docs/Porting.md`.
- **Peripheral-bus device access is out of scope.** Reading a sensor over I2C
  or driving a display over SPI is a different problem — master-driven register
  traffic, not peer messaging. If MicroWorld ever needs it, it becomes its own
  clean system behind its own seam, designed then, not now.

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
  classes. The peer-messaging layer above the seam never learns which side
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
  the seam exists to prevent.
- **Add a portable "wired transport" abstraction above `INetDriver`.** Rejected
  (YAGNI): `INetDriver` already is that abstraction. Wired links need nothing the
  seam does not already provide.

## Revisit triggers

- A real application needs device/peripheral access (a sensor or display) — that
  is the separate system this ADR defers; design it then, behind its own seam.
- Multi-drop topology (one master, many slaves on one bus) becomes a
  requirement — that is a `TNetHost` capacity question, not a driver-seam change.
