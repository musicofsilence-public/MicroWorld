# Portable E32 LoRa Device

Inherits `../AGENTS.md`.

## Architecture

This directory holds the portable half of the LoRa medium: the E32 node-address
shape, the `IDevice` realisation over a UART byte stream, and the fixed frame
state beneath it. LoRa is the one medium whose protocol MicroWorld owns end to
end, which is why a single implementation here serves every board instead of one
per platform family.

Portability is what makes that possible. The driver borrows Core's
`IUartByteStream` by reference and never opens, configures, clocks, or closes a
UART. Pin assignment, mode pins, vendor SDK calls, and adapter lifetime belong
to `Platform/Esp32` and `Platform/Pico`, whose byte streams this directory
consumes without naming them.

These sources are optional. `MICROWORLD_TRANSPORT_RADIO` (declared in
`Modules/CMakeLists.txt`, default ON) selects them into the Transport target, so
a radio-less build links Transport without the E32 framing at all. Nothing
outside this directory may assume the sources are present.

`Internal/` holds the transport state header the driver composes; consumers depend
on the public driver and address headers only.

## Concepts and boundaries

- The medium underneath is a byte stream, not a datagram service. Packet
  boundaries are recovered with `FrameCodec` magic/length/CRC framing, while the
  `IDevice` contract above still hands out whole packets.
- `FRadioE32Driver` performs no I/O while constructing. `Initialize` is
  single-shot — `Success` on the first call, `Unavailable` after — and stamps
  the local node id into every frame later queued.
- One transmit slot. A `TrySend` arriving while a frame is still queued returns
  `Full` rather than buffering a second packet.
- `AdvanceTransmit` exists because this driver accepts a packet before it is on
  air. Each call moves a fixed encoded-frame byte budget; a byte commits only
  after UART `Success`, `Unavailable` retains it for the next turn, and `Error`
  discards the queued frame so a dead UART cannot leave later sends `Full`
  forever.
- A LoRa address is one byte of node id. Transparent-mode transmission is a
  broadcast, so the destination is driver-relative metadata rather than an
  on-air routing command; only the sender byte on a received frame names a peer.
- `E32MaxPayloadBytes` is 58 — the 64-byte transparent frame minus six bytes of
  framing overhead — and every E32 adapter shares it, so a frame accepted on one
  board decodes on another without capacity negotiation.
- No heap, exceptions, RTTI, hidden clocks, threads, vendor SDKs, or global
  mutable state; every buffer is fixed-capacity value storage.

## Verification

Build the Transport target once with `MICROWORLD_TRANSPORT_RADIO=OFF` after
changing anything here. That is the only proof a radio-less build still links,
and it fails the moment a non-optional source starts including these headers.
The shared payload bound is covered on the host by
`microworld_lora_payload_regression_tests`. A host build proves framing and
portability only: any claim about air time, range, or a real E32 module needs
the two-board hardware checkpoint in `examples/17-TwoBoardLora`.
